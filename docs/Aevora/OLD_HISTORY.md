
# Aevora Old History

Archived, append-only record moved out of WORK_PLAN.md on 2026-10-05. Entries are kept verbatim; for current status, plans, and key facts see [WORK_PLAN.md](WORK_PLAN.md).

## Measurements moved out of arm_recomp.cpp comments (2026-10-10)

The original text is also retained in git history before the commit that shortened the code comments.

### Conditional-branch chaining

The emitter deliberately chains only unconditional B and BL instructions; conditional branches do not chain. In an earlier emitter, adding conditional-branch chaining measured 2.00 ms/frame versus 1.80 ms/frame without it. Across six repetitions, it changed a tight distribution into one spanning 1.70–2.54 ms/frame, while the Dynarmic arm measured 2.905 ms/frame in the same runs. It also increased JIT transitions from 69,008 to 88,983 and more than doubled lookup misses. The causes of the transition and lookup changes were unexplained, so conditional-branch chaining was not tried again until they could be understood.

### Chain budget

`kShortSliceChainBudget` is the number of direct-call chain blocks that may run before returning. Only this side sets it; generated code only decrements it, so the emitter does not need to agree on the value.

The budget bounds both how long a guest loop can run without the interrupt and SVC checks getting a look, and how deep the host stack goes when generated calls are not tail calls. Guest threads run on 2 MiB fibers (`512 * 4096` bytes, `common/fiber.cpp`), and a block frame carrying SIMD locals is not small. At the time, only the opt-in chains called blocks from blocks: the direct B/BL chain (`g_emit_direct_chain`) and the indirect RET/BR/BLR chain (`g_emit_indirect_chain`). Both were guaranteed tail calls under clang (`musttail`) and plain calls, bounded by this budget, elsewhere.

The measurements below were taken with an earlier emitter whose `ChainTo` chained B, BL, and the conditional forms. They do not describe the current emitter, where the module's `run_slice` loop iterates between blocks.

The earlier comment said that 256 overflowed the stack and crashed on boot, and that raising the budget was safe only when generated code was built with `-foptimize-sibling-calls`. That flag was only on the GNU branch; the MSVC branch emitted `/O1` then (`/O2` now) with no tail-call guarantee, and `ChainTo` calls into another translation unit. By that reasoning, Windows should have fallen over well below the ABI 4 default of 4096.

That did not happen. Mario Kart 8 was measured fully static with zero JIT transitions, replaying the same 8861-command fixture at each budget:

```text
32 -> 0.666x realtime    256 -> 0.696x    1024 -> 0.695x
4096 -> 0.678x           8192 -> 0.692x
```

Every run completed, including 256 and the 8192 ceiling, and each executed about 8.5 billion blocks. Either MSVC `/O1` does tail-call these, or the chains never get deep enough to matter. The budget was worth about 4% either way, with 32 the slowest, so there was little to gain from tuning it.

The bound remains necessary: one title not overflowing is not proof that a deeper-recursing one cannot. However, a raised budget should not be treated as known-dangerous on MSVC, because that is not what the measurement says.

The fixture replay measured throughput, not how quickly a core answers an interrupt. The budget also bounds that latency: the dispatcher checks `interrupted` only between slices, and `recomp_image_run_slice` does not check it. A core that wants a guest thread still running on another core spins in `KScheduler::ScheduleImplFiber` until that core reaches the check. Pokemon Legends: Arceus was measured in Hybrid mode (every module static, zero JIT transitions, overworld, 6-core/12-thread host) with `SUYU_RECOMP_CHAIN_BUDGET`:

```text
4096 -> about 14 FPS, 21-24% of the two busiest core threads' samples
        spinning in ScheduleImplFiber
256  -> 15-20 FPS, 8-10% spinning
128 and 512 -> same as 256
1024 -> about 1 FPS below 256
```

Therefore, long slices use 256, the middle of that plateau.

### Relocation policy

`kRelaPolicy` controls how much of the guest's relocation work is performed locally and what happens to `DT_RELASZ`/`DT_PLTRELSZ` afterwards; see `ApplyAllRelocations`.

Only rtld's own self-relocation needs local handling: it is a hand-written bootstrap loop that the recompiled path exits early out of. Everything after that is rtld's ordinary C++ relocation pass, which runs correctly once rtld itself is relocated.

- `rtld-only` — default. Pre-apply and zero rtld alone; leave every other module untouched for rtld to relocate, exactly as under dynarmic. Smash reaches an actual match on this.
- `zero-all` — the previous default: pre-apply every module and zero every module's sizes. It boots, but main's `.dynamic` then reports no relocations, so when `nn::ro` loads a fighter NRO it cannot bind main's deferred imports. Measured: 0 of main's 909 `lua2cpp::create_agent_fighter_*` slots are ever bound, and the match never starts.
- `zero-rtld` — pre-apply every module but zero only rtld's sizes. Measured not to work: rtld's pass then re-relocates modules already handled, and `svcBreak`s immediately (12071 breaks, first at 0.1s after the pre-apply).
- `restore-on-main` — zero every module, then put the sizes back on first execution in main. Measured not to work: rtld caches the sizes into its own module state when it parses `.dynamic` at boot, so a later write is never re-read and the slots stay unbound just as under `zero-all`.

## Archived on 2026-10-10 from WORK_PLAN.md (twenty-second-session cleanup)

## Work log, sessions 17-19

- Session 17: overlay stages A-C were completed in `97e32dd` and `0ae08ea`.
- Session 17: overlay stages D-E and split-main work were completed in `575b796`.
- Session 17: default-on options and their review fixes were completed in `585c886`.
- Session 17: the seventeenth-session record contains the associated smoke, integration, and negative-control evidence.
- Session 18: `2bed3c6` added the per-guest-thread ledger.
- Session 18: FMI-off A/B image and FPS observations were recorded.
- Session 18: hot sampler offsets were attributed to real RVAs.
- Session 18: idle samples were attributed to no runnable guest thread, not GPU/fence wait.
- Session 19: cleanup rebuild exited 0 after export-running work.
- Session 19: `SUYU_AOT_FP_SHARED` shared exact FP bodies in `c821526`, with harness, smoke, and integration evidence.
- Session 19: three reviewer P2 findings were fixed and the documentation batch was completed.
- Session 19: the host was rebuilt with `GIT_REV c821526d36db0427e44be438c682afb8fa8cf905`.
- Session 19: the user observed smaller generated files and about 1-2 FPS higher FPS; exact values remain pending.
- Session 20: `544dad2` added the FPXV all-lane vector FADD/FSUB fast path, with FPX harness and smoke evidence recorded in the dev19 changelog.
- Session 20: code review reported one AArch64 negative-control issue, fixed in `tests/recompiler_fpx/run.py` (included in the dev19 commit); no other findings were reported.
- Session 20: the host was rebuilt with `GIT_REV 544dad26944b3c5913b12be0e47b159d6ea59c42`; the user observed no FPS gain after a fresh FPXV-on export.

## Session handoff (2026-10-09, twenty-first session)

### State

Current HEAD is `544dad2` (`recomp: Add all-lane vector FADD/FSUB fast path (SUYU_AOT_FPX_VEC)`), on top of `a67035d` dev18 and `c821526`. FPXV is default-on unless exactly `0`, requires FPX1, is absent from shadow builds, records `fpx_vec` in manifests, and changes split bundle identity only when enabled. The helper covers S-lane FADD/FSUB only and declines on hosts without SSE2; results and ABI are unchanged.

### Contracts to preserve

- Preserve FP_SHARED's exact per-module bodies, option/cache identity, and shared generator source of truth.
- Preserve overlay architecture, honest `near`/fallback wording, and process/input ownership contracts.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error.
- `SUYU_RECOMP_THREAD_TIME` is opt-in and must not change execution; never fake cache producer identity.
- Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.
- Do not extend FPXV to FMUL/FMLA/D/FADDP without PLA sampler and `RECOMP_FPX_PROBE` keep-rate evidence.

### Open issues, in priority order

0. Obtain from the user the FPXV export's manifest `fpx_vec`, exact image sizes, `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`, FPS, hardware, and optional `SUYU_AOT_FPX_VEC=0` A/B.
1. Measure with the PLA sampler and `RECOMP_FPX_PROBE` whether vector FP operations are hot and what the all-lane keep rate is; do not extend FPXV without that evidence.
2. Carry over per-opcode-class host-size totals, SVC attribution, one-variable chain exports, Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 only with explicit approval.

### Immediate next action

Ask for the pending FPXV PLA export data, then measure vector-FP hotness and all-lane keep rate before considering any broader FPXV coverage.

## Earlier export evidence

The following observations come from the user's previously supplied Suyu log. Preserve a local copy before another run overwrites it; share only a sanitized summary.

- Runtime version string: `HEAD-40aeb31824-HEAD`, previously associated with the Suyu v0.0.12 release. Verify this checkout's relationship to that release separately.
- Firmware 18.1.0 was reported installed. This is an observed test condition, not a proven requirement or recommended universal version.
- The base game and update were read/decrypted and generated-code compilation was reached.
- Clang 22.1.8 reached a link failure: `LNK1181` for `recomp_registration.c.obj`. The displayed path contained a garbled form of `Pokemon` with an accented character in the original title.
- The MSVC 14.51 fallback encountered `C1002` (compiler out of heap space in pass 2) while compiling `recompiled_main_33.c`.
- Hybrid fallback did not produce a usable executable; linking still failed.

Treat these as separate investigations:

1. **Missing object/link failure:** determine whether the object was generated, compiled, moved, or referenced incorrectly. A path-encoding problem is a hypothesis; a garbled log alone does not prove it.
2. **Compiler memory failure:** determine the generated translation-unit size, peak memory, compiler configuration, and existing splitting behavior.

The earlier patch idea replaced non-ASCII characters in the generated filesystem name while preserving the display title. Its original line numbers differ from the current checkout. Reconstruct and test any needed fix against current code; do not apply it blindly.


## Progress record (archived)

## Session handoff (2026-10-06, end of tenth session)

Archived verbatim from the previous `WORK_PLAN.md` handoff. It recorded HEAD `02c0298`, completion of part 2 units 1-5, the 35/16/24/11 synthetic scenario counts, the unreachable fresh-staging cache reuse path, open launcher/static-module/MSVC/LNK1248 and PLA questions, and the decision to reintroduce a persistent AOT cache (option b). The previous handoff's immediate next action was to confirm the three cache questions and implement U1; the detailed source text remains in the repository history.

### 2026-10-07: Persistent AOT cache U1-U4 handoff

- U1 `2043b92`, U2 `edb6eb5`, U3 `71ce879`, and U4 uncommitted implement the persistent user-data AOT cache, atomic publish, lock, producer identity, reuse/invalidation, and synthetic integration matrix.
- Final `--only-aot-cache` verification passed 262/0; `--only-aot-cache-matrix` is available for the matrix alone. Real PLA size, hash, publish/copy timing, split export, and FMI remain open.

### 2026-10-04: Handoff documentation

Created the root agent guide and this work plan. Verified the existing repository layout, README, local Git baseline/origin, exporter naming code, and CMake dependency entry points. Documentation creation does not constitute a host build, export fix, or successful PLA run. No source-code fix was applied during this handoff.

### 2026-10-04: Codebase index

- Milestone: preparation for Milestones 1-2. Revision `9f932d5` plus untracked handoff docs.
- Added [CODEBASE_INDEX.md](CODEBASE_INDEX.md), a read-only survey of the recompiler, exporter, runtime, GPU, build system, frontends, and tests. Key references were checked against source and marked "(v)".
- Findings relevant to later milestones:
  - The ARM64驕ｶ鬘假ｽｾ・ｰ translator is `src/core/recompiler/arm64_to_c.h` (`EmitProject`).
  - The backend switch is `KProcess::InitializeInterfaces`.
  - Strict mode is `SUYU_RECOMP_STRICT`, and JIT-less builds use `SUYU_NO_JIT`.
  - `tests/` contains recompiler partitioning and export tests that were not yet run.
- Lineage question added to Milestone 1: `.gitmodules` pins dynarmic branch `mk8-recomp`, and `GenerateSCMRev.cmake` hard-codes `v0.0.11 (mk8-recomp)`.
- Unreviewed fork-specific components to assess before any release: the Qt MCP server (`SUYU_MCP_PORT`), "Hacker"/"Gamer" modes, and `src/nintendo_library`.
- No build, test, or export was run.
- Next: begin Milestone 1 environment and dependency verification.

### 2026-10-04: Milestone 1 environment and first configure attempt

- Milestone 1. Revision `9f932d5` plus untracked handoff docs.
- **Host environment (observed):** Windows 11 Home 10.0.26300; Intel Core i5-12400F; 15.8 GB RAM; NVIDIA GeForce RTX 2060 SUPER, driver 32.0.16.1714; 53.6 GB free on D:.
- **Tools (observed):** CMake 4.4.3; Ninja 1.13.2; LLVM/Clang 22.1.8 (clang, clang-cl on PATH); Python 3.14.7; Git 2.53.0 from GitHub Desktop's bundled MinGit (no full Git for Windows install). MSVC 14.51.36231 (cl 19.51.36260) in both Visual Studio Community 2026 (18.10) and Build Tools 2026; MSVC 14.44.35207 in Build Tools 2022. Windows SDKs 10.0.26100.0 and 10.0.28000.0. No system Qt, no vcpkg, no Vulkan SDK.
- The Visual Studio Community 2026 install has `vcvars64.bat` but no `vcvarsall.bat`, so its developer environment cannot be initialized; Build Tools 2026 was used instead (same MSVC version).
- **Dependency acquisition findings:** the active build system is Eden-derived CPMUtil (`CMakeModules/CPMUtil.cmake`, `AddJsonPackage` entries in `cpmfile.json`, downloads cached in `.cache/cpm`, which is git-ignored). `.gitmodules` (Suyu-era submodule list, dynarmic `mk8-recomp` fork) is not the acquisition path; dynarmic is in-tree at `src/dynarmic/`. On MSVC, Qt defaults to a bundled download (`YUZU_USE_BUNDLED_QT`, `AddQt(Eden-CI/Qt 6.11.1)`), as do SDL3, FFmpeg, and OpenSSL. Archived `docs/Suyu/_main-folder/README.md` identifies the predecessor source as `dougchansan/suyu-v0.0.4` branch `mk8-recomp` and documents a system-Qt 6.9.3 path; this checkout's defaults differ.
- **Host prerequisites not satisfied:** (1) `glslangValidator` (normally from the Vulkan SDK) is required for host shaders and executables (`src/video_core/host_shaders/CMakeLists.txt`); `SUYU_DEFER_SHADER_COMPILER_CHECK=ON` only allows configure for CPU/library targets. (2) CPMUtil requires a `patch` executable (and searches Git for Windows `usr/bin` and `usr/bin/core_perl` hints); GitHub Desktop's MinGit does not provide one.
- **Command:** a local untracked helper batch file (outside the repository, ASCII-only build path) called Build Tools 2026 `vcvars64.bat`, then `cmake -S <source> -B <ascii-build-dir>/msvc-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DYUZU_TESTS=OFF -DSUYU_DEFER_SHADER_COMPILER_CHECK=ON`.
- **Result:** compiler detection succeeded (MSVC 19.51.36260.0, x86_64). Configure failed at `CMakeLists.txt:406` (`AddJsonPackage(openssl-ci)`, bundled OpenSSL v4.0.0-11b7b6ea3b) with `Could not find patch executable` (`CPMUtil.cmake:253`). No dependency beyond that point was verified; no build was run.
- **Side effect:** configure copies `hooks/pre-commit` (whitespace check, sets `core.whitespace` locally) into `.git/hooks/` when absent (`CMakeLists.txt:308-311`); this happened.
- **Next / blocking:** install Git for Windows (for `patch`/perl) and the Vulkan SDK (for `glslangValidator`), or choose alternatives, then rerun configure without the deferral option. These are system installs awaiting the user's decision.

### 2026-10-04: Configure success, CPMUtil extraction fix, and first build

- Milestone 1. Revision `9f932d5` plus untracked handoff docs and one source change (below).
- **Prerequisites installed by the user:** Git for Windows 2.56.0 (GNU patch 2.8, perl in `usr/bin`); Vulkan SDK 1.4.363.0 (glslang 16.6.0).
- **Configure command:** Build Tools 2026 `vcvars64.bat`, then `cmake -S <source> -B <ascii-build-dir>/msvc-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DYUZU_TESTS=OFF -DCPM_SOURCE_CACHE=<ascii-build-dir>/cpm "-DGIT_EXECUTABLE=C:/Program Files/Git/cmd/git.exe" "-DGLSLANGVALIDATOR=<VulkanSDK>/Bin/glslangValidator.exe"`. `GIT_EXECUTABLE` is passed explicitly because CPMUtil locates `patch` relative to the first Git found, and GitHub Desktop's MinGit may precede Git for Windows on PATH.
- **Problem: Boost extraction failure.** `file(ARCHIVE_EXTRACT)` (CMake 4.4.3) aborted on `boost-1.90.0-cmake.tar.xz` with `archive_write_header(): Invalid empty pathname`. Evidence: extraction stopped just before entry `libs/wave/test/testwave/testfiles/utf8-test-繝ｻ繝ｻ・ｷ讖ｸ・ｽ・ｵ鬩阪・・ｽ・｢驕ｶ荳翫・`; system ANSI code page is 932; a minimal `cmake -P` script reproduced the failure; `cmake -E tar xf` extracted the full archive including that directory under both console code pages 932 and 65001. A path-length hypothesis (longest entry 278 characters under the original cache path; LongPathsEnabled=1) was tested by moving the CPM cache to a short path and ruled out.
- **Fix:** `CMakeModules/CPMUtil.cmake` `fetch_package` now extracts with `execute_process(${CMAKE_COMMAND} -E tar xf ...)` in the destination directory and calls `fatal` with archive path, destination, exit result, and stderr on failure. Other `file(ARCHIVE_EXTRACT)` uses (`externals/nx_tzdb/CMakeLists.txt`, `tools/cpm/cmake/update.cmake`) were not changed; nx_tzdb extracted without error in this run. The short CPM cache path was kept for an ASCII-only reproduction.
- **Configure result:** success in 167 s. All CPMUtil packages resolved as bundled, matching `cpmfile.json` (e.g. OpenSSL v4.0.0-11b7b6ea3b CI build, Boost 1.90.0, fmt 12.1.0, SDL3 3.4.8, FFmpeg 8.0.1, sirit v1.0.5, Vulkan-Headers v1.4.345, Qt6 6.11.1 bundled). Warnings: optional Qt components Svg and Test missing from the bundled Qt; `windeployqt` not found (`src/suyu/CMakeLists.txt:532`); RenderDoc and Doxygen not found (optional).
- **Build command:** same environment, `cmake --build <build-dir> --target suyu suyu-cmd -j 8` (parallelism limited for a 16 GB host).
- **Build result:** all compilation steps succeeded; `suyu-cmd.exe` linked (about 31.6 MB). `suyu.exe` failed to link with `LNK1169` after many `LNK2005` errors: VMA (VulkanMemoryAllocator) symbols such as `VmaAllocator_T::VmaAllocator_T` are defined both in bundled `Qt6Gui.lib(qrhivulkan.cpp.obj)` and in `video_core.lib(vma.cpp.obj)` (`src/video_core/vulkan_common/vma.cpp` defines `VMA_IMPLEMENTATION`). The bundled Qt 6.11.1 for windows-amd64 is a static build (no Qt DLLs; `Qt6Gui.lib` about 33 MB) using the `/MD` CRT, matching the project. A `LNK4098` LIBCMT defaultlib conflict warning also appeared (not yet investigated). Linker messages were emitted in Japanese and appear garbled in the UTF-8 log because of console code-page mismatch.
- **Not yet verified:** application startup, PLA loading, export kit.
- **Next / decision needed:** choose how to resolve the VMA duplicate definition 驕ｯ・ｶ郢晢ｽｻe.g. build against a shared (DLL) Qt with `-DYUZU_USE_BUNDLED_QT=OFF` (the archived predecessor README documents Qt 6.9.3 msvc2022_64), versus a source-level change. Linker options such as `/FORCE:MULTIPLE` would hide an ODR conflict and are not preferred.

### 2026-10-04: VMA implementation moved to executables; suyu.exe links

- Milestone 1. Base revision `53a4de4` (v0.0.1-dev1) plus uncommitted changes listed below.
- **Problem:** `suyu.exe` failed to link (LNK2005/LNK1169) because the static bundled Qt 6.11.1 (`Qt6Gui.lib`, `qrhivulkan.cpp.obj`) and `video_core` (`vulkan_common/vma.cpp`) both define the VulkanMemoryAllocator implementation.
- **Reference:** Eden solved the same conflict in PR #2994 (commit `7ca657d22f59`, investigated by the user): the implementation was removed from `video_core`; the Qt frontend defines it only for a dynamic Qt or Apple (`#if !defined(QT_STATICPLUGIN) || defined(__APPLE__)`), and the CLI always defines it.
- **Compatibility evidence:** the bundled Qt SBOM (`sbom/qtbase-6.11.1.spdx`) lists VMA 3.2.1; this build uses VMA headers v3.3.0. The VMA 3.3.0 changelog adds only `vmaImportVulkanFunctionsFromVolk` (unused here) and changes no public structures. Qt 6.11.1 `qrhivulkan.cpp` defines `VMA_DYNAMIC_VULKAN_FUNCTIONS 1` and `VMA_STATIC_VULKAN_FUNCTIONS 0`, matching `video_core/vulkan_common/vma.h`; it passes only `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`, as `vulkan_device.cpp` does. Difference: Qt sets `VMA_DEDICATED_ALLOCATION 0`, so with static Qt the GUI's allocator does not use the VK_KHR_dedicated_allocation path; the runtime effect is untested. `QT_STATICPLUGIN` is defined in this build.
- **Change (approach differs slightly from Eden):** `vma.cpp` is kept as the single implementation translation unit but removed from `video_core` and compiled per executable: added to `SUYU_CMD_SOURCES` (covers `suyu-cmd`, `suyu-cmd-static`, and the export-host targets, whose objects the export build kit packages), to `suyu_libretro` and `libretro_tas_frame_smoke` (with `Vulkan::Headers` and `GPUOpen::VulkanMemoryAllocator`), and to `suyu` only when `Qt6::Core` is not a `STATIC_LIBRARY` or on Apple. `tests` and Android already define the implementation themselves and were unchanged. Files: `src/video_core/CMakeLists.txt`, `src/video_core/vulkan_common/vma.cpp` (comment only), `src/suyu_cmd/CMakeLists.txt`, `src/libretro_core/CMakeLists.txt`, `src/suyu/CMakeLists.txt`.
- **Build:** same configuration and build command as the previous entry (incremental, 0.8 min). `suyu.exe` (about 60.3 MB) and `suyu-cmd.exe` (about 31.6 MB) linked. The earlier `LNK4098` LIBCMT warning did not appear in this link.
- **Smoke test:** `suyu-cmd.exe --help` exited with code 0 but printed nothing to the console; help output was not confirmed. `suyu.exe` was not launched by the agent, to avoid modifying an existing local Suyu configuration; GUI startup awaits the user's local test.
- **Not verified:** export build kit (`SUYU_EXPORT_BUILD_KIT=ON`) and libretro (`SUYU_BUILD_LIBRETRO_CORE=ON`) builds; Vulkan allocation at runtime in either frontend; `research/ios` targets (not inspected; may need `vma.cpp` if they link `core`). Old export build kits lack the new `vma.cpp.obj` and must be regenerated.
- **Next:** user launches `suyu.exe` (static Qt; no `windeployqt` step needed because there are no Qt DLLs) and, if possible, boots content with the Vulkan renderer to compare with the JIT baseline; build the export kit and libretro configurations.

### 2026-10-04: First PLA JIT run on the source build

- **Milestone:** 1. Revision: v0.0.1-dev2 (`be6181c`), committed and pushed by the user.
- **GUI startup:** `suyu.exe` from the MSVC Release build (bundled static Qt) started successfully.
- **PLA JIT run (observation):** Pokemon Legends: Arceus, title ID `01001F5010DFA000`, game version 1.1.1; Vulkan renderer on NVIDIA GeForce RTX 2060 SUPER, driver reported as NVIDIA 617.14.0.0; CPU backend Dynarmic (DYNAMIC JIT shown in the status bar), CPU accuracy Auto; GPU accuracy High; docked; resolution 1x; bilinear scaling filter; no anti-aliasing; `accelerate_astc = Gpu`; `astc_recompression = Uncompressed`; asynchronous shaders off; Vulkan driver pipeline cache on. Game progressed normally into the opening beach area; status bar showed 30 FPS / 33.33 ms frame time; no noticeable stutter reported.
- **Rendering defect:** several objects render with flat, untextured colors (teal/dark blue): the pier, rocks, a building, barrels, and Professor Laventon's hair. Other terrain, sand, trees, sky, and characters render with textures. The session log contains no Vulkan error lines, but `Debugging.gpu_log_level` was `Off`, so absence of errors is not conclusive. Window title shows the inherited version string `suyu v0.0.11 (mk8-recomp)`.
- **Layer:** GPU/texture path (`video_core`), not CPU recompilation; it occurs in the ordinary JIT path, so it would also affect Hybrid/static exports. Cause not yet identified.
- **Hypotheses to test (not yet confirmed):** (1) GPU ASTC decoding (compute-shader path; host shaders compiled with glslang 16.6.0 from Vulkan SDK 1.4.363.0); (2) regression inherited from the fork versus a pre-existing upstream issue; (3) stale shader/pipeline cache from a different build; (4) VMA differences after the static-Qt change (Qt's VMA 3.2.1 with `VMA_DEDICATED_ALLOCATION 0`); (5) GPU accuracy setting.
- **Minor:** the log repeatedly reports failure to open the play-time file in the configured play_time directory (directory missing); unrelated to rendering.
- **Next:** controlled A/B tests at the same location, changing one setting at a time (ASTC decoding Gpu to Cpu; cleared per-title shader cache; GPU accuracy), plus comparison with a previously used prebuilt Suyu if available and, if needed, Vulkan validation layers.

### 2026-10-04: GPL rendering defect isolated; GPL disabled by default

- Milestone 1 (JIT baseline). Base revision `be6181c` plus uncommitted changes below.
- **A/B results (user, PLA 1.1.1, same locations):** ASTC decoding Gpu驕ｶ鬘假ｽｾ・ｰpu: no change. Clearing the per-title pipeline cache: the originally affected objects rendered correctly, but other objects broke instead (player character showed another texture in patches; a large black triangle covered part of the sky), i.e. the affected objects depend on pipeline build order. GPU accuracy changes: no change. Prebuilt Suyu v0.0.12: same defect, so it is inherited code, not caused by this build's toolchain or the VMA change. Setting `use_graphics_pipeline_library=false` (edited in qt-config.ini): rendering correct.
- **Interpretation (hypothesis):** the fork's graphics pipeline library implementation (`vk_graphics_pipeline.cpp` ~1107 onward) caches four library parts under reduced keys (`GraphicsPipelineLibraryKey`, state bits masked per part, e.g. lines ~1141-1169); a key that drops state that matters would let a part built for another pipeline be reused. Not yet confirmed.
- **Relevance:** GPL only reduces pipeline-compilation stutter; it is unrelated to static CPU recompilation and should not change rendering results.
- **UI finding:** the `RendererExtensions` settings (GPL, `dyna_state`, `vertex_input_dynamic_state`, `sample_shading`) were not reachable from the Qt frontend: `configure_graphics_extensions.*` was compiled but never instantiated and populated itself from `RendererAdvanced`; `src/suyu/configuration/shared_translation.cpp` lacked entries for the three EDS-related settings and the `ExtendedDynamicState` enumeration.
- **Change:** `src/common/settings.h`: `use_graphics_pipeline_library` defaults to `false` on all platforms (with reason comment). `configure_graphics_extensions.cpp` now uses `Category::RendererExtensions`; the tab was added to the global configuration dialog (Graphics group, "GraphicsExtensions") and to per-game configuration ("GPU Extensions"); `shared_translation.cpp` gained the missing entries and an updated GPL tooltip. Files: `src/common/settings.h`, `src/suyu/configuration/configure_dialog.{h,cpp}`, `configure_per_game.{h,cpp}`, `configure_graphics_extensions.cpp`, `shared_translation.cpp`. Existing configurations still marked `\default=true` follow the new default.
- **Build:** same configuration; incremental build of `suyu` and `suyu-cmd` succeeded (4.8 min).
- **User verification:** the GraphicsExtensions tab appears in the global configuration with the GPL option off; per-game configuration shows the GPU Extensions tab; with qt-config.ini restored to `use_graphics_pipeline_library\default=true` (following the new default) and the pipeline cache cleared, PLA renders correctly at both test locations.
- **Not verified yet:** saving a changed value from the new tab, and a per-game override taking effect.
- **Deferred (root cause):** add a temporary diagnostic that bypasses the GPL part cache to confirm a key defect, then fix the per-part keys and re-enable GPL; verify by clearing caches and restarting repeatedly at the same locations.

### 2026-10-04: Export build kit produced

- Milestone 1 (export kit). Revision `0ddf47d` (v0.0.1-dev3), clean working tree.
- **Command:** same environment as earlier entries (Build Tools 2026 `vcvars64.bat`, Vulkan SDK on the environment); reconfigured the existing MSVC Ninja Release tree with `-DSUYU_EXPORT_BUILD_KIT=ON`, then `cmake --build <build-dir> --target export-build-kit -j 8`. This builds `suyu-export-host-strict` and `suyu-export-host-hybrid` and runs `tools/export_build_kit/package.py`.
- **Result:** success (configure 14 s; build 0.8 min incremental). Output `<build-dir>/bin/export-build-kit`, beside `suyu.exe` as the kit README expects: `CMakeLists.txt`, `strict.cmake`, `hybrid.cmake`, `manifest.json`, `revision.txt`, `src/`, and 59 files under `inputs/`; about 357 MB.
- **Manifest:** revision `suyu-aot-kit-abi6-fm1-gg1-fpx1-control-r3`; `producer_compiler_version` 19.51.36260.0; `policy_version` `suyu-package-policy-1`; `producer_source_revision` `0ddf47d2a6c01e08e2fd8537d88ec267dbeb11b1` (matches HEAD); 64 hashed files.
- **VMA check:** `vma.cpp.obj` from each export-host target is packaged and referenced once in both `strict.cmake` and `hybrid.cmake`, so the v0.0.1-dev2 per-executable VMA change reaches exported launchers.
- **Path hygiene:** no user-profile paths found in the kit's `.cmake`/`.json` files (objects and libraries may still contain debug source-path strings, as the kit README notes).
- **Compatibility note:** the kit README says release CI uses the VS 2022 14.44 toolset as the baseline so newer MSVC can consume a kit; this local kit was produced with MSVC 14.51, so a consumer must use 14.51 or newer. Decide the release baseline before publishing kits (Milestone 6).
- **Licensing note for Milestone 6:** the build prints "Warning static builds of getopt violate the Lesser GNU Public License" (MSVC targets link `getopt` statically). Review before distributing binaries.
- **Not verified:** consuming the kit through the GUI exporter (Build output) with PLA; generated-code compilation and linking; the earlier `LNK1181`/`C1002` failures.
- **Next:** Milestone 2 驕ｯ・ｶ郢晢ｽｻreproduce the PLA export with this application and kit in an ASCII-only export path, collect the export/build logs, and separate the missing-object (`recomp_registration.c.obj`) and compiler-memory (`C1002`) investigations.

### 2026-10-04: Generated-code build parallelism (Milestone 2)

- Milestone 2. Base revision `0ddf47d` (v0.0.1-dev3) plus uncommitted changes below.
- **Observation (PLA 1.1.1 Hybrid + Build export in progress, ASCII path, clang-cl 22.1.8):** only one `clang-cl` ran at a time; CPU load ~31% of 12 logical CPUs. The generated module CMake had `RECOMP_JOBS=1`, and its Ninja pool `recomp_compile` had depth 1. 223 generated units (main 174, sdk 18, subsdk1 18, subsdk0 11, rtld 2), median 22 MB / max 32 MB of C; 105 units took 94 min (~54 s each), projecting ~3.4 h for compilation. One clang-cl compile peaked at ~1.3 GB working set. The exporter configured the module build inside the source-build tree (`<build-dir>/recomp_clang`) with generated sources in the export's hidden `.suyu-export-<id>/aot_cache/exefs`.
- **Cause:** `arm64_to_c.h` emitted `RECOMP_JOBS = min(cores, total RAM MB / 8192)`; 16191 MB / 8192 = 1. The 8 GB budget came from cl.exe heap failures (C1060/C1002), not clang-cl. Units were limited mainly by the 20,000-block ceiling.
- **Change:**
  - Unit limits: new constants `kDefaultUnitSourceBytes` (8 MiB) and `kDefaultUnitBlocks` (5,000) replace the 32 MiB / 20,000-block production limits; `EmitProject` gains a `unit_block_limit` parameter (0 treated as 1). The exporter accepts `SUYU_RECOMP_UNIT_BLOCKS` and `SUYU_RECOMP_UNIT_KB` overrides (positive integers; invalid values are logged and ignored) and logs the effective limits once.
  - Job count in the generated CMake: per-compile budget 8192 MB for real MSVC cl.exe and 1536 MB for clang/clang-cl/GCC; 4096 MB reserved for the OS and emulator; jobs = clamp((RAM 驕ｶ荳翫・reserve) / budget, 1, logical cores). Overrides: `SUYU_RECOMP_JOB_MEMORY_MB`, `SUYU_RECOMP_JOBS`, or `-DRECOMP_JOBS`.
  - Files: `src/core/recompiler/arm64_to_c.h`, `src/suyu/game_export.cpp`, `tests/recompiler_partition/{export.cpp,run.py,README.md}` (block-ceiling fixture now 5,001 blocks; baseline mode passes an explicit 20,000-block limit).
- **Verification:** `python -B tests/recompiler_partition/run.py --build <ascii-dir>` from a VS 2026 Build Tools x64 environment: PASS (exact bodies/dispatch entries, GG slots, single- and multi-module execution). Configuring the generated test project reports 1 job with cl.exe (8192 MB budget) and 7 jobs with clang-cl (1536 MB budget) on this 16 GB / 12-thread host; `SUYU_RECOMP_JOBS=3` yields 3.
- **Not verified yet:** suyu has not been rebuilt with this change (the running export locks `suyu.exe`); no PLA export with the new limits; actual peak memory of 8 MiB / 5,000-block units; total export time; link time with ~4x more objects; runtime performance (fewer cross-unit inlining opportunities may reduce FPS); `build_native_unix.sh` (Source export helper for Linux/macOS) still uses RAM / 8192 and was not changed.
- **Next:** after the current export finishes, record its result (link outcome, compiler, JIT-fallback modules, time), rebuild suyu, re-export, and measure time, peak memory per compile, and in-game FPS against the 1-job build; tune unit limits and budget from those measurements.

### 2026-10-05: First PLA Hybrid export completed; launcher does not start

- **Milestone 2.** Exporter: `suyu.exe` built from `0ddf47d` (v0.0.1-dev3) with the locally built export build kit; the generated-code parallelism change was not yet in this binary.
- **Settings (user):** PLA 1.1.1, CPU backend suyu Hybrid JIT + AOT, format Build, Dynarmic fallback allowed, ASCII-only output parent directory. Compiler: clang-cl 22.1.8 (LLVM). Duration about 4 hours (user), dominated by one-at-a-time compilation of 223 generated units.
- **Result:** export reported success. `aot_manifest.json`: `effective_backend` suyu-hybrid, `image_abi` 6, `total_modules` 5, `fallback_modules` empty (no module left to JIT), `recompiled_c_blocks` 4,131,332, `total_blocks_analyzed` 3,854,104, `total_instructions` 17,169,816, `total_text_bytes` 68,679,264, `requires_runtime_codegen` false. The single-file launcher is about 1.87 GB (1,870,654,976 bytes).
- **Earlier failures:** neither the `LNK1181` missing `recomp_registration.c.obj` nor the MSVC `C1002` heap failure reproduced in this run (clang-cl path, ASCII parent directory). This does not prove they are fixed: the earlier run used a non-ASCII-containing path, and the MSVC fallback path was not exercised.
- **Non-ASCII naming:** the package folder and launcher name are derived from the game title and contain a non-ASCII character (`Pok繝ｻ繝ｻ・ｽ・ｩmon Legends_ Arceus - Hybrid AOT + JIT`), even with an ASCII output parent directory.
- **Defect found:** `README_NATIVE_EXPORT.txt` says "Game code compiled with: O2" and the manifest has `recomp_compiler` = `O2`; the compiler name (clang-cl) is not recorded correctly (`last_recomp_compiler` handling in `src/suyu/game_export.cpp`). Not yet investigated.
- **Launch failure (user):** double-clicking the launcher does not start the game. Not yet investigated; deferred until after the next commit at the user's request. Hypotheses to check: image size (~1.87 GB) and loader limits, the non-ASCII executable/package name, missing runtime files or keys/firmware lookup, and the package's own `user/` log.
- **Next:** commit the pending changes, then diagnose the launch failure from the package log and a console launch; afterwards re-export with the parallel build to measure time and compare.

### 2026-10-05: Exported launcher crash on non-ASCII name fixed

- **Milestone 2.** Base revision `6f4dffd` (v0.0.1-dev4) plus uncommitted changes below.
- **Evidence:** the package log (`user/log/suyu_log.txt` in the export) ended 0.4 s after start with `Frontend <Critical> suyu_cmd\suyu.cpp:2937:main: Unhandled fatal exception in suyu-cmd: No mapping for the Unicode character exists in the target multi-byte code page.` The last step logged before it was the SDL configuration write; `Window created, loading game...` was never logged.
- **Cause:** `src/suyu_cmd/emu_window/emu_window_sdl2_vk.cpp` (static-recompiled launcher builds) derived the window title with `exe.stem().string()`. On Windows, `std::filesystem::path::string()` converts through the ANSI code page; the launcher name contains `繝ｻ繝ｻ・ｽ・ｩ`, which code page 932 cannot represent, so the conversion threw. The same class of conversion existed in `src/suyu_cmd/suyu.cpp` for the Discord title fallback (`std::filesystem::path(filepath).stem().string()`, which also interpreted UTF-8 as the ANSI code page). Other `.string()` uses in `suyu_cmd` are in non-Windows branches.
- **Fix:** both sites now use `Common::FS::PathToUTF8String` (and `Common::FS::ToU8String` for the UTF-8 input path). Files: `src/suyu_cmd/emu_window/emu_window_sdl2_vk.cpp`, `src/suyu_cmd/suyu.cpp`.
- **Verification:** rebuilt `suyu`, `suyu-cmd`, and `export-build-kit`. Copied the rebuilt `suyu-export-host-hybrid.exe` (built with `SUYU_CMD_STATIC_RECOMP`) to a portable folder under a name containing `繝ｻ繝ｻ・ｽ・ｩ`, with an empty `exefs/main` so startup proceeds past ROM discovery: the log reached `Window created, loading game...` and then failed to load the dummy ROM (Error 30), as expected. A full PLA launch still requires a new export, because the existing package cannot be relinked: the export's hidden working directory (generated sources and `recomp_registration.c`) is deleted after export.
- **Exporter side effects found:** when `suyu.exe` runs from a source build, the exporter configures the source build tree itself with `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, leaves those cache entries pointing at the deleted hidden directory, links `bin/suyu-cmd-static.exe` (game-derived, ~1.87 GB) in the build tree, and builds generated modules in `<build-dir>/recomp_clang`. With those entries set, `package.py` refuses to package an export build kit. They were removed with `cmake -U ...`, which then rebuilt the whole tree (1,740 steps) because compile definitions changed. Treat the build tree's `bin/suyu-cmd-static.exe` and `recomp_clang/` as local game-derived output.
- **Kit note:** the regenerated kit records `producer_source_revision` `6f4dffd` although it was built with uncommitted changes; the manifest records HEAD only.
- **Next:** user commits; then re-export PLA (Hybrid, Build, ASCII parent directory) with the rebuilt `suyu.exe` to verify launch and measure export time, peak memory per compile, and job count with the new parallel limits.

### 2026-10-05: Hybrid re-export with parallel build; launcher starts; 17 FPS

- Milestones 2-3. Exporter `suyu.exe` built with the v0.0.1-dev5 changes (committed afterwards as `28e6caf`); PLA 1.1.1, Hybrid JIT + AOT, Build format, ASCII-only output parent directory, clang-cl 22.1.8.
- **Parallel compile (sampled every 15 s):** 881 generated units (223 before); 7 concurrent clang-cl compiles in nearly all samples; per-process peak working set 0.97 GB (budget 1.5 GB); total clang-cl working set at most ~5.0 GB; CPU load average 52%, maximum 76%. Compilation took ~46 min (02:37-03:22), versus ~3.4 h at one job.
- **Remaining export time:** ~28 min full rebuild of the source build tree at `-j 2` after the exporter changed its cache entries (03:22-03:50), then the MSVC link of `suyu-cmd-static` with a PDB from 03:50; `link.exe` reached ~7.8 GB working set, free RAM fell to ~0.2 GB, and commit charge reached ~30.1 of 30.9 GB. The export completed successfully (user); the exact end time was not recorded.
- **Launch:** the launcher (name containing `繝ｻ繝ｻ・ｽ・ｩ`) now starts and plays (user), confirming the v0.0.1-dev5 window-title fix in a real export.
- **Performance (user):** about 17 FPS in the overworld, unstable, versus 30 FPS on the JIT baseline. Settings differ (package: GPU accuracy Normal, CPU video decoding, GPL off, EDS2; JIT baseline: GPU accuracy High).
- **Coverage (package `user/log/recomp_coverage.txt`):** 9,453,517,616 static blocks executed; 16,300,755 SVCs; 219,674 static-to-JIT transitions, all from unimplemented opcodes, 0 lookup misses; 43,034 blocks per transition. All 15 unimplemented encodings decode to `SHL` (vector, immediate) with 32-bit elements (Q=0 and Q=1).
- **Guard state:** at activation, 0 of 5 modules verified on every entry; after the first JIT fallback the log reports `module N pinned to verify-always: JIT fallback created` for all five modules (`RecompGuardGen::OnJitFallback`).
- **Hypothesis (not confirmed):** per-entry instruction verification across all modules, triggered by the `SHL` fallbacks, is the main Hybrid overhead. See the session handoff for the proposed experiment.

### 2026-10-05: SHL already implemented but gated off for Hybrid

- **Milestone:** 3 (investigation). **Source revision:** code inspection only, no build/run; applies to current HEAD.
- **Finding:** `SHL` (vector, immediate) with 32-bit elements (observed encoding `4F235426`) is not missing from the ARM64-to-C translator but is deliberately disabled for Hybrid exports. It is implemented in two places in `src/core/recompiler/arm64_to_c.h`: the non-saturating immediate-shift block (~line 1732, condition `(!left || U || g_translate_all)`) and a dedicated SHL block (`kTranslateShiftLeftImmediate`, ~line 2889). Both are gated by `g_translate_all`.
- **Gate mechanism:** `g_translate_all` defaults to off for Hybrid exports. It is set by `TranslateAllForExport(strict_static, explicitly_requested)` (~line 340); for a Hybrid export it is enabled by environment variable `SUYU_AOT_TRANSLATE_ALL` (non-empty value) before starting `suyu.exe` (`src/suyu/game_export.cpp` ~line 3430). The `translate_all` setting is recorded in the export report and triggers code regeneration on re-export.
- **Rationale:** the comment records a measurement inherited from upstream (MK8D recompilation work): 2.13 vs 1.95 ms/frame when the block was compiled versus left to the JIT. That measurement predates the first-JIT-fallback pin effect where `RecompGuardGen::OnJitFallback()` sets all five modules to verify-always, causing ~9.45 billion block entries to re-check their instruction words. The original measurement does not account for this pin's impact.
- **Next action:** re-export with `SUYU_AOT_TRANSLATE_ALL=1`, measure FPS under matched settings, and check `user/log/recomp_coverage.txt` (static-to-JIT transitions should drop to near 0) and package logs (verify-always pins should not appear). Only after measuring, decide whether to enable SHL by default for Hybrid exports.

### 2026-10-05: Hybrid re-export with SHL translated; no fallbacks, 14 FPS

- Export: same PLA 1.1.1 Hybrid/Build/clang-cl setup, started with `SUYU_AOT_TRANSLATE_ALL=1`; `aot_manifest.json` records `"translate_all": true`, no fallback modules, `requires_runtime_codegen: false`. Package written 07:59 (suyu.exe started 07:01; stage timings not recorded). `export-package.json` still records producer revision `6f4dffd` and version string `suyu v0.0.11 (mk8-recomp)`.
- Run (user): intro through the first-area cutscene, ~389 s. Average about 14 FPS (user), versus about 17 FPS in the earlier overworld run. The scenes differ, so the two numbers are not a direct comparison.
- Coverage: 8,097,224,085 static blocks, 10,394,840 SVCs, 0 static-to-JIT transitions (0 lookup misses, 0 unimplemented opcodes); `recomp_gaps.json` reports a clean run with no unimplemented opcodes. Most frequent SVCs: 0x1B 3.78 M, 0x1A 3.60 M, 0x10 1.16 M, 0x1C 0.59 M, 0x1D 0.56 M.
- Guard: `0 of 5 module(s) verify on every entry` at activation; at exit `sticky=0`, bumps `jit=0`, `code_write=0`; no `pinned to verify-always` line. The per-entry verification pin did not occur.
- Conclusion: the hypothesis that SHL fallbacks and the resulting verify-always pin are the main Hybrid overhead is refuted; removing them did not raise FPS. The cause of the gap to the JIT baseline (30 FPS) is unknown.
- Confounders found: the package started with an empty shader cache (`Total Pipeline Count: 0`) with asynchronous shader compilation off, so the intro run included pipeline compilation stalls; the cache is now populated (`vulkan.bin` 3.2 MB, `vulkan_pipelines.bin` 5.8 MB). Video decoding differs (package `nvdec_emulation=1` CPU, JIT baseline `nvdec_emulation=2` GPU). GPU accuracy is `gpu_accuracy=1` in both configs (the earlier note that the package used Normal should be re-checked).
- Shader cache warmth test (user): re-ran the same scene after the shader cache was populated from the previous run; FPS stayed about 14. The cold shader cache was not the cause of the FPS gap.

### 2026-10-05: Hybrid per-thread CPU sampling in the overworld

- **Method:** launcher running in the overworld (user). Per-thread CPU time over 60 s (`Get-Process` thread times, names from `GetThreadDescription`), then instruction-pointer sampling of selected threads (SuspendThread/GetThreadContext, 2000 rounds at 5 ms) symbolized with dbghelp against `suyu-cmd-static.pdb` (public symbols only) and `dumpbin /disasm`. The sampler is a local diagnostic outside the repository.
- **CPU over 60 s (process total ~3.4 cores of 12):** `HostTiming` 99%, `CPUCore_2` 90%, `CPUCore_1` 88%, `CPUCore_0` 47%, `GPU` 6%, `VulkanWorker` 3%. The emulated GPU thread is mostly idle (95% of samples in a wait), so the run is not GPU-thread bound.
- **`CPUCore_1`/`CPUCore_2` hot path:** 21-24% of samples in an unnamed function that public symbols attribute to `Service::HID::Gesture::~Gesture` (+0x10A); disassembly shows it is the context-switch loop of `KScheduler::ScheduleImplFiber` (`src/core/hle/kernel/k_scheduler.cpp`, `while (!highest_priority_thread->m_context_guard.try_lock())`), spinning on `xchg [thread+0x730]` until another core releases the next thread's context guard. About 10-12% of samples are idle waits; the rest is generated code (one hot block `blk_main_0000000002d1e290` at 15-18% in the first sample set), `recomp_load*/ldp/stp` helpers, and `recomp_lookup_*`. `CPUCore_0`: 57% idle wait.
- **`HostTiming` hot path:** 99% of samples in `Common::Event::WaitFor`, which busy-waits on Windows by design (`src/common/thread.cpp`); this is shared with the JIT path and not specific to recompiled execution.
- **Hypothesis (not confirmed):** preemption latency. ABI 6 modules with `run_slice` enable long slices (`SetRecompLongSlices(true)` in `src/suyu_cmd/suyu.cpp`, chain budget 4096 in `src/core/arm/recomp/arm_recomp.cpp`). The recompiled dispatcher checks `interrupted` only between slices, and `recomp_image_run_slice` runs up to the budget without checking it. At roughly 7 M blocks/s per core, 4096 blocks is on the order of 0.5 ms, during which a core that needs a thread still held by another core spins in `ScheduleImplFiber`. Dynarmic checks for halts much more often. The earlier budget measurement (32-8192, about 4% difference) was single-title MK8 fixture replay and did not measure multi-core contention.
- **Proposed experiment (no rebuild or re-export):** launch the package with `SUYU_RECOMP_CHAIN_BUDGET` set (read once at startup, valid 1-8192) to 256 and 32, compare FPS in the same overworld scene, and re-sample the share of `ScheduleImplFiber` spinning.

### 2026-10-05: Chain budget experiment and hot-block inspection

- **Run (user):** same Hybrid package, overworld, launched with `SUYU_RECOMP_CHAIN_BUDGET=256` (default with long slices: 4096). FPS improved from about 14 to about 15-20 (user). A run with 32 was also tried; the user reported the improvement for 256.
- **Re-sampled at 256** (launcher started from a non-elevated shell; an elevated launch hid thread times from the non-elevated sampler): `HostTiming` 99.5%, `CPUCore_2` 88%, `CPUCore_1` 88%, `CPUCore_0` 41%, `GPU` 7%, `CPUCore_3` idle. Share of samples spinning in `KScheduler::ScheduleImplFiber` fell from 21-24% to 8-10% on `CPUCore_1`/`CPUCore_2`. The preemption-latency hypothesis is partly supported: a smaller budget reduces the spin and raises FPS, but does not close the gap to JIT (30 FPS).
- **Hot block:** `blk_main_0000000002d1e290` takes 15-18% of `CPUCore_1`/`CPUCore_2` samples, spread thinly over a host function of about 19,000 x64 instructions (`dumpbin /disasm` of `suyu-cmd-static.exe`). The instruction mix is dominated by flag materialization (`test`/`cmp`/`sete`/`setne`/`cmov` together several thousand), every guest load/store in the sampled part is a call to an out-of-line runtime helper (`recomp_load32/64`, `recomp_store32`, `recomp_ldp64`, `recomp_stp64`) even though FM1 fastmem is negotiated, and the guest context pointer is reloaded from the stack before most accesses.
- **Build flags verified** in `recomp_clang/build.ninja`: generated units are compiled by clang-cl with `/O2 /Ob2 /GS- /Gy /bigobj /fp:precise`, so the code shape is not caused by a missing optimization flag. The generated sources were already deleted from the temporary export directory, so the C for this block was not inspected.
- **Remaining hypotheses (not confirmed):** (a) out-of-line memory helpers per guest access, (b) eager NZCV flag computation, and (c) context-pointer spills in very large block functions make generated code slower than Dynarmic's output.
- **Candidate changes:** make the chain-budget improvement permanent (lower default for long slices and/or check the interrupt flag inside `recomp_image_run_slice`), measured with budgets such as 128/512/1024; then inspect generated C for the hot block (re-export keeping the sources) before changing the emitter's memory-access or flag code.

### 2026-10-05: Long-slice chain budget lowered to 256 (v0.0.1-dev6, unverified in export)

- **Milestone:** 3 (performance investigation and runtime tuning). **Base revision:** uncommitted changes in `src/core/arm/recomp/arm_recomp.cpp`.
- **Budget sweep results (user, PLA Hybrid, overworld, per `SUYU_RECOMP_CHAIN_BUDGET`):** 4096 (previous default) about 14 FPS; 256 驕ｶ鄙ｫ繝ｻ15-20 FPS; 128 and 512 驕ｶ鄙ｫ繝ｻsame as 256; 1024 驕ｶ鄙ｫ繝ｻabout 1 FPS lower than 256; 32 was tried earlier and was not better than 256. CPU sampling at 256 reduced `KScheduler::ScheduleImplFiber` spinning from 21-24% to 8-10% on busy cores.
- **Code change:** defined new constants `kShortSliceChainBudget = 32` and `kLongSliceChainBudget = 256` in `arm_recomp.cpp` (~lines 209-210), with a comment recording the PLA measurement that led to the 256 choice. `SetRecompLongSlices(true)` now selects `kLongSliceChainBudget` instead of a hard-coded 4096. The environment variable `SUYU_RECOMP_CHAIN_BUDGET` (1-8192) still overrides at runtime.
- **Verified:** `arm_recomp.cpp` compiled successfully (object file `core.dir/arm/recomp/arm_recomp.cpp.obj` updated; only pre-existing LOG_* macro redefinition warnings emitted).
- **Not verified:** full `suyu` rebuild, new PLA Hybrid export with the default, in-game FPS confirmation with the new default, recompiler smoke tests (`tests/recompiler_smoke/run.c` sets chain budgets explicitly and does not depend on the runtime default; no test runs were performed).
- **Next:** rebuild `suyu` and re-export PLA Hybrid (Build format, ASCII parent path, no `SUYU_RECOMP_CHAIN_BUDGET` set, keep `SUYU_AOT_TRANSLATE_ALL=1`); confirm 15-20 FPS overworld. Then either continue to option B (generated-code quality: keep sources on re-export and inspect the hot block `blk_main_0000000002d1e290`) or try other candidate budgets (128, 512, 1024) if tuning is needed. Longer-term: ending a slice on `SignalInterrupt` instead of a fixed budget needs a race-free design review.

### 2026-10-05: Is the Hybrid CPU path ahead-of-time static recompilation?

- **Question (user):** does the current pipeline really run the game's CPU code as AOT static recompilation? Answered from code inspection plus evidence already recorded in earlier records (no new run).
- **Export side:** the exporter translates each module's ARM64 `.text` to C (`src/core/recompiler/arm64_to_c.h`), compiles it with clang-cl into static libraries (`src/suyu_cmd/recomp_modules/CMakeLists.txt`), and links them into `suyu-cmd-static.exe`. `dumpbin /disasm` of that exe shows native x64 block functions such as `blk_main_0000000002d1e290`. `aot_manifest.json`: `requires_runtime_codegen: false`, `host_machine_code_blocks: 0`, five modules, no fallback modules.
- **Runtime side:** `KProcess::InitializeInterfaces` (`src/core/hle/kernel/k_process.cpp`) gives only the application process `Core::ArmRecomp`; Dynarmic (`ArmDynarmic64`) is constructed lazily in `ArmRecomp::EnterFallback` (`src/core/arm/recomp/arm_recomp.cpp`) on a lookup miss or unhandled opcode, and that path always calls `RecompGuardGen::OnJitFallback()`. The SHL-translated run recorded 8.10 billion static blocks, 0 static-to-JIT transitions, and guard bumps `jit=0`, so no JIT instance was created in that run.
- **Conclusion:** the application's CPU code ran as ahead-of-time recompiled native code in that run. Limits: (a) Hybrid keeps Dynarmic linked as a fallback, so "no JIT" is an observation per run; `SUYU_RECOMP_STRICT=1` makes any fallback fatal and logs the PC, but a Hybrid launcher cannot be switched to it from outside: `suyu-cmd-static` built with `SUYU_CMD_STATIC_RECOMP_HYBRID` sets `SUYU_RECOMP_STRICT=0` at the start of `main` (`src/suyu_cmd/suyu.cpp`), overriding the environment. A "suyu static AOT (Experimental)" export forces translate-all (`TranslateAllForExport`) and builds the launcher with `SUYU_CMD_STATIC_RECOMP_STRICT`, which sets it to 1; that export is the check. (b) Translation is per block with a runtime dispatcher and lookup for indirect branches and slice boundaries, not function-level decompilation. (c) Kernel, services, memory, GPU, and audio remain the Suyu HLE runtime; guest code and data stay mapped in the emulated page table and the code guard verifies instruction words. (d) Runtime code generation outside the CPU path remains: the GPU macro JIT (Xbyak, seen in GPU-thread samples) and shader/pipeline compilation.
- **Next:** export PLA with the "suyu static AOT (Experimental)" backend (Build format) on v0.0.1-dev6 and run it to the overworld; any fallback stops execution and logs the PC. Correction: an earlier suggestion to set `SUYU_RECOMP_STRICT=1` when starting the Hybrid launcher would have had no effect.

### 2026-10-05: Session close

- Updated the agent setup with `AGENTS.md`, documented the Orca roles in `CLAUDE.md`, and removed the obsolete `.claude/agents/md-implementer.md` subagent in favor of the Codex md Implementer. Rewrote the session handoff with the current v0.0.1-dev6 state, performance evidence, open issues, and next action.


### 2026-10-05: First static AOT export (v0.0.1-dev6) runs with no fallback, 16-20 FPS

- **Milestone:** 4 (fully static execution evaluation). The `suyu.exe` exporter was built from v0.0.1-dev6. The export package recorded backend `static`, format `build`, producer revision `a6b869025143832cd6f6488f9c4abb25ad8e8568` (dev6), and the existing `suyu v0.0.11 (mk8-recomp)` build string. The parent output directory was ASCII-only; the package folder and launcher name contained `繝ｻ繝ｻ・ｽ・ｩ`, and the launcher started successfully.
- **Settings:** PLA `suyu static AOT (Experimental)` export with fallback disabled and translate-all enabled. `aot_manifest.json` recorded requested/effective backend `suyu-static`, `fallback_enabled: false`, no fallback modules, `requires_runtime_codegen: false`, image ABI 6, and five modules (`main`, `rtld`, `sdk`, `subsdk0`, `subsdk1`). It recorded 4,131,332 recompiled C blocks, 17,169,816 total instructions, and 68,679,264 total text bytes. The package was written at 12:20; export start time and stage timings were not recorded. `recomp_compiler` still recorded `O2`, which is a known defect. The launcher size was about 1.87 GB (1,870,680,576 bytes). The user did not report setting `SUYU_RECOMP_CHAIN_BUDGET`; the dev6 default of 256 is therefore assumed.
- **Result:** The launcher reached gameplay and ran stably at about 16-20 FPS in the user's observed scene. The user compared this with about 30 FPS for the JIT baseline and 15-20 FPS for the earlier Hybrid translate-all run with a manual budget of 256. The scene was not specified, so this is not a fully matched performance comparison.
- **Coverage and runtime diagnostics:** The run lasted about 417 seconds. All five modules reported `ArmRecomp active`; guard-v2 was ready; image ABI 6, page-table fastmem, and FPX1 native FP were negotiated; and the application process used ArmRecomp. Coverage recorded 9,716,893,306 static blocks, 10,977,276 SVCs, zero static-to-JIT transitions (zero lookup misses and zero unimplemented opcodes), zero JIT-to-static transitions, zero unresolved import traps, and no transitions because execution was fully static. `recomp_gaps.json` recorded one strict-static run, one clean run, `last_run_strict_static: true`, no unimplemented opcodes, and no unattributed misses. At exit, guard sticky, JIT-bump, and code-write counters were zero; at activation, zero of five modules verified on every entry. There were no Critical log lines or crash dump.
- **Observations only:** Eight unresolved rtld GOT/PLT symbols were reported during relocation (`__EX_start`/`__EX_end`, `__got_start`/`__got_end`, and `__tdata`/`__tbss` alignment absolute/relative). The log also reported that BSD networking was not initialized, `profiles.dat` was missing, and the Suyu path was empty. These errors were not investigated.
- **Conclusion:** In the exercised run, which reached gameplay, PLA CPU code ran entirely as AOT-recompiled native code with JIT fallback disabled by construction (strict static), structurally confirming the earlier Hybrid translate-all observation. The dev6 default of 256 gives about the same FPS as the earlier manual budget of 256, but the gap to JIT remains. Generated-code quality (option B) is now the top open issue.
- **Not verified:** A complete gameplay checklist across regions, battles, transitions, and save/load cycles; complete gameplay compatibility; or a fully matched JIT/Hybrid performance comparison. Dynarmic remains linked into the binary because `SUYU_NO_JIT` was not used. GPU macro JIT and shader compilation remain runtime code generation outside the CPU path.
- **Next:** Run a Source-format static AOT export with the same settings to obtain generated C for `blk_main_0000000002d1e290`, then inspect memory-access helpers, flag materialization, and context-pointer reloads in that block and the secondary candidates `blk_main_0000000002d3e81c`, `blk_main_0000000002e1bbd0`, and `blk_main_0000000002d1cf14`. Static AOT RIP sampling is complete; the result is recorded below.

### 2026-10-05: Static AOT per-thread CPU and RIP sampling in the overworld

- **Target and method:** The v0.0.1-dev6 static AOT Build launcher (the package from the previous entry) was paused in the overworld by the user and started from a non-elevated shell. As with the Hybrid sampling entries, per-thread CPU time was measured over 60 s using thread names from `GetThreadDescription`, followed by instruction-pointer sampling of the `CPUCore_*` and `GPU` threads for 2,000 rounds at 5 ms; samples were symbolized with dbghelp against the build tree's `suyu-cmd-static.pdb` produced by the same export, using public symbols only. The sampler is a local diagnostic outside the repository.
- **CPU over 60 s:** Process total was about 3.34 cores of 12. `HostTiming` used 99%, `CPUCore_2` 89%, `CPUCore_1` 88%, `CPUCore_0` 40%, `GPU` 7%, and `VulkanWorker` 5%; `CPUCore_3` was idle (99.8% wait). This matches the Hybrid budget-256 profile (`HostTiming` 99.5%, `CPUCore_2`/`CPUCore_1` 88%, `CPUCore_0` 41%, `GPU` 7%).
- **`CPUCore_2` RIP samples:** `blk_main_0000000002d1e290` 19.4%; `KScheduler::ScheduleImplFiber` spin, which public symbols attribute to `Service::HID::Gesture::~Gesture`, 10.3%; idle wait 9.9%; `recomp_load64` 2.9%; `blk_main_0000000002d3e81c` 2.9%; `blk_main_0000000002e1bbd0` 2.7%; `blk_main_0000000002d1cf14` 2.6%; and `recomp_lookup_main` 2.3%.
- **`CPUCore_1` RIP samples:** `blk_main_0000000002d1e290` 15.5%; `ScheduleImplFiber` spin 12.5%; idle wait 11.0%; `recomp_load64` 4.5%; `blk_main_0000000002e1bbd0` 2.3%; `blk_main_0000000002d3e81c` 2.3%; `recomp_lookup_main` 2.2%; and `blk_main_0000000002d1cf14` 2.2%.
- **`CPUCore_0` and `GPU` RIP samples:** `CPUCore_0` was 61% idle wait; the remainder was spread across `recomp_load64` 3.8%, `recomp_lookup_main` 1.3%, `recomp_image_run_slice_main` 1.0%, `recomp_ldp64`/`recomp_stp64`, and small blocks. The `GPU` thread was 93.7% idle wait, so it was not GPU-thread bound.
- **Conclusion:** `blk_main_0000000002d1e290` is re-confirmed as the hot block on the static AOT launcher, at 15-19% of busy-core samples, matching the Hybrid result of 15-18%. The secondary hot blocks are the same set as in the Hybrid budget-256 sample. `ScheduleImplFiber` spin was 10-12.5%, versus 8-10% for Hybrid at budget 256; this difference is within the variation of a single 10 s sample and was not investigated. Out-of-line memory helpers (`recomp_load64/32`, `recomp_stp64`, and `recomp_ldp64`) and `recomp_lookup_main` remain visible.
- **Next:** Static AOT and Hybrid translate-all therefore have the same CPU cost profile, so generated-code quality analysis can use either. The next step remains the Source-format static AOT export and inspection of `blk_main_0000000002d1e290`, with `blk_main_0000000002d3e81c`, `blk_main_0000000002e1bbd0`, and `blk_main_0000000002d1cf14` as secondary candidates.

### 2026-10-05: Hot block C inspection; FZ closes the FPX1 native-FP gate (hypothesis)

- **Evidence and scope:** Code inspection only, with no build or run: a Source-format static AOT export made with v0.0.1-dev6 and the same settings, plus the current emitter source.
- `blk_main_0000000002d1e290` is in generated unit `recompiled_main_614.c` and contains 89 guest instructions: 31 guest memory accesses, all calls to out-of-line helpers (`recomp_load64`, `recomp_ldp64`, and related helpers); one NZCV flag computation; and 49 vector floating-point operations (FMUL/FMLA/FADD-family, four 32-bit lanes), plus vector permutes (ZIP/UZP/EXT/INS) emitted as `memcpy` and byte loops.
- Each vector FP operation is emitted as a per-lane loop. If `RECOMP_FPX_OPEN(c)` is true and `recomp_fpx_<op>32` accepts the native result, the native result is kept; otherwise the inline exact body runs. The exact body is a software IEEE implementation using 9-word multi-precision arrays, about 60 lines of C per operation, instantiated at each operation site. This explains the approximately 19,000 x64 instructions in the host function.
- `RECOMP_FPX_OPEN(c)` is `((c)->fpsr & 16) && !((c)->fpcr & RECOMP_FPX_FPCR_MASK)`, with mask `0x07C8FF07` plus inhibit bit 32 (`src/core/recompiler/arm64_to_c.h`, approximately lines 6446-6467). The mask includes FPCR.FZ (bit 24), DN (bit 25), AHP (bit 26), RMode (bits 22-23), trap enables, and FEAT_AFP bits. Therefore, whenever the guest has FZ set, every covered FP operation takes the exact software body.
- PLA's main module contains guest functions that set FPCR.FZ: `blk_main_0000000002b057f0` (`MRS FPCR; ORR 0x1000000; MSR FPCR`), `blk_main_00000000031c8e30` (the same sequence, then `RET`), and `blk_main_00000000031c8e50` (sets or clears FZ from argument bit 0). The SDK also contains further FPCR/FPSR writes that were not inspected. The kernel starts threads with FPCR 0 (`src/core/hle/kernel/k_thread.cpp`).
- The emitter has FPX shadow mode (`g_emit_fpx_shadow`) with per-operation call/gate-open/kept counts and `SUYU_RECOMP_FPX_SHADOW_LOG`, but the exporter never enables it; the FPX1 gate-open rate in PLA has not been measured.
- **Hypothesis (not confirmed):** PLA sets FZ early on its threads, so FPX1 never applies and float-heavy blocks such as this hot block run the software FP body. Dynarmic handles FZ with host FTZ/DAZ natively, which could explain much of the JIT gap.
- The `recomp_runtime.h` comment records that inlining memory helpers at every access was measured worse on MK8D (image 100 to 222 MB, -14% FPS). A `game_export.cpp` comment records that ABI 6 features (FM1, GG1, FPX1) together raised MK8D from 34.6 to 56.3 FPS; whether MK8D runs with FZ set is unknown.
- Also observed: every guest instruction in a block is a possible entry point (a switch on `c->pc` with one label per instruction in `arm64_to_c.h`, approximately lines 5254-5288), because lookup indexes every instruction; guest registers live in `GuestContext` memory.
- **Proposed next steps:** (1) verify guest FPCR and gate-open rate at runtime with a diagnostic counter or shadow build; (2) if confirmed, extend the FPX1 gate to accept FZ only when no input is denormal and the result is a normal finite value (or an exact zero case), so FZ cannot change the value or add IDC/UFC. An FZ-aware sum rule is needed because 驕ｯ・ｶ陟匁ｺｶy finite sum驕ｯ・ｶ郢晢ｽｻmay be tiny. Add targeted tests, expect generated output and `tests/recompiler_smoke` hashes to change, re-export, and measure FPS.

### 2026-10-05: Guest FPCR measured: PLA runs with FZ set (FPX1 gate closed)

- **Change:** `src/core/hle/kernel/physical_core.cpp` gains the env-gated `FpcrStats` diagnostic in `PhysicalCore::SaveContext` (application processes only; function-local singleton; mutex; up to 16 tracked values, overflow warning; summary every 65,536 saves). Reviewed by the Code reviewer (Orca, gpt-6.1-sol high): no blocking issues; three minor points (named capacity constant and overflow warning, process-wide wording, documentation) were applied. The reviewer confirmed `SaveContext` runs before the old thread's context guard is released, and that `ArmDynarmic64::GetContext` and `ArmRecomp::GetContext` both fill `fpcr`/`fpsr` (`ArmRecomp` masks off the host inhibit bit 32).
- **Build:** `rebuild-suyu.bat` (clears the export cache entries; full tree rebuild) then an incremental rebuild of `suyu` and `suyu-cmd` after the review fixes; both linked.
- **Run (user):** `suyu.exe` from the source build, `SUYU_FPCR_STATS=1`, CPU backend Dynarmic (JIT), PLA 1.1.1 to the overworld, about 3 minutes of logging after boot.
- **Result:** Exactly two guest FPCR values were seen: `0x00000000` (default) and `0x01000000` (RN, FZ=1, DN=0, AHP=0, no traps). FZ=1 first appeared about 2.5 s after the application started. Share of context saves with FZ=1: 57.4% after 65,536 saves, 75.7% at 131,072, 79.7% at 196,608, and 96.1% at the last summary (3,473,408 saves: 3,336,948 FZ=1, 136,460 FZ=0). FPSR.IXC was set in every recorded save (JIT FPSR tracking may not be exact).
- **Conclusion:** The hypothesis from the hot-block inspection is confirmed at the guest-state level: PLA runs almost entirely with FPCR.FZ set, and `RECOMP_FPX_OPEN` rejects FZ (mask `0x07C8FF07`), so in recompiled code nearly every covered FP operation takes the exact software body instead of the native result. The FPCR value is guest state written by MSR FPCR and is backend-independent; a static AOT run with this diagnostic has not been made. The per-operation gate-open rate and the FPS effect of fixing it are not yet measured.
- **Next:** Implement an FZ-aware FPX1 gate: accept FZ (and evaluate DN, since NaN results are never kept) and, when FZ is set, keep a native result only if no input is denormal and the result is a normal finite value (or an exact zero-product/quotient case); sums need the same rule because a tiny sum is flushed under FZ. Add targeted tests (synthetic inputs covering denormal inputs/outputs under FZ), expect generated output and `recompiler_smoke` hashes to change, then re-export static AOT with `SUYU_FPCR_STATS=1` and measure FPS.

### 2026-10-05: FZ-aware FPX1 gate (v0.0.1-dev8, unverified in PLA)

- **Change:** `RECOMP_FPX_FPCR_MASK` changed from `0x07C8FF07` to `0x06C8FF07`, so FZ no longer closes the gate. `RECOMP_FPX_FZ(c)` is passed as `int fz` to every `recomp_fpx_<op>{32,64}`. Under FZ, subnormal inputs are refused (FMA also checks the addend; sqrt checks its input); sums are kept only if BIG or exactly zero (`RECOMP_FPX_KEEPSUM`); products, quotients, FMA, recps, and rsqrts keep their existing BIG / exact zero-operand rules. ARM FPRound flushes under FZ using the unrounded exponent: a BIG result (at least smallest normal + 1 ulp) comes from an unrounded value above the smallest normal, and with normal/zero inputs FZ changes neither value nor FPSR. The host FP mode check (`guest_fp_env.h` `kFpcrMask`) is unchanged by design.
- **Tests:** FPX harness (clang-cl 22.1.8, Ninja, `--jobs 8 --hits --controls`, default cases): L1/L2/L3 fpx vs soft covered 100,587,904 cases, with 0 value and 0 FPSR mismatches across FPCR `{0, FZ, DN, RP, RM, RZ, FZ|DN}`; negative controls `nokeep`/`nomid`/`mxcsr` failed as designed; inhibit had 0 mismatches; shadow had 0 fast-vs-exact mismatches; env check passed. L1 aggregate hit rate: FPCR 0 67.28%, FPCR FZ 64.52% (0% before). The new harness assertion passed on the change and failed on HEAD `ff44909` with `FPX fast path kept no result at FPCR FZ although it did at FPCR 0` in a reduced run (`--cases 1000 --adv 2000 --legs 13`).
- **Smoke suite:** Committed goldens were stale. The apparent compiler dependence came from UTF-8 em dashes in `arm64_to_c.h` string literals and the MSVC `smoke_export` target lacking `/utf-8` (code page 932), which garbled comments/messages; clang-cl and real suyu builds (`/utf-8`, `src/CMakeLists.txt`) were unaffected, so PLA exports were never affected. The fix adds `/utf-8` for MSVC and recomputes the goldens: ABI5 `a325a2d0...`, FM1 `e2785f0c...`, FPX `1ef792bc...` (MSVC and clang-cl identical; HEAD to change differs only in `recomp_runtime.h`). `python tests/recompiler_smoke/run.py` passed with MSVC and with `--cc`/`--cxx` clang-cl, including the coverage loop and all ABI5/ABI6/ABI6GG/FPX/GGFPX checks. An earlier note that the goldens did not match since import was incorrect: with `/utf-8` or clang-cl the import commit matches the old goldens; the v0.0.1-dev4 CMake change moved them.
- **Review:** Code reviewer (Orca, gpt-6.1-sol high) found no correctness issues in the FZ logic after checking the BIG boundary, smallest normal, tiny/exact sums, signed zeros, zero-operand cases, FMA including the x86-64 binary64 path and midpoint fallback, recps/rsqrts, sqrt, and FMA64 hardware-FMA gating. The stale smoke goldens were P1 and are resolved; the nonblocking suggestion to assert FZ hits was implemented.
- **Build:** suyu and suyu-cmd rebuilt with the emitter change (including `game_export.cpp` recompilation); both linked.
- **Not verified:** L5 hardware golden (requires AArch64); partition test was not rerun. The PLA static AOT re-export and FPS measurement are recorded in [FZ-aware FPX1 gate verified in PLA static AOT: 25-30 FPS](#2026-10-05-fz-aware-fpx1-gate-verified-in-pla-static-aot-25-30-fps).
- **Next:** Confirm the next runtime bottleneck with per-thread CPU and RIP sampling on the dev8 static AOT launcher.

### 2026-10-05: FZ-aware FPX1 gate verified in PLA static AOT: 25-30 FPS

- **Export:** `suyu.exe` was rebuilt with the dev8 emitter change at 13:54. Generated-module build started at 14:16 and the package was written at 15:40. The Build-format package used effective backend `suyu-static`, fallback disabled, translate-all enabled, image ABI 6, and image features 7 (`FM1+GG1+FPX1`). `export-package.json` records producer revision `d0be6ce`, which is stale because the working tree was `ff44909` plus uncommitted dev8 changes. Build-format exports delete generated headers after linking, so the presence of the FZ change in the package is established by build timing rather than generated text inspection.
- **Run:** The log recorded `Recompiled FPX1 native FP: negotiated (handshake 0x102)` and `ArmRecomp FPX1 native FP: on, host FP mode enforced`. Over about 102 seconds, coverage recorded 4,585,440,060 static blocks and 6,333,722 SVCs, with static-to-JIT 0 (0 lookup misses and 0 unimplemented opcodes), JIT-to-static 0, and unresolved import traps 0. `recomp_gaps.json` recorded `strict_static_runs: 4`, `clean_runs: 4`, `last_run_strict_static: true`, and no unimplemented opcodes. Guard sticky, JIT, and code-write counters were all zero, and there were no crash dumps. `SUYU_FPCR_STATS` was not set.
- **Performance:** The user observed about 25-30 FPS in the overworld, versus about 16-20 FPS with the dev6 static AOT export and 30 FPS with the JIT baseline. The observations used the same overworld area as earlier reports, but exact scene matching was not controlled.
- **Conclusion:** The FZ-closed FPX1 gate was the main cause of the remaining static AOT performance gap; with it fixed, static AOT is close to the JIT baseline in the observed scene. A complete gameplay checklist, frame-time stability measurements, and renewed CPU sampling to identify the next bottleneck remain unverified.
- **Next:** User commits dev8, then per-thread CPU and RIP sampling runs on the dev8 static AOT launcher in the overworld. Candidate remaining costs are out-of-line memory helpers, per-instruction entry labels, `ScheduleImplFiber` spin, and `HostTiming` busy-wait.

### 2026-10-05: Session close (fifth session)

Rewrote the session handoff for a new session, recording the dev8 static AOT result, confirmed FPCR/FZ finding, diagnostics, test commands, open issues, and immediate next action. Preserved the earlier progress records.

### 2026-10-05: Dev8 static AOT per-thread CPU and RIP sampling in the overworld

- **Milestone and configuration:** Milestone 4. Source revision `HEAD` was `70336e7`; runtime/export version was `v0.0.1-dev8`. This was a dev8 static AOT Build export with fallback disabled. The user stopped in the overworld. The launcher was started from a non-elevated shell, and the shell running the sampler was non-elevated.
- **Command and comparison:** `powershell -File sample_static.ps1 -ProcId <launcher PID> -Out rip_samples_static_dev8.txt` measured a 60 s per-thread CPU window, then collected 2,000 RIP samples per `CPUCore`/`GPU` thread at 5 ms. Output was kept in the local diag directory outside the repository and compared with the dev6 static sample `rip_samples_static.txt` taken the same way. FPS during the sampling window was not recorded.
- **CPU and RIP summary:**

| Metric | Dev6 static AOT | Dev8 static AOT |
|---|---|---|
| Process CPU (sum of threads, % of one core) | 334.3% | 320.3% |
| HostTiming thread CPU | 99.0% | 98.7% |
| CPUCore_0 / 1 / 2 / 3 CPU | 40.4 / 88.1 / 89.2 / 0.2% | 55.4 / 68.7 / 68.0 / 0.4% |
| GPU thread CPU | 7.3% | 16.7% |
| `ZwWaitForAlertByThreadId` share on CPUCore_1 / CPUCore_2 / CPUCore_0 | 11.0 / 9.9 / 61.2% | 34.8 / 37.6 / 48.6% |
| `blk_main_0000000002d1e290` share on CPUCore_1 / CPUCore_2 | 15.5 / 19.4% | 2.0 / 3.5% |
| Symbol shown as `Service::HID::Gesture::~Gesture` (previously attributed to `ScheduleImplFiber`) on CPUCore_1 / CPUCore_2 | 12.5 / 10.3% | 1.2 / 1.1% |
| `recomp_load64` on CPUCore_1 / CPUCore_2 / CPUCore_0 | 4.5 / 2.9 / 3.8% | 5.3 / 4.8 / 5.9% |
| `recomp_lookup_main` on CPUCore_1 / CPUCore_2 / CPUCore_0 | 2.2 / 2.3 / 1.3% | 3.3 / 3.2 / 2.7% |

- **Other observations:** On CPUCore_1, the memory helpers `recomp_load64/load32/load8/ldp64/stp64` were about 10.9% of all RIP samples (about 17% of non-wait samples); `recomp_lookup_main` plus `recomp_lookup_sdk` were about 4.4%. `Core::ArmRecomp::RunThread` was 2.4-2.7% and `recomp_image_run_slice_*` was 0.9-1.5% per core. `<unknown>` was 5.0-5.9% per core; these are unresolved addresses and are not yet attributed. CPUCore_0 showed `Dynarmic::SpinLock::Unlock` at 0.9% and `Core::Memory::Memory::Impl::WriteExclusive<unsigned int>` at 0.7%; this is an exclusive-access path, not JIT execution. The GPU thread showed `NtGdiDdDDIQueryAdapterInfo` at 6.0%, which was not seen in the dev6 sample. CPUCore_3 was idle (99.5% wait) in both samples. No per-instruction entry labels appeared in the top-15 lists.
- **Findings:** The dev8 FZ-aware FPX1 change reduced the dominant hot block from 15-19% to 2-3.5% and reduced the scheduler-fiber symbol share from 10-12.5% to about 1%. CPU cores now wait 35-49% of samples instead of about 10% on cores 1-2.
- **Hypotheses (unverified):** Guest CPU execution is no longer the saturating cost in this scene. The remaining 25-30 versus 30 FPS gap may come from frame pacing, GPU/presentation, `HostTiming`, or remaining memory-helper/lookup overhead. A JIT sample in the same scene, with FPS/frame-time recorded during both samples, is needed to decide.
- **Next:** Sample the JIT baseline in the same scene with the same method and record FPS during both samples; attribute `<unknown>` addresses; investigate `NtGdiDdDDIQueryAdapterInfo` on the GPU thread only if the JIT sample lacks it.

### 2026-10-05: Same-scene JIT sampling: remaining gap is guest CPU cost

- **Target and method:** The `suyu.exe` JIT process was sampled in the same overworld scene with the same `sample_static.ps1` method used for dev8 static AOT. FPS during either sampling window was not recorded.

| Metric | JIT | Dev8 static AOT |
|---|---:|---:|
| Process CPU (sum of threads, % of one core) | 275.7% | 320.3% |
| CPUCore_0 / CPUCore_1 / CPUCore_2 CPU | 44.7 / 47.2 / 45.7% | 55.4 / 68.7 / 68.0% |
| Core wait share | 56-61% | 35-49% |
| JIT code (`<unknown>`) share | 32-35% of samples | Not applicable |
| HostTiming CPU | 98.2% | 98.7% |
| GPU thread CPU | 18.0% | 16.7% |
| `NtGdiDdDDIQueryAdapterInfo` on GPU thread | 6.3% | 6.0% |

- **Conclusion:** HostTiming, GPU thread load, and `NtGdiDdDDIQueryAdapterInfo` are not AOT-specific. The remaining gap is guest CPU execution cost: static AOT uses roughly 1.5-2x the JIT CPU time per core in this scene, estimated from non-wait shares. This refutes the earlier GPU/presentation hypothesis in the dev8 sampling entry; the `<unknown>` JIT samples are JIT code and do not identify a more specific cost.
- **Unmeasured:** FPS during the samples, image size, compile time, and frame-time stability were not recorded.

### 2026-10-05: Source inspection of memory access and the opt-in FMI inline fast path (v0.0.1-dev9, unmeasured in PLA)

- **Why:** Dev8 static AOT RIP sampling showed out-of-line memory helpers at about 11% of CPUCore_1 samples. Source-export inspection of the dev8 static export (clang-cl `/O2`, no LTO) found the helpers defined in `recomp_runtime.c`, a separate translation unit, so every guest memory access is a call even when FM1 serves it; a clang-cl `/O2` compile of the hot block confirmed `call recomp_load64` and similar calls at each access.
- **Additional inspection:** Per-instruction entry labels (`_recomp_entry_N`) force guest registers through `GuestContext` memory between instructions (store then reload); FMI does not address this later candidate. The hot block expands to about 24,000 assembly lines because exact FP fallback bodies are inline; instruction-cache pressure is a hypothesis and is unverified.
- **Design:** FMI appends `recomp_fmi_*` static inline fast paths, including a GG1 watch-word store entry, to `recomp_runtime.h`. Object-like renames such as `#define recomp_load64 recomp_fmi_load64` apply only in generated units; `recomp_runtime.c` defines `RECOMP_RUNTIME_C 1` first so its definitions retain their original names. The unchanged out-of-line helper remains the fallback. FMI changes no ABI or feature bits and is opt-in because an existing header note records whole-helper inlining as worse on MK8D (image 100 to 222 MB, -14% FPS); FMI only inlines the fast path and is unmeasured on PLA.
- **Exporter:** `SUYU_AOT_FM_INLINE=1` enables FMI only when FM1 is enabled. The manifest records `"fm_inline"`; cache reuse requires a matching value, and manifests without the field count as FMI off.
- **Verification:** `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl` passed fmdiff's five legs (FM1 fast, FM1 slow, ABI 5, FMI fast, FMI fallback) and fmdiff_gg's four legs (GG1 runtime with a random eighth of pages watched), each with 20,000,768 operations and 0 mismatches. A deliberate GG1 FMI store mutation without the watch check was caught. `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl` passed all seven variants (ABI5, ABI6, ABI6GG, FPX, GGFPX, ABI6FMI, GGFMI); goldens were unchanged and `check_fm_inline` asserted that only `recomp_runtime.c` and `.h` differ. `rebuild-suyu.bat` succeeded after clearing export cache entries and building `suyu` and `suyu-cmd -j 8`.
- **Review:** The Code reviewer (Orca, gpt-6.1-sol, high effort) reported no findings and additionally built FMI with FPX1/shadow and GG1 combinations. It noted that the synthetic standalone GG1 target fails to link `g_recomp_gg_word` with FMI on and off, a pre-existing issue outside this change, and recommended permanent FMI+FPX smoke variants and exporter cache tests for old/off/on manifests.
- **Not run:** PLA export with FMI, image size/compile-time/FPS/sampling measurements, the FPX harness, partition test, and build-kit linking remain unmeasured.

### 2026-10-05: PLA export with FMI fails: static launcher image exceeds 2 GB (LNK1248)

- **Export and evidence:** The user exported PLA static AOT in Build format with fallback disabled and `SUYU_AOT_FM_INLINE=1`. clang-cl compiled all five modules without errors from 17:43 to 18:39 local time; the local `recomp_static_main.lib` was about 3.71 GB. Linking `suyu-cmd-static` with link.exe failed after about 33 minutes: `LINK : fatal error LNK1248: image size (8C0E0000) exceeds maximum allowable size (80000000)`. The resulting image would be about 2.19 GB, over the 2 GB MSVC x64 image limit.
- **A/B compile:** With clang-cl `/O2 /bigobj /fp:precise`, dev8 versus FMI compile results were: unit 614, 60 to 62 seconds, 691 to 695 MB peak working set, 5.5 to 5.8 MB object; unit 661, 141 to 155 seconds, 1,039 to 1,023 MB, 4.8 to 4.9 MB; unit 350, 19 to 19 seconds, 385 to 383 MB, 4.1 to 4.7 MB. Object growth was 2-15%, with no compile errors and similar compile memory. The dev8 header was regenerated from the emitter and verified byte-identical to the dev8 Source export header.
- **Failure path:** The fallback-disabled export then reconfigured the source tree and entered the existing MSVC per-module path. Its 8 GB-per-`cl.exe` job-pool budget allowed one compiler at a time on the 15.8 GB host; units took about 100-300 seconds and the 701-unit `main` module was estimated at about 20 hours. A Windows low-virtual-memory event named `cl.exe` at about 5.2 GB, and the first attempt ended with a compile failure dialog. This path would also hit LNK1248 or compiler memory limits and cannot solve the image-size problem.
- **Diagnostics and interpretation:** suyu rotated its log on restart and the exporter deleted the failed export work directory, so the first compiler/linker output was lost; the second log retained LNK1248. The earlier commit-charge hypothesis was superseded by the linker error; the observed 28.9 GB commit limit was not causal. The direct cause is FMI code growth in the one static launcher image. Candidate contributors (unmeasured hypotheses) are per-block `_expected[]` guard copies, per-instruction entry labels and dispatch switches, and inline exact-FP bodies; the 89-instruction hot block compiles to about 24,000 assembly lines.
- **Scope:** The 2 GB limit applies per executable or DLL image to CPU recompiled code linked into the static launcher. It does not block a planned native rendering pipeline in the runtime (`video_core`/`shader_recompiler`), which can live in separate images. CPU-side options are reducing generated code or splitting modules such as `main` into DLLs.

### 2026-10-05: Session close (sixth session)

The sixth-session handoff records the dev8/JIT sampling, FMI verification, and the failed PLA FMI export; the next priority is measuring the no-FMI image margin and improving LNK1248 diagnostics.
## Session handoff (2026-10-05, seventh session, complete)

### State at handoff

- **Current local state:** The host build tree was rebuilt with export variables cleared; no working static Build export exists locally. Clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before host builds, then re-export without FMI to obtain a package; the dev8 module libraries remain available for image inspection outside the repository.

- **Repository:** HEAD is `a3063a2`. The README edit flagged last session was committed in `a3063a2`; the project was renamed from HisuiWake to Aevora on 2026-10-05, and the other documents now follow the rename in this change. The local repository folder remains named HisuiWake. Uncommitted: v0.0.1-dev10 exporter change and agent-role documentation (Orca Code Explorer added; Claude `code-explorer` subagent removed; `code-quick-implementer` now does all code implementation).
- **Local environment:** The dev8 module libraries built for the image measurement are in a local directory outside the repository; the host build tree was rebuilt with export variables cleared (dev10 `suyu.exe`/`suyu-cmd.exe`). No working static Build export exists locally.
- **Diagnostics:** the local diag directory outside the repository contains `sample_static.ps1` and `RipSampler2.cs`, with `asm_probe` and `fmi_probe` material. Launch from a non-elevated shell; use `powershell -File sample_static.ps1 -ProcId <launcher PID> -Out <file>`.
- **Build/test:** in the Build Tools 2026 `vcvars64` environment, run `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls` (about 100.6 M cases; reduced: add `--cases 1000 --adv 2000 --legs 13`), `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`, and `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl`. Python must read generated files as UTF-8. `rebuild-suyu.bat` clears export variables and builds with `-j 8`; incremental builds use `cmake --build <build-dir> --target suyu suyu-cmd -j 8`.
- **Switches:** `SUYU_FPCR_STATS=1` logs guest FPCR and adds a lock per save; never use it for FPS. `SUYU_RECOMP_CHAIN_BUDGET` is 1-8192, default 256. `SUYU_AOT_TRANSLATE_ALL=1` applies to Hybrid; static AOT forces translate-all and strict mode. `SUYU_AOT_FM_INLINE=1` enables FMI at export only when FM1 is enabled, but currently breaks the PLA static export with LNK1248 and remains off.

### What this session did

1. [Image budget measurement](#2026-10-05-static-launcher-image-budget-dev8-no-fmi-versus-fmi-section-totals): measured dev8 and FMI module section totals and estimated the dev8 launcher image.
2. Renamed project documentation to Aevora and moved it from `docs/HisuiWake/` to `docs/Aevora/`.
3. [Exporter LNK1248 stop and failure logs](#2026-10-05-exporter-stops-on-lnk1248-and-keeps-build-failure-logs-v001-dev10-unverified-by-export): documented the dev10 exporter change and its unverified paths.
4. Updated agent roles: added the Orca Code Explorer role, removed the Claude `code-explorer` subagent, and assigned all code implementation to `code-quick-implementer`.
5. Planned the main-module DLL split design (Milestone 4a) and archived the sixth-session progress entries to OLD_HISTORY.md.

### Where things stand

| Observation | Result |
|---|---|
| JIT baseline | 30 FPS. |
| Hybrid first / translate-all / budget 256 | 17 / 14 / 15-20 FPS. |
| Dev6 static AOT | 16-20 FPS, no fallback. |
| Dev8 static AOT | 25-30 FPS, no fallback. |
| [Dev8 static sampling](OLD_HISTORY.md#2026-10-05-dev8-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld) | Hot block 2-3.5% (was 15-19%), scheduler-fiber symbol about 1% (was 10-12.5%), CPU cores waiting 35-49% of samples. |
| [Same-scene JIT sampling](OLD_HISTORY.md#2026-10-05-same-scene-jit-sampling-remaining-gap-is-guest-cpu-cost) | JIT process CPU 275.7% versus 320.3% dev8 static; remaining gap is guest CPU execution cost after excluding wait shares (estimate). |
| [FMI export](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248) | FMI tests passed, but the PLA static launcher image was about 2.19 GB and failed LNK1248 against the 2 GB x64 limit. |
| Image budget | Dev8 about 1,820 MiB estimated (about 228 MiB margin); FMI +421 MiB does not fit. |
| FPCR measurement | FZ set in about 96% of 3,473,408 JIT context saves; do not measure FPS with `SUYU_FPCR_STATS=1`. |
| Sampling profile | Hot block was 15-19% of busy-core RIP samples; `ScheduleImplFiber` was 10-12.5%. The FZ finding is confirmed, not a hypothesis. |

Key conclusions: static AOT ran without fallback in the exercised scenario; Dynarmic remains linked for Hybrid and GPU macro JIT/shader compilation remain runtime generation. The SHL fallback hypothesis was refuted, and budget 256 addressed long-slice interrupt delay. The FZ-closed FPX1 gate was the main remaining static AOT cost; the smoke golden mismatch came from MSVC without `/utf-8` in the test only, so real exports were unaffected. Build exports delete `aot_cache`; use Source format to inspect generated C. The FPS comparison is not under fully matched conditions.

### Open issues, in priority order

1. Main-module DLL split: follow Milestone 4a design steps (user decision 2026-10-05).
2. Exercise the dev10 failure paths synthetically.
3. Optionally confirm the dev8 image estimate by an actual link.
4. Re-measure FPS and sampling for the chosen variant.
5. Keep code review of `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, the pre-existing GG1 `g_recomp_gg_word` link failure, and existing export/GPL/rtld/BUILD.md/version investigations.

### Immediate next action

Milestone 4a step 1: Code Explorer survey of globals/TLS and exe-module dependencies, then write the design note.

### How to work

- Measure FPS in the same overworld scene and record backend, settings, cache state, and chain-budget condition; never use `SUYU_FPCR_STATS` during FPS measurements.
- Use the test commands above and Source format when generated C must be inspected. Keep build/export paths and diagnostics outside the repository.
- Do not export PLA with `SUYU_AOT_FM_INLINE=1` until the image budget is solved. Read generated files as UTF-8 in Python on Windows, and copy `suyu_log.txt` before restarting suyu after a failed export.
- Follow [CLAUDE.md](../../CLAUDE.md), [CODEBASE_INDEX.md](CODEBASE_INDEX.md), and the Orca coordination notes.

Useful entry points: [agent guide](../../CLAUDE.md), [codebase index](CODEBASE_INDEX.md), [project README](../../README.md), [upstream build guide](../Suyu/Build.md), [upstream export guide](../Suyu/user/GameExport.md), [exporter source](../../src/suyu/game_export.cpp), and [dependency manifest](../../cpmfile.json).

After each meaningful change, add a dated entry with:

- Milestone and source revision.
- Problem and supporting evidence.
- Files changed and reason for the chosen approach.
- Exact build/test commands and relevant settings.
- Observed result, comparison baseline, and untested behavior.
- Next action or blocking information.

Keep raw logs, machine-specific paths, and game-derived artifacts local. Link only to sanitized, shareable evidence from public documentation.

## Archived on 2026-10-06 (from WORK_PLAN.md)

The following sections were moved from WORK_PLAN.md on 2026-10-06 and are unchanged from the committed source.

## Milestone 4a: Main-module DLL split (design plan)

**Why (evidence):** Estimated image budget (see the image-budget progress entry): dev8 static launcher about 1,820 MiB (about 228 MiB margin); `main` is about 1,455 MiB (no FMI) / 1,780 MiB (FMI) of generated code. With `main` split out, the estimate is main DLL about 1,455 / 1,780 MiB and the launcher about 365 / 460 MiB (other modules plus runtime, estimate). Risk: `main` alone keeps only about 268 MiB margin with FMI; further growth of `main` would require splitting `main` itself, which is harder (intra-module direct calls).

**Existing building blocks (survey, 2026-10-05; verify line numbers before use):**

- The generator already emits a per-module shared library (`recompiled_image` for `main`, `recompiled_<module>` otherwise) with `SUYU_HOSTED_RECOMP=1` / `RECOMP_SHARED_MODULE=1` and an exported ABI surface (image ABI, guard v2, optional guard_gen/features/fastmem/fpx, image_lookup, run_slice, index, set_base, entry) in `src/core/recompiler/arm64_to_c.h` (~5570-5909). `RECOMP_STATIC_ONLY` suppresses it for static exports.
- `src/suyu_cmd/suyu.cpp` (~2021-2186) loads module DLLs from the executable directory in NSO order with `GetProcAddress` handshakes (ABI, ABI6), combines lookup/run_slice, and routes bases by load-order index - but only when the static registry is empty (~2024).
- Static registration: `game_export.cpp` (~4043-4210) writes `recomp_registration.c` (NSO-order externs/table, guard and ABI6 registries); `src/suyu_cmd/CMakeLists.txt` (~107-218) imports `recomp_runtime_shared` once and each `recomp_static_<module>` with per-module renames, and links `suyu-cmd-static`; `src/suyu_cmd/recomp_modules/CMakeLists.txt` repeats the renames for the clang-cl prebuild.
- Generated code reaches the host through `RecompHostMem` callbacks and `GuestContext` fields filled by `src/core/arm/recomp/arm_recomp.cpp` (~908-1043), not host symbol imports. Cross-module guest calls go through the dispatcher (generated direct calls only within one module, `arm64_to_c.h` ~378-410) and guest GOT/PLT relocation (`arm_recomp.cpp` ~1357-1480); no native cross-module symbol references were found.
- Per-image state exists: `g_module_base`, GG1 watch word, FPX shadow static/_Thread_local state, exclusive-monitor fallback state (`arm64_to_c.h` ~6836-6847, ~7630). CRT is /MD in the project and the export build kit.
- Packaging assumes one file: `game_export.cpp` (~5190-5486) packages `static_launcher.exe` only and README_NATIVE_EXPORT says one executable; the export build kit (`tools/export_build_kit/`) imports static libraries only and `package.py` rejects `recomp_static_*` inputs; manifests carry no module binary topology.

**Design decisions to make (each recorded with rationale before implementation):**

- D1 Runtime placement: the main DLL carries its own `recomp_runtime.c` copy (memory helpers stay DLL-local calls) versus importing helpers from the launcher (DLL-to-exE call per access). Default proposal: DLL-local copy; verify that no runtime global must be process-wide.
- D2 Per-image globals: list every global/TLS in `recomp_runtime.c`/`.h` and the generated units; classify as per-image (fine duplicated) or shared (must move into `GuestContext`/host callbacks). Must cover GG1 watch word, FPX shadow, exclusive state, guard generation, chain budget.
- D3 Mixed registry and loading: how `recomp_registration.c` represents a DLL module (placeholder entry filled at load, or a separate DLL list) while keeping NSO order, base routing, and the ABI6 all-module handshake; the launcher must load the DLL even when the static registry is non-empty; error behaviour when the DLL is missing or mismatched (fail fast; no JIT fallback in strict mode).
- D4 Build: generator/CMake mode to build `main` as a DLL and others as static libs in both the in-tree MSVC path and the clang-cl `recomp_modules` prebuild (clang-cl compiles, link.exe or lld-link links the DLL); module renames; import library handling; /MD; PDB size.
- D5 Packaging and manifest: the Build export ships `static_launcher.exe` plus the main DLL; update the package layout, README_NATIVE_EXPORT and UI “single-file” wording, package policy, manifest field for module binary topology, cache reuse, and the LNK1248 handling (per-image size check).
- D6 Export build kit: decide whether the kit supports the split now or is explicitly documented as static-only for now.
- D7 Selection: always split main for PLA static exports, or split only when the estimated image (from `dumpbin /summary` sizes) exceeds a threshold; name the threshold constant.

**Design work steps:**

1. Code Explorer (Orca): enumerate globals/TLS and every exe<->module symbol dependency for D1/D2; confirm the launcher DLL loader path and ABI6 handshake details for D3. (done 2026-10-06)
2. Parent session: write the design note (sections D1-D7, data flow at load time, failure modes, test plan) as a new subsection of this milestone; Code reviewer reviews the design against CLAUDE.md before any code change. (done 2026-10-06)
3. Prototype measurement before full implementation: from a Source-format export, build the generated `main` module's existing SHARED target with `RECOMP_STATIC_ONLY` off, without and with FMI; measure the DLL image and dependency evidence, then record it. (done 2026-10-06; see progress entry)
4. Implementation (code-quick-implementer), in reviewable steps: (a) generator/CMake mixed mode ((a) done 2026-10-06; (b) done 2026-10-06), (c) exporter packaging/manifest/wording (part 1 done 2026-10-06; part 2 pending), (d) build-kit decision. Code reviewer reviews each step.
5. Tests: extend `tests/recompiler_smoke` with a mixed static+DLL variant (synthetic modules) covering load order, base routing, cross-module dispatch, guard checks, GG1, and FPX; keep existing goldens unchanged for non-split exports; run the fastmem and FPX harnesses.
6. PLA verification: export static AOT (fallback disabled) without FMI, then with FMI; record per-image sizes, link time, zero JIT transitions, FPS in the same overworld scene, and sampling; compare with dev8 (25-30 FPS) and JIT (30 FPS).

**Acceptance:** design note reviewed with no unresolved findings; main DLL and launcher each link under 2 GB for PLA with FMI; strict static mode shows zero JIT transitions and no lookup misses in the exercised scene; synthetic mixed-mode tests pass; packaging, manifest, and documentation describe the two-file layout; remaining limits (main margin, build-kit support) are documented.

### Step 4(c) part 2 acceptance (from the part 1 reviews)

The part 1 reviews accepted the split wiring as an intermediate step, not as completion of step 4(c). Part 2 must:

- Drive real synthetic split static and Hybrid exports through `tests/export_integration`, run a linked v1 launcher with guest JIT fallback disabled, and inspect the registry, marker, selection, DLL collection, manifests, package hashes, and per-image `SizeOfImage`.
- Exercise VFS/deconstructed-source failures for `main` and a canonical static module: zero text with data, unreadable text, decompression failure, bad or short NSO header, missing `main`, and all analyses failing; allow ancillary non-NSO files, but refuse an analyzable non-canonical NSO with a named diagnostic.
- Inject emission, compiler, DLL link, and launcher LNK1248 failures, including clang-to-MSVC retry, and verify split never removes a module or finalizes a failed package; diagnostics must name the module/image, stage, and saved compiler/linker log where applicable.
- Exercise `SUYU_AOT_SPLIT_MAIN` unset, empty, `0`, `1`, and invalid values across Dynarmic, Hybrid, strict static, non-Windows, Source, and kit-only paths; verify common-entry validation, the per-export snapshot, and re-entry protection.
- In one dialog, cover successful split followed by option-off JIT/all-static, and failure after DLL collection followed by JIT/all-static retry; verify no stale split record, DLL requirement, bundle id, or identity remains.
- Verify identical split cache reuse and invalidation for ancillary ExeFS files, omitted/extra bindings, main binding, selection/DLL lists, bundle id, topology disagreement, DLL file/size/hash, and missing DLL; all-static and split caches must not cross-reuse.
- Cover main-only, all-DLL, and mixed topology from clang prebuilt and in-tree MSVC paths; all-DLL prebuild must create no static runtime and the launcher must import none, while mixed/all-static still require it.
- Complete the carried runtime checks: unreadable embedded path in a middle canonical module, complete inventory and pre-relocation input on every core, named `Core::Load` refusals with zero guest execution, lookup unregister, fresh-process rebinding, and two application lifetimes with one pre-relocation log per bound process.
- Verify split completion-dialog wording, unchanged all-static v4 artifacts and `tests/export_regression`, Windows non-ASCII/space/bracket paths, and the PE truncated-header/file regressions.

#### Part 2 planning notes

The part 2 survey found that the MCP harness can drive real exports and isolated launchers but cannot mutate `SUYU_AOT_SPLIT_MAIN` inside one running frontend; use a small per-export seam or separate frontend processes. A valid multi-module fixture can use sparse canonical ExeFS sets with contiguous page-aligned bases in canonical order, but cross-module targets must be seeded as discovery roots and fixed-seed base formulas must be used. `FindModules` drops a loaded module when its embedded path read fails, so the bound canonical inventory must remain independent of that debug-name scan. The smallest two-lifetime seam is a test-only host around independent `Core::System` loads with lookup unregister/rebind, although normal renderer and service dependencies still need verification. Existing named export failure injection covers staging checkpoints, not emission/compiler/link failures; lower-level failures need synthetic toolchain or process-runner injection. The `export_regression` blocker was a Qt-root layout mismatch: resolve the CPM Qt package root rather than the build tree.

These are survey findings to verify during implementation, not completed acceptance evidence.

#### Open questions

1. Resolved by step 3: measured main-DLL `SizeOfImage` is 1,459.1 MiB without FMI and 1,783.7 MiB with FMI, leaving margins of 588.9 MiB and 264.3 MiB to the 2,048 MiB limit, respectively.
2. (Resolved in draft 3 `[R12]`: NSP/XCI already forward `ReadNSOModules`.)
3. Whether the base-binding callback should also replace index routing for all-static exports later (out of scope now; v4 unchanged).

**Why (evidence):** Estimated image budget (see the image-budget progress entry): dev8 static launcher about 1,820 MiB (about 228 MiB margin); `main` is about 1,455 MiB (no FMI) / 1,780 MiB (FMI) of generated code. With `main` split out, the estimate is main DLL about 1,455 / 1,780 MiB and the launcher about 365 / 460 MiB (other modules plus runtime, estimate). Risk: `main` alone keeps only about 268 MiB margin with FMI; further growth of `main` would require splitting `main` itself, which is harder (intra-module direct calls).

**Existing building blocks (survey, 2026-10-05; verify line numbers before use):**

- The generator already emits a per-module shared library (`recompiled_image` for `main`, `recompiled_<module>` otherwise) with `SUYU_HOSTED_RECOMP=1` / `RECOMP_SHARED_MODULE=1` and an exported ABI surface (image ABI, guard v2, optional guard_gen/features/fastmem/fpx, image_lookup, run_slice, index, set_base, entry) in `src/core/recompiler/arm64_to_c.h` (~5570-5909). `RECOMP_STATIC_ONLY` suppresses it for static exports.
- `src/suyu_cmd/suyu.cpp` (~2021-2186) loads module DLLs from the executable directory in NSO order with `GetProcAddress` handshakes (ABI, ABI6), combines lookup/run_slice, and routes bases by load-order index - but only when the static registry is empty (~2024).
- Static registration: `game_export.cpp` (~4043-4210) writes `recomp_registration.c` (NSO-order externs/table, guard and ABI6 registries); `src/suyu_cmd/CMakeLists.txt` (~107-218) imports `recomp_runtime_shared` once and each `recomp_static_<module>` with per-module renames, and links `suyu-cmd-static`; `src/suyu_cmd/recomp_modules/CMakeLists.txt` repeats the renames for the clang-cl prebuild.
- Generated code reaches the host through `RecompHostMem` callbacks and `GuestContext` fields filled by `src/core/arm/recomp/arm_recomp.cpp` (~908-1043), not host symbol imports. Cross-module guest calls go through the dispatcher (generated direct calls only within one module, `arm64_to_c.h` ~378-410) and guest GOT/PLT relocation (`arm_recomp.cpp` ~1357-1480); no native cross-module symbol references were found.
- Per-image state exists: `g_module_base`, GG1 watch word, FPX shadow static/_Thread_local state, exclusive-monitor fallback state (`arm64_to_c.h` ~6836-6847, ~7630). CRT is /MD in the project and the export build kit.
- Packaging assumes one file: `game_export.cpp` (~5190-5486) packages `static_launcher.exe` only and README_NATIVE_EXPORT says one executable; the export build kit (`tools/export_build_kit/`) imports static libraries only and `package.py` rejects `recomp_static_*` inputs; manifests carry no module binary topology.

**Design decisions to make (each recorded with rationale before implementation):**

- D1 Runtime placement: the main DLL carries its own `recomp_runtime.c` copy (memory helpers stay DLL-local calls) versus importing helpers from the launcher (DLL-to-exE call per access). Default proposal: DLL-local copy; verify that no runtime global must be process-wide.
- D2 Per-image globals: list every global/TLS in `recomp_runtime.c`/`.h` and the generated units; classify as per-image (fine duplicated) or shared (must move into `GuestContext`/host callbacks). Must cover GG1 watch word, FPX shadow, exclusive state, guard generation, chain budget.
- D3 Mixed registry and loading: how `recomp_registration.c` represents a DLL module (placeholder entry filled at load, or a separate DLL list) while keeping NSO order, base routing, and the ABI6 all-module handshake; the launcher must load the DLL even when the static registry is non-empty; error behaviour when the DLL is missing or mismatched (fail fast; no JIT fallback in strict mode).
- D4 Build: generator/CMake mode to build `main` as a DLL and others as static libs in both the in-tree MSVC path and the clang-cl `recomp_modules` prebuild (clang-cl compiles, link.exe or lld-link links the DLL); module renames; import library handling; /MD; PDB size.
- D5 Packaging and manifest: the Build export ships `static_launcher.exe` plus the main DLL; update the package layout, README_NATIVE_EXPORT and UI 窶徭ingle-file窶・wording, package policy, manifest field for module binary topology, cache reuse, and the LNK1248 handling (per-image size check).
- D6 Export build kit: decide whether the kit supports the split now or is explicitly documented as static-only for now.
- D7 Selection: always split main for PLA static exports, or split only when the estimated image (from `dumpbin /summary` sizes) exceeds a threshold; name the threshold constant.

**Design work steps:**

1. Code Explorer (Orca): enumerate globals/TLS and every exe<->module symbol dependency for D1/D2; confirm the launcher DLL loader path and ABI6 handshake details for D3. (done 2026-10-06)
2. Parent session: write the design note (sections D1-D7, data flow at load time, failure modes, test plan) as a new subsection of this milestone; Code reviewer reviews the design against CLAUDE.md before any code change. (done 2026-10-06)
3. Prototype measurement before full implementation: from a Source-format export, build the generated `main` module's existing SHARED target with `RECOMP_STATIC_ONLY` off, without and with FMI; measure the DLL image and dependency evidence, then record it. (done 2026-10-06; see progress entry)
4. Implementation (code-quick-implementer), in reviewable steps: (a) generator/CMake mixed mode ((a) done 2026-10-06; (b) done 2026-10-06), (c) exporter packaging/manifest/wording (part 1 done 2026-10-06; part 2 pending), (d) build-kit decision. Code reviewer reviews each step.
5. Tests: extend `tests/recompiler_smoke` with a mixed static+DLL variant (synthetic modules) covering load order, base routing, cross-module dispatch, guard checks, GG1, and FPX; keep existing goldens unchanged for non-split exports; run the fastmem and FPX harnesses.
6. PLA verification: export static AOT (fallback disabled) without FMI, then with FMI; record per-image sizes, link time, zero JIT transitions, FPS in the same overworld scene, and sampling; compare with dev8 (25-30 FPS) and JIT (30 FPS).

**Acceptance:** design note reviewed with no unresolved findings; main DLL and launcher each link under 2 GB for PLA with FMI; strict static mode shows zero JIT transitions and no lookup misses in the exercised scene; synthetic mixed-mode tests pass; packaging, manifest, and documentation describe the two-file layout; remaining limits (main margin, build-kit support) are documented.



### Design note (accepted 2026-10-06, draft 3)

Review history: draft 1: 1 blocker/9 major/1 minor (R1-R11); draft 2: 0/2/1; draft 3: 0/0/0, implementation may start.

#### Verified starting facts

- The shared-module shape already exists: `add_library(recompiled_image SHARED recomp_export.c recomp_runtime.c ...)` with `SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1` (`src/core/recompiler/arm64_to_c.h` ~5718-5730). Without `RECOMP_STATIC_HOST`, the DLL's own `recomp_runtime.c` defines `g_module_base` (~7798-7804) and the load/store/exclusive/SVC helpers.
- The static shape builds `recomp_runtime.c` once as `recomp_runtime_shared` (`RECOMP_STATIC_HOST=1`) and renames each module's image symbols through `-D` (~5733-5785).
- Generated code reaches the emulator through `GuestContext::host_mem` (`RecompHostMem`) callbacks for memory, exclusive, and counter operations (mapped fastmem executes locally) and through `GuestContext::pending_svc` plus a return to the host for SVCs; the callbacks are installed per `ArmRecomp::Impl` (`src/core/arm/recomp/arm_recomp.cpp` ~909-926, ~1020-1050); there are no native imports of host C++ symbols and direct chains are intra-module (~380-410). A null exclusive/counter callback makes generated code use private fallbacks (`arm64_to_c.h` ~7633-7671).
- Launcher (`src/suyu_cmd/suyu.cpp`): the static path calls `suyu_recomp_static_modules_v4` (~1933-2017); the DLL path runs only if `s_recomp_modules.empty()` (~2025), probes fixed file names, silently skips missing files and DLLs without `recomp_image_lookup`, and warns-and-continues on `LoadLibraryW` failure. `SuyuRecompStaticModule` is declared separately in `suyu.cpp` (~196) and emitted as text by `game_export.cpp` (~4108).
- Base routing: `ArmRecomp::ModuleBaseFor` (~1187-1201) calls the setter with `(index, name, base)` over `FindModules` results (`src/core/arm/debug.cpp` ~254-313), which drop modules whose embedded path is unreadable; the launcher routes by index (`suyu.cpp` ~2181). Embedded names do not match exporter names (`arm_recomp.h` ~100-110).
- The loader keeps a canonical inventory: `AppLoader_DeconstructedRomDirectory` records `rtld`, `main`, `subsdk0..9`, `sdk` by load address (`src/core/loader/deconstructed_rom_directory.cpp` ~191-265, `ReadNSOModules` ~334); NCA/NAX forward it, and NSP (`nsp.cpp` ~263) and XCI (`xci.cpp` ~211) forward to their nested loaders `[R12]`; a standalone NSO records `file->GetName()` rather than a canonical name (`nso.cpp` ~242-252); classes that do not override it return `ErrorNotImplemented` (`src/core/loader/loader.h` ~339). `FindModules` results also feed the host pre-relocator through `Impl::modules` (`arm_recomp.cpp` ~1191, ~1883). `Core::Load` already reads this inventory before process publication on the explicit-prepare path (`src/core/core.cpp` ~355-365); that path also changes relocation behavior (`arm_recomp.cpp` ~1715-1730) and is not reused here.

#### D1 Runtime placement

Decision: the main DLL carries its own `recomp_runtime.c`, compiled exactly as the existing shared shape. Static modules keep `recomp_runtime_shared`. Rationale: helpers are per-access and intentionally out of line (~7692); the DLL copy needs no new exe-export ABI. Cost is measured in step 3.

Constraint `[R10]`: hosted code must never take the private fallbacks. The host validates bridge completeness itself: when each `ArmRecomp::Impl` builds its `RecompHostMem`, it checks that every exclusive (single and pair), clear-exclusive, and counter callback is non-null and fails initialization with a named diagnostic otherwise. This is a host-side check after per-core bridge construction, not a DLL export, and applies to all bundles.

#### D2 Per-image globals

| State | Placement | Reason |
|---|---|---|
| `g_module_base`, `g_recomp_guard_host_v2`, `g_recomp_gg_word`, `g_recomp_gg_host`, `recomp_gg_seen[]`, lookup index `_idx*` | Per image | One image's base, handshake, generation epoch, or address range; the host already keeps per-image pointers. |
| FPX shadow statics/TLS | Per image | Diagnostics only; mixed layout yields one report per runtime copy. Documented, not aggregated. |
| `g_excl_addr`, `g_excl_valid`, standalone `base_ns` | Never used in hosted mode | Guaranteed by the D1 bridge check. |
| Chain budget, combined lookup, readiness flags, GG1 registry, counters | Process-wide host state | Already host-owned; unchanged. |

No global moves into `GuestContext`. Step 3 records the DLL's exports and imports (`dumpbin /exports /imports /dependents`).

#### D3 Topology contract, loading, and base binding

**Topology contract `[R2]`.** A new shared C header (proposed `src/core/arm/recomp/recomp_topology.h`, plain C, included by the launcher and by the generated `recomp_registration.c`) defines a versioned registry: entry point `suyu_recomp_topology_v1()` returning a header `{abi_version, struct_size, entry_count, static_count, dll_count, bundle_abi, bundle_id[32]}` and an entry array `{canonical_name, binding (static|dll), dll_file_utf8, image_abi, expected_features, static function pointers (null for dll)}` in NSO order. The struct layout is defined once in that header; the exporter emits only values. Old all-static exports and the existing `suyu_recomp_static_modules_v4` path are unchanged: the exporter emits v1 only when a split is requested, and the launcher prefers v1 when present, otherwise v4. Each entry's features are checked individually against `expected_features` (not only the bundle OR). Static-registry handshakes (GG1, FPX, FM1, guard v2) gain v1 forms that report results per entry index so a mixed bundle has separate total/static/DLL counts and GG1 descriptors are placed at their full topology index.

**Single source.** The exporter computes one topology value (module list in NSO order, binding, DLL file name from one helper, bundle ABI, expected features, bundle id). The generated CMake binding list, `recomp_registration.c`, package file list, AOT cache manifest, and package manifest are all written from that value, and each carries `topology_version` and `bundle_id`; consumers compare them and fail with the mismatched item named.

**Bundle and module identity `[R3]`.** `bundle_id` is SHA-256 over the source ExeFS digest already used for cache identity (`game_export.cpp` ~3594), the generator revision, and the generation options. In split mode the generator emits an extra export in each shared module, `recomp_image_identity_v1(const char** canonical_name, const uint8_t** bundle_id)`, with values from compile definitions written from the topology. The launcher accepts a DLL only if name and bundle id match its table entry. The package manifest also records each DLL's SHA-256 for integrity checks.

**Transactional loading `[R11]`.** The launcher builds a pending bundle: static entries are taken from v1; each DLL entry is loaded (see D3 paths), its exports resolved (`lookup`, `set_base`, `run_slice`, `abi`, `features`, `fastmem_v1`, `guard_gen_v1` if GG1, `fpx_v1` if FPX1, `guard_v2`, `identity_v1`), and checked. Any failure releases all uncommitted handles and exits with the module name, file, and cause (missing file, Win32 error, missing export, identity, ABI, feature, or handshake refusal). Only after the whole bundle succeeds are `SetRecompLookup`, readiness flags, `SetRecompGuardGenModules`, and the binding callback published. Accepted DLL handles are owned by the bundle object and intentionally retained for the process lifetime (no `FreeLibrary` after commit, because lookup/GG1/base pointers stay installed). Strict static mode never continues with a missing slot and never falls back to JIT.

**Base binding `[R1]`.** Bases are bound by canonical identity, not by debug-name index. A new host callback `SetRecompBindCallback(bool (*)(const RecompModules& inventory))` is invoked from `Core::Load` after `MakeApplicationProcess` and before process publication, using `app_loader->ReadNSOModules` (the inventory already used at `core.cpp` ~359), without enabling the explicit-prepare relocation path. The launcher's bind callback matches the inventory's canonical names (`rtld`, `main`, `subsdkN`, `sdk`) against the topology entries; it rejects count mismatch, duplicates, missing slots, and extra slots, then calls each image's `set_base` and records the canonical name and base. Failure makes `Core::Load` stop with `ErrorLoader` and a diagnostic naming the module, before any block runs; the failure path releases the service process before kernel shutdown, as the existing load error path does (`core.cpp` ~320-325).

Per-core module map `[R1b]`: binding is a process-aware host stage (proposed `BindRecompProcess(KProcess&, const RecompModules& inventory)` in `arm_recomp.cpp`). After the launcher callback validates and binds the inventory, the stage installs the same canonical inventory into every application `ArmRecomp::Impl` as `modules` and sets `modules_read`, without setting `rela_applied` or `explicitly_prepared`, so the existing host pre-relocation (`ApplyAllRelocations`, ~1883) still runs and sees every module, including one whose embedded path `FindModules` cannot read. `ModuleBaseFor` then neither calls `FindModules` nor the index setter. Debug/display names are kept separately (from `FindModules`, for logs and `recomp_gaps.json` only). The bound state belongs to that process; a new process starts unbound and runs the stage again. `PrepareRecompProcess` is not reused, because it disables pre-relocation (~1727).

Inventory sources `[R12]`: ExeFS directory, NCA, NAX, NSP, and XCI loaders already supply the canonical inventory through existing delegation; no new forwarding is added, and tests cover the existing delegation. Any `ReadNSOModules` error fails a split bundle fast with the loader named. A standalone NSO's inventory name is its file name and is not normalized; it fails the canonical-name match with a diagnostic, so split exports are not supported for standalone NSO loading. All-static v4 exports keep today's index routing unchanged.

**Legacy all-DLL probe.** Kept unchanged for exports without a registry; not used by Build exports.

**Windows paths `[R9]`.** Registry strings are UTF-8 and converted with the existing `Common::UTF8ToUTF16W`. The executable directory is read with a growing-buffer `GetModuleFileNameW` loop that checks truncation and errors. Each `dll_file_utf8` must be a plain basename (no separators, no `..`, no drive), unique case-insensitively; the exporter validates this when writing the topology and the launcher re-validates. DLLs are loaded with `LoadLibraryExW(absolute_path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`; step 3 confirms the DLL depends only on the CRT and OS DLLs already required by the launcher. Filesystem errors name the module and path.

#### D4 Build `[R6]`

- Generator: split mode is an opt-in emitter path. With the split off, emitted files are byte-identical to today (smoke goldens unchanged) `[R10]`. With it on, the generated module CMake receives the binding list from the topology; DLL-bound modules build only the `SHARED` target plus the identity definitions, static modules build as today.
- Exporter: both build loops (`game_export.cpp` ~4561, ~4782) take target names and artifact paths from the topology (`recomp_static_<m>` or `recompiled_image`). The in-tree MSVC path (`src/suyu_cmd/CMakeLists.txt`) and clang-cl prebuild (`src/suyu_cmd/recomp_modules/CMakeLists.txt`) both read the same binding list; the DLL is built as its own target, copied next to the launcher, and not linked into it.
- Failure stages are logged separately: module compile, DLL link, launcher link, package. LNK1248 (or any link failure) of the DLL in the module-target branch stops the export with the image name and saved output; it does not prune a strict slot and does not retry with MSVC. The dev10 launcher-link stop stays as is.
- Size reporting: after each successful link, read PE `SizeOfImage` from the image header (not file length) and log it against the hard limit constant `kPeImageLimitBytes` (2 GiB) with the remaining margin; record it in the manifest.
- Flags: `/MD`; the DLL uses the same generator variables for FM1/GG1/FPX/FMI defines, and features are still verified per entry at load. Separate PDB, not packaged.

#### D5 Packaging, cache, and manifest `[R5]`

- `ValidateLocalExport` / package policy (`game_export.cpp` ~6625, `src/common/package_policy.cpp` ~1167-1240) receive the required module-DLL set derived from the topology; listed DLLs are required and non-empty, unlisted DLLs remain rejected.
- AOT cache (`aot_manifest.json`, ~3416-3574) stores `topology_version`, `bundle_id`, binding list, and each DLL's path, size, and SHA-256. Reuse requires all of them to match and every required DLL to exist with the recorded digest; otherwise rebuild.
- Package staging copies and verifies DLLs (digest) before the cache is removed (~5393). Cache failure policy `[R5b]`: unchanged from today for both all-static and split exports. The AOT cache lives inside package staging, and a failed, cancelled, or exception-terminated package discards the whole staging directory, including the cache (`StagingCleanup`, ~6368-6374); the next export rebuilds. This milestone does not add cache retention across failed packages; a recoverable cache outside staging would be a separate change with its own ownership and promotion rules.
- `export-package.json` gains `module_topology` (`name`, `binding`, `file`, `size_bytes`, `size_of_image`, `sha256`) and `bundle_id`, written from the same topology value.
- README_NATIVE_EXPORT and UI text describe "launcher plus module DLL(s) in the same folder" for split exports only.

#### D6 Export build kit `[R4]`

The kit stays static-only in this milestone. Rejection happens in the consumer: the kit CMake (`tools/export_build_kit/CMakeLists.txt` ~78-105) rejects a topology with a `dll` entry before creating targets, and exporter preflight rejects a split request on a kit-only installation (`game_export.cpp` ~4319) before compiling modules, with a message that the split requires a source build. The split is never silently undone. `package.py` keeps rejecting game-specific inputs as today. All-static kit exports keep using v4 and are unaffected; the kit revision check is unchanged for them.

#### D7 Selection `[R7]`

Explicit opt-in only in this milestone: the documented generator option `SUYU_AOT_SPLIT_MAIN=1` splits `main` for Windows static AOT exports. On other export types or platforms the option fails the export with a clear message. No automatic size-based selection; it may be proposed later as a separate auto mode with its own decision phase. Reports show each image's `SizeOfImage` against the hard limit and the remaining margin for the main DLL.

#### Load-time data flow

Exporter -> topology value -> (a) generated CMake binding list, (b) `recomp_registration.c` v1, (c) AOT cache manifest, (d) package file list and `export-package.json`. Launcher start -> read v1 -> pending bundle (static entries + DLL loads + identity/ABI/feature/handshake checks) -> commit (publish lookup, readiness, GG1, bind callback) -> `Core::Load` -> loader inventory -> bind callback validates and sets bases -> process published -> guest runs.

#### Failure modes

| Failure | Stage | Behaviour |
|---|---|---|
| DLL missing, unloadable, export missing | Launcher, before commit | Exit; names module, file, Win32 error or export. |
| Identity, ABI, or per-entry feature mismatch | Launcher, before commit | Exit; names module and both values. |
| FM1/GG1/FPX refusal or guard v2 disagreement | Launcher, before commit | Exit (guard v2 "not negotiated" kept only if every entry agrees). |
| Inventory unavailable, count/duplicate/missing/extra slot | `Core::Load`, before publication | `ErrorLoader`; names loader or module. |
| Bridge callback null | `ArmRecomp` init | Fail initialization; names callback. |
| DLL or launcher image over limit / link failure | Export build | Stop; names image, stage, saved log; no retry, no pruning. |
| Packaged DLL missing, empty, digest mismatch, or extra DLL | Export package | Export fails. |
| Split requested with kit-only install | Exporter preflight / kit CMake | Fails with "requires source build". |

#### Test plan `[R10]`

- Refactor the launcher's bundle loading and binding into a testable unit (for example `src/core/arm/recomp/recomp_bundle.{h,cpp}`) used by `suyu.cpp`, so tests run production logic.
- Mixed synthetic fixture (new, alongside `tests/recompiler_smoke`): three or more modules with one real DLL in a middle slot, loaded through the production unit. Checks: NSO order, inventory-based base binding, cross-module dispatch, guard v2, GG1 descriptors at full indices, FPX agreement, single and pair exclusive reservation across the static/DLL boundary, CLREX, counter continuity.
- Negative cases, each asserting the named diagnostic and that no block executes: missing DLL, unloadable DLL, missing export, same-ABI/same-feature DLL from another synthetic bundle or module (identity), ABI mismatch, per-entry feature mismatch, individual FM1/GG1/FPX refusal, guard v2 disagreement, empty and duplicate topology, inventory missing a middle module, extra inventory module, valid loader inventory with an unreadable embedded name in a middle module (all cores keep it for relocation and dispatch, no index setter call) `[R1b]`, standalone NSO inventory rejected by name, NSP/XCI delegation returning the canonical inventory `[R12]`, null bridge callback, strict lookup miss without JIT.
- Paths: Japanese parent directory, spaces, brackets, different current directory, path beyond `MAX_PATH` (works or explicit diagnostic).
- Exporter: package success, extra DLL rejection, missing/empty DLL, cache reuse in both binding directions, stale cached DLL digest, failure injected after DLL verification and before promotion (staging and cache discarded, no partial package promoted) `[R5b]`, kit-only split rejection, unchanged all-static kit exports (strict and Hybrid), injected DLL-link and launcher-link LNK1248 on both compiler paths.
- Regression: existing smoke goldens byte-identical with the split off; `tests/recompiler_fastmem` and `tests/recompiler_fpx` (reduced and full); existing ABI5/ABI6 all-static coverage.
- PLA (step 6): static AOT without FMI, then with FMI; per-image `SizeOfImage`, link time, zero JIT transitions, loader inventory names, FPS in the same overworld scene, sampling.

#### Step 3 prototype measurement (revised) `[R8]`

Do not link static dev8 objects (they carry static renames and no runtime). Instead, from a Source-format export generated outside the repository, configure the generated `main` module CMake with `RECOMP_STATIC_ONLY` off so the existing `recompiled_image` SHARED target builds with the real shared definitions. Measure without FMI and with FMI: `SizeOfImage`, file size, PDB size, exports, imports, dependents, compiler and link flags, link time. Record this as evidence for the main DLL only; the launcher size remains an estimate until a split launcher links.

#### Modifiability and reference correctness

One exporter-owned topology value is the source for module names, bindings, DLL file names, bundle id, ABI, and features; every consumer is written from it and verified at build or load. Moving a module between static and DLL binding is a one-entry change. Generated code is not edited; the split is an opt-in generator/exporter option, and the default output is unchanged.

### Carried acceptance criteria (from the step 4(b) review)

The following criteria are carried from the accepted step 4(b) review and must be completed in steps 4(c) and 4(d); none may be replaced by a PLA observation.

- **4(c):** Produce a real split export through the exporter and execute a linked v1 launcher with fallback disabled; cover an unreadable-embedded-path middle-module variant while checking every core's complete inventory and relocation input; exercise Core load refusals with named diagnostics and zero guest execution; verify lookup-removal unregister and fresh-process rebinding; wire registry, marker, and targets coherently and run the unchanged all-static v4 regression.
- **4(d):** Exercise a three-or-more-module static/DLL/static fixture through the production dispatcher with GG1, guard, and FPX checks; cover cross-image reservation in both directions, STXR success/failure, CLREX, LDXP/STXP, and counter continuity; require bridge-null refusal per callback and strict lookup-miss refusal; complete remaining loader/handshake refusals and kit/package/cache/path tests, goldens, fastmem, and reduced/full FPX.

## Session handoff (2026-10-06, eighth session, complete)

### State at handoff

- **Current local state:** The host build tree was rebuilt with export variables cleared; no working static Build export exists locally. Clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before host builds, then re-export without FMI to obtain a package; the dev8 module libraries remain available for image inspection outside the repository.

- **Repository:** HEAD is `1d9ba61` (v0.0.1-dev11, committed and pushed by the agent at the user's explicit request while the user was away); the project was renamed from HisuiWake to Aevora on 2026-10-05, and the other documents now follow the rename. The local repository folder remains named HisuiWake. Uncommitted: v0.0.1-dev12 step 4(b) code and docs.
- **Local environment:** The dev8 module libraries built for the image measurement are in a local directory outside the repository; the host build tree was rebuilt with export variables cleared (dev10 `suyu.exe`/`suyu-cmd.exe`). No working static Build export exists locally.
- **Diagnostics:** the local diag directory outside the repository contains `sample_static.ps1` and `RipSampler2.cs`, with `asm_probe` and `fmi_probe` material. Launch from a non-elevated shell; use `powershell -File sample_static.ps1 -ProcId <launcher PID> -Out <file>`.
- **Build/test:** in the Build Tools 2026 `vcvars64` environment, run `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls` (about 100.6 M cases; reduced: add `--cases 1000 --adv 2000 --legs 13`), `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`, and `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl`. Python must read generated files as UTF-8. `rebuild-suyu.bat` clears export variables and builds with `-j 8`; incremental builds use `cmake --build <build-dir> --target suyu suyu-cmd -j 8`.
- **Switches:** `SUYU_FPCR_STATS=1` logs guest FPCR and adds a lock per save; never use it for FPS. `SUYU_RECOMP_CHAIN_BUDGET` is 1-8192, default 256. `SUYU_AOT_TRANSLATE_ALL=1` applies to Hybrid; static AOT forces translate-all and strict mode. `SUYU_AOT_FM_INLINE=1` enables FMI at export only when FM1 is enabled, but currently breaks the PLA static export with LNK1248 and remains off.

### What this session did

1. Completed Milestone 4a step 1: Orca Code Explorer surveyed globals/TLS, exe<->module symbol dependencies, and the launcher DLL loader/ABI6 handshake.
2. Completed step 2: wrote the draft-3 design note and received three reviews: draft 1 (blocker 1, major 9, minor 1; R1-R11), draft 2 (blocker 0, major 2, minor 1; residual R1/R5 and new R12), and draft 3 (blocker 0, major 0, minor 0; implementation may start).
3. Completed step 3: built the generated `main` module's existing SHARED target without and with FMI; measured `SizeOfImage` at 1,459.1 MiB and 1,783.7 MiB, with 588.9 MiB and 264.3 MiB margins.
4. Completed Milestone 4a step 4(a): implemented and reviewed the authoritative module topology, shared-image identity export, and prebuild DLL binding checks; all-static output remains unchanged.
5. Completed Milestone 4a step 4(b): implemented and reviewed registry v1, transactional bundle loading, inventory binding, and host bridge checks; accepted as an intermediate with F3 carried.

### Where things stand

| Observation | Result |
|---|---|
| JIT baseline | 30 FPS. |
| Hybrid first / translate-all / budget 256 | 17 / 14 / 15-20 FPS. |
| Dev6 static AOT | 16-20 FPS, no fallback. |
| Dev8 static AOT | 25-30 FPS, no fallback. |
| [Dev8 static sampling](OLD_HISTORY.md#2026-10-05-dev8-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld) | Hot block 2-3.5% (was 15-19%), scheduler-fiber symbol about 1% (was 10-12.5%), CPU cores waiting 35-49% of samples. |
| [Same-scene JIT sampling](OLD_HISTORY.md#2026-10-05-same-scene-jit-sampling-remaining-gap-is-guest-cpu-cost) | JIT process CPU 275.7% versus 320.3% dev8 static; remaining gap is guest CPU execution cost after excluding wait shares (estimate). |
| [FMI export](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248) | FMI tests passed, but the PLA static launcher image was about 2.19 GB and failed LNK1248 against the 2 GB x64 limit. |
| Image budget | Dev8 about 1,820 MiB estimated (about 228 MiB margin); FMI +421 MiB does not fit. |
| Main DLL (step 3) | `SizeOfImage`: 1,459.1 MiB without FMI / 1,783.7 MiB with FMI; margins: 588.9 MiB / 264.3 MiB to 2,048 MiB. |
| FPCR measurement | FZ set in about 96% of 3,473,408 JIT context saves; do not measure FPS with `SUYU_FPCR_STATS=1`. |
| Sampling profile | Hot block was 15-19% of busy-core RIP samples; `ScheduleImplFiber` was 10-12.5%. The FZ finding is confirmed, not a hypothesis. |

Key conclusions: static AOT ran without fallback in the exercised scenario; Dynarmic remains linked for Hybrid and GPU macro JIT/shader compilation remain runtime generation. The SHL fallback hypothesis was refuted, and budget 256 addressed long-slice interrupt delay. The FZ-closed FPX1 gate was the main remaining static AOT cost; the smoke golden mismatch came from MSVC without `/utf-8` in the test only, so real exports were unaffected. Build exports delete `aot_cache`; use Source format to inspect generated C. The FPS comparison is not under fully matched conditions.

### Open issues, in priority order

1. Milestone 4a step 4(c): exporter `SUYU_AOT_SPLIT_MAIN` option and bundle id, registry v1/marker emission, in-tree launcher DLL handling, packaging/manifest/cache, and the carried 4(c) acceptance criteria.
2. Exercise the dev10 failure paths synthetically.
3. Remaining items from the old list item 5: code review of `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, the pre-existing GG1 `g_recomp_gg_word` link failure, and existing export/GPL/rtld/BUILD.md/version investigations.

### Immediate next action

Milestone 4a step 4(c): implement exporter `SUYU_AOT_SPLIT_MAIN` option and bundle id, registry v1/marker emission, in-tree launcher DLL handling, packaging/manifest/cache, and the carried 4(c) acceptance criteria.

### How to work

- Measure FPS in the same overworld scene and record backend, settings, cache state, and chain-budget condition; never use `SUYU_FPCR_STATS` during FPS measurements.
- Use the test commands above and Source format when generated C must be inspected. Keep build/export paths and diagnostics outside the repository.
- Do not export PLA with `SUYU_AOT_FM_INLINE=1` until the image budget is solved. Read generated files as UTF-8 in Python on Windows, and copy `suyu_log.txt` before restarting suyu after a failed export.
- Follow [CLAUDE.md](../../CLAUDE.md), [CODEBASE_INDEX.md](CODEBASE_INDEX.md), and the Orca coordination notes.

Useful entry points: [agent guide](../../CLAUDE.md), [codebase index](CODEBASE_INDEX.md), [project README](../../README.md), [upstream build guide](../Suyu/Build.md), [upstream export guide](../Suyu/user/GameExport.md), [exporter source](../../src/suyu/game_export.cpp), and [dependency manifest](../../cpmfile.json).

After each meaningful change, add a dated entry with:

- Milestone and source revision.
- Problem and supporting evidence.
- Files changed and reason for the chosen approach.
- Exact build/test commands and relevant settings.
- Observed result, comparison baseline, and untested behavior.
- Next action or blocking information.

Keep raw logs, machine-specific paths, and game-derived artifacts local. Link only to sanitized, shareable evidence from public documentation.

### 2026-10-06: Milestone 4a step 4(c) part 1: split export wiring in exporter, CMake, cache, and package (v0.0.1-dev13)

- **Milestone and revision:** Milestone 4a step 4(c), part 1; base HEAD `e38f7ad` (v0.0.1-dev12); implementation remained uncommitted.
- **Problem and evidence:** The main-module DLL split needed exporter ownership of the option, bundle identity, canonical topology, cache/package records, and linked launcher/CMake handling. `SUYU_AOT_SPLIT_MAIN` is resolved once per export; split mode emits registry v1 and its marker, binds every canonical module, collects and verifies DLLs, and records per-image PE `SizeOfImage`; all-static v4 remains unchanged.
- **Files changed and reasons:** `src/suyu/game_export.cpp` adds split validation, bundle-id/cache/manifest/package wiring, image-size diagnostics, and completion wording; `src/core/recompiler/pe_image_size.h` adds a validated PE32+ reader; `src/core/loader/nso_module_names.h` centralizes the canonical list; `src/suyu_cmd/CMakeLists.txt` and `src/suyu_cmd/recomp_modules/RecompDllTarget.cmake` implement v1 in-tree/prebuild DLL handling; `src/common/package_policy.*` records required module DLLs; `src/core/arm/recomp/arm_recomp.cpp` adds bound-inventory and process-scoped pre-relocation diagnostics; packaging and test files cover the corresponding checks.
- **Commands/settings:** Smoke (including PE/canonical-list units, 21 bundle cases, and all-DLL prebuild), fastmem, reduced FPX, package-policy, export-build-kit, host `suyu`/`suyu-cmd` build with export variables cleared, and split/all-static in-tree/prebuilt configure/link checks were run under Build Tools 2026 `vcvars64`; exact local logs remain outside the repository.
- **Result and untested behavior:** Smoke, fastmem, reduced FPX, package policy, kit tests, host build, and configure/link checks passed. Exporter end-to-end execution, linked v1 launcher guest execution, loader behavior against a game, and `export_regression` remain unverified; part 2 acceptance is still open.
- **Next action:** Plan and implement Milestone 4a step 4(c) part 2 from the acceptance list above.

### 2026-10-06: Milestone 4a step 4(b): registry v1, bundle loading, and inventory binding (v0.0.1-dev12)

- **Milestone and revision:** Milestone 4a step 4(b), base HEAD `1d9ba61` (v0.0.1-dev11); implementation remained uncommitted.
- **Problem and evidence:** The split design needed one registry layout and transactional bundle loader before exporter wiring. The implementation validates registry headers and entries, loads DLLs with identity/ABI/feature/handshake checks, binds canonical loader inventories before process publication, and refuses incomplete host bridges; the v1 launcher path remains macro-gated and not yet linked or run.
- **Files changed and reasons:** `src/core/recompiler/recomp_registry_v1.h` defines the shared C registry v1 layout and export names; `src/core/recompiler/recomp_topology.h` writes registry and marker data; `src/core/arm/recomp/recomp_bundle.h/.cpp` provides transactional loading, inventory matching, and base application; `src/core/arm/recomp/arm_recomp.h/.cpp` provides process binding and bridge checks; `src/core/core.cpp` binds before publication; `src/suyu_cmd/suyu.cpp` adds the `SUYU_RECOMP_REGISTRY_V1` launcher path; `tests/recompiler_smoke/bundle_host.cpp` and `bundle_exec.c` cover 21 bundle cases and mixed-image execution.
- **Commands/settings:** `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl`; `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`; reduced FPX with `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls --cases 1000 --adv 2000 --legs 13`; `cmake --build <host build dir> --target suyu suyu-cmd -j 8` after clearing export variables; compile-only `suyu.cpp` with `-DSUYU_RECOMP_REGISTRY_V1`.
- **Result and untested behavior:** Smoke exited 0 with 8 variants, unchanged goldens, and 21 bundle cases; fastmem had zero mismatches; reduced FPX and host build exited 0; the v1 launcher source compiled. A linked/running v1 launcher, Core::Load/BindRecompProcess behavior, three-module fixture, cross-image reservation/CLREX/pair/counter, bridge-null refusal, non-Windows execution, and long paths remain unverified. Re-review accepted step 4(b) as an intermediate with F3 carried at its original major severity.
- **Next action:** Milestone 4a step 4(c): exporter `SUYU_AOT_SPLIT_MAIN` option and bundle-id/registry/marker emission, in-tree launcher DLL handling, packaging/manifest/cache, and the carried 4(c) acceptance criteria.

### 2026-10-06: Milestone 4a step 4(a): module topology, identity export, and prebuild DLL binding (v0.0.1-dev11)

- **Milestone and revision:** Milestone 4a step 4(a), base HEAD `d4c8cf9`; implementation remained uncommitted.
- **Problem and evidence:** The split design required one authoritative module topology and a verifiable identity contract before registry and launcher loading. The implementation validates module names, order, DLL bindings, bundle identity, and generated target/file references; all-static generated CMake remains byte-identical.
- **Files changed and reasons:** `src/core/recompiler/recomp_topology.h` centralizes topology validation, load ordering, image names, bundle-byte conversion, and `recomp_modules.cmake` output; `src/core/recompiler/recomp_image_identity.h` defines the shared-image identity ABI; `src/core/recompiler/arm64_to_c.h` emits the opt-in identity symbol; `src/suyu/game_export.cpp` uses the topology writer; `src/suyu_cmd/recomp_modules/CMakeLists.txt` validates and builds DLL-bound modules; `src/suyu_cmd/CMakeLists.txt` and `tools/export_build_kit/CMakeLists.txt` reject unsupported split configurations; smoke tests cover topology, identity, and prebuild consumers.
- **Commands/settings:** `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl` (8 variants); `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`; `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls --cases 1000 --adv 2000 --legs 13`; `cmake --build <host build dir> --target suyu suyu-cmd -j 8`, after clearing export variables. The smoke prebuild reported the Visual Studio 18 2026 generator.
- **Result and untested behavior:** All listed commands exited 0; smoke goldens were unchanged, topology/prebuild/identity checks passed, and fastmem had zero mismatches. Review findings were resolved, including DLL artifact-name verification, the raw 32-byte digest contract, and the MSVC-safe negative-array size check. In-tree marker and kit rejection, the main special target and DLL-first ordering, non-Windows paths, Ninja smoke prebuild, cl.exe C-mode identity compilation, and a PLA export remain unverified.
- **Next action:** Milestone 4a step 4(b): implement registry v1 (shared C layout), launcher transactional loading with identity checks, and inventory-based base binding (`BindRecompProcess`).

### 2026-10-06: Milestone 4a step 4(c) part 2 units 1-5: per-export split seam, multi-module fixture, split export through the real exporter, failure injection

- **Milestone and revision:** Milestone 4a step 4(c) part 2 units 1-5; source revision `02c0298` (v0.0.1-dev13 sources plus these commits).
- **Problem and evidence:** The split path needed one-export option control, a sparse cross-module fixture, real exporter coverage, and failure/retry evidence. The existing AOT reuse path is unreachable: `OnExport` creates fresh staging and `RunAotPrecompile` creates its cache there, so no manifest is available for reuse.
- **Files changed:** `src/suyu/game_export.cpp`, `src/suyu/game_export.h`, `src/suyu/main.cpp`, and `tests/export_integration/*`; the fixture generator emits canonical modules, page-aligned uneven text, cross-module `bl` targets, markers, unreadable-path and code-less variants.
- **Approach:** Added the per-export `split_main` seam and named rejection status, split static/Hybrid real-export scenarios, clang-cl build-stage injection (`emit`, `compile`, `dll_link`, `launcher_lnk1248`, plus packaging tamper/remove), and retry checks. Missing `main` and non-canonical NSO are stopped earlier by ExeFS validation; `launcher_link`, static-module failures, MSVC failures, and a real 2 GB LNK1248 reproduction remain open.
- **Verification:** `--only-multimodule` passed the JIT marker run and split static checks (35 passed, 0 failed), Hybrid checks (16 passed), injection checks (24 passed), and all-static checks (11 passed); split-option scenarios passed with invalid/JIT/Source refusal and same-instance retry. The runner was used with `--skip-aot` where applicable; logs and diagnostics stayed outside the repository, and the host tree was restored after each export run. The Python `cryptography` package was absent for two pre-existing runner checks.
- **Remaining uncertainty:** Persistent cache design, implementation, review, and integration matrix are pending; cache location/key, locking, disk use, eviction, Source packages, and test-driven interaction need decisions. Build packages must continue deleting `aot_cache`.

### Where things stand (2026-10-06 tenth-session measurements)

| Observation | Result |
|---|---|
| JIT baseline | 30 FPS. |
| Hybrid first / translate-all / budget 256 | 17 / 14 / 15-20 FPS. |
| Dev6 static AOT | 16-20 FPS, no fallback. |
| Dev8 static AOT | 25-30 FPS, no fallback. |
| [Dev8 static sampling](OLD_HISTORY.md#2026-10-05-dev8-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld) | Hot block 2-3.5% (was 15-19%), scheduler-fiber symbol about 1% (was 10-12.5%), CPU cores waiting 35-49% of samples. |
| [Same-scene JIT sampling](OLD_HISTORY.md#2026-10-05-same-scene-jit-sampling-remaining-gap-is-guest-cpu-cost) | JIT process CPU 275.7% versus 320.3% dev8 static; remaining gap is guest CPU execution cost after excluding wait shares (estimate). |
| [FMI export](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248) | FMI tests passed, but the PLA static launcher image was about 2.19 GB and failed LNK1248 against the 2 GB x64 limit. |
| Image budget | Dev8 about 1,820 MiB estimated (about 228 MiB margin); FMI +421 MiB does not fit. |
| Main DLL (step 3) | `SizeOfImage`: 1,459.1 MiB without FMI / 1,783.7 MiB with FMI; margins: 588.9 MiB / 264.3 MiB to 2,048 MiB. |
| FPCR measurement | FZ set in about 96% of 3,473,408 JIT context saves; do not measure FPS with `SUYU_FPCR_STATS=1`. |
| Sampling profile | Hot block was 15-19% of busy-core RIP samples; `ScheduleImplFiber` was 10-12.5%. The FZ finding is confirmed, not a hypothesis. |

Key conclusions: static AOT ran without fallback in the exercised scenario; Dynarmic remains linked for Hybrid and GPU macro JIT/shader compilation remain runtime generation. The SHL fallback hypothesis was refuted, and budget 256 addressed long-slice interrupt delay. The FZ-closed FPX1 gate was the main remaining static AOT cost; the smoke golden mismatch came from MSVC without `/utf-8` in the test only, so real exports were unaffected. Build exports delete `aot_cache`; use Source format to inspect generated C. The FPS comparison is not under fully matched conditions.


- **Archive rule:** At each session close, move progress entries older than the previous session (and their session-close entry) verbatim to OLD_HISTORY.md, and add any still-relevant fact to Key facts from archived history.

### 2026-10-06: Milestone 4a step 3: main module DLL image measured without and with FMI

- **Milestone and revision:** Milestone 4a step 3, HEAD `d4c8cf9`.
- **Problem and evidence:** The design needed a measured `main` DLL image before implementation. A local wrapper CMake project outside the repository built only the generated `main` module's existing SHARED target `recompiled_image` with `RECOMP_STATIC_ONLY` off and `RECOMP_BUILD_STATIC_LIB` off, once without FMI and once with FMI.
- **Inputs:** No-FMI used generated code from the dev8 static AOT Source export; FMI used generated code left in the aot_cache of the failed dev9 FMI export, whose `recomp_runtime.h` contains `recomp_fmi`. Non-ASCII source paths were reached through ASCII junctions.
- **Files changed:** No repository source changed; this measurement was recorded in `docs/Aevora/WORK_PLAN.md` and `CHANGELOG.md`.
- **Commands/settings:** Clang-cl 22.1.8 (Build Tools 2026 vcvars64), Ninja, Release, `RECOMP_JOBS=7`, lld-link (`/dll /INCREMENTAL:NO`), 704 build steps (701 generated units plus runtime/export). Compile definitions and options matched `src/suyu_cmd/recomp_modules/CMakeLists.txt`: `BUILD_ID="amd64"`, `_CRT_SECURE_NO_WARNINGS`, `_CRT_NONSTDC_NO_DEPRECATE`, `_SCL_SECURE_NO_WARNINGS`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE`, `_UNICODE`, `/utf-8 /GS- /Gy -Wno-everything`; generated CMake applied `SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1`, `/O2 /Ob2 /bigobj /fp:precise -MD /std:c11`. No `/DEBUG` was used, so no PDB was produced.
- **Result:**

| Item | No FMI (dev8) | FMI |
|---|---:|---:|
| SizeOfImage | 1,530,019,840 bytes (1,459.1 MiB) | 1,870,344,192 bytes (1,783.7 MiB) |
| Margin to 2,048 MiB | 588.9 MiB | 264.3 MiB |
| Size of code | 1,195.4 MiB | 1,520.0 MiB |
| File size | 1,516,892,160 bytes | 1,857,218,560 bytes |
| `.text / .rdata / .pdata / .data / .reloc` (hex) | `4AB6A000 / CF46000 / 2582000 / C81000 / 66F000` | `5EFF9000 / CF46000 / 2582000 / C81000 / 66F000` |
| Wall time, configure-to-link (one run each) | 40.7 min | 47.8 min |
| Link time (DLL mtime minus last object mtime, approximate) | about 23 s | about 25 s |

- **Exports and dependents:** Both DLLs export only `recomp_image_abi`, `recomp_image_entry`, `recomp_image_fastmem_v1`, `recomp_image_features`, `recomp_image_fpx_v1`, `recomp_image_guard_gen_v1`, `recomp_image_guard_v2`, `recomp_image_index`, `recomp_image_lookup`, `recomp_image_run_slice`, and `recomp_image_set_base`. Both depend on `KERNEL32.dll`, `VCRUNTIME140.dll`, `api-ms-win-crt-stdio-l1-1-0.dll`, `api-ms-win-crt-runtime-l1-1-0.dll`, `api-ms-win-crt-heap-l1-1-0.dll`, and `api-ms-win-crt-environment-l1-1-0.dll`; observed KERNEL32 imports were `DisableThreadLibraryCalls`, `GetCurrentProcessId`, `GetCurrentThreadId`, `GetSystemTimeAsFileTime`, `InitializeSListHead`, and `QueryPerformanceCounter`, with no host/launcher symbol imports. This satisfies the design's D2/D3 check that the DLL exports only the image ABI and depends only on CRT/OS DLLs.
- **Comparison and limits:** The image-budget estimate was about 1,455 MiB without FMI / 1,780 MiB with FMI; measured `SizeOfImage` was 1,459.1 / 1,783.7 MiB. DLL only; the split launcher (four static modules plus runtime) was not linked, so its size remains an estimate of about 365 / 460 MiB. No PDB was produced because `/DEBUG` was not used, so PDB size was not measured; the link used lld-link and the exporter's MSVC `link.exe` path was not measured. No runtime load, FPS, or PLA test was run. Timings are single runs on a 12-core / 16 GB machine.
- **Next action:** Milestone 4a step 4(a): implement the generator/CMake opt-in split mode (`SUYU_AOT_SPLIT_MAIN`) and the topology value, by code-quick-implementer and reviewed by Code reviewer.

### 2026-10-06: Milestone 4a design note reviewed and accepted (steps 1-2)

- **Milestone and revision:** Milestone 4a, HEAD `e66f19c`.
- **Problem and evidence:** The dev9 FMI static launcher exceeded the 2 GB x64 image limit; step 1 surveyed globals/TLS, exe<->module dependencies, and the launcher ABI6/DLL path, and the design note was reviewed through draft 3 with no unresolved findings.
- **Files changed:** `docs/Aevora/WORK_PLAN.md` records the accepted D1-D7 design, review history, and handoff; `docs/Aevora/CODEBASE_INDEX.md` records the verified inventory and launcher paths; `CHANGELOG.md` records the documentation closure.
- **Commands/settings:** No build, test, export, or runtime command was run in this session; design review used read-only source inspection and the local report outside the repository.
- **Result and untested behavior:** The design specifies a DLL-local runtime copy; versioned topology registry v1 as the single source (v4 unchanged); bundle identity export; transactional loading; inventory-based base binding via `BindRecompProcess` before process publication; opt-in `SUYU_AOT_SPLIT_MAIN`; kit static-only with consumer-side rejection; package/cache derived from topology; and default output byte-identical. Implementation correctness, DLL image size, dependencies, performance, and PLA compatibility remain untested.
- **Next action:** Milestone 4a step 3 measurement of the main DLL without and with FMI via a Source-format export and the generated SHARED target.





### 2026-10-05: Static launcher image budget: dev8 (no FMI) versus FMI section totals

- **Milestone and revision:** Milestone 4. HEAD `a3063a2`; generated code from the dev8 static AOT Source export (no FMI; its `recomp_runtime.h` contains no `recomp_fmi`), and the dev9 FMI module libraries left by the failed FMI export.
- **Method:** The dev8 Source export's five modules were built with the exporter's own module project (`src/suyu_cmd/recomp_modules`, clang-cl 22.1.8 `/O2`, Ninja, Release, `RECOMP_JOBS=7`) in a local directory outside the repository, through ASCII junctions to the Source export (the project only checks that `recomp_registration.c` exists, so an empty stub was used; no launcher was linked). `dumpbin /summary` was run on each `recomp_static_<module>.lib` for both builds. The build ran in two parts (interrupted at 531/893 and resumed) and overlapped a host rebuild, so compile time was not measured.
- **Result (MiB, sum of five modules):**

| Section | Dev8 (no FMI) | FMI | Change |
|---|---:|---:|---:|
| .text | 1,421.7 | 1,840.6 | +29.5% |
| .rdata | 264.8 | 264.8 | 0 |
| .pdata | 47.3 | 47.3 | 0 |
| .xdata | 33.0 | 34.7 | +5.2% |
| .bss | 15.8 | 15.8 | 0 |
| Image-relevant sum | 1,782.5 | 2,203.2 | +23.6% |

Per module (image-relevant sum, dev8 / FMI): main 1,455.3 / 1,780.5; sdk 131.1 / 165.5; subsdk1 121.9 / 163.0; subsdk0 74.0 / 94.0; rtld 0.1 / 0.2. main is about 82% of the generated code.

- **Calibration and estimate:** The FMI link reported image size 0x8C0E0000 (2,240.9 MiB), 37.7 MiB above the FMI object sum; this difference is attributed to the runtime and launcher code (assumption). Applying the same offset, the dev8 static launcher image is estimated at about 1,820 MiB, leaving about 228 MiB (about 11%) under the 2 GiB limit. This is an estimate; the dev8 launcher was not linked in this measurement.
- **Composition:** `_expected[]` per-block guard arrays, counted from the dev8 generated C (`(3 + instruction count)` 32-bit words per array), total about 112 MiB (main 88.0, sdk 9.2, subsdk1 9.7, subsdk0 5.5 MiB), about 42% of .rdata. The rest of .rdata (dispatch/jump tables and constants) and the .text split between block code, per-instruction entry labels, and inline exact-FP bodies were not attributed.
- **Interpretation:** FMI adds about 421 MiB, roughly twice the remaining margin, so FMI for all generated code cannot fit one image. Earlier per-unit A/B compiles (2-15% object growth) under-represented the whole-image .text growth (+29.5%). Options remain: restrict FMI to hot code, reduce generated-code contributors (`_expected[]`, entry labels, FP bodies), or move `main` to a separate DLL image.
- **Next:** Choose the image-budget strategy; any generated-code change must be checked with the same `dumpbin /summary` method before a full PLA export.

### 2026-10-05: Exporter stops on LNK1248 and keeps build-failure logs (v0.0.1-dev10, unverified by export)

- **Problem:** see the FMI LNK1248 entry above: after the clang-cl link failed with LNK1248, the exporter fell into the MSVC per-module path (about 20 h estimate, cannot fit the same image), and the first compiler/linker output was lost to log rotation and work-directory cleanup.
- **Change (`src/suyu/game_export.cpp`):** new `SaveBuildFailureLog` appends failed compile/link output from the clang-cl and MSVC paths to `export_build_failure.log` in suyu's log directory (UTF-8, write/flush errors checked, truncated before appending once over 8 MiB). In the clang-cl link-failure branch, LNK1248 that the existing Hybrid largest-module retry does not handle now shows a "Build Failed" dialog with the saved log path and fails the export instead of falling back to MSVC.
- **Review:** Code reviewer (Orca, gpt-6.1-sol, high) found one P2 issue (write/flush errors not checked before reporting the log as saved) and a wording caveat (MSVC is not proven unable to produce smaller code; the change now says it does not remove the limit and is costly); both were fixed. Control flow (failure return, staging cleanup, Hybrid retry, one-module Hybrid) was reviewed with no findings.
- **Verification:** `rebuild-suyu.bat` (Build Tools 2026 vcvars64, Ninja, `-j 8`) built suyu and suyu-cmd after the change and after the review fix; no warnings from `game_export.cpp`.
- **Not run:** no export exercised the new paths; injected LNK1248 for static, Hybrid retry, and one-module Hybrid, and log-helper Unicode/append/rotation/open-failure/write-failure cases remain untested.
## 2026-10-07 persistent AOT cache U1-U4 (design, decisions, implemented behavior, matrix, evidence)

### Persistent AOT cache (DECIDED; option b)

The user chose option b: reintroduce and verify a persistent AOT cache. Benefits are faster re-exports, reproducibility checks, preservation of good builds after packaging failures, and making the existing reuse code live and tested; costs are about 3 GB per PLA entry, stale-artifact risk, and implementation effort. `RunAotPrecompile` already compares the manifest's full scan, backend, ABI/features, FMI, correctness revision, translate-all, source hash, coverage fingerprint, fallback policy, and split bundle/bindings/topology/DLL records, but reuse is unreachable because each export creates fresh staging; Build packaging also removes `aot_cache`.

The confirmed defaults are a user-data store, Build format only, and producer identity based on the suyu revision plus build id. The store is `aot_export_cache` under suyu user data, outside the repository and output folder. Each entry is `aot_export_cache/<title_id>_<platform>_<backend>_<split|all-static>/`; underscores are separators and key parts may contain dashes but never underscores. Layout is authoritative in `src/suyu/aot_export_cache.h`: `launcher/static_launcher.exe`, `aot_manifest.json`, `exefs/recomp_modules.cmake` for split entries, and `cache_entry.txt` with layout version, entry name, `producer.suyu_revision`, and `producer.build_id`. The platform name is `windows`.

**Implemented behavior:** U1 (store/key, atomic publish, lock, producer identity) is committed as `2043b92`; U2 (publish and failed-export preservation) as `edb6eb5`; U3 (persistent reuse and split reader) as `71ce879`; U4 (integration matrix and supporting status fields) is implemented but uncommitted at hand-off. Publishing occurs after successful AOT precompile and before packaging; valid artifacts remain available when later packaging fails. Reuse takes the per-entry lock without waiting, validates the record and artifacts, compares every recorded input and the manifest launcher hash/size, and copies only a full match. Mismatches name the item; lock contention skips both reuse and publish; entries replace in place without generations/LRU; source-format and non-Windows exports do not use the cache; packages contain no `aot_cache`.

The integration matrix covers identical reuse, source/backend/split/coverage/generator/producer invalidation, missing or tampered artifacts, failed export preservation, lock contention, and package exclusion. Test seams provide a cache root and producer revision/build id only for test-driven exports; status fields expose `reused`, named mismatch/reason, busy, publish, and not-applicable outcomes. Open: Hybrid publish/reuse on real PLA; PLA entry size, multi-GB launcher hash time, publish/copy time, real split export, and FMI; ACL-unreadable manifests, post-match copy failure, rename/restore failures, process-to-process unit lock contention, and dirty-tree-only producer changes; plus items 6/7 and the remaining acceptance cases listed above.

### Eleventh-session handoff (verbatim)

- **Current local state:** HEAD is `71ce879`; U1 `2043b92`, U2 `edb6eb5`, and U3 `71ce879` are committed; U4 is uncommitted. Uncommitted files are `src/suyu/game_export.cpp`, `src/suyu/game_export.h`, `src/suyu/main.cpp`, `tests/export_integration/run_export_integration.py`, and these Markdown files; the user commits. The host build tree was restored with the three export variables cleared; local work folders and logs are outside the repository.

- **Build/test:** Final `python tests/export_integration/run_export_integration.py --suyu-dir <build>/bin --work <new short folder> --only-aot-cache` passed 262/0; use `--only-aot-cache-matrix` for the matrix alone. Fixture entries are about 0.03 GiB. `rebuild-suyu.bat` rebuilt suyu and suyu-cmd afterwards and the three export variables were cleared. Never run two rebuilds or ninja invocations on one build tree concurrently.
- **Switches:** `SUYU_FPCR_STATS=1` is diagnostic-only and must not be used for FPS. `SUYU_RECOMP_CHAIN_BUDGET` is 1-8192, default 256. `SUYU_AOT_TRANSLATE_ALL=1` applies to Hybrid; static AOT forces translate-all and strict mode. `SUYU_AOT_FM_INLINE=1` remains off for PLA because the prior static export exceeded the 2 GB limit.

### What this session did

- Implemented persistent AOT cache U1-U4: U1 `2043b92`, U2 `edb6eb5`, U3 `71ce879`, and U4 uncommitted. The three confirmed defaults are a user-data store, Build format only, and producer revision plus build id.
- Verification counts: `--only-multimodule` 35 passed/0 failed; split Hybrid 16/0; injection 24/0; all-static 11/0; source refusals 3/3.
- Per-export seam run passed new split-option checks; two pre-existing failures need the Python `cryptography` package.
- Fixed the launcher LNK1248 blocking dialog in automated runs.
- The final synthetic-fixture cache run passed 262/0; status fields are the evidence because suyu's log tail is lost at exit.

### Verified context retained from history

JIT 30 FPS; Hybrid 15-20 FPS; dev6 static 16-20 FPS; dev8 static 25-30 FPS with zero fallback; FMI export 2.19 GB fails LNK1248; main DLL `SizeOfImage` is 1,459.1 MiB without FMI / 1,783.7 MiB with FMI. These measurements are historical evidence, not a matched comparison; details remain in [OLD_HISTORY.md](OLD_HISTORY.md).

## 2026-10-07: Eleventh-session handoff replaced by the 4(d) batch

### Session handoff (2026-10-07, end of eleventh session; verbatim)

#### State

HEAD is `1844217` (`v0.0.1-dev15`); the tree was clean before this Markdown task and the host build tree was restored. Persistent AOT cache U1-U4 is implemented and committed; synthetic-fixture verification passed 262/0. Logs and short-path work folders stay outside the repository.

#### What was done this session

Implemented and verified cache U1-U4, completed the integration matrix and status fields, and completed the Orca review rounds. The cache defaults are user-data store, Build format only, and producer revision plus build id; status fields are authoritative because suyu's log tail is lost at exit. Historical counts and detail moved to [OLD_HISTORY.md](OLD_HISTORY.md#eleventh-session-handoff-verbatim).

#### Open issues, in priority order

1. Two-application-lifetime seam: bind `RecompProcess` twice, unregister on `SetRecompLookup(nullptr)`, and verify the smallest test-only `Core::System` host dependencies allow; then named `Core::Load` refusals with zero guest execution.
2. Fix `tests/export_regression` Qt-root lookup for the CPM Qt layout so `run_fixture.py` runs again.
3. Complete step 4(d)'s three-or-more-module fixture and carried criteria.
4. Complete remaining 4(c)/4a cases: `launcher_link` generic failure (fallback to MSVC), static-module compile/link injection, MSVC-route failures, real LNK1248 (needs PLA), real PLA split export, and part 2 items 6/7.
5. On PLA, repeat static AOT without/with FMI, gameplay checklist, and measure cache entry size (~3 GB expected), multi-GB launcher hash time, publish/copy time, and Hybrid publish/reuse.
6. Cover ACL-unreadable manifest, post-match copy failure, rename/restore publish failure, two-process lock contention unit test, and dirty-tree-only producer difference.
7. Older investigations: `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, pre-existing GG1 `g_recomp_gg_word` link failure, and export/GPL/rtld/BUILD.md/version investigations; two pre-existing per-export-seam failures need Python `cryptography`.

#### Immediate next action

Read `CLAUDE.md`, this file, and `git status`/`git log`; then start item 1: design the lifetime/unregister contract, verify the smallest host seam, obtain Code reviewer review, and update Markdown only after the unit loop is complete.

#### How to work

- Never run two `rebuild-suyu.bat`/ninja invocations on one build tree. Run `cmd /c <local>\rebuild-suyu.bat` from PowerShell, clear the three export variables after exports, and keep logs outside the repository.
- The exporter log tail is not evidence; use `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused`. Use a short ASCII `D:\hwk\<name>` work folder.
- After `worker-start`, ensure `turnStart` is `observed`; otherwise send `orca terminal send --terminal <handle> --enter`. Run the full integration suite once per finished review-fix set; a test-only expectation fix needs syntax plus the affected case.
- Useful commands: `python tests/export_integration/run_export_integration.py --suyu-dir <build>/bin --work <new short folder> --only-aot-cache` and `--only-aot-cache-matrix`. Switches: `SUYU_FPCR_STATS`, `SUYU_RECOMP_CHAIN_BUDGET`, `SUYU_AOT_TRANSLATE_ALL`, `SUYU_AOT_FM_INLINE`.
+

## Archived on 2026-10-08 (from WORK_PLAN.md, end of thirteenth session)

This archive preserves the displaced Step 6 and U1 evidence from the end-of-session batch. The complete verified facts are retained here so the current plan remains concise.

### Step 6 PLA run (user-run): static AOT + FMI + main-DLL split

- The exporting process had `SUYU_AOT_FASTMEM=1`, `SUYU_AOT_FM_INLINE=1`, and `SUYU_AOT_SPLIT_MAIN=1`; CPU Backend was “suyu static AOT (Experimental)”. The manifest recorded effective_backend `suyu-static`, fallback_enabled false, fm_inline true, image_abi 6, image_features 7, translate_all true, bundle_id `a064231234e1da3dc7560138bf901fabef907c3182811f4422ef1cf6a716299e`, module bindings main=dll and rtld/sdk/subsdk0/subsdk1=static, launcher size 471,994,880 bytes and SizeOfImage 479,465,472, main DLL size 1,857,218,560 bytes and SizeOfImage 1,870,344,192, 4,131,332 recompiled C blocks, 17,169,816 total instructions, producer revision `7db8f01` (dev16), and build id main. The export linked and ran without LNK1248.
- User-observed FPS: intro and early pier stable at 30 FPS; Kotobuki Village unstable at 17-20 FPS. JIT at the same spot was 30.0 FPS. GPU utilization was JIT 30-33% versus AOT 18-25%.
- “(JIT-capable)” is added only for strict static AOT built with a JIT; Hybrid is titled “suyu Hybrid JIT + AOT” without that qualifier.
- Per-thread CPU over 60 seconds at Kotobuki (recomp/JIT): Core_0 76.3/76.8%, Core_1 80.4/78.7%, Core_2 81.4/77.2%, HostTiming 98.5/98.8%, GPU 18.8/27.7%, VulkanWorker 9.2/14.1%. At 17 versus 30 FPS this is roughly 1.8x more core time per frame; the JIT run may be limited by the 30 FPS cap.
- RIP sampling (2,000-8,000 samples/core; no PDB for recompiled_image.dll) found about 46-55% in main-DLL generated code, 17-31% idle wait, about 7% in Dynarmic’s SpinLock page, and about 3.4% per core in the DLL guest-address lookup helper. Call-stack sampling found idle waits in Cnd_wait/Idle/MultiCoreRunIdleThread, not GPU/fence waits; the GPU thread was 83% in its command queue. Lower GPU utilization is consistent with fewer submitted frames, and a GPU synchronization fault is not supported by this evidence.
- SpinLock samples were pause spin 1.7-2.1%, acquire xchg 2.7-3.2%, release xchg 0.9-1.2%, redundant mfence 1.0-1.4%, total about 6.8-7.3%. Callers were ExclusiveMonitor reservation reads/writes. The earlier statement that JIT showed about 0% SpinLock was invalid because its lock is inline. Removing all exclusive-monitor cost is estimated, not measured, at roughly 1-2 FPS.
- Local diagnostics were outside the repository under `D:\HisuiWakeBuild\diag\` and logs under `D:\HisuiWakeBuild\logs\`. FMI-off, per-image comparison, link/hash/publish timings, loader inventory names, Hybrid publish/reuse, cache-entry size, and resolution-lowering FPS testing were not done. Step 6 remains open.

### U1 exclusive-monitor change

- U1 changed `src/core/memory.h/.cpp`, added `src/core/arm/dynarmic/dynarmic_exclusive_access.h` and the monitor fast path in `dynarmic_exclusive_monitor.cpp/.h`, listed the header in `src/core/CMakeLists.txt`, removed the redundant unlock mfence in `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp`, and added `tests/exclusive_access/`.
- Plain mapped pages resolve a host pointer before the global lock; reservation bookkeeping plus one load or compare-and-swap run under the monitor lock. Cross-page, rasterizer-cached, unmapped, and out-of-range accesses use the old locked path. `NoteExclusiveWrite` runs after unlock only when the CAS ran.
- Host builds of suyu and suyu-cmd exited 0 after each revision. The standalone test passed exact u8/u16/u32/u64/u128 counters, same-old-value reservation race, a race-free negative control, reservation semantics, upper-half compare failure, ClearProcessor isolation, and the attempted flag.
- Three Orca Code reviewer rounds completed. Round 1 fixed lost-reservation GPU handling and a data race in the negative control; round 2 fixed page-crossing 128-bit/unaligned handling and conditional notification; round 3 approved with no findings.
- PLA, recompiler smoke/fastmem/FPX, export integration, and Core::System notification/rasterizer coverage were not run. Suggested user commit title: `recomp: Resolve the exclusive access host pointer before taking the monitor lock and drop the redundant spin-lock unlock mfence`.

### Persistent cache constraint and planned follow-ups

- The user-data cache entry records producer revision `7db8f01c6bb726fb0736854b14f7667945699371`, build id `main`, launcher size 471,994,880 bytes, and the old runtime. Producer identity is the git commit hash plus branch; uncommitted changes do not change it.
- The rebuilt host embeds `8818bd38d0c95d35fed1603df289cf75b66a40ce`. An export now rejects the cache producer and regenerates/recompiles; if the revision matched, it would reuse the old launcher without U1. Do not fake producer identity. Relink-only against the cache or commit plus full re-export are proposed; neither was performed.
- U2 is a proposed lock-free reservation publish requiring explicit approval and tolerating an ABA-class identical-value race. U3 would inline ldxr/stxr through FM1 with ABI/version/handshake/hash changes.

### Session handoff (2026-10-07, end of twelfth session; verbatim)

#### State

HEAD is `7db8f01` (`v0.0.1-dev16`); U1 `33ed262`, U2 `3dc53c7`, and U3a `71b5fd6` are committed, while the current test additions remain uncommitted. The final regression evidence is synthetic-fixture only; logs and work folders stay outside the repository.

#### What was done this session

Completed 4(d) U1-U4: the xmod three-module static/DLL/static fixture, the required bridge callback list and bundle-boundary lookup-miss model, loader-side refusals and path cases, and full/targeted regression runs. Full smoke exited 0 with unchanged ABI5/FM1/FPX golden hashes; fastmem had 0 mismatches across 5 fmdiff and 4 GG1 legs; reduced FPX had 1,786,000 and full FPX had 100,587,904 plus 30,320,000 cases, all with 0 mismatches. Step 5 added and verified cache reuse in both binding directions, failure after DLL verification and before promotion, and kit-only split rejection; the integration run passed 272 tests with 0 failures and the build-kit package suite passed 14 tests. The host tree was rebuilt successfully, linking both `suyu.exe` and `suyu-cmd.exe`. The bridge is a test model and FPX evidence shows value/flag agreement only, not native-path selection. Review findings for U1-U3a were fixed; no re-review of U1 fixes was run.

#### Open issues, in priority order

1. Two-application-lifetime seam, including the real `ArmRecomp` null-callback refusal and strict lookup-miss refusal; the synthetic bridge/list and lookup model does not cover these.
2. Fix `tests/export_regression` Qt-root lookup for the CPM Qt layout so `run_fixture.py` runs again.
3. Step 6 PLA verification and PLA cache measurements.
4. Remaining Step 5 cases: MSVC-route LNK1248 / `launcher_link` injection pairs; NSO/NSP/XCI real-loader paths; real `ArmRecomp` null-callback and strict-miss refusals; a stub image missing only the identity export; static-module compile/link injection; and generic `launcher_link` fallback to MSVC.
5. Extra cache cases and older investigations.

#### Immediate next action

Start item 1: design the lifetime/unregister contract, verify the smallest host seam, obtain Code reviewer review, and update Markdown only after the unit loop is complete.

#### How to work

- Never run two `rebuild-suyu.bat`/ninja invocations on one build tree. Run `cmd /c <local>\rebuild-suyu.bat` from PowerShell, clear the three export variables after exports, and keep logs outside the repository.
- The exporter log tail is not evidence; use `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused`. Use a short ASCII `D:\hwk\<name>` work folder.
- Useful commands: `python tests/export_integration/run_export_integration.py --suyu-dir <build>/bin --work <new short folder> --only-aot-cache` and `--only-aot-cache-matrix`. Switches: `SUYU_FPCR_STATS`, `SUYU_RECOMP_CHAIN_BUDGET`, `SUYU_AOT_TRANSLATE_ALL`, `SUYU_AOT_FM_INLINE`.


## Archived on 2026-10-07 (from WORK_PLAN.md, end of twelfth session)

### Carried acceptance criteria

4(c) requires real split export and linked v1 launcher with fallback disabled, unreadable middle-module path, complete inventories/relocation input, named `Core::Load` refusals with zero guest execution, unregister/rebind, and unchanged all-static v4. 4(d) U1-U4 verify the synthetic three-module static/DLL/static fixture, loader refusals, bridge-list and lookup-miss model, and regression suites. Step 5 covers cache reuse in both binding directions, failure after DLL verification and before promotion, and kit-only split rejection; the remaining test cases and real `ArmRecomp` seam remain open.

### Part 2 acceptance list

- [x] Synthetic split static/Hybrid exports, linked v1 launcher without fallback, inventories, relocation diagnostics, and main-only all-DLL topology.
- [x] Source/analysis refusals, including code-less module refusal; earlier ExeFS validation stops missing `main` and non-canonical NSO.
- [x] Injected emit/compile/DLL-link/launcher-LNK1248/tamper/missing-DLL failures and retry reporting.
- [x] All-static v4 regression and persistent AOT cache reuse/invalidation matrix; Build packages exclude `aot_cache`.
- [ ] Remaining cases: MSVC-route LNK1248 / `launcher_link` injection pairs; NSO/NSP/XCI real-loader paths; real `ArmRecomp` null-callback and strict-miss refusals; a stub image missing only the identity export; static-module compile/link injection; and generic `launcher_link` fallback to MSVC.

### Persistent AOT cache (implemented, U1-U4)

- Defaults: user-data `aot_export_cache`, Build format only, producer = suyu revision + build id.
- Entry key/layout authority: `src/suyu/aot_export_cache.h`; launcher, manifest, split module selection, and producer record are stored per entry.
- Publish occurs after precompile and before packaging; reuse copies only on a full match.
- Mismatches are named; a busy lock means no reuse and no publish; entries replace atomically.
- Build/Windows only; packages contain no `aot_cache`.
- Test seams and status fields cover cache root/producer identity, reuse, mismatch, busy, publish, and not-applicable outcomes.
- Synthetic fixture final run: `--only-aot-cache` 272 passed / 0 failed; full design/evidence is in [OLD_HISTORY.md](OLD_HISTORY.md#2026-10-07-persistent-aot-cache-u1-u4-design-decisions-implemented-behavior-matrix-evidence).

### Session handoff (2026-10-07, end of twelfth session; verbatim)

#### State

HEAD is `7db8f01` (`v0.0.1-dev16`); U1 `33ed262`, U2 `3dc53c7`, and U3a `71b5fd6` are committed, while the current test additions remain uncommitted. The final regression evidence is synthetic-fixture only; logs and work folders stay outside the repository.

#### What was done this session

Completed 4(d) U1-U4: the xmod three-module static/DLL/static fixture, the required bridge callback list and bundle-boundary lookup-miss model, loader-side refusals and path cases, and full/targeted regression runs. Full smoke exited 0 with unchanged ABI5/FM1/FPX golden hashes; fastmem had 0 mismatches across 5 fmdiff and 4 GG1 legs; reduced FPX had 1,786,000 cases with 0 mismatches, and full FPX had 100,587,904 plus 30,320,000 cases with 0 mismatches. Step 5 added and verified cache reuse in both binding directions, failure after DLL verification and before promotion, and kit-only split rejection; the integration run passed 272 tests with 0 failures and the build-kit package suite passed 14 tests. The host tree was rebuilt successfully, linking both `suyu.exe` and `suyu-cmd.exe`. The bridge is a test model and FPX evidence shows value/flag agreement only, not native-path selection. Review findings for U1-U3a were fixed; no re-review of U1 fixes was run.

#### Open issues, in priority order

1. Two-application-lifetime seam, including the real `ArmRecomp` null-callback refusal and strict lookup-miss refusal; the synthetic bridge/list and lookup model does not cover these.
2. Fix `tests/export_regression` Qt-root lookup for the CPM Qt layout so `run_fixture.py` runs again.
3. Step 6 PLA verification and PLA cache measurements.
4. Remaining Step 5 cases: MSVC-route LNK1248 / `launcher_link` injection pairs; NSO/NSP/XCI real-loader paths; real `ArmRecomp` null-callback and strict-miss refusals; a stub image missing only the identity export; static-module compile/link injection; and generic `launcher_link` fallback to MSVC.
5. Extra cache cases and older investigations.

#### Immediate next action

Start item 1: design the lifetime/unregister contract, verify the smallest host seam, obtain Code reviewer review, and update Markdown only after the unit loop is complete.

#### How to work

- Never run two `rebuild-suyu.bat`/ninja invocations on one build tree. Run `cmd /c <local>\rebuild-suyu.bat` from PowerShell, clear the three export variables after exports, and keep logs outside the repository.
- The exporter log tail is not evidence; use `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused`. Use a short ASCII `D:\hwk\<name>` work folder.
- Useful commands: `python tests/export_integration/run_export_integration.py --suyu-dir <build>/bin --work <new short folder> --only-aot-cache` and `--only-aot-cache-matrix`. Switches: `SUYU_FPCR_STATS`, `SUYU_RECOMP_CHAIN_BUDGET`, `SUYU_AOT_TRANSLATE_ALL`, `SUYU_AOT_FM_INLINE`.
## Archived on 2026-10-08 from WORK_PLAN.md (end of fourteenth session)

This section preserves the superseded thirteenth-session handoff. At that point HEAD was `8818bd3`, U1 was uncommitted, the Kotobuki observation was 17-20 FPS versus 30 FPS JIT, and the next action was to ask whether to relink only or commit and fully re-export before measuring U1. That handoff is superseded by the verified U1 launcher measurements and chain experiments recorded in the fourteenth-session plan.

Correction to the earlier chain claim: the historical statement that direct chains were intra-module (~380-410) described an older intended/state measurement, not active current call sites. Before `d89d6c2`, `ChainTo` and `ChainIndirect` had no call sites and B/BL/RET/BR/BLR/conditional branches returned through `run_slice`; current direct and indirect chaining are opt-in and independently flagged.


## Archived on 2026-10-08 from WORK_PLAN.md (fifteenth-session cleanup)

The complete superseded WORK_PLAN text follows verbatim so no removed detail is lost.

## Archived on 2026-10-08 from WORK_PLAN.md (sixteenth-session replacement)

The superseded fifteenth-session handoff follows verbatim.

## Session handoff (2026-10-08, fifteenth session)

### State and completed work

HEAD is `0a9813a`/dev17 (code last changed in `cba2193`). Milestone 4a synthetic work, U1, direct chaining, indirect chaining, reviews, smoke, cache matrix, negative controls, and host build are complete. PLA observations show no measurable CPU change from U1 or chaining; generated code and unexplained idle waits remain the likely investigation targets.

### Open issues, in priority order

1. Disassemble and map hot main-DLL RVAs to generated functions; decide whether an attribution-only re-export is needed.
2. Stack-sample what the roughly 20% idle core wait is waiting on.
3. Finish the Step 6 checklist above.
4. Consider U2 only with explicit user approval and low priority; then address held work.

### Immediate next action

Disassemble and map the hot RVAs, then ask the user for FPS from the chain builds if it is still unknown.

# Aevora Work Plan

Handoff prepared: 2026-10-08. HEAD is `cba2193` on `main`; the pending dev17 commit contains Markdown only, and the user commits, tags, and pushes. Start at [Session handoff](#session-handoff-2026-10-08-end-of-fourteenth-session); detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Handoff status

| Item | Evidence/status |
|---|---|
| Git baseline | `main` at `cba2193`; the pending dev17 commit contains Markdown only. |
| Milestone 4a | Steps 1-3, 4(a)-(c) part 1, 4(d) U1-U4, and covered Step 5 cases are done on synthetic fixtures. |
| Persistent cache | Synthetic `--only-aot-cache` passed 272/0; build-kit package suite passed 14 tests. |
| Step 6 PLA | Static AOT + FMI + main-DLL split exported and ran; Kotobuki is 17-20 FPS versus 30 FPS JIT. Remaining measurements/items are open. |
| U1 exclusive access | Implemented and reviewed through three rounds; standalone test passed. PLA launcher observation completed; no material FPS change was reported. |
| Chaining | Direct B/BL and indirect RET/BR/BLR chaining are opt-in, independently flagged, reviewed, and synthetic-fixture verified. |

## Key facts

- Windows baseline: MSVC 14.51/Build Tools 2026, Ninja 1.13.2, CMake 4.4.3, clang-cl 22.1.8, Python 3.14.7, Vulkan SDK 1.4.363.0; use a short ASCII `CPM_SOURCE_CACHE`.
- The Step 6 exporting process had `SUYU_AOT_FASTMEM=1`, `SUYU_AOT_FM_INLINE=1`, and `SUYU_AOT_SPLIT_MAIN=1`; effective backend was `suyu-static`, fallback was false, image ABI/features were 6/7, and the bundle id was `a064231234e1da3dc7560138bf901fabef907c3182811f4422ef1cf6a716299e`.
- The launcher was 471,994,880 bytes (`SizeOfImage` 479,465,472); main DLL was 1,857,218,560 bytes (`SizeOfImage` 1,870,344,192; 1,783.7 MiB). The export linked and ran without LNK1248. Producer revision was `7db8f01` (dev16).
- Observed FPS: intro and early pier stable at 30; Kotobuki unstable at 17-20; matched JIT spot 30.0. GPU utilization was JIT 30-33% and AOT 18-25%.
- At Kotobuki, recompiled/JIT CPU samples were Core0 76.3/76.8%, Core1 80.4/78.7%, Core2 81.4/77.2%, HostTiming 98.5/98.8%, GPU 18.8/27.7%, VulkanWorker 9.2/14.1%. This is roughly 1.8x core time per frame at 17 versus 30 FPS; the JIT cap may make the true gap larger.
- RIP sampling found 46-55% in main-DLL generated code, 17-31% idle wait, about 7% Dynarmic SpinLock page, and about 3.4% per core in the guest-address lookup helper. Stack sampling identified idle core waits, not GPU/fence waits; the GPU thread waited 83% in its command queue. A GPU synchronization fault is not supported by this evidence.
- SpinLock breakdown was about 6.8-7.3% per core: pause 1.7-2.1%, acquire xchg 2.7-3.2%, release xchg 0.9-1.2%, redundant mfence 1.0-1.4%. The JIT's equivalent lock is inline and is not comparable as a separate page. Removing all exclusive-monitor cost is estimated at 1-2 FPS, not measured.
- Step 6 did not run FMI-off, per-image comparison, link/hash/publish timings, loader-name check, Hybrid publish/reuse, or cache-entry size measurement. Resolution-lowering FPS testing was suggested but not done.

- U1 launcher observation: 17-21 FPS average (user-reported); Core0/1/2 77.5/82.5/81.3%, HostTiming 98.8%, GPU 19.5%, VulkanWorker 9.9%; idle wait 27/19/22% of core samples; SpinLock about 3-4% per core, down from about 7%, while new exclusive helpers add about 2-3% per core.
- Direct-chain PLA observation: main DLL 1,894,430,208 bytes and SizeOfImage 1,907,552,256; Core0/1/2 77.0/82.1/83.6%, HostTiming 99.7%; run_slice about 2% and lookup about 1.2-1.5% per core. FPS was not reported.
- Direct-plus-indirect-chain PLA observation: main DLL 1,909,940,736 bytes and SizeOfImage 1,923,067,904 (about 1,834 MiB, about 214 MiB below LNK1248); launcher 487,511,552 bytes; Core0/1/2 75.8/80.8/81.0%, HostTiming 99.3%, GPU 19.1%; idle wait 19-21%, run_slice 1-1.4%, lookup 1-1.5%. FPS was not reported.

Interpretation: block-transfer overhead (run_slice plus lookup) is about 3-4% of samples and is not the main cost; about 46-55% is in generated main-DLL code, with recurring hot RVAs `+0x20B0`, `+0x20D2`, `+0x211A`, and `+0x21AE`. The roughly 20% per-core idle wait remains unexplained. These are observations, not acceptance criteria.

## Chain work

Direct chaining (`g_emit_direct_chain`, `SUYU_AOT_DIRECT_CHAIN=1`) tail-calls in-module unconditional B/BL targets through `RECOMP_TAIL_CALL`; conditional branches remain unchained because an earlier measurement was worse (2.00 versus 1.80 ms/frame). Indirect chaining (`g_emit_indirect_chain`, `SUYU_AOT_INDIRECT_CHAIN=1`) independently handles RET/BR/BLR through `recomp_lookup(c->pc-g_module_base)`, only for same-module blocks while budget remains; lookup misses return to `recomp_image_run_slice` without spending budget. Both are off by default, preserve byte-identical output when off, and add manifest and bundle-id inputs.

`recompiler_smoke` variants `abi6dc`, `ggdc`, `abi6ic`, and `ggdcic` pass. The `--bl` fixture covers all four settings at bases 0 and `0x40000`, including middle-block RET/BR, below-base targets, budget exhaustion, and BLR x30 ordering; structural checks and negative controls passed as intended. The AOT cache matrix passed 194 tests with 0 failures; the host build exited 0. Code review approved indirect chaining after two rounds; direct chaining had two rounds with findings fixed. MSVC `cl`, non-Windows compilers, PLA correctness beyond the observations above, and old manifests without chain fields were not tested.

## Milestones 1-3 and 4a

Milestones 1-3 record environment, exporter/build issues, generated-code loading, and gap diagnostics. Milestone 4a's topology, registry, loader, split exporter, cache, and synthetic U1-U4 work is complete for covered cases; real loader/refusal and PLA acceptance remain open. Accepted D1-D7 remain authoritative: one exporter-owned topology defines bindings, names, bundle, ABI, and features; generated code is never edited. See [design note](OLD_HISTORY.md#design-note-accepted-2026-10-06-draft-3).

## Milestones 5-6

Milestone 5 features are up to 4K, ultrawide, correctly timed 60 FPS, ambient occlusion, increased LoD/draw distance, and additional vegetation; none is accepted. Milestone 6 requires a clean-checkout build, release-input audit, GPL/source instructions, defined `devN`, and evidence-aligned README/changelog.

## U1 exclusive-monitor change

U1 adds a side-effect-free plain-page pointer, lock-light reservation/load or compare-and-swap wrappers, conditional write notification, page-aware slow-path fallback, and removes the redundant unlock `mfence`. It includes `tests/exclusive_access`; host builds passed after each revision and the standalone test passed exact u8/u16/u32/u64/u128 counters, reservation semantics, isolation, upper-half compare failure, attempted reporting, and a race-free negative control. Review round 3 approved with no findings. PLA, integration, recompiler smoke/fastmem/FPX, and Core::System notification coverage were not run.

U2 (lock-free reservation publish) needs explicit user approval and tolerates an ABA-class identical-value race; U3 would inline ldxr/stxr through FM1 with an ABI/version/handshake/hash change. Generated-code quality, lookup misses, and FMI comparison remain separate investigations.

## Remaining Step 6 checklist

- [x] Put U1 into a launcher and repeat the same Kotobuki position with FPS, per-thread CPU, RIP, and stack samplers.
- [ ] Compare FMI-off and FMI-on runs and record each image's `SizeOfImage`.
- [ ] Record link-time, hash-time, publish/copy-time, cache-entry size, and loader inventory names.
- [ ] Test Hybrid publish/reuse and confirm fallback transitions separately from strict static results.

## Persistent cache testing rule

The cache is under `%APPDATA%\\suyu\\aot_export_cache`; its producer is `Common::g_scm_rev` plus branch and its launcher is part of the entry. An uncommitted runtime change therefore cannot be observed through a reused launcher. A committed revision intentionally causes a producer mismatch and a full regeneration; do not edit or spoof producer metadata. The proposed relink-only experiment must use a copied package and backup, then the host export variables must be cleared before the normal host rebuild.

## Older open work

The two-application-lifetime seam still needs bind/unregister coverage and the smallest test-only `Core::System` dependencies. `tests/export_regression` still needs its CPM Qt-root lookup corrected. Older held work includes MSVC-route LNK1248 injection, real NSO/NSP/XCI and `ArmRecomp` refusal paths, stub identity-only failure, static-module injection, generic MSVC launcher fallback, extra cache failure cases, and the earlier export/GPL/rtld/BUILD.md/version investigations.

## Evidence boundary

PLA observations are user-run, scene-specific, and not acceptance of general compatibility or a release. Generated-code and ABI behavior are not changed by U1, but the host/runtime path and its launcher must be measured before drawing a performance conclusion.

## Session handoff (2026-10-08, end of fourteenth session)

### State

HEAD is `cba2193` on `main`; the pending dev17 commit contains Markdown only. U1 and both independently opt-in chain paths are implemented and verified; PLA evidence is user-run and specific to Kotobuki Village, and chain-run FPS was not reported.

### What was done

U1 launcher measurements and direct/direct-plus-indirect chain experiments were run at the same Kotobuki spot under strict static AOT + FMI + main-DLL split. The chain experiments increased image size but left run_slice/lookup at only about 3-4% of samples; recurring hot main-DLL RVAs and an unexplained roughly 20% idle wait remain. Synthetic smoke, BL execution, cache-matrix, negative controls, host build, and code reviews passed as recorded above.

### Open issues, in priority order

1. Identify hot RVAs `+0x20B0..+0x21AE` by disassembly and generated-function mapping, and measure generated-code cost (RVA attribution only).
2. Use a stack sampler to determine what the roughly 20% idle core wait is waiting on.
3. Finish Step 6: FMI-off/per-image comparison, link/hash/publish timings, loader inventory names, cache-entry size, and Hybrid publish/reuse.
4. Consider U2 only with explicit user approval and lower priority because U1 is not the bottleneck.
5. Held items: two-lifetime seam, real loader/refusal paths, `tests/export_regression` Qt-root lookup, MSVC `cl` route for the chain macro, old-manifest reuse, and six tracked `.pyc` files.

### Immediate next action

Disassemble the hot main-DLL RVAs and map them to generated functions. Keep the RVA attribution caveat because the recompiled image has no PDB.

### How to work

- Keep builds, exports, logs, samplers, and game-derived outputs outside the repository; never run concurrent rebuilds/ninja; user commits/tags/pushes.
- Use `D:\HisuiWakeBuild\diag\buildsuyu.cmd` through `cmd.exe /c` with an absolute path and clear the three export variables after export-running tests. The standing export environment is `SUYU_AOT_FASTMEM=1`, `SUYU_AOT_FM_INLINE=1`, `SUYU_AOT_SPLIT_MAIN=1`; add the direct/indirect chain variables only for those experiments.
- For PLA sampling, use the listed local samplers; a recompiled image has no PDB, so report DLL samples by RVA. The JIT exclusive lock is inline and cannot be compared with the SpinLock page.
- A standalone test must include a deliberately weakened, data-race-free negative control. Use cache status fields rather than the exporter log tail.

Repository hygiene: six `.pyc` files are tracked and `.gitignore` has no `pycache` entry; the user must decide whether to untrack them. Archived history: see [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-08-from-work_planmd-end-of-fourteenth-session).

## Archived on 2026-10-09 from WORK_PLAN.md (sixteenth-session cleanup)

## Session handoff (2026-10-08, sixteenth session)

### State and completed work

HEAD is `575b796`. Overlay stages D and E are committed; after a fresh full export, the user reported that the Debug window worked 窶徘erfectly窶・on PLA. This is a user observation with no itemized detail or performance numbers; the host build and smoke run pass, including the new overlay logic and build-identity tests.

The default-on environment change is implemented through `src/common/env_option.h`: unset/empty/other text keeps the flag options on, exactly `0` turns them off, and split uses a validated default/0/1 choice. The host rebuild passed with no AOT/overlay variables initially set; clang-cl and MSVC `cl` smoke passed, eight env-option negative controls were caught, exporter-wiring negative controls were caught and restored byte-identically, and the final integration runs passed 24/0 default scenarios and 308/0 AOT-cache scenarios. A real PLA export with the new defaults has not yet been run.

### Open issues, in priority order

1. The user must commit this change, then run a new full export to see the defaults in a PLA launcher and measure its `SizeOfImage` (including LNK1248 risk); ask the user for FPS and the detail of the hardware check if wanted.
2. Map hot main-DLL RVAs and investigate the idle-core wait from the previous handoff; these remain open and unchanged, and the overlay’s guest PCs do not replace them.
3. Implement planned Settings -> Controller / Graphics ImGui windows and keyboard forwarding.
4. A Hybrid default-split export loses module-level JIT fallback until `SUYU_AOT_SPLIT_MAIN=0`; consider an automatic all-static re-plan.
5. Finish the Step 6 checklist above.
6. Consider U2 only with explicit user approval.

### Immediate next action

The user must commit this change before the persistent cache can produce a launcher with the new defaults; then run a new full PLA export and measure its image size.

## How to work / procedures

- Run `.cmd` helpers through PowerShell’s `cmd.exe /c "D:\HisuiWakeBuild\diag\buildsuyu.cmd > D:\HisuiWakeBuild\diag\<log> 2>&1"`; read logs for ASCII `error C` and `BUILD_EXIT`. Run `runsmoke.cmd` once per finished fix set and require both smoke pass markers; a first-pass new test needs a deliberately weakened negative control.
- The overlay smoke tests need `nlohmann/json.hpp`; pass `--nlohmann-include`, set `NLOHMANN_INCLUDE_DIR`, or provide a `CPM_SOURCE_CACHE` containing `nlohmann_json/*/include`.
- The overlay is on by default; set `SUYU_DEBUG_OVERLAY=0` to disable it. Empty and other values keep it on.
- For cache integration, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, use a fresh short work directory, run `--only-aot-cache-matrix`, then rebuild. Keep logs/builds/exports outside the repository.
- For PLA, find the largest-working-set `Pokemon Legends_ Arceus` PID, run `sample_raw_kotobuki4.ps1` and `sample_stacks.ps1`, and confirm flags in `aot_manifest.json`. Defaults are on; set `SUYU_AOT_FM_INLINE=0`, `SUYU_AOT_DIRECT_CHAIN=0`, `SUYU_AOT_INDIRECT_CHAIN=0`, or `SUYU_AOT_SPLIT_MAIN=0` to disable an option. Chain experiments now switch a chain off to compare.
- For Orca workers, write the specification with the Write tool, start the requested model/effort, submit an unobserved turn through its terminal handle, wait once for `worker_done`/`escalation`, verify the diff, release the worker, and acknowledge the delivery. See `CLAUDE.md` for role rules.

Repository hygiene: six `.pyc` files are tracked and `.gitignore` has no `pycache` entry; the user must decide whether to untrack them. The displaced handoff is archived under [the sixteenth-session replacement](OLD_HISTORY.md#archived-on-2026-10-08-from-work_planmd-sixteenth-session-replacement).
## Archived on 2026-10-09 from WORK_PLAN.md (eighteenth-session cleanup)

## Session handoff (2026-10-09, eighteenth session)

### State and completed work

The user commit `709dbd6` updates README only; `2bed3c6` adds the opt-in per-guest-thread ledger. Hot sampler offsets `+0x20B0/+0x20D2/+0x211A/+0x21AE` are real RVAs `0x32C0/0x32E2/0x332A/0x33BE`, all in the guest-to-host lookup helper. Same-build PDB stack attribution identifies 21-33% idle samples as `PhysicalCore::Idle -> Cnd_wait` because no guest thread is runnable, not GPU/fence wait.

The fresh all-default export succeeded without LNK1248: main SizeOfImage 1,923,067,904 and launcher 495,382,528; FPS stayed 15-20. The `FM_INLINE=0` A/B reduced main/launcher images by about 17%/20% but changed FPS by about one FPS with direction unrecorded, so code size alone is not the limiting explanation. The host-RIP profile was flat as recorded above. The ledger measurements and coverage show no single saturated thread; lock contention remains only a hypothesis. Local diagnostic scripts and full result files remain outside the repository under `D:\HisuiWakeBuild\diag\hotrva`.

### Contracts to preserve

- Preserve overlay architecture and honesty: Vulkan present pass, scheduler-worker frame, process-wide lifetime seam, error disable; say `near`, not `inside`; PC 0 means no recorded recompiled dispatch; attempts include lookup misses and unhandled/refused paths.
- Preserve input ownership through release, queue only overlay-owned events, reset on focus loss/minimize, and apply the same rule to keyboard/text when added.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error. Do not claim unsupported default split occurred.
- `SUYU_RECOMP_THREAD_TIME` is an opt-in measurement switch, off by default, and must not change execution. Never fake cache producer identity. Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.

### Open issues, in priority order

0. Cleanup rebuild after the FMI-off export: user closes game and `suyu.exe`; verify `SUYU_AOT_FM_INLINE`, `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` are unset; run `buildsuyu.cmd` through PowerShell.
1. Obtain the Orca Code reviewer review of `2bed3c6`, then run `tests/recompiler_smoke/run.py` including `smoke_thread_time_unit`.
2. Recommended next investigation: generator-side host-size totals per guest opcode class, with a synthetic test, to identify the apparent condition/flag expansion. Alternatives are SVC attribution by thread/address, then one-variable direct/indirect chain exports only after (a) or (b).
3. Read `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused` from the next export dialog, never the log tail.
4. Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 approval remain as above.

### Immediate next action

The new session first reads `CLAUDE.md` and this handoff, checks `git status`/`git log`, asks the user to close the game and `suyu.exe` for item 0, then proposes investigation 2(a) and asks which investigation to run.

# Aevora Work Plan

Handoff prepared: 2026-10-09. HEAD is `585c886` on `main`; start at the seventeenth-session handoff below. Historical text moved from the prior handoff and procedures is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-09-from-work_planmd-sixteenth-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

| Area | Current status |
|---|---|
| Milestone 4a | Topology, registry, loader, split exporter, persistent cache, and covered U1-U4 synthetic work are complete. |
| Step 6 PLA | Static AOT + FMI + main-DLL split runs; prior Kotobuki observation is 17-21 FPS versus 30 FPS JIT; all-default image size and chain FPS are unmeasured. |
| Verification | Host rebuild, clang-cl/MSVC smoke, negative controls, `--only-multimodule` 76/0, `--only-defaults` 24/0, and `--only-aot-cache` 308/0 passed. |

## Evidence so far (observations)

| Run | Core0/Core1/Core2 CPU % | User-reported FPS | Image size |
|---|---:|---:|---:|
| JIT | 76.8/78.7/77.2 | 30 | not measured |
| Static AOT (dev16 launcher) | 76.3/80.4/81.4 | 17-20 | Main DLL 1,857,218,560 bytes |
| + U1 | 77.5/82.5/81.3 | 17-21 | not measured |
| + direct chain | 77.0/82.1/83.6 | unreported | Main DLL 1,894,430,208 bytes |
| + direct and indirect chain | 75.8/80.8/81.0 | unreported | Main DLL 1,909,940,736 bytes; SizeOfImage 1,923,067,904 |

The last image is about 214 MiB below the 2 GiB LNK1248 limit. `run_slice` plus lookup is about 3-4% of samples; generated main-DLL code is about 46-55%, with hot RVAs `+0x20B0`, `+0x20D2`, `+0x211A`, `+0x21AE`. Each core waits idle about 20%; GPU command-queue wait is about 81%. These are observations, not acceptance criteria.

## What the code now contains

- Overlay: forced-bundled ImGui v1.92.9b, Vulkan extension point, present-pass recording under `scheduler.submit_mutex`, and a whole frame on the scheduler worker thread. Vulkan errors disable it; `SuyuCmd::AcquireDebugOverlay` is process-wide and never destroyed because the renderer owns raw-pointer shutdown.
- Input/UI: top-bar Settings and Cheats placeholders plus Debug toggle; bounded SDL-event-thread to UI-thread mouse queue, event-position ownership until release, overlay-only queueing, and Reset on focus loss/minimize. Keyboard forwarding is not implemented; fullscreen draws ImGui's pointer over the UI.
- Debug honesty: policy text only when recompiled code is active; fallback attempts are lookup misses plus unhandled/refused attempts, with none/attempted/refused verdicts. Guest PCs display `near <module>+offset`; PC 0 means no recompiled dispatch recorded. Build identity reads packaged manifest display fields; exporter-only cache fields are not shown. Native status supplies shader-build count.
- Options: `src/common/env_option.h` defines default-on flags (`SUYU_AOT_FM_INLINE`, `SUYU_AOT_DIRECT_CHAIN`, `SUYU_AOT_INDIRECT_CHAIN`, `SUYU_DEBUG_OVERLAY`; exactly `0` disables) and split default/0/1/error semantics (`SUYU_AOT_SPLIT_MAIN`), with unsupported default split quietly skipped.
- Tests: overlay logic, build identity, env-option units, default-option scenarios, multimodule scenarios, and AOT-cache matrix; negative controls and exporter-wiring weakening were caught and restored byte-identically.

## Milestones

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; real loader/refusal paths, PLA acceptance, and Milestones 5-6 remain open.

## Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers.
- [ ] Compare FMI-off/on and record each image's `SizeOfImage`.
- [ ] Record link/hash/publish timings, loader inventory names, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS after each PLA run; chain-run FPS remains unknown.

## Persistent cache rule

The user-data cache producer is git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration; never spoof producer metadata. After export-running tests, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before the normal host rebuild.

## Held work and evidence boundary

Held items: two-lifetime seam; real loader/refusal paths; `tests/export_regression` Qt-root lookup; MSVC `cl` route for the chain macro; old-manifest-without-chain-fields reuse; six tracked `.pyc` files with no `.gitignore` entry. PLA results are user-run and scene-specific; the recompiled image has no PDB, so DLL samples remain RVA attribution only.

## Session handoff (2026-10-09, seventeenth session)

### State and completed work

Commits this session are `97e32dd` (overlay stages A-C), `0ae08ea` (A-C review fixes), `575b796` (stages D-E), and `585c886` (default-on options, review fixes, tests). The overlay is user-reported working 窶徘erfectly窶・on PLA after a fresh export made before `585c886`; no new-default PLA launcher exists yet. Final verification is host rebuild, clang-cl/MSVC synthetic smoke (`SMOKE_EXIT=0`), `--only-multimodule` 76/0, `--only-defaults` 24/0, `--only-aot-cache` 308/0, and the recorded negative controls.

### Contracts to preserve

- Keep the overlay in the Vulkan present pass and scheduler-worker frame path; preserve the process-wide lifetime seam and disable on Vulkan errors.
- Preserve event-thread ownership from the event's own last-drawn rectangles through release; queue overlay-owned events only; Reset on focus loss/minimize. Add keyboard/text with the same rule.
- Treat default-on options as exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error. Do not claim unsupported default split occurred.
- Debug must say `near`, not `inside`; PC 0 means no recorded recompiled dispatch; attempts count lookup misses plus unhandled/refused paths; zero Hybrid attempts is not proof of static execution.

### Open issues, in priority order

1. Run a new FULL export from HEAD, regenerate the committed-revision cache, measure launcher and main-DLL `SizeOfImage` with all defaults on, and use LNK1248 advice (`FM_INLINE=0`, `DIRECT_CHAIN=0`, `INDIRECT_CHAIN=0`) if needed. Ask the user for FPS and hardware-check details. After export tests run `rebuild-suyu.bat`-style cleanup; never build twice on one tree.
2. Map hot main-DLL RVAs `+0x20B0`, `+0x20D2`, `+0x211A`, `+0x21AE` by disassembly and investigate the roughly 20% idle core wait by stack sampling; overlay guest PCs do not replace RVA mapping.
3. Stage F (not started): Settings -> Controller and Graphics ImGui windows, Graphics Resolution Scale, replacing the Win32 F12 controls; Cheats remains a placeholder. Add keyboard/text routing and test horizontal-wheel sign.
4. Consider automatic all-static re-plan because default-split Hybrid loses module-level JIT fallback; do not silently change the default without evidence.
5. Finish Step 6 and held items. MSVC `cl` chain support is synthetic-smoke-verified only; no real-title run.
6. U2 only with explicit user approval.

### Immediate next action

The user starts the new session by checking `git status`/`git log`, then chooses item 1 (recommended) or another item; ask what to do first before proceeding.

## How to work / procedures

- Do not feed Python or Markdown through shell heredocs when quotes/backslashes are present; write the script with the Write tool and run it. Edit/Write tools are fine; LF working copies are harmless when the repo expects CRLF.
- After `cl`/`clang-cl` produces an exe, wait for the file if it appears a few seconds late. After any export-running test, run the cleanup rebuild; never run two builds on one tree.
- `worker-start` can return unobserved `turnStart`: use `worker-show --dispatch`, confirm with `orca terminal read`, then submit with `orca terminal send --terminal <handle> --enter`; never send keys to a terminal you did not start. `check --wait` may replay the previous delivery; use `--ack`, read JSON as UTF-8, and ignore heartbeat lines.
- Long builds/tests over ten minutes move to the background; wait for the completion notification instead of polling. Throwaway negative-control runners and logs live under `D:\HisuiWakeBuild\ovl` and `D:\HisuiWakeBuild\diag`, outside the repo.
- Use `D:\HisuiWakeBuild\diag\buildsuyu.cmd` through `cmd.exe /c`; clear the three export variables after export runs. Keep builds, exports, logs, caches, and game-derived outputs outside the repository.
- For PLA, use the local samplers, confirm `aot_manifest.json`, and report user FPS. Use cache status fields (`aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`), not the lost log tail, as cache evidence.

## Archived on 2026-10-09 from WORK_PLAN.md (twentieth-session cleanup)

## Session handoff (2026-10-09, twentieth session)

### State

Current HEAD was `a67035d` (`v0.0.1-dev18: Optimization of the compilation process`). The shared-FP change and nineteenth-session user observation were recorded above; the next session was to reconcile the checkout and the recorded `c821526` host revision.

### Contracts to preserve

- Preserve FP_SHARED's exact per-module bodies, option/cache identity, and shared generator source of truth.
- Preserve overlay architecture, honest `near`/fallback wording, and process/input ownership contracts.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error.
- `SUYU_RECOMP_THREAD_TIME` is opt-in and must not change execution; never fake cache producer identity.
- Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.

### Open issues, in priority order

0. Obtain exact image sizes, `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`, FPS, hardware, and optional `SUYU_AOT_FP_SHARED=0` A/B from the user.
1. Investigate generator-side per-opcode-class host-size totals with a synthetic test and negative control; then consider SVC attribution by thread/address and one-variable chain exports.
2. Carry over Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 only with explicit approval.

### Immediate next action

Ask for the pending PLA data, then propose the per-opcode-class host-size investigation and let the user choose the next investigation.

## How to proceed

1. Read `CLAUDE.md` and this handoff; check `git status`/ `git log` and verify `GIT_REV` matches HEAD.
2. Ask for exact image sizes, the three cache fields, FPS and hardware, and optionally an `SUYU_AOT_FP_SHARED=0` A/B.
3. Propose per-opcode-class host-size totals with a synthetic test and negative control, then SVC attribution by thread/address, then one-variable chain exports; run one investigation at a time with the user choosing.
4. Use the standard cycle: design, implement, run the required FPX harness/smoke/integration tests, Code reviewer review, fix findings, one full suite run, host rebuild, Markdown batch, then give the user the commit file list and message.
5. After export-running tests, run the cleanup rebuild. After the user commits, reconfigure and verify `GIT_REV` before exporting.

## How to work / procedures

- Run `.cmd` scripts from PowerShell with `cmd.exe /c script *> log`; Bash `cmd.exe` invocation is ineffective here. Do not feed quoted Python/Markdown through shell heredocs.
- Exports are user-started in the GUI; environment experiments use a new empty output folder and clean up with `suyu.exe` closed.
- The game log is written only after normal game exit. Use a non-elevated shell and convert sampler `+0xN` offsets to real RVA by subtracting the module base.
- Never run concurrent builds; keep builds, exports, logs, caches, and game-derived outputs outside the repository. Use cache status fields rather than the lost log tail.
- FPX harness, smoke, and integration commands and verified logs are recorded in the archived nineteenth-session procedures.

## Archived on 2026-10-09 from WORK_PLAN.md (nineteenth-session cleanup)

## Evidence so far (observations)

| Run | Core0/Core1/Core2 CPU % | FPS | Image size |
|---|---:|---:|---:|
| JIT | 76.8/78.7/77.2 | 30 | not measured |
| Static AOT + FMI + split, earlier | 76.3/80.4/81.4 | 17-20 | Main DLL 1,857,218,560 bytes |
| 2bed3c6 standard, all defaults | 71/76/76 busy | 15-20 | Main SizeOfImage 1,923,067,904; launcher 495,382,528 |
| 2bed3c6 `SUYU_AOT_FM_INLINE=0` | not recorded | about 1 FPS change, direction unrecorded | Main 1,588,723,712; launcher 397,508,608 |

The standard main image is about 214 MiB below LNK1248. Host-RIP samples: 46.9% recompiled functions, 49.4% outside the DLL; 4,242 functions sampled, top 1/10/50/200/1000 = 1.90%/7.2%/13.2%/20.4%/31.9%, so the profile is flat. Host bytes per guest instruction: mean 150 B, median 151 B, p90 313 B, p99 383 B; sample-weighted mean 636 B and about 34% of in-DLL samples in functions at least 900 B per guest instruction. Lookup/run-slice dispatch is about 5% of samples. These are observations, not acceptance criteria; function grouping can overstate the size ratio.

Per-thread ledger, steady state from log time 62 s: core 0 busy 71% (thread 77 57%, 88 11%); core 1 busy 76% (80 54%, 89 9%, 100 7%, 93 2%); core 2 busy 76% (81 53%, 90 9%, 100 8-9%). Threads 77/80/81 and 88/89/90 resume at SDK SVC stubs; thread 100 enters main code. Coverage in the roughly three-minute run: 10,589,833 SVCs, including GetCurrentProcessorNumber 5,117,171, ArbitrateUnlock 2,063,210, ArbitrateLock 1,961,915, WaitProcessWideKeyAtomic 343,495, SignalToAddress 322,484, SignalProcessWideKey 316,923, WaitForAddress 307,213, and SendSyncRequest 133,884; static-to-JIT transitions 0. The lock-contention explanation for stalled time and idle cores is a hypothesis, not verified.

## What the code now contains

- Overlay/UI diagnostics, honest `near`/fallback wording, ownership-based mouse routing, default-on option parsing, split topology, cache publish/reuse, and their synthetic/integration tests remain as previously documented.
- `SUYU_AOT_FP_SHARED` shares the exact FPX1 add/sub/div/fma-mul/recps/rsqrts/sqrt bodies once per generated module through `recomp_runtime.c`/`.h`; its option, manifest, bundle-id, cache-reuse, changed-items, and build-identity paths are covered by the recorded tests.
- `src/core/arm/recomp/recomp_thread_time.h` defines `RecompThreadTimeLedger`; `arm_recomp.cpp` enables `ThreadTimeScope` only when `SUYU_RECOMP_THREAD_TIME=1`, one ledger per implementation, and logs a ten-second top-six report. It measures `RunThread` wall time (guest plus in-call host callbacks), excludes idle waits and post-return SVC service, and must not alter execution when unset or any other value.
- `tests/recompiler_smoke/thread_time_unit.cpp` is registered and invoked by the smoke runner. Its standalone test passed; deliberate weakening controls caught sorting, clock clamp, reset, and wait regressions.

## State and completed work

HEAD is `c821526` on `main`; the last code commit is `c821526`. The committed change adds `SUYU_AOT_FP_SHARED`: with FPX1 enabled and the option not exactly `0`, each module emits one exact body for add, sub, div, fma/mul, recps, rsqrts, and sqrt, while the existing FPX1 sites make one-line calls; without FPX1 the option is ignored, and an explicit `1` produces a warning. The inline and out-of-line bodies come from the same generator functions, and disabling the option preserves byte-identical emitted text for the verified units and variants.

The MSVC FPX harness passed: `fpxs` had 0 mismatches versus soft over 76,041,704 L1-L3 cases; `fpxb` had 61,560 mismatches as the weakened negative control; the kept fast-path count check also catches a closed shared path. Smoke passed with goldens unchanged, export integration passed `--only-defaults` 26/0 and `--only-aot-cache-matrix` 213/0, and the host rebuild exited 0. The code reviewer reported three P2 findings, all fixed; shared plus FPX shadow was not exercised. Cleanup after the FMI-off export also exited 0 with the four environment variables unset and no `suyu` process running.
## Archived on 2026-10-10 from WORK_PLAN.md (twenty-third session cleanup)

# Aevora Work Plan

Handoff prepared: 2026-10-10. Current HEAD is `2805512` on `main`; detailed prior sessions are archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-10-from-work_planmd-twenty-second-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `2805512` (`v0.0.1-dev19: FPXV vector FADD/FSUB docs and AArch64 control exemption`) on `main`, on top of `544dad2` (FPXV code) and `a67035d` (dev18). FPXV had no measurable FPS effect in the user's usual PLA scene. User-observed A/B results show about 8.5 FPS with all listed optimizations off versus about 22 FPS with defaults, while one-variable direct/indirect chain-off results were 21/23.5 FPS; the chain-budget sweep was flat at 21-23 FPS. These observations are not agent measurements, and export identity, image sizes, cache fields, hardware, and exact environments were not provided.

## Evidence so far

| Run | Result | Evidence status |
|---|---|---|
| JIT | About 30 FPS in the usual scene | User observation |
| Fresh FPXV-on export (host `544dad2`) | Same FPS as before; no gain | User observation |
| All listed optimizations off | About 8.5 FPS | User observation; split-main setting unanswered |
| `SUYU_AOT_DIRECT_CHAIN=0` / `SUYU_AOT_INDIRECT_CHAIN=0` only | 21 / 23.5 FPS | User observation |
| `SUYU_RECOMP_CHAIN_BUDGET=32,64,128,256,512,1024` | 21, 22, 23, 22, 23, 22 FPS | User observation; value confirmed only by FPS |

The all-off result indicates generated-code speed matters; the earlier hypothesis that it was not the bottleneck is refuted. The flat budget sweep weakens the hypothesis that budget-bounded interrupt latency limits FPS. Chain-off results are within noise of the default and do not establish that chains hurt. The cause of the all-off drop and the remaining roughly 22-versus-30-FPS JIT gap are unresolved hypotheses.

Verified code facts: AOT CNTPCT uses `ArmRecomp::HostReadCntpct` and `CoreTiming().GetClockTicks()`, matching Dynarmic's source; the dispatcher checks `interrupted` only between slices; and `RecompThreadTimeLedger` records `RunThread` time, not waits or SVC service time, with no JIT counterpart.

## Milestones

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; PLA acceptance, remaining real refusal paths, and Milestones 5-6 remain open.

## Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers; obtain standard and FMI-off image sizes.
- [ ] Read cache outcome/publish/reused from the next export dialog; record timings, loader inventory, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS and hardware details after each PLA run.

## Persistent cache rule

The user-data cache producer is git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration; never spoof producer metadata. Before the next export, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, run the cleanup rebuild, reconfigure, and verify `GIT_REV`.

## Held items and evidence boundary

Held: per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 only with explicit approval. Also held are the two-lifetime seam, real loader/refusal paths, `tests/export_regression` Qt-root lookup, old-manifest reuse, and six tracked `.pyc` files. PLA results are user-run and scene-specific; the recompiled image has no PDB, so DLL samples are RVA attribution only.

## Work log

- Session 17: overlay, split-main, and default-on option work completed; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 18: thread-time ledger, FMI A/B, and sampler evidence recorded; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 19: FP_SHARED and review-fix batch completed; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 20: FPXV implementation and dev19 verification completed; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 21: FPXV export handoff recorded; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 22: A/B and chain-budget observations documented; this handoff is current.

## Session handoff (2026-10-10, twenty-second session)

### State

HEAD is `2805512` on `main`, on top of `544dad2` and `a67035d`. The host build tree `D:\HisuiWakeBuild\msvc-release` still has `GIT_REV 544dad26944b3c5913b12be0e47b159d6ea59c42`, so it is not configured for HEAD. User-observed PLA measurements in the same usual scene found no FPXV gain, about 8.5 FPS with all listed optimizations off, 21/23.5 FPS with direct/indirect chain disabled individually, and 21-23 FPS across chain budgets 32-1024; image sizes, cache fields, hardware, and exact export environments were not provided. It is unknown whether post-export cleanup was run.

Diagnostic-overhead change (uncommitted): `RecompShardedCounters` now supplies per-thread cache-line-aligned dispatch counters; `svc_calls`, `jit_to_static`, and the SVC histogram moved from the atomic-plus-mutex/map `RecompCounters` path, with SVC ids `>= 256` reported in one overflow line. Fallback JIT SVCs still count only toward `svc_calls`, `early_dispatches` stops at the 64-entry log limit, and `g_current_pcs` uses one cache line per core. `RecordMiss`/`RecordUnhandled` remain unchanged because they run only on static-to-JIT transitions; comments are cleaned up only when touched and are limited to two lines.

Chain-start experiment (uncommitted): `SUYU_AOT_CHAIN_START=1` adds `_s` entries for direct B/BL targets of at most eight instructions, requires the direct chain, and applies a per-image projection refusal near the 2 GiB limit. After commit and reconfigure, measure one export with the option enabled at the same position/orientation using averaged FPS; record image size from the `SizeOfImage` log and the `Chain start [module]` log line, plus the cache fields. Keep it opt-in and do not enable it by default until measured.

### Contracts to preserve

- Preserve FP_SHARED's exact per-module bodies and FPXV's option, manifest, cache-identity, ABI, and result contracts; do not extend FPXV without sampler/probe evidence.
- Preserve overlay architecture, honest `near`/fallback wording, and process/input ownership contracts.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error.
- `SUYU_RECOMP_THREAD_TIME` is opt-in and must not change execution; never fake cache producer identity.
- Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.
- Measure at the same position/orientation with an averaged FPS value for every A/B; report one number per condition.

### Open issues, in priority order

0. Reconfigure the host for `2805512`: clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, run `D:\HisuiWakeBuild\diag\reconf_build.cmd` via `cmd.exe /c` with a long timeout, then check `GIT_REV`; confirm cleanup state before exporting.
1. Decompose the all-off drop at the same position with `SUYU_AOT_FM_INLINE=0` only and `SUYU_AOT_FP_SHARED=0` only, plus a default re-measurement; collect image sizes and all three cache fields. `SUYU_AOT_FPX_VEC=0` already showed no change.
2. Explain the roughly 22-versus-30-FPS JIT gap with an opt-in, backend-agnostic per-guest-thread breakdown of run/SVC-service/wait time per frame, plus per-reason dispatcher-return counters (chain budget, outside-module indirect target, cross-module call, SVC, guard, conditional branch). Code must be byte-identical when off; require a synthetic test and negative control. Design only until approved.
3. Keep the chain-budget default at 256 unless new evidence; the user's 512 proposal is pending a decision.
4. Carry over per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 only with explicit approval.

### Immediate next action

Commit the change, reconfigure the host for the new hash, then run a same-position/orientation A/B with defaults to see whether FPS changes; carry forward the unchanged `RecordMiss`/`RecordUnhandled` behavior and comment-cleanup-when-touched rule. Then ask the user for same-position FMI/FP_SHARED one-variable exports and, in parallel, design (do not implement without approval) the issue-2 instrumentation.

## How to proceed

1. Read `CLAUDE.md` and this handoff; check `git status`/`git log`, clear export variables, reconfigure, and verify `GIT_REV` before exporting.
2. Ask for same-position/orientation averaged FPS, image sizes, the three cache fields, hardware, and exact export options for each condition.
3. Design the issue-2 instrumentation and synthetic negative control; do not implement it without approval.
4. Use the standard cycle: design, implement, required tests, Code reviewer review, fix findings, one full suite run, host rebuild, Markdown batch, then give the user the commit file list and message.
5. After export-running tests, run the cleanup rebuild. The user makes commits, tags, and pushes.

## How to work / procedures

- Run `.cmd` scripts from PowerShell with `cmd.exe /c script *> log`; Bash `cmd.exe` invocation is ineffective here. Do not feed quoted Python/Markdown through shell heredocs.
- Exports are user-started in the GUI; use a new empty output folder and clean up with `suyu.exe` closed.
- The game log is written only after normal game exit. Use a non-elevated shell and convert sampler `+0xN` offsets to real RVA by subtracting the module base.
- Never run concurrent builds; keep builds, exports, logs, caches, and game-derived outputs outside the repository. Use `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused`, not the lost log tail.
- Before exports or builds, remove the chain-budget variable with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET`; PowerShell `$env:` variables persist for the session.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"`; the value is read once at process start and is confirmed only by FPS. Sweep `32,64,128,256,512,1024`, one condition at a time.
- Run `D:\HisuiWakeBuild\diag\reconf_build.cmd` with `cmd.exe /c`; a hash change recompiles about 1,700 targets, so use a long timeout. Clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` first.
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`.
- FPXV smoke: `python tests/recompiler_smoke/run.py`; ABI5/ABI6 golden hashes must remain unchanged.
- Orca `check --wait --json` output can contain several JSON objects, with heartbeats first; parse each object.


### Chain-start PLA export evidence (twenty-third session, 2026-10-10; moved from the WORK_PLAN handoff State)

Chain-start experiment: the user ran one fresh full PLA export with `SUYU_AOT_CHAIN_START=1` and the default values for other options; it completed normally and was not refused by the size check. The persistent cache entry `01001f5010dfa000_windows_suyu-static_split` has an `aot_manifest.json` written at `2026-10-10 17:42:49` with `chain_start: true`, `direct_chain: true`, `indirect_chain: true`, `fm_inline: true`, `fp_shared: true`, `fpx_vec: true`, image ABI 6, features 7, and bundle id `93f06e0457f26f5c6f19262f876a890e7012e4581adb5d925cee25e624a1d998`. Its main module text size is 53,106,320 bytes with 3,049,140 blocks; `recompiled_image.dll` has `SizeOfImage` 1,789,706,240 bytes (1,706.8 MiB; file 1,694.3 MiB), and the launcher has `SizeOfImage` 504,958,976 bytes (file 474.4 MiB; `launcher_size_bytes` 497,486,848).

User observation: FPS showed no particular change; no FPS number, hardware, cache status fields, or `Chain start [main]` log line was provided. The earlier 2026-10-10 PLA export had a 1,603.1 MiB main DLL with `chain_start` absent and `indirect_chain: false`; because this export also has `indirect_chain: true`, the 103.7 MiB difference is not attributable to chain start alone. The generator estimate was about 134 MiB at the eight-instruction threshold, and the estimated total (53,106,320 x 31 + 134 MiB, about 1,704 MiB) is close to the observed 1,706.8 MiB, but the indirect-chain and chain-start contributions are not separated.

Hypothesis: the unchanged FPS is consistent with the earlier flat 21-23 FPS chain-budget sweep and suggests that the per-block entry PC check is not what limits PLA at its observed frame rate; the 26% synthetic microbenchmark gain (about 1.9 versus 1.4 ns per tiny block) does not carry over. A same-position chain-start-off A/B and sampler attribution of hot blocks were not tested. Decision: keep chain start opt-in (off by default); do not enable it by default without a measured gain. Superseded by the session 30 default-on decision below.

Read-only PLA image aggregates behind the design (main image `recompiled_image.dll`, 1603 MiB, 3,276,463 block functions): about 400,199 unique direct-chain tail-jump target blocks (12.2%); threshold 4: 167,775 blocks / about 60 MiB; threshold 8: 291,542 / about 134 MiB (about +8.3%); threshold 16: 389,972 / about 215 MiB; 91.6% of blocks have at most 8 instructions. Scripts: `D:\HisuiWakeBuild\diag\entry_study\`.


## Archived on 2026-10-11 from WORK_PLAN.md (twenty-fourth session cleanup)

Superseded by the twenty-fourth-session WORK_PLAN; kept verbatim for evidence.

#### Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

#### Status

HEAD is `2b77bc2` (`v0.0.1-dev20`) on `main`. Host `D:\HisuiWakeBuild\msvc-release` is configured and built for `GIT_REV 2b77bc29c2e27b83323a018237c04fd571d32292`; the cleanup rebuild with export variables cleared also exited 0, and no suyu/ninja process was running. The working tree has only the user's unrelated `README.md` edit and asset changes.

#### Evidence

| Run | Result | Evidence status |
|---|---|---|
| JIT | About 30 FPS in the usual scene | User observation |
| Fresh FPXV-on export | No FPS gain | User observation |
| All listed optimizations off | About 8.5 FPS | User observation; split-main setting unanswered |
| Direct/indirect chain off individually | 21 / 23.5 FPS | User observation |
| Chain-budget sweep 32..1024 | 21, 22, 23, 22, 23, 22 FPS | User observation |
| Chain-start-on PLA export | Export OK; main DLL 1,706.8 MiB; FPS unchanged | User observation; indirect chain was also on, so size/FPS effect is not attributable to chain start alone |

The all-off cause and roughly 22-versus-30-FPS JIT gap remain hypotheses. See [CHANGELOG.md](../../CHANGELOG.md) dev19/dev20 and [OLD_HISTORY.md](OLD_HISTORY.md) for evidence details.

#### Milestones

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; PLA acceptance, remaining refusal paths, and Milestones 5-6 remain open.

#### Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers; obtain standard and FMI-off image sizes.
- [ ] Read cache outcome/publish/reused from the next export; record timings, loader inventory, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS and hardware details after each PLA run.

#### Persistent cache rule

The user-data cache producer is git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration; never spoof producer metadata. Before exporting, clear the three export variables, rebuild/reconfigure, and verify `GIT_REV`.

#### Held items

Per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, U2 only with explicit approval, the two-lifetime seam, real loader/refusal paths, the `tests/export_regression` Qt-root lookup, old-manifest reuse, and six tracked `.pyc` files remain held. `RecordMiss`/`RecordUnhandled` remain mutex+map by design.

#### Work log

- Sessions 17-18: overlay/split/default options, thread-time ledger, FMI A/B, and sampler evidence; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 19: FP_SHARED and review-fix batch; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 20: FPXV implementation and dev19 verification; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 21: FPXV export handoff; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 22: A/B and chain-budget observations; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 23: sharded counters, comment cleanup, opt-in chain start, export evidence, and cleanup rebuild; details in [CHANGELOG.md](../../CHANGELOG.md) dev20 and [OLD_HISTORY.md](OLD_HISTORY.md).

#### Session handoff (twenty-third session)

##### State

HEAD/host state is recorded above. Dev20 delivered per-thread sharded dispatch counters, the two-line comment rule and touched-comment cleanup, and opt-in chain-start entries with manifest/cache identity, budget refusal, and tests. The chain-start PLA export succeeded with a 1,706.8 MiB main DLL and no user-observed FPS change; indirect chain was also enabled.

##### Contracts to preserve

- Preserve FP_SHARED and FPXV option, manifest, cache-identity, ABI, and result contracts; do not extend FPXV without sampler/probe evidence.
- Preserve overlay architecture, honest fallback wording, ownership contracts, and exactly-`0` default-on option semantics.
- Keep `SUYU_RECOMP_THREAD_TIME=1` opt-in and execution-neutral; never fake cache producer identity or treat zero Hybrid attempts as static proof.
- Measure same position/orientation with averaged FPS for every A/B and report one number per condition.
- Keep code comments at most two lines; chain start stays opt-in until a measured gain (superseded by the session 30 decision); sharded counters are single-writer per thread.

##### Open issues, in priority order

1. Run same-position averaged-FPS A/B exports: FMI-off only, FP_SHARED-off only, and a default re-measurement; record image sizes and `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`.
2. Design, but do not implement without approval, opt-in backend-agnostic per-guest-thread run/SVC-service/wait breakdown and per-reason dispatcher-return counters; require byte-identical-off behavior, synthetic coverage, and a negative control.
3. Decide the chain-budget default: 256 remains current; the 512 proposal is pending.
4. Optionally run same-position chain-start-off A/B and sampler attribution of hot blocks.
5. Carry the held items listed above; U2 requires explicit approval.

##### Immediate next action

Ask the user which export condition to run next, starting with the same-position FMI-off, FP_SHARED-off, or default averaged-FPS A/B and its image/cache fields.

#### How to proceed

For a fresh session, read `CLAUDE.md` and this file, run `git status`/`git log`, check host `GIT_REV` against HEAD, then ask the user which export condition to run next. The user makes commits, tags, and pushes.

#### How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` after vcvars64; keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke suite: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include` via `cmd.exe /c` after vcvars64.
- Export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix`; use a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd`, or `cmake --build D:\HisuiWakeBuild\msvc-release --target suyu suyu-cmd -j 8`, after vcvars64 and with the three export variables cleared.
- Read-only recompiled-image scripts are in `D:\HisuiWakeBuild\diag\entry_study\`; record aggregate numbers only. Use cache status fields, not the lost log tail.
- Exports are user-started; use a new empty output folder and same-position/orientation sampling. Do not run concurrent builds or exports.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"` (read once at process start, confirmed only by FPS; sweep 32..1024 one condition at a time); remove the variable with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET` before exports or builds.
- The game log is written only after normal game exit; sample from a non-elevated shell and convert `+0xN` offsets to RVA by subtracting the module base (the image has no PDB).
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes in the smoke suite must stay unchanged.
- Orca `check --wait --json` output can hold several JSON objects (heartbeats first); parse each one.

## Archived on 2026-10-12 from WORK_PLAN.md (session 26 cleanup)

### Superseded WORK_PLAN material

The session-24/25 measurement detail, lookup-diagnostics implementation notes, and the implemented XMOD1 design proposal were removed from the day-to-day plan. The verified short-form facts and preserved XMOD1 contract remain in the current [WORK_PLAN.md](WORK_PLAN.md); the preceding session-25 archive contains the full measurement tables and investigation narrative.

#### Archived key observations

- Session-24 Kotobuki measurements: JIT 30 FPS with the limit on, 38 with it off, AOT 23; busy core time was AOT 98.5 ms/frame versus JIT 52.0/54.1, giving about a 17.2 ms/frame AOT-versus-JIT-limit-off gap.
- AOT DLL low-RVA helpers were 7.53% (9.8 ms/frame), including flat-index lookup 4.22% (5.5 ms), dispatch 1.59% (2.1 ms), exact-FP bodies 1.45% (1.9 ms), and other 0.3% (0.4 ms); the earlier 65% runtime-services claim was retracted.
- Lookup diagnostics found 578/1,521 samples after the flat-index load and about 440 out-of-range binary-search misses over 661 segments, estimated at about 1.6 ms/frame.
- Steady 10-second windows from about 52 s showed about 31.5M dispatches, 30.4M covered exits, 0.70M SVC, 0.55M budget exhausted, and no halted/uncovered exits; main<->sdk pairs were about 15.2M each way and tiny sdk leaf targets dominated the top list, with overflow making shares lower bounds.
- XMOD1 user-readings were 23 FPS without/with the existing chain and 24.5 FPS in the first cross-chain run or 24 FPS at budget 256; the 256/1024/2048 sweep showed no change. Readings during a host rebuild were discarded.

#### Archived lookup-diagnostics implementation

The lookup diagnostics were implemented behind exact-`1` `SUYU_RECOMP_LOOKUP_STATS` and `SUYU_RECOMP_LOOKUP_TARGETS` (targets requires stats), with per-thread return counters, module-pair and target reports; target collection adds a second host lookup per exit. The standalone targets were `smoke_lookup_stats_unit` and `smoke_exit_targets_unit`.

#### Archived XMOD1 design proposal (implemented)

Cross-module indirect chaining is feature bit XMOD1 (`0x8`), enabled only by `SUYU_AOT_CROSS_CHAIN=1` and requiring FM1, the indirect chain, and a split export. Manifest/cache/bundle identity records `cross_chain` only when enabled, while off output remains unchanged. A shared immutable table uses ascending load bases, last-base ownership, normal block entries, and no `_s` entries; all images verify version/size/count and lookup, and publication is cleared on refusal. Force-miss and main-hole hooks suppress publication, and registry v1 is the supported loader.

Fixture coverage included `smoke_xmod_table_unit`, the three-module `cross`/`cross-off`/`cross-refuse`/`cross-guard` cases with budgets 1/2/256, and AOT cache matrix cases. Real `ArmRecomp` accounting, `RecordMiss`, `RecompGaps`, IC IVAU through the chain, and the 256-budget stop remained untested at this archive point.


## Archived on 2026-10-11 from WORK_PLAN.md (session 25 cleanup)

### Previous WORK_PLAN contents

# Aevora Work Plan

Handoff prepared: 2026-10-11 (twenty-fourth session). Superseded material through the twenty-third session is in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-11-from-work_planmd-twenty-fourth-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `5bb3381` (`kernel: add opt-in SUYU_GUEST_TIME run/idle/host ledger for JIT/AOT comparison`) on `main`, on top of `2b77bc2` (`v0.0.1-dev20`). Host `D:\HisuiWakeBuild\msvc-release` was reconfigured and built at HEAD (`GIT_REV 5bb33816c483988e66087a96199ae7c194a90740`, exit 0). The PLA export in `D:\HisuiWakeExports\Pokﾃｩmon Legends_ Arceus` and the user-data cache entry were produced at `5bb3381` (split topology: `main` as a DLL of 1,667,841,024 bytes; exported with default options, and the `chain_start` manifest value was not re-read). The Markdown files carry this session's uncommitted documentation batch.

## Key observations (user-reported FPS; one number per condition)

| Condition | FPS |
|---|---|
| JIT (`suyu.exe`, speed limit on) | 30 |
| JIT, speed limit off (Ctrl+U) | 38 |
| Static AOT export at `5bb3381` | 23 |
| All listed optimizations off (earlier) | about 8.5 |
| Direct / indirect chain off individually (earlier) | 21 / 23.5 |
| Chain-budget sweep 32..1024 (earlier) | 21, 22, 23, 22, 23, 22 |
| FPXV on; chain start on (earlier) | no FPS change |

## Current investigation: where JIT and AOT time goes (session 24)

Question: in the same scene (Kotobuki, same position and orientation), how do guest-code execution, host-side work, and waiting differ between JIT and AOT? Conditions A = AOT 23 FPS, B = JIT speed limit on 30 FPS, C = JIT speed limit off 38 FPS; `nvdec_emulation` was set to 1 for all (the AOT package value; the Qt config had 2).

### Method

- Opt-in `SUYU_GUEST_TIME=1` ledger (`src/core/hle/kernel/guest_time_ledger.h`, hooked in `PhysicalCore::RunThread`/`Idle`): per core and 10 s window it splits wall time into run (inside `ArmInterface::RunThread`), idle (inside `PhysicalCore::Idle`), and host (the gap between runs minus idle, charged to the preceding guest thread and SVC number). The three add up to about 100% (`unattributed_pct` 0). Backend-agnostic: identical for JIT and AOT.
- Host RIP sampling (`sample_raw_rip2.ps1`, 3 x 12,000 samples of `CPUCore_0..2`, all module bases recorded) symbolized with `llvm-symbolizer` and the matching PDB (launcher PDB GUID/Age verified equal to the packaged exe), the DLL block tables, and the dumped ARM64 `main` NSO.
- Per-frame cost = category share x 3 cores / FPS (ms of core time per frame). JIT cost per frame is stable under the speed limit (52.0 vs 54.1 ms), so the 30 FPS cap does not hide cost.

### Results

| Per core, steady state | run | idle | host |
|---|---:|---:|---:|
| JIT (ledger) | 51.6% | 42.2% | 5.0% |
| AOT (ledger) | 73.3% | 21.2% | 4.0% |

| ms of core time per frame (3 cores) | AOT (23) | JIT limit on (30) | JIT limit off (38) |
|---|---:|---:|---:|
| Wait | 32.0 | 48.0 | 24.8 |
| Busy (excluding wait) | 98.5 | 52.0 | 54.1 |
| JIT code region (generated code and its built-in handling) | - | 44.6 | 47.3 |
| AOT generated block functions (DLL) | 55.3 | - | - |
| AOT guest code in launcher (static modules) | 6.3 | - | - |
| AOT dispatch (`recomp_lookup_*`, slice, `RunThread`) | 6.3 | - | - |
| AOT DLL runtime helpers (FP and others) | 9.8 | - | - |
| AOT exclusive-access path (stub page + C++ layers) | 12.6 | about 0.1 (callbacks only) | about 0.1 |
| SVC/kernel, scheduler, other host | about 7.0 | about 6.5 | about 6.0 |

- SVC counts per frame are similar (about 1,116 JIT vs 1,185 AOT mutex SVCs per core), so the per-frame workload is comparable.
- The gap is guest-code execution plus its runtime services; SVC/kernel/scheduler/GPU-side host work is the same in both. The AOT-minus-JIT(limit off) busy difference is about 44.4 ms of core time per frame; divided by 3 it is about 14.8 ms, close to the 17.2 ms frame-time difference (43.5 vs 26.3 ms). This is a lead, not proof: the effect of any reduction also depends on inter-thread dependencies.
- How the gap splits between runtime services and code body is **undetermined**. The 28.7 ms AOT total for dispatch, helpers, exclusive path, and stubs is an AOT-side sum; the JIT 44.6 ms region contains its own equivalents (inline dispatch, exclusive handling, FP), so the true difference of each item is smaller. An earlier "about 65% runtime services" statement was retracted.
- Three main guest threads each run about 14 ms per frame (JIT) vs about 24 ms (AOT); the frame time minus main-thread run is about 19 ms in both.
- FPS estimates from any reduction are conditional on the even-split dependency assumption and must be stated as such.

### AOT profile facts (24,000-sample run, DLL tables)

- Profile is flat: top function 1.4% of samples, top 200 functions 19.5%, top 1,000 30.6%; 4,317 distinct sampled block functions. Sample-weighted host size is about 375 bytes per guest instruction word (table median about 120).
- First 24,000-sample run, of all samples: OS wait 25.7%, generated block functions 42.7%, DLL runtime helpers 7.4%, launcher plus dynamic page about 24%.
- Second 36,000-sample run (modules recorded, launcher symbolized with its PDB), of all samples: wait 24.5%, block functions 42.4%, DLL helpers 7.5%, launcher 17.8% (static-module guest code 4.9%, dispatch 4.8%, exclusive/lock 4.6%, SVC/kernel 1.7%, other host 1.0%, scheduler 0.4%, memory helpers 0.3%), SpinLock stub page 5.0%, other 2.6%.
- Block-function samples by ARM64 class (share of all samples): loads/stores 15.3%, branches 10.8%, integer 7.9%, FP 6.0%, NEON other 2.6% (functions without an entry table are spread uniformly; approximate).
- Representative top function (guest `0x2d1e290`, 88 ARM64 instructions to 6,133 x64): a plain 64-bit load expands to about 22 x64 instructions (bounds, page-cross, two-level table, slow-path call, write-back to the context); vector `fadd.4s` is four calls to an exact FP helper (about 57 x64), `fmul.4s` about 127-135 with eight calls; integer `lsl/add/mov` are 1-3. Guest registers live in the context (`[rsi+offset]`), not host registers. Vector FP helpers (`rva 0x2f90`, `0x47f0`) are heavy exact implementations observed running.
- Dispatch (`rva 0x1100`: slice check, `recomp_lookup`, indirect call) is 1.40% of samples; `recomp_lookup_sdk` 1.4% in the launcher.

### Exclusive-access path (investigated; designs only, not implemented)

- The dynamic page (5.04%) is the Dynarmic `SpinLock` stub (one 4 KB Xbyak page, `lock` then `unlock`), confirmed by source and by page-layout match; samples sit right after the `xchg` instructions (cache-line transfer between three cores), almost none in the `pause` loop. The `mfence` in unlock is already removed in source. JIT executes the same lock/unlock instructions inline, so this 6.6 ms is not AOT-specific.
- AOT-only extra: bridge `HostExclusive*` (1.09% of samples), monitor wrappers (1.78%, partly the shared `cmpxchg`), `Memory::GetPlainExclusivePointer` (0.54%), `NoteExclusiveWrite` fence and watch check (0.51%), `CheckAndClear` and others (0.42%), C++ `SpinLock` wrappers (0.37%): about 4.7% or at most about 6.1 ms per frame of core time, so even full removal is about 2 ms of frame time (about +1-2 FPS under the dependency assumption).
- Designs (approval needed): E1 direct exclusive helper per size using the existing inline page-table fast path and calling `ReadAndMark`/`DoExclusiveOperation` without virtual dispatch or out-of-line pointer resolution; E2 compiler barrier instead of `seq_cst` fence in `NoteExclusiveWrite` on x86-64 after a locked CAS. Both need targeted 2-thread tests with a negative control and a generator/ABI version check. First an opt-in exclusive-operation counter (sharded counters) is needed to get operations per frame.

## Contracts to preserve

- Preserve FP_SHARED and FPXV option, manifest, cache-identity, ABI, and result contracts; do not extend FPXV without sampler/probe evidence.
- Preserve overlay architecture, honest fallback wording, ownership contracts, and exactly-`0` default-on option semantics.
- Keep `SUYU_RECOMP_THREAD_TIME=1` and `SUYU_GUEST_TIME=1` opt-in (exactly `"1"`), execution-neutral when off, and single-writer per core/thread; never fake cache producer identity or treat zero Hybrid attempts as static proof.
- Measure same position/orientation with averaged FPS for every A/B and report one number per condition; compare backends in ms per frame, and before calling a cost AOT-specific state what the other backend pays for the same work.
- Keep code comments at most two lines; chain start stays opt-in until a measured gain (superseded by the session 30 decision).

## Open issues, in priority order

1. Code reviewer (Orca) review of commit `5bb3381` and one full `tests/recompiler_smoke/run.py` run: only the standalone `guest_time_unit` (with negative controls) and the host build were run.
2. User decision on the next target. Candidates by measured size: FP exact helpers (about 9.8 ms; measure call frequency and which FPX1/FPXV gate path is taken), dispatch/lookup (about 6.3 ms; `recomp_lookup_*`, slice checks), code-body difference (representative short load -> integer -> store blocks, not only the top function), exclusive path (low payoff, needs counters first). Re-measure with the same three conditions after any change.
3. Optional: attribute JIT's built-in costs (inline dispatch, FP, exclusive) so the AOT-minus-JIT difference per item can be stated; the JIT code region is currently one dynamic block.
4. Carried: same-position averaged-FPS A/B exports (FMI-off only, FP_SHARED-off only, default re-measurement) with `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`; chain-budget default (256 current, 512 proposed); chain-start-off A/B; per-reason dispatcher-return counters design (do not implement without approval).
5. Held items: per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC `cl` real-title support, U2 only with explicit approval, the two-lifetime seam, real loader/refusal paths, the `tests/export_regression` Qt-root lookup, old-manifest reuse, and six tracked `.pyc` files. `RecordMiss`/`RecordUnhandled` remain mutex+map by design.

## Immediate next action

Run `git status`/`git log`, check host `GIT_REV` against HEAD, then ask the user to choose the next target from open issue 2 (and to commit the Markdown batch). Do not start implementation, exports, or builds without the user's go-ahead.

## Milestones and cache rule

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; PLA acceptance, remaining refusal paths, and Milestones 5-6 remain open. The user-data cache producer is git revision plus branch and build id and the launcher is part of the entry; a committed producer change requires regeneration; never spoof producer metadata. A launcher carrying new runtime code needs commit plus full export: relink-only is not feasible because the generated registration sources and ABI headers are deleted after an export and the cache keeps only the launcher, DLL, manifest, and `recomp_modules.cmake`.

## How to proceed

For a fresh session: read `CLAUDE.md` and this file, run `git status`/`git log`, verify `GIT_REV`, then ask the user which target to take next. The user makes commits, tags, and pushes.

## How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` from PowerShell (vcvars fails under Bash); keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke suite: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include` via `cmd.exe /c` after vcvars64. Export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix` with a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd` (reconfigure plus build; needed after a commit to refresh `GIT_REV`), with the three export variables cleared. Never run concurrent builds or exports.
- Exports are user-started; use a new empty output folder and same-position/orientation sampling. The game log is written only after normal game exit; start from a non-elevated shell.
- Guest-time measurement: set `$env:SUYU_GUEST_TIME="1"` in the same non-elevated shell, start the game (JIT: `suyu.exe` with `SUYU_RECOMP_DIR` unset; AOT: the export launcher), stand still about 30 s, record the averaged FPS, exit normally, and read the `guest-time core=N ...` lines (steady state only; skip the loading windows). JIT speed limit off is Ctrl+U in `suyu.exe`.
- Sampling: `D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>` (non-elevated; writes `<file>.modules` with every module base/size/path). Before the next build or export overwrites them, copy the measured launcher/`suyu.exe` and its PDB (verify GUID/Age equality) to a safe folder; `D:\HisuiWakeBuild\tmp_gt\sym_5bb3381` holds the `5bb3381` pair. Symbolize with `llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines`.
- Local analysis scripts (outside the repository, may be recreated) are in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py` (ledger lines), `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py` and `locate_tables.py` (find the `{lo,hi,fn}` block tables in the DLL `.rdata`; the old fixed table RVAs in `diag\hotrva` are build-specific), `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py` (decompress an NSO), `arm_dis.py` (AArch64 via `llvm-objdump` COFF wrapper; there is no `llvm-mc`). Run Python with `-I -X utf8` (isolated mode ignores `PYTHONIOENCODING` and the script directory).
- ARM64 source bytes: enable Settings > Filesystem > Dump ExeFS, start the game, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress the `.text` segment and check the NSO build id against the manifest module `build_id`; turn the setting off afterwards.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokﾃｩmon Legends_ Arceus.exe"` (read once at process start, confirmed only by FPS); remove it with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET` before exports or builds.
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes in the smoke suite must stay unchanged.
- Orca: write long specs to a file and pass `--spec "$(cat <file>)"`; `check --wait --json` output can hold several JSON objects (heartbeats first); parse each one.

## Archived on 2026-10-12 from WORK_PLAN.md (session 27 cleanup)

### Superseded WORK_PLAN material

- Dev19 delivered FPXV; dev20 added sharded counters, the two-line comment rule, and opt-in chain start; dev21 added the guest-time ledger, lookup diagnostics, and the implemented XMOD1 cross-module chain.
- AOT versus JIT (speed-limit-off) gap was about 17.2 ms/frame at session 24 (AOT 98.5 versus JIT 52.0/54.1 busy core-ms/frame); exact-FP bodies about 1.9 ms/frame; flat-index lookup about 5.5 ms/frame; dispatch about 2.1 ms/frame.
- Main<->sdk exits were about 15.2M each way per 10 s and were dominated by tiny `nn::os` leaf calls. XMOD1 measured about +1 to +1.5 FPS (1.7 to 2.7 ms/frame) from single user readings; normal baseline was 23 FPS.
- AOT session-24 profile (36,000 CPUCore samples): recompiled_image.dll 49.9%, ntdll 26.7% (mostly idle waits), launcher exe 17.8%, dynamic code 5.0% (one Dynarmic SpinLock stub page, 3 hot offsets); JIT dynamic code was spread over many pages (maximum about 1%). Exclusive-monitor host functions in the exe were about 4.4%.
- XMOD1 with all diagnostics on, same position: 20 FPS off versus 23 FPS on; dispatches 31.5M -> 17.0M per 10 s; sdk<->main exits about 15.2M -> about 8M each way per 10 s (likely direct calls not chained).
- Exclusive callbacks per 10 s were about 20.5M loads and 20.1M stores, with 0.5% stores failing. Ledger-lock timing was about 4.7 s thread time (load 110 ns, store 122 ns average, including about two TSC reads), about 20 core-ms/frame at 23 FPS. With lock-free marking: load 53 ns, store 98 ns, 3.3 s/window (load 1.16 s + store 2.11 s), about 13.6 core-ms/frame, FPS 23 -> 24 (about 1.8 ms/frame; single integer readings may be noise). JIT cost for the same exclusive work was not measured.
- Lookup early-miss estimate was about 1.6 ms/frame. Chain-budget sweep 256/1024/2048 showed no change; readings during a host build were invalid.
- Old session-26 Status: the user-committed baseline `d1981da` was v0.0.1-dev21 and included XMOD1; repository HEAD was `2740fee` (the docs measurement commit), and host `D:\HisuiWakeBuild\msvc-release` still had embedded `GIT_REV d1981da` at that time.

## Archived on 2026-10-13 from WORK_PLAN.md (session 28 cleanup)

### Verbatim WORK_PLAN.md before session 28 cleanup

# Aevora Work Plan

Handoff prepared: 2026-10-12 (session 27). Earlier detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-12-from-work_planmd-session-27-cleanup).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

The exported launcher log reports rtld 1,560 words (6 KiB), main 13,276,580 words (51,861 KiB), subsdk0 861,276 words (3,364 KiB), subsdk1 1,574,740 words (6,151 KiB), and sdk 1,455,660 words (5,686 KiB), totaling 67,068 KiB. The three same-condition FPS readings were 24.5, 25.0, and 24.0 FPS (mean 24.5 FPS).

The export package records `aaaedd5696f61ed6822e06b6d58343cd95f4800c`; `aot_manifest.json` has `cross_chain`, `direct_chain`, `indirect_chain`, and `fm_inline` true, with `chain_start` false, matching the earlier baseline configuration.

HEAD `aaaedd5` (user commit `v0.0.1-dev24: 32-bit block index, no segment search on index misses, launcher index report`). The host `D:\HisuiWakeBuild\msvc-release` was reconfigured and rebuilt at `GIT_REV aaaedd5696f61ed6822e06b6d58343cd95f4800c` (exit 0). The user re-exported PLA to `D:\HisuiWakeExports\_NEW\Pokﾃｩmon Legends_ Arceus` at this revision; the launcher log reports the new per-module index sizes, and the export was run with the same configuration as the earlier baseline.


## What was done so far

- Dev19: FPXV.
- Dev20: sharded counters, two-line comment rule, and chain start.
- Dev21: guest-time ledger, lookup diagnostics, and XMOD1.
- Dev22 (9eda41f): `SUYU_RECOMP_EXCL_STATS` timing of the four guest exclusive bridge callbacks and `smoke_exclusive_stats_unit`.
- Dev23: a262080 enabled `SUYU_AOT_CROSS_CHAIN` by default (export-integration baseline pins it off; defaults checks both values); feb17ad added opt-in `SUYU_EXCL_LOCKFREE_MARK=1` and locked/lock-free `tests/exclusive_access`; 3b3e244 applied Orca-review fixes (x64 locked slot checks/clears use relaxed atomic_ref; the negative monitor test exits early with FAILED (9), exit 1). Verification: smoke SMOKE_EXIT=0, AOT cache matrix 279 passed/0 failed, `--only-defaults` 29 passed/0 failed (both on a262080), exclusive_access passes in both modes, host builds exit 0.
- AOT-vs-JIT short-block investigation compared production-configuration emitters with Dynarmic x64 JIT dumps; no repository change or production-DLL measurement was made. Block A was about 71 AOT instructions plus about 10 in `recomp_lookup`, versus about 39 JIT fastmem and 48 JIT without fastmem; Block B's taken path was about 54 AOT plus the run-slice return versus about 23 JIT.
- Commit `884af21` changed generated `recomp_lookup` to use the flat index for in-range aligned addresses and call `_lookup_search` only when index allocation failed. The new `smoke_lookup` fixture passed as part of `SMOKE_EXIT=0`; the full integration run had 212 passes and 2 unrelated `cryptography`-package failures. No speed effect has been measured.
- Dev24: Commit `884af21` changed generated `recomp_lookup` to use the flat index for in-range aligned addresses and call `_lookup_search` only when index allocation failed; the 32-bit flat block-index change adds the shared `recomp_image_index.h` contract, generated image tables and fallback reasons, host `RecompIndexView`, registry `index32_v1`, and launcher index diagnostics. It changes generated code and AOT cache identity; no speed effect has been established.

## Key observations

- AOT-vs-JIT (speed limit off) gap at session 24 was about 17.2 ms/frame (AOT 98.5 vs JIT 52.0/54.1 busy core-ms/frame); exact-FP bodies about 1.9 ms/frame, flat-index lookup about 5.5 ms/frame, dispatch about 2.1 ms/frame.
- AOT samples (session 24, 36,000 CPUCore samples): recompiled_image.dll 49.9%, ntdll 26.7% (mostly idle waits), launcher exe 17.8%, dynamic code 5.0% (one Dynarmic SpinLock stub page, 3 hot offsets); JIT dynamic code is spread over many pages (max about 1%). Exclusive-monitor host functions in the exe were about 4.4%.
- XMOD1 with all diagnostics on, same position: 20 FPS off, 23 FPS on; dispatches 31.5M -> 17.0M per 10 s; sdk<->main exits about 15.2M -> about 8M each way per 10 s (likely direct calls not chained).
- Exclusive callbacks per 10 s: about 20.5M loads, 20.1M stores, 0.5% failed stores. Ledger-lock: about 4.7 s thread time (load 110 ns, store 122 ns), about 20 core-ms/frame at 23 FPS. With lock-free mark: load 53 ns, store 98 ns, 3.3 s/window, about 13.6 core-ms/frame, and 23 -> 24 FPS; single integer readings may be noise. JIT cost is not measured.
- Remaining store cost is about 2.1 s/window (about 8.8 core-ms/frame): lock, CheckAndClear, guest-memory CAS under the lock, unlock.
- Lock-free trade-offs: pros are load about 2x faster, store about 20% faster, about 6 core-ms/frame less, untouched JIT inline paths, and saved-value CAS retained; cons are possible ABA, torn 128-bit saved value (CAS then fails), shared-env impact on JIT `ExclusiveRead`, no long PLA soak, plain slot accesses on arm64/loongarch64/riscv64, and FPS gain within noise.
- Short-block comparison: Block A (`ldr/add/str/ret`) was about 71 AOT instructions plus about 10 in non-inline `recomp_lookup` (about 13 branches; 424-byte lookup function), versus about 39 JIT fastmem (about 5 branches, 139-byte block code, shared RSB return excluded) and about 48 without fastmem (255 bytes). Block B's taken path was about 54 AOT plus the run-slice return versus about 23 JIT (186 bytes); its linked conditional branch was `cmp [halt],0; je target`.
- The roughly 41-instruction AOT Block A difference is approximately memory checks 28, block entry 16 (GG1 generation compare 4, halted 2, entry-point 5, frame 5), and RET 8. AOT memory access checks cost about 18-22 instructions each; JIT fastmem costs about 5-6 and omits the page-cross and watch-word checks. AOT CMP stores all four flags in the context (about 15 instructions); the earlier claim that it stored fewer flags was incorrect, while JIT packs them with `lahf/seto`.
- AOT conditional branches return to `recomp_image_run_slice` (lookup, budget decrement, indirect call); only unconditional B/BL chain. The earlier chaining experiment (2.00 versus 1.80 ms/frame, with unexplained JIT-transition and lookup-miss increases) is not a settled refutation of the current emitter.
- `recomp_lookup` now avoids the segment search on flat-index misses: the old path could perform about 10 probes over 661 PLA segments, while the new miss path is about 6 instructions versus roughly 90-190 by instruction count. Earlier session-24 lookup samples and the estimated 1.6 ms/frame were from a different configuration and do not measure this change; generated code changed, so reconfigure and a full export are required before an FPS comparison.
- The `aaaedd5` PLA export's launcher report lists rtld 1,560 words (6 KiB), main 13,276,580 words (51,861 KiB), subsdk0 861,276 words (3,364 KiB), subsdk1 1,574,740 words (6,151 KiB), and sdk 1,455,660 words (5,686 KiB): 67,068 KiB total, about 65.5 MiB. As 64-bit entries the same modules would use about 131 MiB (computed, not measured); no declined-table warning appeared in the checked log lines.
- The first three same-condition FPS readings of the 32-bit-index export were 24.5, 25.0, and 24.0 FPS, mean 24.5 FPS (about 40.8 ms/frame; individual readings about 40.8, 40.0, and 41.7 ms/frame). The baseline export at `3b3e244` measured 24.6 FPS (about 40.7 ms/frame), and the `recomp_lookup` change alone at `8dd61a1` measured 25.2 FPS (about 39.7 ms/frame); earlier same-condition readings varied by about 1-1.5 FPS.
- These readings show no measurable FPS difference: 24.5 FPS mean versus 24.6 and 25.2, all within the observed spread. The memory reduction is real (about 65 MiB less index memory), but the cache/TLB-footprint cost hypothesis is neither supported nor disproved by three readings without counters. The older baseline exports no longer exist because `_NEW` was overwritten; a further A/B requires re-exports of those commits.
- From `aot_manifest.json` (computed, not measured), PLA main text is 53,106,320 bytes (~13.3M instruction words; ~106 MB as 8-byte entries versus ~53 MB as 4-byte entries) and sdk text is 5,822,640 bytes (~11.6 MB versus ~5.8 MB), about 118 MB versus 59 MB together. Session-24's 578 of 1,521 samples immediately after flat-index load motivate the cache/TLB hypothesis, but do not measure this change.
- Full smoke exited 0; full integration after final fixes was 212 passed and 2 failed because Python lacks `cryptography`. The one-off split-failure integration result did not reproduce in two later full runs or the 25-pass split-only run; its cause is unknown. Reviewer should-fix findings and all listed negative controls were addressed.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, FP_SHARED/FPXV ABI/result/cache contracts, exactly-`0` default-on semantics, overlay and fallback wording, comment limit, and same-position averaged FPS with ms/frame comparisons. XMOD1 is on by default since dev23; `SUYU_AOT_CROSS_CHAIN=0` disables it. `SUYU_RECOMP_EXCL_STATS` and `SUYU_EXCL_LOCKFREE_MARK` are exact-`1` opt-ins and execution-neutral when off. Stores keep the saved-value compare-and-swap; every locked access to the monitor's reservation slots is an `atomic_ref`. The 32-bit index has one C/C++ definition in `recomp_image_index.h`; offset 0 means no block and is rejected for a real block, failed builds publish no partial table, and the legacy export answers "no table". Registry v1's optional `index32_v1` layout is accepted only when `entry_size` equals the current struct size.

## Open issues in priority order

1. The three 32-bit-index readings found no measurable FPS effect (24.5 FPS mean); a further A/B requires re-exporting the older commits because their exports were overwritten.
2. Evaluate the nested naturally-aligned page-cross check candidate; the `&&` form did not reduce instructions, and whole-helper inlining was worse on MK8D (image 100 to 222 MB, -14% FPS).
3. Older all-static launcher path and legacy DLL path do not report 32-bit index status; dispatch remains on `lookup` and is correct.
4. Integration environment note: the two portable unsealing checks need the missing Python `cryptography` package; the same failures appeared in earlier logs.
5. Re-experiment with conditional-branch chaining only after explaining the old regression and its increased JIT-transition/lookup-miss counts.
6. Design how to remove per-block GG1 generation, halted, entry-point, frame, and page-table-walk costs; fault handling is needed before removing the page-table walk.
7. Decide whether `SUYU_EXCL_LOCKFREE_MARK` becomes default-on (needs a long PLA soak; if so, `=0` disables per the default-on rule).
8. Test-and-test-and-set for the Dynarmic SpinLock stub (`spin_lock_x64.cpp` `EmitSpinLockLock`); gate it behind an opt-in and keep JIT unchanged unless measured.
9. Shorten the bridge path of exclusive ops (switch in `HostExclusive*`, out-of-line `GetPlainExclusivePointer`, `NoteExclusiveWrite`).
10. Remaining sdk<->main exits (about 8M each way per 10 s).
11. XMOD1 untested items: real ArmRecomp accounting, `RecordMiss`, `RecompGaps`, IC IVAU through the chain, 256-budget stop; optional positive table-publication log line.
12. Held: opcode host-size totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC `cl` real-title support, U2 only with explicit approval, two-lifetime seam, real loader/refusal paths, export_regression Qt-root lookup, old-manifest reuse, six tracked `.pyc` files, mutex+map `RecordMiss`/`RecordUnhandled`; carried FMI-off/FP_SHARED-off/default A/B exports, chain-start-off A/B, optional JIT built-in-cost attribution, and final sdk+0x346040 callee.

## Immediate next action

Ask the user which open issue to take next. Do not build or export without the user's go-ahead.

## How to proceed

Read `CLAUDE.md`, this file, and indexed source locations; run `git status`/`git log`, verify host `GIT_REV` against HEAD, then ask the user which target to take next. Agents never commit, tag, or push; the user makes commits.

## How to work / procedures

- Clean up after every build, test, export, probe, or experiment: put scratch output under `D:\HisuiWakeBuild\scratch\<task>\` and remove it at task end with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1`; check targets before deleting and leave no `D:\fx_*`, `__pycache__`, or `.bak`.
- Folders that must stay: `msvc-release` (active host build), `cpm` (dependency cache), `diag` (analysis tools cited by the docs, plus `diag\hotrva` measurement data), `tmp_gt` (referenced; PDBs and measurement data), `ovl`, `imgui_dl`, `backup`, `logs`, and root `.bat` build helpers.
- Run `.cmd` scripts through `cmd.exe /c` from PowerShell (vcvars fails under Bash); keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include`; export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix`, via `cmd.exe /c` after vcvars64 and with a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd` with the three export variables cleared; never run concurrent builds or exports.
- Do not chain scripts that each call `vcvars64.bat` in one `cmd` session; start each stage as its own process and check that it is running.
- AOT-vs-JIT diagnostic tools are outside the repository in `D:\HisuiWakeBuild\diag\block_diff\`; lookup negative controls and comparison scripts are in `D:\HisuiWakeBuild\diag\lookup_work\`.
- The export integration test uses the host's compiled-in generator; rebuild the host before running integration after generator changes.
- Fixture-only smoke uses `python tests/recompiler_smoke/run.py --only-xmod --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include`; full suite takes about 10 to 15 minutes and may run in background.
- The `--only-xmod` run takes the same arguments as the smoke suite and finishes in a few minutes; run the full suite in the background when appropriate.
- FPXV: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes must stay unchanged.
- Exports are user-started; use a new empty output folder, same-position/orientation sampling, normal exit for logs, and a non-elevated shell. Never run CPU-heavy work during FPS measurement.
- Guest-time: set `$env:SUYU_GUEST_TIME="1"`, `$env:SUYU_RECOMP_LOOKUP_STATS="1"`, `$env:SUYU_RECOMP_LOOKUP_TARGETS="1"`; start JIT (`suyu.exe`, `SUYU_RECOMP_DIR` unset) or AOT launcher, stand still ~30 s, record averaged FPS, exit, read steady-state `guest-time core=N ...`; JIT speed-limit off is Ctrl+U.
- Lookup stats: set the same three variables, run the export launcher, then select `lookup-stats|lookup-targets` from `<export>\user\log\suyu_log.txt`; use windows from ~52 s.
- Chain sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokﾃｩmon Legends_ Arceus.exe"`; remove with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET`.
- Sampling: `D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>` (non-elevated; writes `<file>.modules` with every module base/size/path); copy launcher/`suyu.exe` and PDB (verify GUID/Age) before overwrite; `D:\HisuiWakeBuild\tmp_gt\sym_5bb3381` holds the `5bb3381` pair; symbolize with `llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines`.
- ARM64 bytes: enable Settings > Filesystem > Dump ExeFS, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress `.text`, check manifest `build_id`, then disable.
- Local analysis scripts outside the repo in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py`, `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py`, `locate_tables.py`, `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py`, and `arm_dis.py`; run Python with `-I -X utf8`. Orca long specs use `--spec "$(cat <file>)"`; parse multiple JSON objects from `check --wait --json`.
- After every export test run `D:\HisuiWakeBuild\rebuild-suyu.bat` via `cmd.exe /c`; after commits use `D:\HisuiWakeBuild\diag\reconf_build.cmd`, then verify `GIT_REV`; long builds may need background execution. Write C++ tests with the repository Write tool, not Python heredocs; restore weakened negative controls in finally.
- Helpers outside the repo are in `D:\HisuiWakeBuild\tmp_gt`; analysis scripts and dumped NSO details remain local.
- Lookup-stats run: set `SUYU_GUEST_TIME=1`, `SUYU_RECOMP_LOOKUP_STATS=1`, and `SUYU_RECOMP_LOOKUP_TARGETS=1` in a non-elevated shell, run the export launcher, stand still ~30 s, exit normally, then `Select-String -Path <export>\user\log\suyu_log.txt -Pattern "lookup-stats|lookup-targets"`; use windows from ~52 s.
- Local analysis scripts (outside the repository, may be recreated) are in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py` (ledger lines), `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py` and `locate_tables.py` (find the `{lo,hi,fn}` block tables in the DLL `.rdata`; the old fixed table RVAs in `diag\hotrva` are build-specific), `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py` (decompress an NSO), `arm_dis.py` (AArch64 via `llvm-objdump` COFF wrapper; there is no `llvm-mc`). Run Python with `-I -X utf8` (isolated mode ignores `PYTHONIOENCODING` and the script directory).
- Exclusive measurement: `$env:SUYU_GUEST_TIME="1"; $env:SUYU_RECOMP_EXCL_STATS="1"; $env:SUYU_RECOMP_LOOKUP_STATS="1"` (and `$env:SUYU_EXCL_LOCKFREE_MARK="1"` for lock-free) in a fresh non-elevated shell; stand still ~30 s, record averaged FPS, exit normally, read `excl-stats` from ~52 s. Keep only `suyu_log.txt` and `suyu_log.txt.old.txt`, so copy the log after EACH condition; remove `Env:SUYU_EXCL_LOCKFREE_MARK` before the other condition. LOOKUP_STATS adds a second lookup per exit and costs FPS; FPS-only runs have diagnostics off.
- Exclusive test: `python tests/exclusive_access/run.py --build-dir D:\HisuiWakeBuild\msvc-release --work <folder outside the repo>` after vcvars64 (via `cmd.exe /c`); it links host dynarmic.lib, so rebuild after dynarmic changes. Weakened ReadAndMarkLockFree storing INVALID_EXCLUSIVE_ADDRESS must exit 1; restore in finally.
- Tooling lessons: Python edit scripts putting `\n` escapes into C++ string literals produced real newlines twice (use Edit for string literals); Start-Process needs its log file created first; Orca `check --wait` replays until its `delivery_*` id is acked.

## Design notes

- The flat index replaces two binary searches over roughly 600k main-image entries; it is read-only after load, needs no locking or per-thread state, and costs 4 bytes per instruction word. A thread-local cache cost 34% in the race phase because the title's thread footprint is not free.
- Static `recomp_registration.c` entries use the v4 writer's renamed symbols and each generated definition has a prototype for `-Werror=missing-declarations`.
- Shared-image handles remain live for the process because published function pointers point into them; only a partially failed load frees its own handles.
- Guard v2 requires all modules to answer alike: agreement on 2 enables v2, and any other agreed answer means not negotiated everywhere.
- `Utf8ToWide` calls `MultiByteToWideChar` directly so malformed UTF-8 is an error without extra common sources.
- The compact dispatcher `OwnerEntry` table keeps the hot path's tens of millions of calls per second on contiguous entries instead of one cache line per `RecompImage`.


## Archived on 2026-10-16 from WORK_PLAN.md (session 29 cleanup)
Verbatim copy of the WORK_PLAN.md sections as they stood at HEAD `9ba03c6` before the session 29 rewrite.

### Status

HEAD is aaaedd5, on 8dd61a1 and 884af21; uncommitted changes are Markdown only (CHANGELOG.md, this plan, and this cleanup). Host D:\HisuiWakeBuild\msvc-release was reconfigured and rebuilt at HEAD with GIT_REV aaaedd5696f61ed6822e06b6d58343cd95f4800c. The PLA export at that revision has cross/direct/indirect chain and FM inline enabled, chain start disabled; its 32-bit index totals 67,068 KiB (about 65.5 MiB).

### What was done so far

- Dev19: FPXV.
- Dev20: sharded counters, two-line comment rule, and opt-in chain start.
- Dev21: guest-time ledger, lookup diagnostics, and XMOD1.
- Dev22: SUYU_RECOMP_EXCL_STATS and smoke_exclusive_stats_unit.
- Dev23: cross chain default-on, opt-in lock-free marking, atomic monitor slots, and review fixes.
- Dev24: 32-bit block index, index-miss segment-search removal, launcher index report, tests, negative controls, and full verification.
- Session 28:
  - Compared production clang-cl /O2 AOT blocks with x64 Dynarmic JIT dumps from the same ARM64 sequences.
  - Hand counts found memory checks, block entry, RET, and flag materialization as the main short-block differences.
  - Confirmed the current host was rebuilt at HEAD and recorded its GIT_REV.
  - Produced the PLA export at aaaedd5 with cross/direct/indirect chain and FM inline enabled.
  - Confirmed the 32-bit index report totals 67,068 KiB across the five modules.
  - Compared baseline, lookup-change, and current FPS readings under the same conditions.
  - Found no measurable FPS effect from lookup-miss removal or the 32-bit index; older exports were overwritten.
  - Removed old build/test leftovers and retained the documented scratch cleanup rule and folders.

### Key observations

- At session 24, AOT versus JIT speed-limit-off differed by about 17.2 ms/frame busy-core time (98.5 versus 52.0/54.1); this is a lead, not a proof of per-component causality.
- In 36,000 samples, recompiled_image.dll was 49.9%; low-RVA helpers, lookup, and dispatch were about 7.5%. Generated block bodies are the remaining high-value area; profile shares do not equal AOT-minus-JIT costs.
- Block A (ldr/add/str/ret) was about 71 AOT instructions plus about 10 in lookup versus about 39 JIT fastmem; memory checks account for about 28 of the roughly 41-instruction difference, with entry about 16 and RET about 8. JIT omits page-cross/watch-word checks; AOT flag stores materialize four flags. These are hand counts for one short block, not whole-program measurements.
- Baseline 3b3e244: 24.6 FPS (~40.7 ms/frame); 8dd61a1: one 25.2 FPS reading; aaaedd5: 24.5/25.0/24.0 (mean 24.5, ~40.8 ms/frame). Same-condition readings varied by about 1–1.5 FPS, so lookup and 32-bit index changes show no measurable FPS effect; older exports were overwritten.
- No measured effect: lookup miss-path change and 32-bit index; chain experiment (2.00 versus 1.80 ms/frame) remains unexplained. Earlier helper/inlining and lock-free single readings are not settled performance evidence.

### Open issues in priority order

#### Group A — remaining AOT/JIT gap (about 17 ms/frame busy-core at session 24)

1. Per-access checks: measure the nested naturally-aligned page-cross candidate; page-table walk and GG1 watch-word removal needs fault handling or a design change.
2. Per-block entry costs: GG1 generation, halted, entry-point, and frame checks; SUYU_AOT_CHAIN_START covers entry testing with a 2 GiB budget.
3. Flag materialization: four stores per compare versus JIT's packed word.
4. RET and conditional-branch transfers: re-experiment with chaining only after explaining the earlier regression.

Evidence for all four is instruction-count analysis, not a measured FPS gain; keep changes small and negatively controlled.

#### Group B — carried work and environment limits

5. Decide whether SUYU_EXCL_LOCKFREE_MARK becomes default-on; needs a long PLA soak.
6. Test-and-test-and-set for the Dynarmic SpinLock stub, opt-in and JIT-neutral until measured.
7. Shorten the exclusive bridge path.
8. Remaining sdk<->main exits (about 8M each way per 10 s).
9. XMOD1 untested items: real ArmRecomp accounting, RecordMiss, RecompGaps, IC IVAU through the chain, 256-budget stop, and optional positive publication log.
10. Older all-static/legacy DLL launcher diagnostics, Python cryptography dependency, and held items (opcode totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC cl title support, U2, two-lifetime seam, real loader/refusal paths, export-regression Qt lookup, old-manifest reuse, tracked .pyc files, mutex+map miss records, carried A/B exports, chain-start-off A/B, optional JIT attribution, and final sdk+0x346040 callee).

### Immediate next action

Ask the user to choose an issue; do not build or export without approval.

### How to proceed

Before more code, use D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 on the current AOT export by RVA (only when the user is not measuring) to attribute the gap to block bodies. Judge instruction counts first, then at least three same-position FPS readings per build; require a gain beyond the 1–1.5 FPS noise spread. Keep each change small, add a negative-controlled test, and obtain one Code reviewer round before a full export.

### Design notes

The flat index replaces two binary searches with a read-only 4-byte-per-instruction-word table; its measured memory reduction is real, but its cache/TLB performance effect was not established. Generated registration uses current ABI names and prototypes; shared image handles remain live while published pointers exist; guard negotiation requires agreement across modules.

## Session 29 investigation record (2026-10-14 to 2026-10-16)
Recorded 2026-10-16 from session 29. Values are as measured; interpretations are marked. Raw evidence: D:\HisuiWakeBuild\logs (`access_stats_a304400_suyu_log.txt`, `access_time_f943708.txt`, `access_sites_f943708.txt`, `fmi_share_f943708.txt`, `block_cats_f943708.txt`, `block_regs_f943708.txt`, `rip_raw_f943708.txt`, `rip_raw_jit_f943708.txt` with `.modules`, `launcher_an_aot_f943708.txt`, `launcher_an_jit_f943708.txt`); scripts in D:\HisuiWakeBuild\tmp_gt; launcher/PDB and JIT `suyu.exe`/PDB in D:\HisuiWakeBuild\backup\aaaedd5_aot, f943708_aot, f943708_jit (PDB GUID/Age matched the executables).

### Sampling at aaaedd5 (before the diagnostics)

36,000 samples over CPUCore_0..2 of the AOT launcher: `recompiled_image.dll` 49.8%, ntdll 26.6% (23.1% in `ZwWaitForAlertByThreadId`, 1.3% condition-variable wait), launcher exe 17.3%, dynamic code 5.9%. Launcher categories (percent of all samples): generated `blk_*` 5.23, other 4.50, dispatch/lookup/slice 3.32, SVC/kernel 2.17, memory helpers 1.34, locks 0.49. DLL block bodies were flat: 5,726 distinct hot functions, the top 10 cover 6.9%, the top 1,000 cover 30.7%; low-RVA runtime helpers (0x1160, 0x48d0, 0x2d60) were about 2.7%. ntdll outside the waits was about 1.5% (`ZwProtectVirtualMemory` 0.55, `RtlRunOnceBeginInitialize` 0.50, `ZwAlertThreadByThreadIdEx` 0.44); their callers were not identified.

### Arena (host base + guest address) fastmem design investigation (read-only; not implemented)

Verified facts. JIT: `config.fastmem_pointer = page_table->fastmem_arena` (`arm_dynarmic_64.cpp:248-252`; `Auto` accuracy sets `fastmem_address_space_bits = 64`, so no range test); access is `[r13 + vaddr]` (`emit_x64_memory.h:169-205`); faults are handled by a SEH handler registered with `RtlAddFunctionTable` over the JIT block that looks the faulting RIP up in `fastmem_patch_info`, pushes a fake call into a per-site thunk and returns to the next instruction (`exception_handler_windows.cpp`, `emit_x64_memory.cpp.inc:30-47,102-109`); `recompile_on_fastmem_failure` rebuilds a failed site without fastmem. Host memory: the arena is `DeviceMemory().buffer.VirtualBasePointer()` with offset = guest VA, reserved 2^39 bytes (`device_memory.cpp`); special pages are represented by OS protection (`PAGE_NOACCESS` placeholders for unmapped, `Protect` to no access for debug pages, to none or read for rasterizer pages depending on `use_reactive_flushing`, which defaults to true off Android, and read-only for code); the table-based AOT accessors read `Common::PageTable::PageEntryData` (32 bytes) with a range test, a page-cross test, one table load with the pointer mask, and for GG1 stores the watch word. AOT slow path: `recomp_*_slow` -> `HostLoad`/`HostStore` (48-bit mask, `Core::Memory` accessors, `ReportUnmapped`). Generated code keeps guest registers in `c->x[]` and writes `c->pc` only at block exit.
Design conclusions. A direct-access design needs: a range test kept (tagged and out-of-range addresses must reach the 48-bit-masked slow path); hardware faults standing in for unmapped/debug/rasterizer/page-cross; GG1's contract kept by the invariant that watched code pages are never writable in the arena (to be verified at activation); the ABI feature bit 4 and a `fm_arena` field after `fm_limit` (offset 888) with its handshake; unchanged output when off. Fault recovery in generated C needs an explicit fault site because the compiler owns register allocation: the candidate is a GNU `asm goto` access with an image-relative exception table in a dedicated section and a vectored exception handler that rewrites `Rip` to the recovery label.
Probe (clang-cl 22.1.8, `/O2`, standalone, deleted afterwards): `asm goto` with an output operand, a `.pushsection .rex` table of `.rva` pairs found through the PE section headers, and a vectored handler recovered 4 of 4 faulting accesses (NOACCESS load, NOACCESS store, store to a read-only page, a load straddling into a NOACCESS page) and passed the normal load/store; without the handler it crashed with 0xC0000005. Not verified: partial writes of a page-straddling store, the cost of a Windows exception per fault, vector and pair sites, instruction-cache effects.
Why it was not pursued: the host callbacks measured below are 98.6% rasterizer pages at 80,000-105,000 per second, which would be faults without per-site demotion (the AOT cannot recompile a site like the JIT), and the inline memory-check share bounds the possible gain at about 4-6% of samples. A software alternative (one bit per page, `fastbit(p) = simple(p) && simple(p+1)`, which also removes the page-cross test) was described but not built.

### Host callback diagnostics (a304400, f943708)

`SUYU_RECOMP_ACCESS_STATS=1` swaps `bridge.load`/`store` for counting wrappers that classify each access by reason from the page entry and time the real callback with the TSC; `SUYU_RECOMP_ACCESS_SITES=1` also records three host return addresses per access (`RtlCaptureStackBackTrace`, anchored at the wrapper's return address) in per-thread tables. Unit tests `smoke_access_stats_unit` and `smoke_access_sites_unit` carry negative controls (weakened classifiers; a site key built from the first frame only fails).
a304400 counters (static position, steady state after about 54 s): about 105,000 callbacks/s, loads 4.86e5 and stores 5.6e5 per 10 s; rasterizer 98.6%, page_cross 1.4%, others 0 (watched 0). f943708 timing run: 80,000/s (stores 3.2e5 per 10 s; the difference to the earlier run is unexplained), `load_ms` 43 and `store_ms` about 352 per 10 s, mean 500 ns on rasterizer accesses, loads about 90 ns, stores about 1.1 us. Sites run (timings not representative): 293-294 distinct stacks, about 741,000 counted per 10 s, top 16 = 80%. Symbolized with the PDB: `blk_sdk_53ae94` 67k, `blk_sdk_53ad64` (two sites) 115k plus `recomp_stp32` called from it 55k, `blk_sdk_53ae20` with `recomp_stp64` 39k, `blk_sdk_4660e0` 37k, `blk_sdk_473fe0` 30k per 10 s, and main-module blocks at guest 0x2a1f94c, 0x2a2131c, 0x184454, 0x1e1d944 (33k-49k each). The three adjacent sdk blocks 53ad64/53ae20/53ae94 are about 37% of all callbacks (likely a copy loop; not disassembled).

### Inline memory-check share (f943708, no diagnostics)

Hot functions were disassembled and each instruction weighted by RIP samples. FMI check windows (the `fm_limit` compare through the page-cross test, and the table load through `and -4`/null test, plus the GG1 watch word): 1,297 samples = 3.60% of all (DLL 2.78% over the top 700 functions covering 60% of DLL samples, exe 0.83%), later 3.91% with all categories below; the unresolved table sites (39%, the pointer mask lies in another code block) are counted by their first instruction only, and the table-load latency skids onto the data access, so this is a lower bound. Extrapolating to the unanalysed DLL functions gives about 5.5%; the range compare stays in any arena design, so the removable share is smaller.

### Block overhead categories (generated blocks)

Generated-block samples in the analysed functions: DLL 9,558 plus exe 1,454 = 30.6% of all samples (41.9% of busy). By category (all / busy): GEN guard 0.41 / 0.56, halted 0.14 / 0.20, entry pc 0.48 / 0.65, chain budget 0.10 / 0.14, in-block transfers 0.16 / 0.22, flag stores 0.10 / 0.13, FMI checks 3.91 / 5.36, prologue/epilogue 1.46 / 1.99, GPR file accesses 5.61 / 7.69, vector register file 1.34 / 1.83; classified total 13.71 / 18.78. Block structure seen: generation guard (two `mov [rip+..]`, `cmp`, `jne`), halted test (`[ctx+0x128]`), entry-pc dispatch (`[ctx+0x100]`, module base, compare, jump table), per-access FMI sequences, a budget decrement at `[ctx+0x360]` and an indirect jump after the lookup call at exit.
Guest register file: 1,464 static GPR loads vs 21,525 stores; samples 33 vs 1,990; 1,277 of the store samples (3.55% of all) are stores directly after the guest data load that produced the value (static 1,312 of 21,525), i.e. data-load latency attributed to the consumer, which the JIT also pays; stores overwritten within a straight-line span 0.25% (the analysis stops at jump targets and calls, so redundancy is under-counted). Conclusion: promoting guest registers to block locals cannot remove a large share.

### AOT vs JIT per frame (same position, speed limit off)

FPS AOT 24.5 (40.8 ms), JIT 30 (33.3 ms). Sampling the JIT `suyu.exe` (f943708 build): dynamic code 57.9%, ntdll 35.8% (29.8% wait, 3.1% condition-variable wait, 1.0% alert), exe 5.9% (SVC/kernel 3.21, other 1.59, memory helpers 0.52). The AOT run: DLL 49.5, ntdll 26.7, exe 17.2 (`blk_*` 5.30, other 4.69, dispatch/lookup/slice 3.18, SVC/kernel 2.05, memory helpers 1.23, locks 0.56), dynamic 6.0 (one page). Converted at 3 cores x frame time: AOT 122.4 core-ms/frame, JIT 100.0; busy (not ntdll/kernelbase) 89.4 vs 64.2; generated code 67.1 vs 57.9; other code 21.9 vs 5.9. Host exclusive functions summed by name: 5.57% (AOT) vs 0.15% (JIT). The 6.0% dynamic page: 2,166 samples in a 0x30-byte range at 0x308e1501000, offsets 0x0e (934), 0x24 (659), 0x07 (440) and a few others, on CPUCore_0/1/2 = 852/664/650; with the stub layout (`jmp start` 5 bytes, `pause` at 5, `mov eax,1` at 7, `xchg` at 0xc, `test` at 0xe, unlock after `align()`) these are the instructions right after the lock xchg, the pause and the unlock xchg. Whether the page is the SpinLock stub was inferred, not checked in memory.
Exclusive monitor structure: one `SpinLock lock` in `Dynarmic::ExclusiveMonitor` (`interface/exclusive_monitor.h`), after `exclusive_addresses` (4 slots) and `exclusive_values`, shared by all cores; `EmitSpinLockLock` is `jmp start; loop: pause; start: mov eax,1; xchg [ptr],eax; test; jnz loop` (the `lock` prefix commented out, `xchg` is implicitly locked); the JIT inlines the same emitter (`emit_x64_memory.h:353`); the AOT bridge locks per exclusive access in `DynarmicExclusiveMonitor` after resolving the plain host pointer outside the lock.

### Method notes and pitfalls

- Attribute host callbacks by stack frames; `ctx.pc` is stale in chains. Frame 0 can be inside the generated block itself (helpers inlined) or an out-of-line `recomp_stp32/64`.
- `llvm-symbolizer` through a PowerShell pipe returned `??`; feeding it from a Python subprocess worked. The exported launcher differs from `bin\suyu-cmd-static.exe` by 270,848 bytes (appended data) but has the same PDB GUID/Age.
- Sampler module list lacks the `dll_base=` key that `hot_analysis.py` expects; a one-line `.modules` file with `dll_base=0x.. size=0x..` works. Disassembly of PE functions: wrap bytes in a COFF object (`mkcoff.py` style) and run `llvm-objdump -d -M intel`.
- Visual Studio 18 BuildTools `vcvars64.bat` prints a harmless `vswhere.exe` message under `cmd.exe /c`; a standalone `cl` or `clang-cl` build still works.
- The host rebuild after a CMake source-list change is a full rebuild of about 1,600 steps (about 15 minutes); an incremental build after a source-only change takes about a minute.

## Session 30 record (2026-10-16)

The TTAS plan from WORK_PLAN was implemented: add `bool ttas = false` to `EmitSpinLockLock`; keep the JIT call site and waitpkg behavior unchanged; have the shared host stub read `SUYU_SPINLOCK_TTAS` once and emit xchg acquire, pause/plain-load waiting, and retry; keep unlock unchanged. Verification covered byte-exact legacy emission, distinct TTAS emission with a plain load, exclusion while held, exact u8/u16/u32/u64/u128 increments, environment selection, and negative controls; `run.py` used unset, `0`, `1`, and `2`.

TTAS measurement at d0ae37b used `D:\HisuiWakeExports\_NEW`, fully static (`static -> JIT: 0`), same position, with diagnostics off for FPS:

| Condition | FPS | SpinLock page | Host exclusive functions |
|---|---:|---:|---:|
| TTAS off | 24.3 (about 41.2 ms/frame) | 2085/36,000 (5.79%) | 5.28% |
| TTAS on | 24.3 (about 41.2 ms/frame) | 2115/36,000 (5.88%) | 5.50% |

Sampling error was about 0.12 points per host-exclusive share, so the difference was not significant. Page offsets were off `0x0e` after lock xchg, `0x07` after pause, and `0x24` after unlock xchg; on `0x07` after first xchg, `0x11` and `0x14` in the pause/cmp/jne loop, and `0x24`. The wait moved from xchg to a plain-load spin, but total lock time did not shrink; no speed effect was shown and the earlier 1.5-5 core-ms/frame estimate was not reached. Evidence: `D:\HisuiWakeBuild\logs\samples_d0ae37b_off.bin`, `_on.bin`, `launcher_an_d0ae37b_off.txt`, `launcher_an_d0ae37b_on.txt`, `page_cmp_d0ae37b.txt`, and `D:\HisuiWakeBuild\tmp_gt\page_cmp.py`; launcher/PDB are in `D:\HisuiWakeBuild\backup\d0ae37b_aot` (PDB GUID/Age equality was not cross-checked with a PDB tool).

Decision: TTAS and lock-free marks are default-on despite no TTAS gain and no demerit; chain start is default-on at the user's request because its former opt-in rationale was only "no measured gain." Each uses exact `=0` to disable; chain start still requires the direct chain and retains the 2 GiB refusal. The next export must test defaults-on against TTAS/lock-free-mark `=0` conditions and separately export chain start off.

The user then ran a full PLA export at ac96270 into `D:\HisuiWakeExports\_NEW`, replacing the d0ae37b export. The launcher contains ac96270, `SUYU_SPINLOCK_TTAS`, and `SUYU_EXCL_LOCKFREE_MARK`; the manifest records chain start, cross/direct/indirect chain, FM inline, FP shared, and FPX vector as enabled. The main DLL is 1,805,439,488 bytes (1,721.8 MiB) and the launcher is 506,941,440 bytes (483.5 MiB), both below 2 GiB; the launcher and PDB are saved in `D:\HisuiWakeBuild\backup\ac96270_aot`, with the EXE PDB reference at GUID `108b7bc317890347bd92cb150e8ac4f8`, Age 1037 (PDB-side equality was not cross-checked). The user reported one 26 FPS average (about 38.5 ms/frame) versus 24.3 FPS (about 41.2 ms/frame) at d0ae37b, but TTAS, lock-free marks, and chain start were all enabled, no option-level A/B was run, and the result is near same-position noise, so no attribution is made. Afterward the host was rebuilt with export variables cleared (exit 0, GIT_REV unchanged at ac96270), and scratch was cleaned.

## Archived on 2026-10-16 from WORK_PLAN.md (session 30 cleanup)

Verbatim copy of the WORK_PLAN.md text of commit 64c9900 (HEAD before the session 30 documentation batches), from `What was done so far` to the end, headings demoted by one level. Newer facts (TTAS measurement, default-on options, the ac96270 export) are in the session 30 record above and in CHANGELOG.md; where this text says TTAS or the lock-free marks are opt-in or planned, the session 30 record supersedes it.

### What was done so far

- Dev19-dev24: FPXV, sharded counters, guest-time ledger, lookup diagnostics, exclusive stats, cross chain default-on, opt-in lock-free marking, 32-bit block index; see CHANGELOG.md and OLD_HISTORY.md.
- Session 29 (dev25): sampled the AOT export and ranked where block time goes; investigated an arena (host base + guest address) fastmem design and probed the fault-recovery mechanism; added the opt-in host-callback diagnostics (access stats with callback timing, call-site stacks); measured block-internal overhead shares; compared AOT with JIT per frame; planned an opt-in test-and-test-and-set SpinLock stub. Details, tables and the design record are in OLD_HISTORY.md (session 29 investigation record).
- Not started: any implementation of the arena fastmem, register promotion, or the TTAS stub (the TTAS plan is written below and awaits user approval).

### Key observations

Measured at f943708 at the same structure-heavy PLA position unless noted; RIP samples are 36,000 per run over CPUCore_0..2 and sampling perturbs FPS, so FPS comes from unsampled runs.

- FPS, speed limit off: AOT 24.5 FPS (40.8 ms/frame), JIT 30 FPS (33.3 ms/frame); wall gap 7.5 ms/frame. Earlier AOT readings: 3b3e244 24.6 FPS; aaaedd5 24.5/25.0/24.0; same-position noise about 1-1.5 FPS.
- Core time per frame (3 cores x frame time, samples as shares): AOT busy 89.4 core-ms vs JIT busy 64.2 (gap 25.2, which does not map 1:1 to wall time). Generated code: AOT 67.1 (DLL 49.5% + `blk_*` in the launcher 5.3% of samples) vs JIT 57.9 (all its dynamic code) = +9.2. Host-side code outside generated blocks: AOT 21.9 vs JIT 5.9 = +16.0 (about 63% of the gap).
- Exclusive-access overhead is the largest measured pool: host exclusive functions (`ExclusiveWrite128/32`, `HostExclusiveLoad`, `GetPlainExclusivePointer`, `NoteExclusiveWrite`, `CheckAndClear`, `ExclusiveRead*`, `SpinLock`) are 5.57% of AOT samples (0.15% in JIT), and one 4 KiB dynamic page with 6.0% of samples (about 7.3 core-ms/frame) is inferred to be the Dynarmic x64 SpinLock stub (hot offsets 0x0e, 0x07, 0x24 hold 43%, 20% and 30% of the page: after the lock xchg, after pause, after the unlock xchg; all three cores equally). Together about 11.6% (about 14 core-ms/frame). The JIT's inline exclusive cost sits inside its generated-code time, so 14 is an upper bound for the gap. The lock is one global `SpinLock` in `Dynarmic::ExclusiveMonitor`, stored next to the reservation slots; the JIT's inline code calls the same `EmitSpinLockLock` emitter.
- Dispatch/lookup/slice 3.18% of samples; SVC/kernel 2.05% (JIT 3.21%).
- Inside generated blocks (30.6% of all samples in the analysed functions; heuristic instruction-pattern shares, with sample skid, not removable costs): FMI memory checks 3.91% (lower bound, 5.4% of busy), guest GPR stores 5.5% of which 3.55% are stores right after guest data loads (load latency, also paid by JIT; GPR loads 0.09%, overwritten stores 0.25%), prologue/epilogue 1.46%, entry checks 1.1% (GG1 0.41, entry pc 0.48, halted 0.14, chain budget 0.10), flag stores 0.10%, in-block transfers 0.16%, vector register file 1.34%.
- Host memory callbacks (accesses that leave the inline path): 80,000-105,000 per second, 98.6% on GPU-tracked (rasterizer) pages, 1.3% page-crossing, essentially none unmapped, debug, watched, tagged or out of range. Callback time about 395 ms per 10 s (loads about 90 ns, stores about 1.1 us), about 1.6 core-ms/frame; the store cost is the rasterizer write notification, not the access method. About 294 distinct call stacks; the top 16 are 80%; three `sdk` blocks (`blk_sdk_53ad64`, `53ae20`, `53ae94`) are about 37%.
- `ctx.pc` is updated only at block exit, so it is stale while blocks chain; attribute host callbacks by stack frames, not by `ctx.pc`.
- Conclusions: the arena fastmem would remove at most the inline-check share (about 4-6% of samples) and rasterizer pages would fault about 100,000 times per second without per-site demotion, so it is not being pursued; promoting guest registers to block locals has little to gain; no single remaining Group A item exceeds about 4% of samples. No FPS effect has been shown for the lookup miss path, the 32-bit index or the chain experiment (2.00 vs 1.80 ms/frame, unexplained).

### Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position averaged FPS with ms/frame. Cross chain is default-on (=0 disables); chain start, ledger, lookup/exclusive diagnostics, and lock-free marking remain opt-in. Preserve saved-value CAS, atomic monitor-slot accesses, and the one-definition 32-bit index contract: offset 0 means no block, failed builds publish no partial table, legacy export means no table, and registry index32_v1 requires the current entry size. The opt-in access-stats and access-sites diagnostics (SUYU_RECOMP_ACCESS_STATS=1, SUYU_RECOMP_ACCESS_SITES=1) stay off unless the value is exactly 1 and must remain execution-neutral when off.

### Open issues in priority order

#### Group B - exclusive access (largest measured pool, about 14 core-ms/frame upper bound)

1. SpinLock stub test-and-test-and-set, opt-in `SUYU_SPINLOCK_TTAS=1` (plan in Design notes; awaiting user approval to implement).
2. Cache-line separation of the monitor lock and the per-core reservation slots (`alignas(64)`); evaluate after item 1.
3. Baseline exclusive timing with `SUYU_RECOMP_EXCL_STATS=1` (existing diagnostic; the user postponed this measurement, so it is taken when item 1 is evaluated) and shortening the exclusive bridge path / lock acquisitions per access.
4. Decide whether `SUYU_EXCL_LOCKFREE_MARK` becomes default-on (needs a long PLA soak).
5. Replace the unlock `xchg` by a plain store: needs an ordering analysis (STLXR followed by LDAR); outside item 1.
6. Dispatch/lookup/slice (3.18% of samples) and the remaining sdk<->main exits (about 8M each way per 10 s).

#### Group A - generated-block overhead (each small; instruction-pattern shares only)

7. Memory checks (FMI): 3.9% of samples; small refinements such as omitting the page-cross check for naturally aligned accesses (page-table walk and watch-word removal need fault handling).
8. Prologue/epilogue 1.5%; block-entry checks 1.1% (`SUYU_AOT_CHAIN_START` covers entry testing under a 2 GiB budget); flag stores and in-block transfers about 0.3%. Re-experiment with RET/conditional-branch chaining only after the earlier regression is explained.

#### Group C - GPU-tracked page callbacks

9. Rasterizer write notification costs about 1.1 us per store (about 1.6 core-ms/frame in total); low priority.

#### Group D - carried work and environment limits

10. XMOD1 untested items: real ArmRecomp accounting, RecordMiss, RecompGaps, IC IVAU through the chain, 256-budget stop, and optional positive publication log.
11. Older all-static/legacy DLL launcher diagnostics, Python cryptography dependency, and held items (opcode totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC cl title support, U2, two-lifetime seam, real loader/refusal paths, export-regression Qt lookup, old-manifest reuse, tracked .pyc files, mutex+map miss records, carried A/B exports, chain-start-off A/B, optional JIT attribution, and final sdk+0x346040 callee).

### Immediate next action

Ask the user to approve implementing item 1 (the TTAS plan below) or to choose another issue; do not build or export without approval.

### How to proceed

Implement one behavior at a time (the opt-in TTAS stub), add the unit/negative-control tests, get one Code reviewer round, then ask the user to commit, reconfigure (check GIT_REV) and run one full export. Evaluate with one export and the environment switch: same-position sampler share of the SpinLock page and of the host exclusive functions (baselines 6.0% and 5.57%), `excl-stats` averages (separate run), and averaged FPS with ms/frame; only a gain beyond the 1-1.5 FPS noise counts for FPS. Keep each change small and negatively controlled.

### How to work / procedures

- Clean every build/test/export/probe/experiment: use D:\HisuiWakeBuild\scratch\<task>\, then D:\HisuiWakeBuild\diag\cleanup_scratch.ps1; check targets and leave no D:\fx_*, __pycache__, or .bak.
- Keep msvc-release, cpm, diag, tmp_gt, ovl, imgui_dl, backup, logs, and root .bat helpers.
- Run .cmd/vcvars stages via cmd.exe /c from PowerShell; start each vcvars64 stage as its own process and check it is running within a minute. Never run concurrent builds/exports.
- Smoke: python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include; fixture-only: add --only-xmod.
- Export matrix: python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix, after vcvars and with an empty work folder.
- Host rebuild: D:\HisuiWakeBuild\diag\reconf_build.cmd with export variables cleared; after export tests run D:\HisuiWakeBuild\rebuild-suyu.bat via cmd.exe /c.
- Rebuild the host before export integration after generator changes; the compiled-in generator otherwise remains stale. A warm full run took about 11 minutes and a first core rebuild after configuration changes may take about 30 minutes.
- Exports are user-started; use a new empty output folder, same-position/orientation sampling, normal exit for logs, and a non-elevated shell. Never run CPU-heavy work during FPS measurement.
- Guest-time: set `$env:SUYU_GUEST_TIME="1"`, `$env:SUYU_RECOMP_LOOKUP_STATS="1"`, `$env:SUYU_RECOMP_LOOKUP_TARGETS="1"`; start JIT (`suyu.exe`, `SUYU_RECOMP_DIR` unset) or AOT launcher, stand still ~30 s, record averaged FPS, exit, read steady-state `guest-time core=N ...`; JIT speed-limit off is Ctrl+U.
- Chain sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"`; remove with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET`.
- Lookup stats: set SUYU_GUEST_TIME=1, SUYU_RECOMP_LOOKUP_STATS=1, SUYU_RECOMP_LOOKUP_TARGETS=1, run the launcher, stand still ~30 s, exit normally, then select lookup-stats|lookup-targets from the log using windows from ~52 s.
- Sampling: D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>; copy launcher/suyu.exe and matching PDB before overwrite; symbolize with llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines.
- ARM64 bytes: enable Settings > Filesystem > Dump ExeFS, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress `.text`, check manifest `build_id`, then disable.
- Local scripts are in D:\HisuiWakeBuild\tmp_gt: analyze.py, analyze_launcher.py, compare.py, hot_analysis.py, locate_tables.py, agg.py, per_insn.py, fn_samples.py, nso.py, arm_dis.py; use Python -I -X utf8. Keep FPS diagnostics off during FPS-only runs.
- FPXV: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes must stay unchanged.
- After commits reconfigure/build and verify GIT_REV; after every export copy logs and rebuild as required. Do not weaken negative controls; C++ tests use repository editing tools.
- Exclusive measurement: `$env:SUYU_GUEST_TIME="1"; $env:SUYU_RECOMP_EXCL_STATS="1"; $env:SUYU_RECOMP_LOOKUP_STATS="1"` (and `$env:SUYU_EXCL_LOCKFREE_MARK="1"` for lock-free) in a fresh non-elevated shell; stand still ~30 s, record averaged FPS, exit normally, read `excl-stats` from ~52 s. Keep only `suyu_log.txt` and `suyu_log.txt.old.txt`, so copy the log after EACH condition; remove `Env:SUYU_EXCL_LOCKFREE_MARK` before the other condition. LOOKUP_STATS adds a second lookup per exit and costs FPS; FPS-only runs have diagnostics off.
- Exclusive test: `python tests/exclusive_access/run.py --build-dir D:\HisuiWakeBuild\msvc-release --work <folder outside the repo>` after vcvars64 (via `cmd.exe /c`); it links host dynarmic.lib, so rebuild after dynarmic changes. Weakened ReadAndMarkLockFree storing INVALID_EXCLUSIVE_ADDRESS must exit 1; restore in finally.
- Tooling lessons: Python edit scripts putting `\n` escapes into C++ string literals produced real newlines twice (use Edit for string literals); Start-Process needs its log file created first; Orca `check --wait` replays until its `delivery_*` id is acked.
- Access stats: set `SUYU_RECOMP_ACCESS_STATS=1` (exactly `1`) for per-window `access-stats` lines (loads/stores by reason: tagged, out_of_range, unmapped, debug, rasterizer, page_cross, watched, other; callback `load_ms`/`store_ms` and `avg_ns_<reason>`); add `SUYU_RECOMP_ACCESS_SITES=1` for `access-sites` lines (top 16 host call stacks as `module+0xrva`; a stack walk per access, so its timings are not representative). Stand still about 30 s, exit normally, read from about 52 s; copy the log after each condition. Symbolize launcher RVAs with `python -I -X utf8` and llvm-symbolizer fed through a Python subprocess (not a PowerShell pipe) against the exported launcher and its PDB (check GUID/Age equality first); map DLL RVAs to guest ranges with the `rvamap` helpers in D:\HisuiWakeBuild\diag\hotrva.
- Sampling analysis: after `sample_raw_rip2.ps1`, `tmp_gt\analyze_launcher.py` gives module and exe-symbol shares; `tmp_gt\check_share.py` (FMI check windows), `check_cats.py` (block overhead categories) and `check_regs.py` (guest register file traffic) disassemble the hot functions with llvm-objdump through a COFF wrapper and weight instructions by samples (heuristic patterns, sample skid; quote the limits); `tmp_gt\dump_fn.py` prints one function. Compare AOT and JIT in core-ms/frame (3 cores x frame time x sample share), taking FPS from unsampled runs.
- JIT comparison run: start `suyu.exe` with `SUYU_RECOMP_DIR` unset, speed limit off (Ctrl+U), same position; back up `suyu.exe` and its PDB before sampling.

### Design notes

Planned SpinLock TTAS (opt-in, JIT-neutral): add `bool ttas = false` to `EmitSpinLockLock` in `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp`; the JIT call site (`emit_x64_memory.h`, waitpkg flag unchanged) keeps the default, so JIT code stays byte-identical. The stub (`SpinLockImpl::Initialize`, `waitpkg=false`) reads `SUYU_SPINLOCK_TTAS` once in `GlobalInitialize` (`getenv`, exactly `1`) and emits `start: mov eax,1; xchg [ptr],eax; test eax,eax; jz done; spin: pause; cmp dword [ptr],0; jne spin; jmp start; done: ret`. The unlock is unchanged. Verification: a unit test that the default emission is byte-identical to today's; the `tests/exclusive_access/run.py` legs (locked and lock-free) repeated with TTAS and exact u8..u128 counters; a negative control that must fail by wrong totals without a data race in the test and without an endless spin (for example a check-then-set lock that is not atomic); incremental host build, smoke and FPXV runs, one Code reviewer round. Expected effect: at most the failed-xchg/pause share of the 7.3 core-ms/frame page (about 1.5-5 core-ms/frame), because the cross-core transfer of the lock line is not removed; judge by sampler and excl-stats, not FPS.
Arena fastmem design record (not implemented): direct access `[arena + va]` with hardware faults for special pages would reuse the JIT arena and page protections, but generated C has no recoverable fault sites; the probe in the record shows `asm goto` with an image-relative exception table and a vectored handler works under clang-cl 22.1.8, and the blockers are the rasterizer-page fault rate and the missing per-site demotion. See OLD_HISTORY.md (session 29 investigation record).

## Archived on 2026-10-17 from WORK_PLAN.md (session 33 cleanup)

### Status

The earlier c456b32-plus-removal export is superseded; its backup folder is `D:\HisuiWakeBuild\backup\c456b32_androidrm_aot`. The Code Implementer role was removed; the main agent implements code and the Code reviewer reviews it. Optimization is paused.

### What was done so far

- The overlay replaced the F12 panel through Stage F0-F6, including keyboard/text routing, actions, Graphics, Player 1 Controller, Mods/folders, Game, and fullscreen support; see [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md).
- The command-bar popup fix was confirmed by the user; F6 removed the Win32 F12 panel and branch.
- The host was reconfigured and rebuilt after dev26 with no compile/link errors; only pre-existing C4005 `LOG_*` warnings remained.

### Immediate next action

1. The user takes the same-position averaged FPS and ms/frame reading on the `bbaff08` export (dev27 baseline) and reports the export dialog `aot_cache_outcome` / `aot_cache_publish` / `aot_cache_reused` fields.
2. Then resume the optimization backlog, starting with B3, one item at a time (main agent implements, Code reviewer reviews), measured against that baseline.

### How to proceed

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; the main agent implements code changes itself and has them reviewed by the Code reviewer; keep changes focused; and record only verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run.
## Archived on 2026-10-17 from WORK_PLAN.md (session 31 cleanup)

Session 31 completed the overlay handoff. F1 added keyboard/text forwarding, owned key presses, bounded UTF-8 text, focus reset, capture requests, and SDL text-input synchronization (`22a1045`, `350af77`). F2 added the bounded FIFO action/status channel with event-thread binding, reentrancy guard, detach refusal, loading-time pump, and overflow status (`2d1a42e`, `2b219fa`). F3 added the Graphics window, shared 13-entry Resolution Scale table, save-only next-launch behavior, and Qt/table drift check (`468cd47`, `7ab529e`). F4 added the Player 1 Controller window, device selection, Map, keyboard and Joy-Con actions, binding rows, analog validation, and the non-blocking five-second rebind session (`6498ed1`, `2fdf3f9`).

F5 added the Mods and folders window and Game section (`59d7338`, `1506b19`). The scanner runs on a background `ModsScanner` thread, coalesces requests, waits in its destructor, reads UserDir/LoadDir/KeysDir from the runtime path table, and scans both mod folders and flat `cheat_*` files. It keeps at most 512 entries and eight errors, distinguishes missing from unreadable folders, reports Windows open failures, and uses the export bootstrap's `<exe>/mods` LoadDir.

The fullscreen toggle was added with F11, Alt+Enter (including keypad Enter), and a checked `View > Fullscreen` item (`7138f29`). Review fixes in `7d73d31` save and restore windowed geometry, remember a maximize fallback when real fullscreen is refused, preserve hotkey ownership until release, read Alt from the event modifier, reset on focus loss, and update overlay/status on SDL enter/leave events and failures. The SDL-free chord contract is in `src/suyu_cmd/fullscreen_toggle.h`.

The command-bar popup fix is in `06c8abb`. A combo popup no longer summons a hidden command bar: the popup rule keeps an already-visible bar alive only when a menu was already visible. A headless replay fed the real `OverlayInputRouter`, command-bar rule, and ImGui 1.92.9b sources with press/release batching, jitter, and focus events; before the fix focus moved from `##Combo_00` to `##MainMenuBar`, while after it the list remained open, an item could be selected, and focus returned to the window. New unit cases cover a popup elsewhere and the hover zone.

Verification: every new or changed standalone test passed with a deliberately weakened negative control; controls fail fast. `suyu-cmd` built with exit 0 after each change. The CMake-driven smoke build was not run; tests were built directly with `cl`. The host was reconfigured and rebuilt at `7d73d31`; HEAD is `06c8abb`, so the next reconfigure refreshes the embedded revision. The last PLA export is `ac96270` and contains none of the overlay work. The user's hand test remains in progress, with no result recorded beyond the combo reproduction/fix.

Open hand checks are Resolution Scale save/next-launch behavior; Controller Map, keyboard, rebind, timeout, Clear, Combine/Split; Mods/folders Open/list/Rescan/cheat files; Game; fullscreen modes, geometry, and multi-monitor; click isolation; and F12 parity. Known UX and backend limitations remain: first-use windows overlap at `(16,48)); held input can remain stuck across capture; `controller_assign.h` lacks a standalone test because of `ParamPackage`; the Qt Resolution Scale table is a second copy; and a hand-undone maximize fallback needs one extra toggle. F6 retires F12 only after the user confirms parity.

## Archived on 2026-10-17 from WORK_PLAN.md (session 32 cleanup)

## Status

HEAD is `a9d6c62`. The host was reconfigured and rebuilt successfully with `GIT_REV` `a9d6c62`, matching HEAD. Logs: `D:\HisuiWakeBuild\logs\reconf_s32.log` and `D:\HisuiWakeBuild\logs\build_s32.log`. The last PLA export is unchanged at `ac96270` in `D:\HisuiWakeExports\_NEW` and contains none of the overlay work. Stage F0-F6 is implemented; F6 is uncommitted, reviewed, and awaiting the user's hand check and the dev26 commit. Optimization is paused. The user confirmed the combo/popup fix and approved F6 after the parity question. These Markdown changes remain uncommitted until the user commits.

## What was done so far

- Dev19-dev25 delivered FPXV, diagnostics, index, chain, TTAS, and default-option work; see [CHANGELOG.md](../../CHANGELOG.md).
- Session 30 measured TTAS and recorded the `ac96270` PLA export; see [CHANGELOG.md](../../CHANGELOG.md).
- Stage F0-F5 delivered the overlay survey, keyboard/text forwarding, action queue, Graphics, Player 1 Controller, Mods and folders, background scanning, cheat-file discovery, errors, and Game section; commits are listed below and in [CHANGELOG.md](../../CHANGELOG.md).
- Fullscreen toggle adds F11, Alt+Enter, View menu, geometry restore, maximize fallback, hotkey ownership, and SDL enter/leave handling (`7138f29`, `7d73d31`).
- The command-bar popup bug was fixed in `06c8abb`; headless replay of the real router/rule and ImGui 1.92.9b sources reproduced the old focus theft and verified the fix.
- F6 removed the Win32 F12 debug panel and key branch; F12 now follows ordinary keyboard routing. The overlay documentation is in [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md).
- Session 32: the host was reconfigured and rebuilt at `a9d6c62`; the user confirmed the command-bar popup fix by hand and kept the shared first-use window position by design; code implementation now goes to the Orca Code Implementer by default.

## Immediate next action

1. The user hand-checks F6 (F12 no longer opens a panel, the title has no F12 suffix, and the overlay still works) and commits dev26.
2. After the commit, reconfigure and rebuild so `GIT_REV` matches.
3. Ask whether to resume the paused optimization backlog or export PLA with the overlay. A full export needs the commit, reconfigure, and the standing export procedure.

## Stage F plan

F0 survey: done. F1 keyboard/text forwarding: done (`22a1045`, `350af77`). F2 action queue: done (`2d1a42e`, `2b219fa`). F3 Graphics: done (`468cd47`, `7ab529e`). F4 Controller: done (`6498ed1`, `2fdf3f9`). F5 Mods/folders/status and Game section: done (`59d7338`, `1506b19`). Fullscreen toggle: done (`7138f29`, `7d73d31`). Command-bar popup fix: done (`06c8abb`). F6: done in the uncommitted tree; F12 panel and branch removed, with hand check pending.

### Group E - overlay hand test

Confirm by hand: Resolution Scale pick/save/next-launch effect; Controller Map, keyboard, rebinding, timeout, Clear, Combine/Split; Mods and folders Open x3, list, Rescan, and cheat files; Game section; fullscreen F11/Alt+Enter/menu, Exclusive/Borderless, restore size, and multi-monitor; and no click leakage while menus/combos are open. The combo/popup fix in `06c8abb` is confirmed by the user. All overlay windows open at `(16, 48)` on first use and overlap; the user kept this by design (2026-10-17).

Other open Group E items: a held key/axis can remain stuck when capture starts and release occurs during capture (`src/input_common/main.cpp`, `input_engine.cpp`); `controller_assign.h` has no unit test because it needs `ParamPackage`; and the Qt Resolution Scale table is a translated second copy guarded by `smoke_graphics_settings_unit`. A maximize fallback that the user undoes by hand needs one extra toggle.

## Design notes

`AcquireDebugOverlay` is never destroyed because the renderer retains a raw pointer and owns shutdown. ImGui state belongs to the scheduler worker; capture regions lag one frame. F3 is save-only because the running renderer reads rescaling information without synchronization. F1 Reset uses `ClearInputKeys`/`ClearInputMouse` because Reset+FocusGained merge in one ImGui batch. A popup keeps an already-visible command bar up but must not summon a hidden bar: combo popups are not menus, and the old rule let `BeginMainMenuBar` steal focus. Hotkey press ownership lasts until release; focus loss resets it.

## Archived on 2026-10-17 from WORK_PLAN.md (session 33 cleanup)

### Superseded WORK_PLAN Status paragraphs

HEAD `bbaff08` (dev27). The user ran a full PLA export at `bbaff08`, finished 2026-10-17 23:00 into `D:\HisuiWakeExports\Pokémon Legends_ Arceus`. The launcher embeds `GIT_REV bbaff08` (the exporter reconfigured the tree at 22:30); `export-package.json` records producer `9113fa9` because the `suyu.exe` that ran the export was built before the commit; the source content is identical to `bbaff08`. Manifest: `chain_start`, cross/direct/indirect chain, `fm_inline`, `fp_shared`, `fpx_vec`, `translate_all` true; `fallback_enabled` false; `requires_runtime_codegen` false; `image_abi` 6; `image_features` 15. Main DLL 1,805,439,488 bytes (`size_of_image` 1,818,562,560); launcher 506,726,912 bytes (`size_of_image` 514,203,648); both below 2 GiB. Launcher and PDB are saved in `D:\HisuiWakeBuild\backup\bbaff08_aot`; EXE RSDS and PDB info stream GUID `108b7bc317890347bd92cb150e8ac4f8`, Age 1041 (equal). The export dialog cache fields (`aot_cache_outcome`/`aot_cache_publish`/`aot_cache_reused`) and the user's FPS reading are not reported yet.

The earlier c456b32-plus-removal export is superseded; its backup folder is `D:\HisuiWakeBuild\backup\c456b32_androidrm_aot`. The Code Implementer role was removed; the main agent implements code and the Code reviewer reviews it. Optimization is paused.

## Archived on 2026-10-18 from WORK_PLAN.md (session 34 cleanup)

### Status

Current status: HEAD `4c711ee`; the host was reconfigured and rebuilt at `4c711ee`, and `GIT_REV` `4c711eeed47b4cbba7b092bf6cbf3dc6bb6baa9a` matches HEAD. The first rebuild failed with `LNK1104` on `bin\suyu.exe`; no `suyu` process was running, the cause is unidentified, and it was judged a transient file lock; the immediate re-run exited 0. Logs are `D:\HisuiWakeBuild\logs\reconf_roboto.log`, `build_roboto_commit_fail1.log`, and `reconf_roboto2.log`. The user then ran a full export of the `4c711ee` build and reported that the overlay features (Roboto with merged BIZ UDPGothic font, 125% scale, `#46478B` theme with translucent bar/title, and rounded windows) work as expected and no bugs were found; this is user-reported and was not reproduced by an agent. No export manifest, `aot_cache_outcome`/`aot_cache_publish`/`aot_cache_reused` fields, launcher backup, or FPS numbers were reported.

Optimization is paused.

### What was done so far

- Dev19-dev27 delivered the recompiler, diagnostics, 32-bit index, chain/default-option work, TTAS and lock-free marks, fullscreen toggle, ImGui overlay, and Android/iOS removal; details are in [CHANGELOG.md](../../CHANGELOG.md).
- Dev28 added the embedded BIZ UDPGothic overlay font, centralized style/scale rules, and the overlay style smoke unit; it was committed as `97f1bcf`.
- The overlay replaced the F12 panel through Stage F0-F6; the Roboto/BIZ UDPGothic font change was committed as normal commit `4c711ee` (not a versioned dev entry), and the user-reported full export confirmed the overlay behavior described in Status.

### Immediate next action

1. The user chooses the next step: B3 or another backlog/open item; the dev27 FPS baseline was dropped.

### How to proceed

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; the main agent implements, Code reviewer reviews, Code Explorer surveys read-only, and md Implementer edits Markdown; keep changes focused; and record only verified facts in the next Markdown batch. The next session first asks the user for the FPS baseline on the `bbaff08` export before starting B3. Do not claim a hand-test or export result that was not reported or run.

### Session 34 record

Overlay work in `src/suyu_cmd/overlay/` covered font, scale, and theme. Commit `97f1bcf` (`v0.0.1-dev28`) embedded BIZ UDPGothic Regular (SIL OFL 1.1), set the UI scale to 125% (`kUiScaleFactor` 1.25, single source `overlay_style.h`), applied the `#46478B` theme (menu bar and title bars take the `WindowBg` alpha 0.94; buttons opaque; only closed combo frames through `BeginThemedCombo`), set window rounding to 6 px at 100% (7.5 px applied), and set the main-menu-bar window background alpha to 0 because a code review found `WindowBg` composited under `MenuBarBg` (about 0.996 effective alpha). It also added `CMakeModules/EmbedBinary.cmake`, `smoke_overlay_style_unit` with negative controls, `LICENSES/OFL-1.1.txt`, the `.reuse/dep5` entry, and the README credit.

Commit `4c711ee` (a normal commit; no dev entry or CHANGELOG section by the user's decision) made Roboto (variable font, OFL 1.1, google/fonts commit `1c627bfa375fc51cf86fabeca4f6e08a95f0aa5c`) the primary Latin font and merged BIZ UDPGothic behind it through `ImFontConfig::MergeMode` for Japanese glyphs. Two sources are embedded (`aevora_overlay_font_latin_*`, `aevora_overlay_font_japanese_*`) and the unit test checks both byte-for-byte.

The host was reconfigured and rebuilt at `4c711ee` (`GIT_REV` equals HEAD). The first rebuild failed with `LNK1104` on `bin\suyu.exe` while no `suyu` process was running (transient lock suspected); the re-run exited 0. Logs: `D:\HisuiWakeBuild\logs\reconf_roboto.log`, `build_roboto_commit_fail1.log`, `reconf_roboto2.log`.

The user ran a full export of the `4c711ee` build and reported that the overlay features work as expected with no bugs (user-reported; no manifest, `aot_cache_*` fields, launcher backup, or FPS were reported). The user will not export or measure at `bbaff08`, so the dev27 FPS baseline is dropped. B3 is undecided. The hover reveal strip (`overlay_logic.h`, 20 px, contract in `overlay_logic_unit`) is intentionally not scaled by 125%.

## Archived on 2026-10-18 from WORK_PLAN.md (session 35 cleanup)

### Header links

Handoff prepared: 2026-10-18 (session 35 end). Older material: [session 29 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-29-cleanup), [session 30 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-30-cleanup), [session 31 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-31-cleanup), [session 32 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-32-cleanup), [session 33 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-33-cleanup), [session 34 cleanup](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-34-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

### Status

Current status: HEAD `941a465`; the working tree contains an uncommitted docs batch. Dev29 (`4c711ee..10fad30`) remains committed, and normal commits `cf07cb2` and `941a465` are recorded here, not as CHANGELOG entries. The host was rebuilt at `941a465`; `D:\HisuiWakeBuild\msvc-release\src\common\scm_rev.cpp` reports `GIT_REV` `941a46522b0d430a8fb279f7510a300aed418cb4`, equal to HEAD. After that rebuild and an export, the user reported that the HUD hand-test checks passed: 150% bold readability, F12 toggle and `N/A`-to-number transition, rough CPU/GPU/system-memory agreement with Task Manager, and F12 while an ImGui window was open. No manifest/cache fields, numerical results, FPS, or overhead were reported; the dev27 FPS baseline was dropped, and optimization is paused. See [CHANGELOG.md](../../CHANGELOG.md) for dev29 evidence.

### What was done so far

- Dev19-dev27 delivered the recompiler, diagnostics, 32-bit index, chain/default-option work, TTAS and lock-free marks, fullscreen toggle, ImGui overlay, and Android/iOS removal; details are in [CHANGELOG.md](../../CHANGELOG.md).
- Dev28 and normal commit `4c711ee` delivered the overlay style/font work: Roboto with merged BIZ UDPGothic, 125% scale, `#46478B`, and rounded windows; see [CHANGELOG.md](../../CHANGELOG.md).
- Dev29 delivered the performance HUD, F12 ownership fixes, embedded Roboto font update, and bundle-host Bad Image dialog suppression; see [CHANGELOG.md](../../CHANGELOG.md).
- Normal commits `cf07cb2` and `941a465` changed the performance HUD to 150% embedded Roboto Bold, and the user reported the listed post-export HUD checks as passing; see [CHANGELOG.md](../../CHANGELOG.md).
- The user-reported full export of `4c711ee` confirmed the overlay behavior described in Status.

### Immediate next action

1. The user commits the docs batch.
2. The user chooses the next step among B3, the non-measurement open items, and the open overlay issues; the still-unreported HUD checks remain an optional follow-up.

## Archived on 2026-10-19 from WORK_PLAN.md (session 36 cleanup)

The complete WORK_PLAN.md as it stood at the end of session 36, before compaction:
### Aevora Work Plan

Handoff prepared: 2026-10-18 (session 35 end). All earlier material is archived in [OLD_HISTORY.md](OLD_HISTORY.md) under dated `Archived on <date> from WORK_PLAN.md (session N cleanup)` sections, including the [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup) and the [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

#### Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

#### Status

HEAD `ec8e1a5` (`SUYU_CPU_BUDGET`, normal commit); host rebuilt and `GIT_REV` verified as above; JIT and AOT baselines measured (see Optimization direction). Still untested are `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py`.

#### What was done so far

- Dev19-dev27 delivered the recompiler, diagnostics, index, chain/default-option work, TTAS, overlay, and Android/iOS removal ([CHANGELOG.md](../../CHANGELOG.md)).
- Dev28 delivered the overlay style/font work; normal `4c711ee` merged Roboto with BIZ UDPGothic ([CHANGELOG.md](../../CHANGELOG.md)).
- Dev29 delivered the performance HUD, F12 ownership fixes, embedded font update, and bundle-host Bad Image suppression ([CHANGELOG.md](../../CHANGELOG.md)).
- Normal `cf07cb2` and `941a465` set the HUD to 150% embedded Roboto Bold ([CHANGELOG.md](../../CHANGELOG.md)).

#### Immediate next action

1. Read `CLAUDE.md`, this plan, and `CODEBASE_INDEX.md`; check `git status`/`git log`; confirm no `suyu.exe`, `ninja`, or `cl` process holds the build tree. A running host `suyu.exe` locks `bin\suyu.exe` and causes `LNK1104`; ask the user before ending a process they may be using.
2. Implement B1 as decided below.
3. After the user's commit, rebuild and verify `GIT_REV`; the user runs the JIT A/B, then a full export and the AOT A/B.
4. Then B2.
5. Optional cheap non-measurement items remain available: tracked `__pycache__`/`.bak` cleanup, the `configure_audio` combo-index fix, a `controller_assign.h` unit test, and optionally the `recomp_bundle.cpp` `LoadLibraryExW` error-mode change with a test.

#### Open items

- Pre-existing Qt combo-index bug in `src/suyu/configuration/configure_audio.cpp:72`: combo index is cast to `AudioEngine` although Libretro is index 4 and value 5; value 4 remains the Null-sink fallback.
- `research/tests` has 7 failures and 1 error pre-existing on HEAD. Tracked repository artifacts that should not be in git are six `__pycache__` `.pyc` files (`tests/exclusive_access`, `tests/export_build_kit`, `tests/export_regression`, `tests/recompiler_smoke`, `tools/export_build_kit`) and `src/suyu/game_card.cpp.bak`; `tools/cpp-lint.sh:37-38` has tab indentation flagged by `git diff --check` (file style).
- The cause of the Bad Image dialog on this Windows build is undetermined.
- Product loader `recomp_bundle.cpp:305` `LoadLibraryExW` still has no error-mode handling; an optional runtime change was offered to the user and was not made.
- The HUD checks listed as not reported in [CHANGELOG.md](../../CHANGELOG.md) remain untested: `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py`.

#### Optimization direction (restarted)

On 2026-10-19, the user changed the goal to reducing per-core CPU usage for low-spec machines. The previous backlog (B2-D11) was removed, and optimization restarts from scratch. The target is the user's i5-12400F (6 cores/12 threads, all P-cores), RTX 2060 SUPER, and 16 GB PC at a 30 FPS cap, using `frame_pacing_mode=0`, `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, and `use_multi_core=true`; the user never changed pacing. Primary metrics are whole-process CPU ms per game frame and busiest-thread CPU ms per frame (`core_pct`), with p99 game-frame interval as secondary; compare AOT and JIT at the same position and optionally use affinity-limited runs to emulate fewer cores. Before this change there were no per-frame process/thread CPU or p99 metrics; the HUD is a 1 s `GetProcessTimes` percentage, guest ledgers cover guest-core wall time, and frame history has only mean/max. Surveyed spin candidates include the 30/60/90/120-only `vk_scheduler.h` frame-pacing yield (not taken in Auto), Dynarmic `SpinLock`, and semaphore spins in `common/atomic_helpers.h`. The new opt-in `SUYU_CPU_BUDGET=1` diagnostic (default off; one relaxed atomic load per game frame when off) samples Windows process/thread CPU, reports top 12 threads and frame percentiles every 1-60 s (default 10), and is available to both Qt JIT and AOT; use windows of 10 s or more because `GetThreadTimes` has roughly 15.6 ms clock-tick granularity. Thread baselines match id plus creation time; missed baselines, exited threads, and dropped intervals are reported or excluded as documented by the diagnostic. The guest-requested `suyu-cmd` `exit(0)` callback does not stop/join the sampler; normal window close does. Verified standalone unit build and 20 runs, host rebuild of `suyu`/`suyu-cmd`, and 33-check smoke run all passed; no PLA measurement has run yet. Keep the backlog ranked by CPU ms per frame saved, with an `=0` switch on each item, and retain the old cheap non-measurement items as a separate optional item. The removed backlog remains in git history (commit `d0e8d1d`).

Baseline (ec8e1a5): with `SUYU_CPU_BUDGET=1`, 10 s windows, the same standing-still position, and user-run logs, JIT (13 steady windows with at least 300 frames) measured 30.1 FPS, frame p50 33.33 ms, p99 33.74 ms, max 34.3 ms, and 113.5 process CPU ms/frame (3416 ms/s, about 3.4 cores); HostTiming was 32.9 ms/frame (98.6% of a core), CPUCore_0/1/2 were 20.5/20.7/20.3 ms/frame (about 61% each, 61.6 total), GPU 9.9, VulkanWorker 4.4, and untracked 0.02. AOT (12 steady windows with at least 245 frames) measured 25.3 FPS, frame p50 39.1 ms, p99 50.8 ms, and 152.1 process CPU ms/frame (3851 ms/s); HostTiming was 39.3 ms/frame (99.1%), CPUCore_0/1/2 were 30.4/32.1/31.6 ms/frame (about 79% each, 94.1 total), GPU 8.8, VulkanWorker 4.7, and untracked 0.01. AOT did not reach the 30 FPS cap, so its per-frame values include slower frames; compare again once AOT reaches 30 FPS.

| Item | Scope | Measured basis |
| --- | --- | --- |
| B1 | Block HostTiming in the OS for long waits; umwait only the last margin. | HostTiming 32.9 (JIT) / 39.3 (AOT) ms/frame at about 99% of a core. |
| B2 | Break down the AOT guest-core gap with `SUYU_GUEST_TIME` and RIP sampling at the same position, AOT vs JIT; no code change. | Guest cores 94.1 (AOT) vs 61.6 (JIT) ms/frame, +32.5. |

#### B1 plan (decided 2026-10-19)

Behaviour: while the remaining wait exceeds a margin, sleep in the OS with the existing `Event::WaitUntil` (condition variable), waking margin early; wait the remainder with the existing `Event::WaitFor`; `Set()` still wakes either wait at once.

Files: new `src/core/host_timing_wait.h` (OS-free: wait split from `wait_ns` and `timer_resolution_ns`, margin = 2 x timer resolution (1 ms at 0.5 ms); single-writer stats: wait-length histogram, wake lateness p50/p99/max, OS-block count); `src/core/core_timing.{h,cpp}` (wait loop, stats written only by the HostTiming thread); `src/core/CMakeLists.txt`; new `tests/recompiler_smoke/host_timing_wait_unit.cpp` with negative controls (no margin subtracted; OS block for short waits) registered in `tests/recompiler_smoke/CMakeLists.txt` and `run.py`.

Switches (user decisions): `SUYU_HOST_TIMING_BLOCK` default-on from the start (`=0` restores umwait-only waiting); `SUYU_HOST_TIMING_STATS=1` opt-in, a `host-timing` line every 10 s, no work when off; stats ship together with B1.

Verification: unit tests with `/W4 /WX`; `run.py`; host build (CMake list change, expect a full rebuild of about 25 minutes); Code reviewer and fixes; user JIT A/B at the same position (`SUYU_HOST_TIMING_BLOCK=0` vs default, both with `SUYU_CPU_BUDGET=1` and `SUYU_HOST_TIMING_STATS=1`) including an audio/feel check; commit, full export, AOT A/B.

Acceptance: HostTiming `core_pct` falls substantially (target 20% or less from about 99%); FPS not lower and frame p99 not worse beyond noise; wake-lateness p99 rises by no more than the timer resolution (0.5 ms); no reported audio or behaviour regression.

Risks: late timer events may disturb audio, VSync, or guest waits (watch AOT, already below 30 FPS); the 1 ms margin is still waited with umwait, so short event intervals limit the saving; if stats show waits are mostly under the margin, switch to a high-resolution waitable timer design.

#### Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Still open: a held key/axis can remain stuck across capture; `controller_assign.h` is untested; the Qt Resolution Scale table has a second translated copy; a hand-undone maximize fallback needs one extra toggle; the overlay is unavailable on OpenGL/Null, which the user plans to remove; and PDH GPU accuracy versus Task Manager is unverified.

#### Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position CPU ms per game frame at the 30 FPS cap (`SUYU_CPU_BUDGET`), with FPS from unsampled runs. Cross chain, chain start, TTAS, lock-free marks, FM inline, direct/indirect chain, split main, and FPX are default-on (`=0` disables). Ledgers, lookup/exclusive/access diagnostics, and `SUYU_CPU_BUDGET` remain opt-in. Preserve saved-value CAS, atomic monitor slots, and the 32-bit index contract.

Overlay contracts are documented in [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md): the action queue is the only way the UI changes settings; settings apply on the event thread; Resolution Scale is save-only until next launch; the UI reads snapshots only; every capture/rebind begin has exactly one stop; and controller UI is Player 1 only.

#### Session 35 lessons

- `tests/recompiler_smoke/CMakeLists.txt` and `run.py` have mixed LF/CRLF line endings; scripted replaces must read with `newline=''` and check exact bytes (`repr`) or use the Edit tool. Git's "LF will be replaced by CRLF" warnings are harmless.
- `orca orchestration worker-start --worktree current` rejects `--name`; the first `check --wait` often returns a status message or replays the previous worker's result. Acknowledge the previous delivery id and wait again for `worker_done`.
- A Windows "Bad Image" modal dialog (loading a garbage DLL) blocks a non-interactive test run; tests that load bad DLLs call `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)`.
- `ImGui::GetFontSize()` already includes the 1.25 UI scale, so HUD text size is `GetFontSize() x kPerfHudTextScale` (1.5); stb_truetype ignores variable-font axes, so bold needs a static Bold TTF embedded as a separate (non-merged) font via `OverlayBoldFont()`.
- A build with `rebuild-suyu.bat` reconfigures and relinks both `suyu.exe` and `suyu-cmd.exe`; if `suyu.exe` is running, only `cmake --build ... --target suyu-cmd` can link.
- An unmodified third-party font must be recorded in `.reuse/dep5` with its origin and SHA-256.

#### How to proceed

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; the main agent implements, Code reviewer reviews, Code Explorer surveys read-only, and md Implementer edits Markdown; keep changes focused; and record only verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run. If a Markdown batch shortens a section, compare git diff against the intended change; one batch dropped unrelated lines.

#### How to work / procedures

- Build standalone tests after BuildTools vcvars64 through `cmd.exe /c` with `cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` when required.
- Every new standalone test needs a shared contract check with a deliberately weakened negative control that fails fast and cannot hang.
- For overlay input bugs, headlessly replay the real router/command-bar header with ImGui sources from `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` before changing code.
- Put scripts containing `\n` or quotes in files; use `D:\HisuiWakeBuild\scratch\<task>\`, then clean it with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1`.
- Build `suyu-cmd` after code changes; after the user's commit, reconfigure and verify `GIT_REV`; never build during FPS measurement. For a new PLA export, follow the standing export procedure in CLAUDE.md.
- For CPU budget measurement, set `$env:SUYU_CPU_BUDGET="1"` and optionally `$env:SUYU_CPU_BUDGET_WINDOW_SEC` to 1-60; read the `cpu-budget` line from the normal `suyu` log, skip loading windows, and use windows of 10 s or more. Thread baselines, dropped intervals, and the `suyu-cmd` `exit(0)` sampler limitation are documented above.
- `python -B tests/recompiler_smoke/run.py` needs `CPM_SOURCE_CACHE` set to the CPM cache (for example `D:\HisuiWakeBuild\cpm`) for `nlohmann/json.hpp`.
- For headless overlay replay, use the real router and ImGui sources above with press/release batching, focus events, and combo-popup coverage.
- Analysis helpers and sampling scripts are outside the repository in `D:\HisuiWakeBuild\tmp_gt` and `D:\HisuiWakeBuild\diag\hotrva`; keep diagnostics off during FPS-only runs.
- Orca: one worker at a time, acknowledge deliveries, release each worker, and remember that `check --wait` can replay earlier messages.

#### Archived on 2026-10-20 from WORK_PLAN.md (session 37 cleanup)

The following is the verbatim text removed from WORK_PLAN.md during session 37 cleanup.

## What was done so far

- Dev19-dev29 delivered the recompiler, diagnostics, index, chain/default-option work, TTAS, overlay, Android/iOS removal, style/font, HUD, F12 fixes, embedded font, and Bad Image suppression; `cf07cb2`/`941a465` set 150% embedded Roboto Bold ([CHANGELOG.md](../../CHANGELOG.md)).
- Session 36 changed the goal, removed B2-D11 (`1b29c7d`; old text at `d0e8d1d`), added `SUYU_CPU_BUDGET` (`ec8e1a5`, no CHANGELOG entry), measured JIT/AOT, found HostTiming's root cause, and decided B1.
- B1 wait split shipped but was ineffective alone: waits were overwhelmingly short, with no OS blocking in either mode; event stats showed about 2600 callbacks/s averaging 0-5 us while HostTiming was almost entirely waiting.
- By user decision (2026-10-20), the Npad default is 4 ms. Same-session JIT and AOT X-Y-X runs showed HostTiming at about 60-64% versus 99%, and process CPU lower by about 12-13 ms/frame (JIT) and 21-24 ms/frame (AOT), with no measurable FPS change; `SUYU_HID_NPAD_UPDATE_MS=1-8` remains an override.

## Immediate next action

1. Read `CLAUDE.md`, this plan, and `CODEBASE_INDEX.md`; check `git status`/`git log`; confirm no `suyu.exe`, `ninja`, or `cl` process holds the build tree. A running host `suyu.exe` locks `bin\\suyu.exe` and causes `LNK1104`; ask the user before ending a process they may be using.
2. B2: compare the AOT guest-core gap versus JIT without a code change first, at the same position, using `SUYU_GUEST_TIME` and RIP sampling per the procedures section.
3. Optional follow-ups: investigate why AudioManager appeared only in the 1 ms AOT run, and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), and other periodic events can be coalesced.
4. Optional cheap items: tracked `__pycache__`/`.bak` cleanup, `configure_audio` combo-index fix, `controller_assign.h` unit test, and optional `recomp_bundle.cpp` `LoadLibraryExW` error-mode change with test.

## Open items

- Pre-existing Qt combo-index bug in `src/suyu/configuration/configure_audio.cpp:72`: index is cast to `AudioEngine`, but Libretro is index 4/value 5; value 4 is Null-sink fallback.
- `research/tests` has 7 failures/1 error pre-existing. Tracked artifacts: six `__pycache__` `.pyc` files (`tests/exclusive_access`, `tests/export_build_kit`, `tests/export_regression`, `tests/recompiler_smoke`, `tools/export_build_kit`) and `src/suyu/game_card.cpp.bak`; `tools/cpp-lint.sh:37-38` tab indentation is flagged by `git diff --check`.
- Bad Image dialog cause on this Windows build is undetermined.
- `recomp_bundle.cpp:305` `LoadLibraryExW` has no error-mode handling; optional runtime change not made.
- The 1 ms AOT run alone showed an AudioManager thread at 3.78 ms/frame (9.4% of a core); whether this is an event-rate effect or scene difference is unverified.
- The remaining roughly 60% of HostTiming is unresolved: umwait wait time versus OS wake cost has not been separated.
- The HUD checks listed as not reported in [CHANGELOG.md](../../CHANGELOG.md) remain untested: `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py`.

## Measurement setup and baseline

Target: i5-12400F (6C/12T, all P-cores), RTX 2060 SUPER, 16 GB; 30 FPS cap. Config: `frame_pacing_mode=0` (Auto), `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, `use_multi_core=true`. Primary metrics: whole-process CPU ms/game-frame and busiest-thread ms/frame (`core_pct`); secondary: p99 frame interval. Compare AOT/JIT at the same standing position.

| Metric | JIT | AOT |
| --- | ---: | ---: |
| FPS | 30.1 | 25.3 |
| frame p50 / p99 ms | 33.33 / 33.74 | 39.1 / 50.8 |
| process CPU ms/frame | 113.5 | 152.1 |
| process CPU ms/s | 3416 | 3851 |
| HostTiming ms/frame (core %) | 32.9 (98.6%) | 39.3 (99.1%) |
| guest cores total ms/frame | 61.6 | 94.1 |
| CPUCore_0/1/2 ms/frame | 20.5 / 20.7 / 20.3 | 30.4 / 32.1 / 31.6 |
| GPU ms/frame | 9.9 | 8.8 |
| VulkanWorker ms/frame | 4.4 | 4.7 |

Baseline: `ec8e1a5`, `SUYU_CPU_BUDGET=1`, 10-second windows, user logs; JIT 13 steady windows (≥300 frames), AOT 12 (≥245). AOT is below cap, so its per-frame values include slower frames. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\jit_ec8e1a5_suyu_log.txt`, `D:\HisuiWakeBuild\diag\cpu_budget\aot_ec8e1a5_suyu_log.txt`; summarizer: `D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py`.

Same-session X-Y-X comparison: 4 ms / 1 ms / 4 ms, with `SUYU_CPU_BUDGET=1`. JIT windows had at least 300 frames; AOT windows had at least 240 frames and 100-170 process ms/frame. JIT C's first 13 windows were a different scene (title/loading plus a load stall), so only windows with 90-110 process ms/frame were retained (15 windows, filtered copy `jit_97cad8f_C_game.txt`). Filtered window counts were JIT 12/12/15 and AOT 17/16/13.

| Backend / Npad period | FPS | HostTiming ms/frame (core %) | process CPU ms/frame | CPUCore_0/1/2 ms/frame | frame p50/p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| JIT 4 ms (X1) | 30 cap | 21.27 (63.8%) | 101.4 | 20.7 / 20.9 / 20.6 | 33.33 / 33.82 |
| JIT 1 ms (Y) | 30 cap | 32.95 (98.9%) | 113.9 | 20.6 / 21.6 / 21.3 | 33.33 / 33.79 |
| JIT 4 ms (X2) | 30 cap | 20.15 (60.5%) | 100.3 | 20.6 / 21.2 / 21.0 | 33.33 / 33.94 |
| AOT 4 ms (X1) | 25.6 (24.1-26.1) | 23.54 (60.3%) | 132.3 | 29.9 / 31.5 / 31.2 | 37.5 / 56.4 |
| AOT 1 ms (Y) | 24.9 (24.6-25.2) | 39.84 (99.2%) | 156.0 | 30.8 / 32.6 / 32.1 | 39.6 / 49.8 |
| AOT 4 ms (X2) | 25.4 (25.1-25.7) | 25.16 (63.9%) | 135.4 | 30.0 / 32.1 / 31.5 | 38.4 / 51.0 |

JIT logs: `D:\HisuiWakeBuild\diag\cpu_budget\jit_97cad8f_A.txt`, `_B.txt`, `_C.txt`; filtered JIT C: `jit_97cad8f_C_game.txt`. AOT logs: `aot_97cad8f_A.txt`, `_B.txt`, `_C.txt`; filtered copies: `aot_97cad8f_A_game.txt`, `_B_game.txt`, `_C_game.txt` in the same folder. In the JIT 4 ms conditions, `os_blocked` was about 3700-4800 of roughly 42000-52000 waits per 10 s, with wake-lateness p99 at most 100 us in A and 500 us in C (maximum about 1-3 ms); AOT 4 ms wake-lateness p99 was at most 100 us (maximum about 0.7-1 ms). The 1 ms conditions had `os_blocked=0`. The AOT 4 ms X1 p99 includes one 166 ms window; AudioManager appeared only in the 1 ms AOT condition at 3.78 ms/frame (9.4% of a core).

| Item | Scope | Measured basis |
| --- | --- | --- |
| B1 | Block HostTiming in the OS for long waits; umwait only the last margin. | HostTiming 32.9 (JIT) / 39.3 (AOT) ms/frame at about 99% of a core. |
| B2 | Break down the AOT guest-core gap with `SUYU_GUEST_TIME` and RIP sampling at the same position, AOT vs JIT; no code change. | Guest cores 94.1 (AOT) vs 61.6 (JIT) ms/frame, +32.5. |

## B1 plan (decided 2026-10-19)

Behaviour: while the remaining wait exceeds a margin, sleep in the OS with the existing `Event::WaitUntil` (condition variable), waking margin early; wait the remainder with the existing `Event::WaitFor`; `Set()` still wakes either wait at once.

Root cause: CoreTiming's HostTiming thread (`core_timing.cpp:73`) is the only caller of `Common::Event::WaitFor`, which on WAITPKG CPUs waits with `umonitor`/`umwait` (`src/common/thread.cpp:211-219`) and keeps the thread running from the OS's view.

Files: new `src/core/host_timing_wait.h` (OS-free: wait split from `wait_ns` and `timer_resolution_ns`, margin = 2 x timer resolution (1 ms at 0.5 ms); single-writer stats: wait-length histogram, wake lateness p50/p99/max, OS-block count); `src/core/core_timing.{h,cpp}` (wait loop, stats written only by the HostTiming thread); `src/core/CMakeLists.txt`; new `tests/recompiler_smoke/host_timing_wait_unit.cpp` with negative controls (no margin subtracted; OS block for short waits) registered in `tests/recompiler_smoke/CMakeLists.txt` and `run.py`.

Switches (user decisions): `SUYU_HOST_TIMING_BLOCK` default-on from the start (`=0` restores umwait-only waiting); `SUYU_HOST_TIMING_STATS=1` opt-in, a `host-timing` line every 10 s, no work when off; stats ship together with B1.

Verification: unit tests with `/W4 /WX`; `run.py`; host build (CMake list change, expect a full rebuild of about 25 minutes); Code reviewer and fixes; user JIT A/B at the same position (`SUYU_HOST_TIMING_BLOCK=0` vs default, both with `SUYU_CPU_BUDGET=1` and `SUYU_HOST_TIMING_STATS=1`) including an audio/feel check; commit, full export, AOT A/B.

B1 result: the wait split is shipped but ineffective on its own. The measured A/B and event breakdown are recorded above; waits were overwhelmingly short, with no OS blocking in either mode, while the 4 ms Npad default produced the indicative HostTiming reduction. Repeat the 1 ms/4 ms comparison in one session before treating the gain as final.

## Lessons (session 36)

- Qt `suyu.exe` rotates logs on restart: a measured log becomes `%APPDATA%\\suyu\\log\\suyu_log.txt.old.txt` when `suyu.exe` starts again (for example for an export); copy it immediately. AOT writes `<export folder>\\user\\log\\suyu_log.txt`; `log_filter` is `*:Info`, so Core Info cpu-budget lines are logged.
- `common/logging/log.h` redefines `LOG_*` after `common/logging.h` and causes C4005; new core files include `common/logging.h`.
- Adding a source to a CMake list made reconfigure rebuild the whole host (1751 steps, about 20 minutes); plan before measurement.
- `run.py` needs `CPM_SOURCE_CACHE` (`D:\HisuiWakeBuild\cpm`) for `nlohmann/json`.
- Orca `check --wait` can replay an earlier worker's `worker_done`; verify the diff, ack, and wait again.
- Shell-written Markdown turned newlines into backslash-n and PowerShell backtick escapes destroyed text; use the file edit tool, and check real LF counts after each batch.
- From session 35: mixed LF/CRLF in `tests/recompiler_smoke/CMakeLists.txt`/`run.py`; `worker-start --worktree current` rejects `--name`; bad-DLL tests call `SetErrorMode`; `rebuild-suyu.bat` relinks both executables and a running `suyu.exe` blocks it.
- Starting `rebuild-suyu.bat` from a Bash-quoted `powershell -Command` silently did not start; start it with `Start-Process` from the PowerShell tool and confirm `cl`/`ninja` are running.
- A `&&` after `cl` under `cmd.exe` invoked from the PowerShell tool did not run the produced executable; run the executable separately.
- Orca `check --wait` returns on heartbeat-only deliveries; acknowledge the delivery and wait again.

## How to proceed

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; the main agent implements, Code reviewer reviews, Code Explorer surveys read-only, and md Implementer edits Markdown; keep changes focused; and record only verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run. If a Markdown batch shortens a section, compare git diff against the intended change; one batch dropped unrelated lines.

## How to work / procedures

- Build standalone tests after BuildTools vcvars64 via `cmd.exe /c`: `cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as required.
- Every new standalone test needs a shared contract check and deliberately weakened negative control that fails fast and cannot hang.
- For overlay input bugs, headlessly replay the real router/command-bar header with ImGui at `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` first.
- Put scripts containing `\\n` or quotes in files; use `D:\HisuiWakeBuild\scratch\<task>\`, then clean it with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1`.
- Build `suyu-cmd` after code changes; after the user's commit reconfigure and verify `GIT_REV`; never build during FPS measurement. Follow `CLAUDE.md` for new PLA exports.
- CPU budget: set `$env:SUYU_CPU_BUDGET="1"`; optionally `$env:SUYU_CPU_BUDGET_WINDOW_SEC` 1-60 (default 10); one `cpu-budget` line/window. Summarize: `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>` (about 300 at 30 FPS, lower below cap). `GetThreadTimes` granularity is about 15.6 ms; missed baselines/exited threads go to untracked; `suyu-cmd` `exit(0)` does not stop the sampler.
- `python -B tests/recompiler_smoke/run.py` needs `CPM_SOURCE_CACHE` set to the CPM cache (for example `D:\HisuiWakeBuild\cpm`) for `nlohmann/json.hpp`.
- For headless overlay replay, use the real router and ImGui sources with press/release batching, focus events, and combo-popup coverage.
- Analysis helpers/scripts are outside the repository in `D:\HisuiWakeBuild\tmp_gt` and `D:\HisuiWakeBuild\diag\hotrva`; keep diagnostics off during FPS-only runs.
- Orca: one worker at a time, acknowledge deliveries, release each worker, and remember that `check --wait` can replay earlier messages.

## Archived on 2026-10-20 from WORK_PLAN.md (session 38 cleanup)

Moved verbatim when the plan was compacted for the session 39 handoff; the current plan is [WORK_PLAN.md](WORK_PLAN.md).

### Status

HEAD is `19ed191` (documentation commits on top of `7609ab3`); the working tree holds the uncommitted macOS removal (see `## Recent work (session 38)`). Host `GIT_REV` is `19ed1919a56bd91044bba87e8cf3ceeec4ce0f5f` (equal to HEAD) and the host binaries were rebuilt from the working tree after the removal. The export at `7609ab3` is done; cache publication was not established here. B1 wait splitting is present but ineffective alone because waits are under 1 ms, with about 2600 timer events/s and HID pad update at 1 kHz the largest source. Commit `97cad8f` made the Npad default 4 ms; X-Y-X results show HostTiming about 60-64% versus 99% and lower process CPU with no measurable FPS change. Untested: AudioManager appearing only in the 1 ms AOT run, the remaining roughly 60% HostTiming cost, other event coalescing, exclusive-access and overlay/backend checks.

### Recent work (session 38)

- The user chose to drop macOS support completely before more optimization work; the removal is uncommitted (see the v0.0.1-devFINAL entry in [CHANGELOG.md](../../CHANGELOG.md)).
- Plan P0-P4: Code Explorer inventory; CMake and dependency edits (reconfigure left `build.ninja` and `compile_commands.json` byte-identical); source edits verified with a before/after `/EP` preprocess comparison of all 1718 Windows translation units (1594 identical, 6 line-shift only, 116 intended removals); package-policy, smoke and exclusive_access tests; one Code reviewer round (four P3 items, fixed).
- The three smoke golden hashes were recomputed because macOS wording left the generated CMake and runtime-header comments; the old generator reproduced the old hashes exactly and only `CMakeLists.txt` and `recomp_runtime.h` moved.
- Left on purpose: third-party macOS branches under `externals/`, `dist/languages`, `GPUVendor::Apple`, `.gitignore` Finder entries, and the archived `docs/Suyu` pages (macOS build and support text there is upstream history; clean up with the Qt GUI removal G4).
- Open: Linux/FreeBSD configure and build were not run; `research/tests` was not run; `NATIVE_ROADMAP.md` decisions were recorded earlier in this session (gameplay path confirmed, Tauri for the GUIs).
- The user labelled this commit `v0.0.1-devFINAL`; README defines `devN` as a development commit count, so the label is a deliberate exception and the counting rule is not changed.

### Recent work (session 37)

- B1 shipped in `5ccacde`; `Event::WaitFor` now returns false on timeout in Windows fallbacks.
- `97cad8f` added default 4 ms Npad updates and `HostTimingEventStats` (`SUYU_HOST_TIMING_STATS=1`).
- `7609ab3` is a README commit; the host was rebuilt and its `GIT_REV` verified after export.
- JIT/AOT X-Y-X results are retained in the measurement table below.
- `ParseEnvIntInRange` is in `src/common/env_option.h` and is used for the 1-8 ms override.
- Smoke `run.py` exited 0 before the last two review fixes; `host_timing_wait_unit` and `env_option_unit` were re-run afterward.
- Code-review rounds found only P2/P3 findings; all were fixed.
- The user's `README.md` change was committed in `7609ab3`; the working tree was clean at the start of session 38.

### Immediate next action

0. The user commits the macOS removal (suggested files and message are in the session report). After the commit, reconfigure and rebuild the host and check `GIT_REV` in `msvc-release\src\common\scm_rev.cpp`; a full export is needed before the next measurement because the producer revision changes.
1. Read `CLAUDE.md`, this plan, and `CODEBASE_INDEX.md`; check status/logs and confirm no `suyu.exe`, `ninja`, or `cl` holds the build tree. Ask before ending a user process.
2. The user is choosing between B2 and [NATIVE_ROADMAP](NATIVE_ROADMAP.md) N0 (2026-10-20). B2: compare the AOT guest-core gap versus JIT at the same position with `SUYU_GUEST_TIME` and RIP sampling; make no code change first.
3. Optional: investigate AudioManager and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), or other periodic events can be coalesced.
4. Cheap: tracked `__pycache__`/`.bak` cleanup, `configure_audio` combo-index fix, `controller_assign.h` test, and optional `LoadLibraryExW` error-mode change with test.

### Open items

- Pre-existing Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` has 7 failures/1 error pre-existing; tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD checks, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py` remain untested.
## Archived on 2026-10-21 from WORK_PLAN.md (session 39 cleanup)

### Recent work (sessions 37-38)

- Session 37: B1 wait splitting (`5ccacde`), the 4 ms Npad default and `HostTimingEventStats` (`97cad8f`, `SUYU_HOST_TIMING_STATS=1`), and `ParseEnvIntInRange` in `src/common/env_option.h`; the X-Y-X results are in the measurement table below.
- Session 38 roadmap: [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md) defines the end goal (a title-specific standalone native PLA application with no CPU JIT, Maxwell/emulated GPU processing, or general emulator runtime) as milestones N0-N10 (runtime) and G1-G4 (tools and GUI). Decisions of 2026-10-20: the supported gameplay path is confirmed, and the Aevora Launcher and Recompiler GUI use Tauri.
- Session 38 code: macOS support and the Metal/MoltenVK backend were removed (`28011a2`, labelled `v0.0.1-devFINAL` by the user as a deliberate exception to the `devN` commit-count rule; the CHANGELOG entry has the evidence).
- Left on purpose after the removal: third-party macOS branches under `externals/`, `dist/languages`, `GPUVendor::Apple` (Asahi Linux), `.gitignore` Finder entries, and the archived `docs/Suyu` pages (clean up with the Qt GUI removal, G4).
- Not verified after the removal: Linux/FreeBSD configure and build, Windows gameplay, a new PLA export, and `research/tests`.

### N0 plan (session 39)

- The user chose N0 on 2026-10-20 and asked that it proceed stage by stage, with a report, a commit message (normal commit), and user approval at the end of every stage.
- Stages: 0 read-only hook survey (Code Explorer); 1 design note (one opt-in option, proposed `SUYU_BOUNDARY_INVENTORY=1`, execution-neutral when off; data model, output, thread safety); 2 header-only recorder plus standalone unit test with a negative control; 3a SVC and IPC hooks; 3b nvdrv, audio, HID, and filesystem hooks; 4 PLA collection on the JIT host `suyu.exe` along the supported path (AOT confirmation only after a user commit, reconfigure, and full export); 5 SDK-to-SVC/IPC mapping and NVN entry points (separate design); 6 structural-gate script with a shared deny list, synthetic test, and first report on the current launcher; 7 Markdown batch (check list, inventory summary, WORK_PLAN, CHANGELOG, CODEBASE_INDEX).
- Decisions (user, 2026-10-20): only summarized inventory goes into the repository; raw PLA logs stay local under `D:\HisuiWakeBuild\diag\n0\`. Stage 6 is not moved earlier.
- Stage 0 result (verified by the main agent against the current code; the survey report is local at `D:\HisuiWakeBuild\scratch\n0_stage0\report.md`):
  - SVC: JIT and AOT both reach `Svc::Call` from `PhysicalCore::RunThread` (`src/core/hle/kernel/physical_core.cpp:322-325`); `GuestTimeLedger` already reads the SVC number there (`:281-284`).
  - SVC caller: the AOT emitter writes `c->pc` (the resume PC, SVC address + 4) before `pending_svc` and `recomp_svc` (`src/core/recompiler/arm64_to_c.h:1511`), so the SVC site is resume PC - 4; x30 is the architectural LR. JIT exposes live LR/PC through `GetContext`.
  - IPC: `ServiceFrameworkBase::HandleSyncRequest`/`InvokeRequest`/`InvokeRequestTipc` (`src/core/hle/service/service.cpp:87-139`) have the service name and command ID; `ReportUnimplementedFunction` (`:67-85`) is the existing unimplemented-command report. Because nvdrv, audio, HID, and filesystem are HLE services, this one hook counts their command IDs; per-subsystem hooks are needed only for detail (ioctl numbers, paths, parameters).
  - nvdrv: `NVDRV::Open` and `Ioctl1/2/3` in `src/core/hle/service/nvdrv/nvdrv_interface.cpp:22,58-128`; the device name is known only at Open (`nvdrv.cpp` `Module::Open`), so the recorder must keep an fd-to-name map.
  - Audio: `audio_renderer_manager.cpp` `OpenAudioRenderer` (initial parameters) and `IAudioRenderer::RequestUpdate`/`RequestUpdateAuto` (`src/core/hle/service/audio/audio_renderer.cpp:77-94`).
  - Filesystem: `fsp_srv.cpp:466-490` (RomFS/data storage), `OpenSaveDataFileSystem`, `IFileSystem::OpenFile` (`fs_i_filesystem.cpp:91-100`, path and mode), `IFile::Read` (offset and size).
  - NVN: no `nvnBootstrapLoader` reference exists in the tree; the only guest symbol source found is each NSO's dynsym (runtime parser in `src/core/arm/recomp/arm_recomp.cpp`). Deferred to stage 5.
  - Build: all hook sites compile into `core`; a header-only recorder avoids a CMake source-list change, though including translation units recompile.
  - Output: follow the coverage pattern (`WriteRecompCoverageFile`, `arm_recomp.cpp:697-707`: env path override, else `LogDir`), plus periodic log reports because teardown may not run.

### N0 design (stage 1)

Design note for the boundary inventory, written in session 39 (stage 1). It describes intended code; nothing below is implemented yet.

- Option: `SUYU_BOUNDARY_INVENTORY=1` (`Common::EnvFlagDefaultOff`); any other value leaves it off. `SUYU_BOUNDARY_INVENTORY_PATH` overrides the report file, otherwise `LogDir/boundary_inventory.txt` (the `WriteRecompCoverageFile` pattern).
- Off path: the flag is read once into a process-wide `inline const bool`; every hook is a single `if` on it and does nothing else. No allocation, lock, clock read, or change to guest-visible results when off; hooks only read values the existing code has already parsed.
- Code: one header-only, OS-free recorder `src/core/boundary_inventory.h` (namespace `Core::BoundaryInventory`), so no CMake source-list change. Report formatting is a pure function of a snapshot, and the clock is passed in, so the unit test does not need `Core::System`.
- Data model (cumulative over the process; keys with count and first-seen time):
  - SVC: per physical core, a cache-line-aligned array of 256 counters indexed by SVC number, single writer per core (relaxed load and store); numbers above 0xFF go to one overflow counter. Hook: `PhysicalCore::RunThread` beside the existing `Svc::Call`.
  - IPC: (service name, command type, command ID) under one mutex, plus an `unimplemented` flag set from `ReportUnimplementedFunction`. Hook: `ServiceFrameworkBase::HandleSyncRequest`. Domain sub-interfaces are recorded under their own interface name.
  - nvdrv: `Open` records the device name and keeps an fd-to-name map (removed at `Close`); `Ioctl1/2/3` record (device name, variant, raw ioctl). Hooks: `NVDRV` in `nvdrv_interface.cpp`.
  - Audio: distinct `AudioRendererParameterInternal` tuples at `OpenAudioRenderer`; at `RequestUpdate` the count and min/max input, output, and performance buffer sizes.
  - HID and applets: HID command IDs come from the IPC record; library-applet creation records the applet ID (exact hook found in stage 3b).
  - Filesystem: opens by (kind: RomFS current process, data storage by ID, save data, SDMC, BIS, other; path; mode); reads by path with count, total bytes, and largest end offset. `IFile` keeps the opened path only when the option is on.
- Thread safety: SVC counters are single-writer per core; every other category has its own mutex. Contention while on is acceptable for a diagnostic; the inventory is not a timing measurement.
- Output: a deterministic, sorted, tab-separated cumulative report with sections `[svc]`, `[ipc]`, `[nvdrv]`, `[audio]`, `[applet]`, `[fs-open]`, `[fs-read]`. The whole file is rewritten at most every 10 seconds from a recording thread (rate limit checked with a try-lock) and once at shutdown, because teardown may not run; each write logs one `LOG_INFO` summary line with the distinct-key counts, not the full report.
- Caller attribution (SDK function per SVC/IPC) is stage 5; the data model leaves room for it. The AOT SVC site is the resume PC minus 4.
- Test: `tests/recompiler_smoke/boundary_inventory_unit.cpp`, registered as `smoke_boundary_inventory_unit`: per-core SVC counts and overflow, IPC keys and the unimplemented flag, nvdrv fd reuse after `Close`, filesystem aggregation, sorted output, and the 10-second write rate limit with an injected clock. Negative control: a build macro that weakens the model (`Close` does not remove the fd, so a reused fd keeps the old device name) must make the test fail fast.
- Repository output: the stage 7 summary aggregates filesystem paths to directory level with counts; raw reports stay in `D:\HisuiWakeBuild\diag\n0\`.
- Verification per stage: standalone build of the unit test and its negative control (stage 2); host build, Code reviewer review including the off-path check, and a short `suyu.exe` start with the option off and on (stages 3a and 3b).

### N0 design amendment (stage 4a: manual segments)

Written in session 39 after the first collection (run1, JIT host `suyu-cmd.exe`, option on). The cumulative report kept per-phase attribution hard, so recording becomes manual and segmented. The user approved the design on 2026-10-21 and chose to start paused.

- Run1 result (local files under `D:\HisuiWakeBuild\diag\n0\`): 32 distinct SVCs, 44 service interfaces with 121 commands, 34 nvdrv rows, 7 fs-open rows; `[audio]` and `[applet]` were empty (PLA uses `audout:u`/`hwopus`, not `audren:u`; no library applet was created on the played path). The cumulative snapshots copied at each step are valid and can be differenced into per-step deltas.
- Operation (suyu-cmd only): F9 marks (ends the open segment, writes its file, starts the next); F10 pauses and resumes; an Inventory window under Debug shows the state, the segment label, and Mark, Pause, Skip step, and Write now actions; a small on-screen line shows the state (for example `INV rec 3/8 battle 12.4 s`). Keys are provisional until checked against `fullscreen_toggle.h` and the game's key bindings.
- Labels: the supported-path steps in order (title, village, field, catch, battle, menu, pokedex, save); the overlay can also take a typed label.
- Start state: paused when a control host (suyu-cmd) is present; `SUYU_BOUNDARY_INVENTORY_START=1` starts recording at launch. Without a control host (Qt `suyu.exe`), recording is on from the start and the single open segment is auto-saved every 10 s, as in stage 3.
- Segments are independent: file `inventory_<run>_<NN>_<label>.txt`, header `# segment label=<label> elapsed_ms=<n>`, counts since the previous mark. The whole run is the sum of the segments (the local summarizer merges files). The open segment is auto-saved to `inventory_<run>_open.txt` every 10 s.
- Environment: `SUYU_BOUNDARY_INVENTORY_DIR` (output folder) and `SUYU_BOUNDARY_INVENTORY_RUN` (run label) replace `SUYU_BOUNDARY_INVENTORY_PATH`, which nothing outside the local helper used.
- Recorder semantics (stage 4a-1, implemented): `SetRecording` pauses the counters but the nvdrv fd-to-device map keeps following the guest; `EndSegment(label, elapsed_ns)` returns the segment text and resets it; SVC counts are the totals minus a baseline, each total read once, so concurrent marks lose and duplicate no call; tables are moved out under their own mutexes; the fd map is kept across marks.
- Stages: 4a-1 recorder segment API and unit tests (done: `smoke_boundary_inventory_unit` passes; ten header mutants fail it, including baseline not advanced, pause ignored, tables not reset, a mark that clears the fd map, and a pause that blocks the fd map); 4a-2 suyu-cmd host controls (done, see below); 4b the user re-collects the supported path.
- Stage 4a-2 (implemented, host build succeeded, not yet run by hand): `src/suyu_cmd/fullscreen_toggle.h` routes F9 (mark) and F10 (pause/resume) like F12, with `IsOverlayIndependentHotkey` so they work while an ImGui window holds the keyboard; they belong to the game when the inventory is off. `overlay/inventory_session.h` (pure: steps title, village, field, catch, battle, menu, pokedex, save then `extraN`, elapsed time that excludes pauses, a typed label that lasts one segment, `PendingSegmentFiles` retry queue, `FormatInventoryStateLine`) and `overlay/inventory_host.h` (event-thread glue: ends segments, writes `inventory_<run>_<NN>_<label>.txt`, `Finalize` at window teardown) are covered by `smoke_inventory_session_unit` (twelve header mutants across the session and hotkey tests fail it). The Inventory window and a state line (bottom right, amber when paused) are in `debug_overlay.cpp`; `emu_window_sdl2_vk.cpp` installs the host. Output: `SUYU_BOUNDARY_INVENTORY_DIR` (read as UTF-16 on Windows; default `<log dir>/boundary_inventory`), `SUYU_BOUNDARY_INVENTORY_RUN` (default `run`), `SUYU_BOUNDARY_INVENTORY_START=1` to start recording; the open segment is auto-saved to `inventory_<run>_open.txt`. Differences from the plan above: the command-bar entry is a top-level `Inventory` item beside `Debug` (Debug is itself a toggle item), and the typed-label field is implemented but its text input has not been tried by hand. Review fixes: a full retry queue retries before it drops anything and names the dropped files; `Finalize` makes a last write attempt at shutdown; the output folder no longer goes through the ANSI code page. Not run: suyu-cmd with the controls, a PLA session.

### N0 stage 4 result and stage 5 design

Written in session 39 after the manual collection (`boot1`: start to title with the option started recording; `test1`: eight segments title to save, started paused) and the stage 5 survey. Raw files stay local under `D:\HisuiWakeBuild\diag\n0\` (`boot1`, `test1`; `segments_report.py` and `summarize_inventory.py` read them); do not delete them, they are the only evidence.

- Stage 4 result (JIT host `suyu-cmd.exe`, one play of the supported path, so not exhaustive): 32 distinct SVCs; 44 service interfaces with 120 commands; six nvdrv devices (`nvmap`, `nvhost-as-gpu`, `nvhost-ctrl`, `nvhost-ctrl-gpu`, `nvhost-gpu`, `nvdisp_disp0`) with 34 open/ioctl rows; file system use is RomFS of the current process and save data (`/main`, `/main2`); no `audren` renderer is opened (audio runs through `audout:u` and `hwopus`); no library applet is created. After the village segment almost no new keys appear (field 2, catch 19 including save-data writes and time/random services, battle/menu/pokedex 0, save 1). The union of both collections matches run1 except one IPC command (probably `ISelfController` command 1, at exit) and the save-data `/backup` open, which are exit-time.
- Stage 5 survey (read-only, verified): SVC caller context is readable at the stopped CPU in `PhysicalCore::RunThread` for JIT and AOT (the AOT emitter and dynarmic both stop at SVC + 4); a frame-pointer backtrace walker exists (`src/core/arm/debug.cpp`); there is no module+RVA resolver (loader gives name and base only, `FindModules`/`GetModuleEnd` scan live memory); IPC handling runs while the client is blocked in SVC 0x21, so reading the client's context from the IPC hook needs scheduler locks; function names exist only where a module's dynsym has them; no NSO/ELF files exist on disk (only recompiled outputs); there is no `nvnBootstrapLoader` or NVN table anywhere in the tree.
- Approved design (2026-10-21): [callers] section with the SVC site (`pc - 4`, checked to be an `svc` instruction), LR, and two more return addresses up the x29 chain, printed as `name+0xRVA`; per SVC or IPC key the eight most frequent callers are kept, the least frequent is evicted into `other=`; module list taken once from `FindModules`/`GetModuleEnd` at the first SVC. IPC callers (5b): the SVC 0x21 hook stores the caller frames per thread id and `InvokeRequest` reads them by `ctx.GetThread()`, so no other thread's context is read. NVN (5c): at load, scan module memory for strings starting with `nvn` and for dynsym names containing `nvn` or `nn::`; the lists stay in local files. Repository rule: no guest addresses or game-derived name lists are committed, only aggregates.
- Stage 5a (done, host build succeeded, not run by hand): `src/core/boundary_inventory.h` has `ModuleRange`, `SetModules`/`Describe`, `ClaimModuleScan`, `RecordCaller`, `CollectCallerFrames`, `IsSvcInstruction` and the `[callers]` section; `PhysicalCore::RunThread` records the SVC caller (`RecordSvcCaller`, `RecordInventoryModules`). Covered by `smoke_boundary_inventory_unit` (eleven header mutants fail it). Review result: no runtime defect found; open point accepted by design: a failed or empty first module scan stays empty.
- Stages left: 5b IPC callers, 5c module scan and NVN strings, 5d collection and local analysis.

### Measurement setup (archived)

Target: i5-12400F (6C/12T, P-cores), RTX 2060 SUPER, 16 GB; 30 FPS cap. Config: `frame_pacing_mode=0`, `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, `use_multi_core=true`. Metrics: process and busiest-thread CPU ms/game-frame (`core_pct`), with p99 frame interval; compare same-position JIT/AOT. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\` (baseline and `97cad8f` X-Y-X files); summarizer is `summarize_cpu_budget.py`.

| Backend / Npad period | FPS | HostTiming ms/frame (core %) | process CPU ms/frame | CPUCore_0/1/2 ms/frame | frame p50/p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| ec8e1a5 JIT baseline | 30.1 | 32.9 (98.6%) | 113.5 | 20.5 / 20.7 / 20.3 | 33.33 / 33.74 |
| ec8e1a5 AOT baseline | 25.3 | 39.3 (99.1%) | 152.1 | 30.4 / 32.1 / 31.6 | 39.1 / 50.8 |
| JIT 4 ms (X1) | 30 cap | 21.27 (63.8%) | 101.4 | 20.7 / 20.9 / 20.6 | 33.33 / 33.82 |
| JIT 1 ms (Y) | 30 cap | 32.95 (98.9%) | 113.9 | 20.6 / 21.6 / 21.3 | 33.33 / 33.79 |
| JIT 4 ms (X2) | 30 cap | 20.15 (60.5%) | 100.3 | 20.6 / 21.2 / 21.0 | 33.33 / 33.94 |
| AOT 4 ms (X1) | 25.6 | 23.54 (60.3%) | 132.3 | 29.9 / 31.5 / 31.2 | 37.5 / 56.4 |
| AOT 1 ms (Y) | 24.9 | 39.84 (99.2%) | 156.0 | 30.8 / 32.6 / 32.1 | 39.6 / 49.8 |
| AOT 4 ms (X2) | 25.4 | 25.16 (63.9%) | 135.4 | 30.0 / 32.1 / 31.5 | 38.4 / 51.0 |

The ec8e1a5 baseline used `SUYU_CPU_BUDGET=1`, 10-second windows, and user logs; AOT was below cap. The X-Y-X windows retained same-position runs (JIT 12/12/15; AOT 17/16/13; JIT C filtered scene/loading windows). In 4 ms runs, JIT `os_blocked` was about 3700-4800 per 42000-52000 waits/10 s and AOT wake-lateness p99 was at most 100 us; 1 ms had zero OS blocks. AudioManager appeared only in 1 ms AOT at 3.78 ms/frame (9.4% of a core).

### Status (before the session 39 cleanup)

HEAD is `2db4218` (`docs: compact WORK_PLAN for the session 39 handoff`) on `main`; the host was reconfigured and rebuilt in session 39 and `GIT_REV` in `msvc-release\src\common\scm_rev.cpp` equals `2db4218ebf260a8b050e29354790f3dae5359ccb`. The PLA export and the persistent AOT cache entry date from `7609ab3`; the new producer revision requires a full export before the next measurement. Optimization state: B1 wait splitting alone was ineffective (waits are under 1 ms; about 2600 timer events/s, HID pad update at 1 kHz the largest source); the 4 ms Npad default (`97cad8f`) cut HostTiming from about 99% to 60-64% of a core with no measurable FPS change (table below). Untested: AudioManager appearing only in the 1 ms AOT run, the remaining roughly 60% HostTiming cost, other event coalescing, and overlay/backend checks. macOS support is removed; Linux and FreeBSD builds were not run.
## Archived on 2026-10-21 from WORK_PLAN.md (session 40 cleanup)

## Status

HEAD is `c338961` on `main` with the user's N0 stages 5b, 5c, and 6 commits included; stage 7 documentation is uncommitted. Host binaries were rebuilt after 5c (`suyu.exe` and `suyu-cmd.exe`, 8/8, 0 errors), but `GIT_REV` in `msvc-release\src\common\scm_rev.cpp` still reads an earlier hash until the next reconfigure. The PLA export/AOT cache entry still dates from `7609ab3`.

## N0 progress (session 40)

Every N0 stage was a normal commit declared by the user; this WORK_PLAN carries the record, so `CHANGELOG.md` is unchanged. N0 proceeds stage by stage; after each stage the main agent reports, gives the file list and a normal-commit message, and waits for the user's approval before the next stage (user instruction, 2026-10-20).

| Stage | What | State |
| --- | --- | --- |
| 0 | read-only hook survey | done (`b1f6610`) |
| 1 | design note | done (`437df06`) |
| 2 | header-only recorder and unit test | done (`511c147`) |
| 3a | SVC and IPC hooks | done (`1c5167e`) |
| 3b | nvdrv, audio, applet, filesystem hooks | done (`917cf94`) |
| 4 | collection on the JIT host; manual segments (4a-1 `0038de1`, 4a-2 `5cbafc6`); user collections `boot1` and `test1` (4b) | done |
| 5a | module table and SVC caller chains in `[callers]` | done (`810c1bf`) |
| 5b | IPC caller attribution through a per-thread stash written at SVC 0x21/0x22 | done (`b619bf9`) |
| 5c | module and NVN scan at load (local lists only) | done (`3c945e4`) |
| 5d | collection and local analysis of callers | done (test2 collection) |
| 6 | structural-gate script with a shared deny list, synthetic test, first report on the current launcher | done (`c338961`) |
| 7 | Markdown batch: supported-path check list, inventory summary (aggregates only), WORK_PLAN, CODEBASE_INDEX | done (uncommitted) |

### What the code does now

- Option `SUYU_BOUNDARY_INVENTORY=1` (exactly `1`; read once at process start); off, every hook is one flag check. Recorder: `src/core/boundary_inventory.h` (header-only, OS-free); file output: `src/core/boundary_inventory_report.h`.
- Hooks: `PhysicalCore::RunThread` (SVC number per core and caller chain, module table filled at the first SVC), `ServiceFrameworkBase::InvokeRequest`/`InvokeRequestTipc` (service, command type, command ID, unimplemented flag in one record), nvdrv `Module::Open/Close/Ioctl1-3`, audio `OpenAudioRenderer`/`RequestUpdateAuto`, `ILibraryAppletCreator`, `fsp_srv.cpp` opens, `IFileSystem::OpenFile`, `IFile::Read`, `IStorage::Read`; `System` shutdown writes the open segment after the cores stop. At SVC 0x21/0x22, `RecordSvcCaller` stashes four caller frames per guest thread id and `RecordBoundaryIpc` consumes the stash with `ctx.GetThread().GetThreadId()`; the stash is bounded to 1024 threads and clears when full. `ScanInventoryModules` runs on the first SVC after the module list is set, scanning loaded modules in 256 KiB chunks and using bounded dynsym parsing; modules larger than 512 MiB are cut.
- Segments: recording can be paused; `EndSegment` returns the segment and resets it; the nvdrv fd-to-device map survives pauses and marks. suyu-cmd controls: F9 mark, F10 pause/resume, Inventory window (command bar item beside Debug), state line at the bottom right. It starts paused when the controls exist (`SUYU_BOUNDARY_INVENTORY_START=1` starts recording); without them (Qt `suyu.exe`) it records from the start and auto-saves the open segment every 10 s.
- Output: folder `SUYU_BOUNDARY_INVENTORY_DIR` (default `<log dir>/boundary_inventory`), run label `SUYU_BOUNDARY_INVENTORY_RUN`; files `inventory_<run>_<NN>_<label>.txt` (segments), `inventory_<run>_open.txt`, and the local-only `inventory_<run>_scan.txt`. Format v1 is unchanged: `# boundary inventory v1`, `# segment label=<label> elapsed_ms=<n>`, sections `[svc] [ipc] [nvdrv] [audio] [applet] [fs-open] [fs-read] [callers]`. `SUYU_BOUNDARY_INVENTORY_SCAN=0` disables the scan.
- Tests: `smoke_boundary_inventory_unit`, `smoke_inventory_session_unit`, `smoke_fullscreen_toggle_unit` (F9/F10 routes); each new check has header mutants that must fail (see procedures).

### Results so far

- Union of boot1, test1, and test2 (18 reports): 32 distinct SVCs, 44 service interfaces with 121 commands, 34 nvdrv rows across six devices, no audio renderer open, no applet, 7 fs-open rows, and fs-read on two directories (RomFS of the current process and save data only).
- Test2: 202,179 IPC calls and 29 caller-recorded IPC keys with matching `[callers]` and `[ipc]` totals; SVC 0x21 was called 202,276 times. All four-frame IPC chains ended in the SDK module; SVC callers reached the main module for part of the calls. Test2 covered 24 distinct SVCs and 61 distinct keys; it had no boot segment.
- Test2 scan: 563 NVN strings and 15,965 matching dynsym names; the main module contained 524 NVN strings and a table of 524 NVN API function names. Scan time was not measured; collection completed. The bag step has no segment of its own in test1 or test2.
- Stage 6: 34 structural gate tests with an independent oracle pass, and all 56 tool mutants fail the suite. The first baseline report matched 1,007,632 public symbols and 37 imported DLLs; import-pattern matches were zero. `--require-absent N1` failed with `FAIL jit`, as expected for the JIT-capable launcher.

### Local data and tools (not in the repository)

`D:\HisuiWakeBuild\diag\n0\` holds `run_n0.bat <game> <label>`, `summarize_inventory.py`, `segments_report.py`, `union_summary.txt`, and local collections `boot1`, `test1`, and `test2` (do not delete). The test2 collection includes eight labeled segments, the open remainder, and the scan file. The structural gate and its tests are in the repository; its first report is local in the gate output folder. `D:\HisuiWakeBuild\scratch\n0_stage0\` holds the read-only survey reports and worker specs of session 39.

### Design decisions to keep

Raw guest addresses and game-derived name lists stay local, the repository gets aggregates only; the caller table keeps the eight most frequent callers per key and counts evicted ones in `other=`. The IPC caller is a per-thread stash written at SVC 0x21/0x22 and consumed once by the service thread, bounded to 1024 threads. The module scan writes only a local file and the repository receives aggregates only; the gate keeps one deny table for all milestones and refuses unverified evidence unless the user opts in.

### Not verified yet

Inventory under the AOT launcher; scan stall time; the asynchronous SVC 0x23 stash limit; deeper IPC attribution beyond the SDK; Linux for the gate; the typed-label input of the Inventory window.

## Immediate next action

1. Read `CLAUDE.md`, this plan, `NATIVE_ROADMAP.md`, `CODEBASE_INDEX.md`; run `git status`; confirm no `suyu`, `ninja`, or `cl` process holds the build tree; compare `GIT_REV` of the host with HEAD and rebuild with `D:\HisuiWakeBuild\rebuild-suyu.bat` through `Start-Process` if sources changed.
2. N0 is complete on the user's acceptance: the inventory files and gate report are recorded, and the supported-path check list is written in `N0_INVENTORY.md`.
3. After acceptance, candidates in roadmap order are N1 (CPU without JIT; the gate's N1 check is the done criterion) or N8a (NVN interception can start after N0), or parked CPU item B2 and the listed cheap cleanups. The choice is the user's.
4. Parked CPU work (B2): compare the AOT guest-core gap with JIT at the same position using `SUYU_GUEST_TIME` and RIP sampling, with no code change first; NATIVE_ROADMAP notes that tuning of the emulated scheduler and HostTiming does not carry over to the native runtime (N3). Optional: investigate AudioManager and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), or other periodic events can be coalesced.
5. Cheap: tracked `__pycache__`/`.bak` cleanup, the `configure_audio` combo-index fix, a `controller_assign.h` test, an optional `LoadLibraryExW` error-mode change with test, the obsolete `*.plist` glob in `tools/translations/qt-source.sh`, and the now-dead `NOT MULTIARCH_BUILD` conditions in `src/dynarmic/CMakeModules/TargetArchitectureSpecificSources.cmake` and `src/dynarmic/tests/CMakeLists.txt`.

