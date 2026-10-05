# Aevora Codebase Index

Snapshot of the source tree at `9f932d5` (2026-10-04), produced from a read-only survey. It is a navigation aid, not a verified architecture specification.

- References marked **(v)** were checked directly against the source. Other `path:line` references come from automated exploration and can drift as files change; search for the symbol if a line does not match.
- Statements marked *inference* were not confirmed by reading the full implementation.
- "Upstream" means Suyu/yuzu. Whether a feature is Aevora/fork-specific was judged by naming and comments only unless stated otherwise.

## 1. Top-level layout

| Path | Purpose |
|---|---|
| `src/` | All C++ sources (about 3,300 `.cpp/.h/.c/.inc` files; `core` alone has about 1,250). |
| `CMakeLists.txt`, `CMakeModules/`, `cpmfile.json` | Build configuration and CPM dependency manifest. |
| `externals/` | Third-party sources; many directories are empty until fetched. |
| `.patch/` | Patches CPM applies to fetched dependencies. |
| `vcpkg.json`, `vcpkg-configuration.json`, `vcpkg-overlays/`, `conanfile.txt`, `shell.nix` | Alternate/legacy package manifests (*inference*: CPM is the active path). |
| `tests/` | Recompiler and export test projects (see section 7). |
| `research/` | Recompiler research harnesses (`ios/`, `tests/`). |
| `tools/` | Export build kit, CPM scripts, package-policy audit, shader tools, Windows helpers. |
| `scripts/` | Build-fix notes and helper scripts. Inspect before running; some clear caches. |
| `dist/`, `hooks/` | Desktop/icon/udev assets; git pre-commit hook installed by CMake. |
| `docs/Aevora/`, `docs/Suyu/` | Current project docs, including `WORK_PLAN.md` and `OLD_HISTORY.md`; archived upstream docs. |

### Lineage markers found in the tree

- `.gitmodules:15` **(v)** pins `externals/dynarmic` to branch `mk8-recomp`.
- `CMakeModules/GenerateSCMRev.cmake:56` **(v)** hard-codes `BUILD_FULLNAME "${REPO_NAME} v0.0.11 (mk8-recomp)"`, although the README names v0.0.12 as the base.
- *Inference*: the recompiler work descends from an `mk8-recomp` fork rather than stock Suyu v0.0.12. Verify this before you describe the lineage publicly (WORK_PLAN Milestone 1).

## 2. Static recompilation pipeline (project-critical)

### 2.1 ARM64 → C translator

`src/core/recompiler/arm64_to_c.h` is a header-only translator of about 7,600 lines. It is the only code generator.

| Symbol | Location | Purpose |
|---|---|---|
| `struct Block` | `arm64_to_c.h:60` (v) | Discovered code block (vaddr, size, count, entry flag). |
| `g_translate_all` | `arm64_to_c.h:303` (v) | Translate forms normally left to the JIT. |
| `g_emit_standalone_runner` | `arm64_to_c.h:310` (v) | Defaults to `true` for synthetic tests. The comment says the game exporter turns it off so game code runs only inside suyu against the user's files. |
| `g_emit_fastmem` / `g_emit_guard_gen` / `g_emit_fpx` / `g_emit_fpx_shadow` | `arm64_to_c.h:316-337` (v) | ABI 6 feature switches (FM1 page-table fastmem, GG1 generation guard, FPX1 exact native FP, FPX shadow instrumentation). All are off by default, which keeps the emitted text byte-identical to ABI 5 (golden hashes in `tests/recompiler_smoke`). |
| `g_emit_fm_inline` / `EmitFmInline()` | `arm64_to_c.h:346-348` (v) | Opt-in FMI switch; true only when FM1 is also enabled. |
| `FastmemInlineH()` / `RuntimeH()` | `arm64_to_c.h:6341`, `:7187-7201` (v) | Appends the FMI static inline fast paths, including the GG1 watch-word store entry, to generated `recomp_runtime.h`; variants are selected with FM1, GG1, and FPX settings. |
| `FmInlineRuntimeC()` / `RuntimeC()` | `arm64_to_c.h:8111`, `:8116-8131` (v) | Renames generated-unit fastmem calls to FMI functions while keeping `recomp_runtime.c` definitions under their original names. |
| `TranslateAllForExport()` | `arm64_to_c.h:340` (v) | Strict static exports translate everything. |
| `kChainBudget` | `arm64_to_c.h:351` (v) | Blocks run before control returns to the dispatcher. |
| `EmitProject()` | `arm64_to_c.h:5084` (v) | Per-module entry point. Emits C sources, `GuestContext`, the module `CMakeLists.txt`, and returns `RecompileStats`. |
| `SUYU_RECOMP_STRICT` read in emitted code | `arm64_to_c.h:7374` (v) | Generated runtime honours strict mode. |
| `kDefaultUnitSourceBytes` / `kDefaultUnitBlocks` | after `kChainBudget`, ~`arm64_to_c.h:357` (v, 2026-10-05) | Generated-unit limits (8 MiB / 5,000 blocks). `EmitProject()` takes `unit_source_limit` and `unit_block_limit`; split condition ~`:5328`. |
| Generated module CMake job pool | ~`arm64_to_c.h:5574-5615` (v, 2026-10-05) | `RECOMP_JOBS` from RAM minus 4 GB, 8 GB per cl.exe or 1.5 GB per clang compile; `SUYU_RECOMP_JOBS`, `SUYU_RECOMP_JOB_MEMORY_MB`, `-DRECOMP_JOBS` overrides; Ninja pool `recomp_compile`. |
| AdvSIMD shift-by-immediate emitters | ~`arm64_to_c.h:1719-1830` (v, 2026-10-05) | `SSHR`/`USHR`/`SHRN` at ~`:1795`. `SHL` (vector, immediate) is implemented in the non-saturating and dedicated emitters but gated by `g_translate_all` for Hybrid exports (see `OLD_HISTORY.md`). |
| FPX1 runtime text and emitters | `arm64_to_c.h:FpxH()` / `FpxH64()` ~`:6416` / `:6627`; `EmitFPNativeValue` / `EmitFPNativeSuffix` ~`:492` / `:505` (v) | `RECOMP_FPX_OPEN` uses `RECOMP_FPX_FPCR_MASK 0x06C8FF07`; `RECOMP_FPX_FZ` passes guest FZ to `recomp_fpx_<op>{32,64}(..., int fz, ...)` and applies the FZ-safe keep rules. |

### 2.2 Exporter (Qt)

`src/suyu/game_export.cpp` (about 6,300 lines) and `game_export.h` (about 300 lines).

| Symbol | Location | Purpose |
|---|---|---|
| `GameExportDialog` | `game_export.h:44` | Export dialog and state. |
| `TargetPlatform`, `RecompileBackend` enums | `game_export.h:134`, `:140` | Windows/Linux/macOS; `SuyuStatic` / `Hybrid` / `Dynarmic`. |
| `FindBestCmakeExecutable()` | `game_export.cpp:2931` (v) | Finds CMake on PATH, then in VS-bundled locations. |
| `WantsCompiledOutput()` | `game_export.cpp:3139` (v) | Build (compile) versus Source-only output. |
| `RunAotPrecompile()` | `game_export.cpp:3355` (v) | Block discovery and translation orchestration. |
| `recomp_modules.cmake` / `recomp_registration.c` generation | `game_export.cpp:3950-4030` (v: `:3991`, `:4019`) | Module list in NSO load order and `suyu_recomp_static_modules_v4()` registration table. **The object involved in the earlier `LNK1181` failure is built from this file.** |
| Generated-tree CMake configure/build | around `game_export.cpp:4229` (v) | Invokes CMake on the generated tree. |
| Pruning of registration entries | around `game_export.cpp:4701` (v) | Comment on modules dropped from the registration. |
| `PackageNativeExport()` | `game_export.cpp:5059` (v) | Stages the final export package. |
| Gap-seeded discovery | around `game_export.cpp:220`, `3220-3330` | Reads `<user dir>/recomp/gaps/<title>.json` to seed blocks at Hybrid gap addresses. |
| Runtime DLL list | around `game_export.cpp:193-200` | FFmpeg, DXC, OpenSSL, and Discord RPC copied into packages. |
| Unit-limit environment overrides | `ReadPositiveSizeFromEnv()` ~`game_export.cpp:124`; read before `EmitProject()` ~`:3828` (v, 2026-10-05) | `SUYU_RECOMP_UNIT_BLOCKS`, `SUYU_RECOMP_UNIT_KB`. |
| FMI export switch and manifest/cache rule | `game_export.cpp:3460-3469`, `:3548-3570`, `:4977` (v, 2026-10-05) | `SUYU_AOT_FM_INLINE=1` enables FMI only with FM1; manifests record `fm_inline`, and cache reuse requires a matching value (missing field means FMI off). |
| clang-cl module build and launcher link | ~`game_export.cpp:4400-4560` (v, 2026-10-05) | Configures `<build-dir>/recomp_clang`, builds `recomp_static_<module>`, then reconfigures the source build tree with `SUYU_CMD_RECOMP_DIR` etc. and links `suyu-cmd-static` at `--parallel 2`. Running from a source build therefore rebuilds and pollutes that tree. |
| Recorded compiler name | `last_recomp_compiler`, ~`game_export.cpp:4560`, `:4790`, `:4985`, `:5370` | Known defect: reports record `O2` instead of the compiler. |

Main window hooks: the export menu is wired at `src/suyu/main.cpp:1739` and `:1788` (v). `SUYU_RECOMP_DIR` auto-loads a recompiled image at `main.cpp:746-782` and `:2109` (v).

### 2.3 Recompiled-code runtime

`src/core/arm/recomp/`:

| File | Purpose |
|---|---|
| `arm_recomp.h/.cpp` (~2,400 lines) | `ArmRecomp : ArmInterface`. Block lookup/dispatch loop, context marshalling (`GuestContextView`, `RecompHostMem` with static_assert-locked offsets), registration hooks (`SetRecompLookup`, `SetRecompPrepareCallback`, `SetRecompBaseSetter`, `PrepareRecompProcess`). |
| `recomp_image_features.h` | ABI 6 feature bits (FM1 `0x1`, GG1 `0x2`, FPX1 `0x4`). |
| `recomp_guard_gen.h/.cpp` | GG1 generation-word code guard. |
| `recomp_gaps.h/.cpp`, `recomp_gap_session.h/.cpp` | Gap/coverage recording. Local `suyu-recomp-gaps` JSON (includes instruction words; keep local) and portable `suyu-shared-coverage` JSON. |
| `recomp_diagnostic_sampler.h` | Diagnostics sampling. |
| `guest_fp_env.h` | Host FP mode (MXCSR/FPCR) matching for guest code; `kFpcrMask` and the host-mode check are near the top of the file (line numbers approximate). |
| `src/core/hle/kernel/physical_core.cpp` | Anonymous-namespace `FpcrStats` (~`:36`), recorded by `PhysicalCore::SaveContext` (~`:316`); `SUYU_FPCR_STATS` enables guest FPCR diagnostics. |

Key runtime facts:

- Backend selection is in `KProcess::InitializeInterfaces()`, `src/core/hle/kernel/k_process.cpp:1311` (v). If the process is the application and `Core::GetRecompLookup()` is set, each core gets an `ArmRecomp` (`:1325-1333` (v)). Otherwise it uses NCE or `ArmDynarmic64`.
- Strict mode: `StrictNoFallback()` reads `SUYU_RECOMP_STRICT`, `arm_recomp.cpp:197` (v). A non-zero value makes JIT entry fatal and names the PC.
- Fallback: `ArmRecomp::EnterFallback()`, `arm_recomp.cpp:1715` (v), lazily creates an `ArmDynarmic64` and marks GG1 via `RecompGuardGen::OnJitFallback()`. It always refuses when built with `SUYU_NO_JIT`. Call sites are around `:2241` and `:2346` (v).
- Chain budget: defaults `kShortSliceChainBudget = 32` and `kLongSliceChainBudget = 256`, `arm_recomp.cpp:209-210` (v). `SetRecompLongSlices()` selects between them (~`:750` (v)). Override: `SUYU_RECOMP_CHAIN_BUDGET` (1-8192) read at startup, `arm_recomp.cpp:220` (v).
- `RecompGuardGen::OnJitFallback()`, `recomp_guard_gen.cpp:481` (v, 2026-10-05), pins every module to verify-always the first time a JIT fallback is created. Pin causes are logged by `EnsureGuardGenPinLogger()` in `arm_recomp.cpp` (~`:815`). The cited design document (`diagnostics/suyu-guard-generation-20260923/DESIGN.md`) is not in this repository.
- Exported packages write `user/log/recomp_coverage.txt` (static/JIT transitions, unimplemented opcodes, SVC counts) and `recomp_gaps.json`.
- `suyu_cmd` sets `SUYU_RECOMP_STRICT` explicitly, at `src/suyu_cmd/suyu.cpp:1351-1359` (v).

### 2.4 End-to-end flows (*inference* from survey; verify before relying on details)

**Export:** ExeFS extraction → NSO parsing and block discovery (optionally seeded from gap JSON) → `EmitProject()` per module → top-level `CMakeLists.txt`, `recomp_modules.cmake`, `recomp_registration.c` → (Build output only) CMake configure/build with clang-cl or MSVC → link against the export build kit into a launcher → `PackageNativeExport()`.

**Run:** load NSO modules → `PrepareRecompProcess()` via frontend callback → ABI 6 handshakes (fastmem/guard/FPX) → `SetRecompLookup()` / base setter → `KProcess::InitializeInterfaces()` creates `ArmRecomp` → loop of `lookup(pc)` → block → SVC/interrupt handling. On a miss, either JIT fallback (Hybrid) or fatal (strict).

## 3. Core runtime (`src/core`, excluding recomp)

| Area | Key locations |
|---|---|
| `System` | `core/core.h:145`. Boot in `core/core.cpp`: `InitializeKernel` (~:263), `SetupForApplicationProcess` (~:273), `Load` (~:303). A recomp prepare-callback check sits in the load path. |
| Timing / CPU threads | `core/core_timing.h:61` (`CoreTiming`), `core/cpu_manager.h:33` (`CpuManager`). |
| Memory | `core/device_memory.h:19` (guest DRAM backing), `core/memory.h:59` (per-process virtual memory). |
| CPU interface | `core/arm/arm_interface.h:48` (`ArmInterface`); dynarmic glue in `core/arm/dynarmic/`. |
| Kernel | `hle/kernel/kernel.h:87` (`KernelCore`); `k_process`, `k_thread`, `k_scheduler`, `k_memory_manager`. |
| Services/IPC | `hle/service/service.h:49` (`ServiceFrameworkBase`), `hle_ipc.h`, `server_manager.h`, `sm/`. Major services include `acc`, `am`, `apm`, `audio`, `filesystem`, `hid`, `ns`, `nvdrv` (GPU driver ioctls), and `vi` (display). |
| Loading | `loader/loader.h:164` (`AppLoader`; NSP/XCI/NCA/NSO/NRO/KIP). Canonical NSO inventory comes from `loader/deconstructed_rom_directory.cpp:191-265` and `ReadNSOModules` ~`:334`; NCA/NAX/NSP/XCI loaders forward it. Standalone NSO records its file name in `loader/nso.cpp:242-252`. The launcher registration/DLL loader is `src/suyu_cmd/suyu.cpp:1889-2187` (static registry v4 path, legacy DLL probe, base setter). `file_sys/patch_manager.h` handles updates. `crypto/key_manager.h` handles keys. |
| Shared libraries | `src/common` (settings, logging, `string_util` UTF-8 helpers, `fs/` paths), `src/hid_core`, `src/audio_core`, `src/input_common`, `src/network`. |

**Boot sequence (*inference*):** `System::Load` → loader → `CreateApplicationProcess` → `ReadNSOModules` → recomp prepare (if registered) → `InitializeInterfaces` chooses the backend → GPU/audio/services setup → `AppletManager` starts the main thread → `System::Run`.

## 4. GPU and shaders (`src/video_core`, `src/shader_recompiler`)

| Area | Key locations |
|---|---|
| GPU front | `video_core/gpu.h:138` (`GPU`), `control/scheduler.h:23`, `control/channel_state.h:35`, `dma_pusher.h`, `gpu_thread.h:105`. |
| Engines | `engines/maxwell_3d.h:56`, `engines/draw_manager.h:20`, `fermi_2d`, `kepler_compute`, `maxwell_dma`, `kepler_memory`. |
| GPU memory | `memory_manager.h:36`. |
| Renderer contract | `rasterizer_interface.h:38`, `renderer_base.h:32`. |
| Backends | `renderer_vulkan/renderer_vulkan.h:40`, `renderer_opengl/renderer_opengl.h:37`, plus null and (per survey) Metal. |
| Caches | `texture_cache/`, `buffer_cache/`, shader/pipeline caches per backend, query cache, fence manager. |
| Presentation | `renderer_vulkan/vk_present_manager.h:39`, blit-screen classes, `present/` (scaling filters, `fsr.h`, `fxaa.h`, `smaa.h`, `window_adapt_pass.h`), `framebuffer_config.h:23`. |
| Shader frontend | `shader_recompiler/frontend/maxwell/translate_program.h:19`, Maxwell decoder, structured control flow. |
| IR and passes | `frontend/ir/program.h:17`, `ir_emitter.h:19`; `ir_opt/passes.h:15` (constant propagation, DCE, SSA, `RescalingPass`, lowering, texture/position/layer passes). |
| Shader backends | `backend/spirv/emit_spirv.h:36`, `backend/glsl/emit_glsl.h:15`, `backend/glasm/`, MSL. |

Fork-specific Vulkan areas checked on 2026-10-04:

| Area | Location | Notes |
|---|---|---|
| Graphics pipeline libraries (GPL) | `renderer_vulkan/vk_graphics_pipeline.h:66` (`GraphicsPipelineLibraryKey`, `GraphicsPipelineLibraryCache`); part creation and per-part key masking from `vk_graphics_pipeline.cpp:1107` (v) | Causes wrong textures/state in PLA; disabled by default (`use_graphics_pipeline_library`, `src/common/settings.h` ~`:597`). Device gating in `vulkan_common/vulkan_device.cpp` ~`:717`. Root cause unfixed. |
| VMA implementation | `video_core/vulkan_common/vma.cpp` | Compiled per executable (not in `video_core`); the Qt frontend skips it with static Qt, which already contains VMA 3.2.1. |

`renderer_vulkan.h:68` (v) declares `InterpolateFrames(Frame*, Frame*)`. Its purpose and origin (fork versus upstream) are unverified. `TurboMode` (`vk_turbo_mode.h`) is an upstream yuzu feature, not Aevora-specific.

**Candidate starting points for Milestone 5 (all *inference*; confirm the responsible layer before you implement):**

| Goal | Where to look first |
|---|---|
| Up to 4K | Resolution scaling settings, `RescalingPass`, SPIR-V rescaling layout, texture cache scaled images. |
| Ultrawide | Presentation layout (`FramebufferLayout`, blit/window-adapt pass). True ultrawide needs game camera/projection changes; presentation alone stretches or letterboxes. |
| 60 FPS | Game-side timing first; emulator presentation/pacing (`PresentManager`, `RendererBase`) only for pacing. |
| AO / post effects | `present/` filter chain for screen-space effects; shader backends for per-draw changes. |
| LoD / draw distance | Likely game-side parameters; GPU-side changes cannot add geometry the game culls. |

## 5. Frontends and auxiliary modules

| Module | Notes |
|---|---|
| `src/suyu` (Qt) | `GMainWindow` (`main.h:170`, `main.cpp` ~8,500 lines), `BootGame`/`LoadROM`, `bootmanager.h` (`EmuThread`, `GRenderWindow`), `game_list.h`, `configuration/`. Recompiled-image actions: `OnLoadRecompiledImage`, `LoadRecompiledImagesFrom`, `OnLaunchRecompiledBuild`, `OnLaunchStaticBuild`. |
| MCP server in Qt app | `src/suyu/mcp_server.{h,cpp}`, created at `main.cpp:5346-5445` (v). Port comes from `SUYU_MCP_PORT`. There are also "Hacker"/"Gamer" modes (`main.cpp:585-590` (v), `gamer_environment.*`). Fork-specific; purpose and security exposure not yet reviewed. |
| `src/suyu_cmd` | SDL command-line frontend (`suyu.cpp`). Also used as the static launcher host (export build kit). The launcher's window title comes from the executable name in `emu_window/emu_window_sdl2_vk.cpp` (~`:35-62`); convert paths with `Common::FS::PathToUTF8String`, never `path::string()`, on Windows. |
| Qt configuration tabs | `src/suyu/configuration/` (`configure_dialog.cpp`, `configure_per_game.cpp`, `configure_graphics_extensions.cpp` for `Category::RendererExtensions`). Labels and tooltips come from `src/suyu/configuration/shared_translation.cpp`; `src/qt_common/config/shared_translation.cpp` is a separate copy. |
| `src/qt_common`, `src/frontend_common` | Shared Qt utilities; cross-frontend config, content/firmware/mod/playtime managers. |
| `src/libretro_core` | Libretro core (`SUYU_BUILD_LIBRETRO_CORE`, default OFF). |
| `src/nintendo_library` | `Nintendo::Library` with authentication/purchase-history types. Fork-specific; no confirmed UI use. Review before any release. |
| `src/android` | Android frontend (upstream lineage). Outside the Windows x64 target. |
| `src/web_service`, `src/dedicated_room`, `src/yuzu_room_standalone`, `src/network` | Upstream online/room features. |

## 6. Build system and dependencies

- Root `CMakeLists.txt` requires CMake 3.22 and C++20. Main options include `ENABLE_QT`, `ENABLE_SDL3`, `YUZU_USE_BUNDLED_{QT,FFMPEG,SDL3}`, `YUZU_CMD`, `SUYU_BUILD_LIBRETRO_CORE`, `SUYU_NO_JIT`, `YUZU_TESTS`, `YUZU_ROOM`, and `ENABLE_WEB_SERVICE`.
- CPM (`CMakeModules/CPMUtil.cmake` + `cpmfile.json`, about 40 entries such as boost 1.90.0, fmt 12.1.0, openssl 3.6.2, SDL3 3.4.8, sirit v1.0.5, Vulkan 1.4.x, catch2 v3.13.0) is the main acquisition path. The cache defaults to `.cache/cpm` and can be moved with `-DCPM_SOURCE_CACHE`. Archives are extracted with `cmake -E tar` (`file(ARCHIVE_EXTRACT)` fails on non-ASCII entries under a non-UTF-8 ANSI code page). Patching needs Git for Windows (`patch`, found relative to `GIT_EXECUTABLE`); host shaders need `glslangValidator` (`src/video_core/host_shaders/CMakeLists.txt`). `SUYU_USE_LOCAL_EXTERNALS` (`LocalDependencies.cmake`) switches to local checkouts, in which case `.patch/` may not apply.
- `CMakeModules/ExportBuildKit.cmake`: `SUYU_EXPORT_BUILD_KIT=ON` (Windows, MSVC, Ninja, Release only; needs Python 3) produces the export build kit. See `tools/export_build_kit/` (`package.py`, `registry_probe.c`, `README.md`; kit revision string `suyu-aot-kit-abi6-fm1-gg1-fpx1-control-r3` per survey).
- Version string: `GenerateSCMRev.cmake` (`HEAD-<hash>-<ref>` or the tag). Note the hard-coded `v0.0.11 (mk8-recomp)` full name above.
- Bundled Qt on MSVC is `AddQt(Eden-CI/Qt 6.11.1)` (root `CMakeLists.txt` ~`:638`), verified on 2026-10-04 as a static build with the `/MD` CRT and without Svg/Test modules.
- Targets (`src/CMakeLists.txt`): `common`, `network`, `core`, `audio_core`, `video_core`, `hid_core`, `input_common`, `frontend_common`, `shader_recompiler`, `nintendo_library`, `dynarmic` (unless `SUYU_NO_JIT`), `suyu`, `suyu_cmd`, `tests`, optional `libretro_core`, `web_service`, and room servers.

Open build questions are tracked in WORK_PLAN Milestone 1: actual fetch success, Qt source and pinning, the dynarmic `mk8-recomp` revision, and whether vcpkg/conan are unused.

## 7. Tests

| Location | Content |
|---|---|
| `src/tests/` | Catch2 `tests` target covering common utilities, core timing, crypto, network, video_core memory tracker, and input calibration. |
| `tests/recompiler_smoke` | Translator golden-hash/smoke tests (synthetic code), including ABI6FMI and GGFMI variants; MSVC golden hashes require `/utf-8`, matching the real build. |
| `tests/recompiler_fastmem`, `recompiler_fpx`, `recompiler_partition` | ABI 6 feature and translation-unit partitioning tests. Fastmem covers FM1 and FMI legs plus the GG1 watched-page differential executable. `recompiler_fpx` differentially checks FPX1 against the exact body over seven FPCR settings and asserts the FZ hit rate; partitioning is relevant to the `C1002` investigation. |
| `tests/export_integration`, `export_regression`, `appdata_path`, `package_policy`, `export_build_kit` | Export pipeline, path, and packaging-policy tests. The test runner and requirements are not yet inspected. |
| `research/tests/` | Python regression scripts (`test_build_regressions.py`, `test_fixed_point_conversions.py`). |

## 8. Entry points for current WORK_PLAN items

| Investigation | Start here |
|---|---|
| `LNK1181` on `recomp_registration.c.obj` | Not reproduced on 2026-10-05 (clang-cl, ASCII parent path). `game_export.cpp:3991` (file creation), generated CMake around `:3920-3960`, configure/build around `:4229`, registration pruning around `:4701`; QString→path conversion in those blocks. |
| Hybrid performance (17 vs 30 FPS) | `SHL` translation gate in `arm64_to_c.h`; `RecompGuardGen::OnJitFallback()`; package `user/log/recomp_coverage.txt`. |
| Export pipeline time and memory | `game_export.cpp` clang-cl module build and launcher link (~`:4400-4560`); generated job pool in `arm64_to_c.h`. |
| GPL rendering defect | `vk_graphics_pipeline.{h,cpp}` part keys. |
| `C1002` heap exhaustion | `EmitProject()` unit splitting in `arm64_to_c.h`, `tests/recompiler_partition`, compiler flags in the generated module CMake. |
| Hybrid fallback accounting | `ArmRecomp::EnterFallback` / `RunFallback`, `recomp_gaps*`, `recomp_gap_session*`. |
| Strict static verification | `SUYU_RECOMP_STRICT`, `SUYU_NO_JIT`, `KProcess::InitializeInterfaces`. |
