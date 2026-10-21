# Aevora Codebase Index

Snapshot of the source tree at `2fdf3f9` (2026-10-16), updated from a read-only survey. It is a navigation aid, not a verified architecture specification.

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
| `AGENTS.md` | Codex entry point; points to `CLAUDE.md`. |
| `vcpkg.json`, `vcpkg-configuration.json`, `vcpkg-overlays/`, `conanfile.txt`, `shell.nix` | Alternate/legacy package manifests (*inference*: CPM is the active path). |
| `tests/` | Recompiler and export test projects (see section 7). |
| `research/` | Recompiler research tests (`tests/` and `tests/fixtures/shader_constant_buffers.h`, used by `research/tests/test_review_regressions.py`). |
| `tools/` | Export build kit, CPM scripts, package-policy audit, shader tools, Windows helpers. |
| `scripts/` | Build-fix notes and helper scripts. Inspect before running; some clear caches. |
| `dist/`, `hooks/` | Desktop/icon/udev assets; `dist/fonts/Roboto/` contains the Roboto variable TTF, Roboto-Bold.ttf, and OFL notice, while `dist/fonts/BIZUDPGothic/` contains BIZ UDPGothic Regular and its OFL notice; git pre-commit hook installed by CMake. |
| `docs/Aevora/`, `docs/Suyu/` | Current project docs, including `WORK_PLAN.md`, `NATIVE_ROADMAP.md` and `OLD_HISTORY.md`; archived upstream docs. |
| Local, outside repository: `D:\HisuiWakeBuild\diag` | `buildsuyu.cmd`, `runsmoke.cmd`, samplers, and `pp_compare` (preprocess-based source-change equivalence tools). |

### Lineage markers found in the tree

- `.gitmodules:15` **(v)** pins `externals/dynarmic` to branch `mk8-recomp`.
- `CMakeModules/GenerateSCMRev.cmake:56` **(v)** hard-codes `BUILD_FULLNAME "${REPO_NAME} v0.0.11 (mk8-recomp)"`, although the README names v0.0.12 as the base.
- *Inference*: the recompiler work descends from an `mk8-recomp` fork rather than stock Suyu v0.0.12. Verify this before you describe the lineage publicly (WORK_PLAN Milestone 1).

## 2. Static recompilation pipeline (project-critical)

### 2.1 ARM64 → C translator

`src/core/recompiler/arm64_to_c.h` is a header-only translator of about 7,600 lines. It is the only code generator.

| `recomp_image_index.h` | `src/core/recompiler/recomp_image_index.h:14-59` (v, 2026-10-13) | Shared C/C++ `SuyuRecompIndex32V1` layout, `recomp_image_index32` export name, signed offset conversion, and refusal reasons. |

The shared topology and image identity contracts are defined in `src/core/recompiler/recomp_topology.h` (versioned module bindings, load ordering, image names, validation, bundle-byte conversion, and `recomp_modules.cmake` output; `kRecompTopologyVersion:31`, `BuildRecompTopology:160`, `WriteRecompModulesCMake:234`) and `src/core/recompiler/recomp_image_identity.h` (v1 symbol and raw 32-byte bundle digest contract; `RECOMP_IMAGE_IDENTITY_V1_SYMBOL:15`, `RecompImageIdentityV1Fn:24`). Registry v1 is defined once in `src/core/recompiler/recomp_registry_v1.h:64` (`SuyuRecompRegistryV1`, entries, ABI/features, and DLL export-name macros); `WriteRecompRegistryV1C:299` and `WriteRecompTopologyV1Marker:396` in `recomp_topology.h` emit the registry and marker.

`src/core/recompiler/pe_image_size.h:64` parses validated PE32+ headers and reads `SizeOfImage` from files (`:114`), rejecting truncated, zero-sized, or out-of-range headers. `src/core/loader/nso_module_names.h:19` defines the 13-name canonical NSO order shared by the loader, topology, and exporter.

| Symbol | Location | Purpose |
|---|---|---|
| `struct Block` | `arm64_to_c.h:60` (v) | Discovered code block (vaddr, size, count, entry flag). |
| `g_translate_all` | `arm64_to_c.h:303` (v) | Translate forms normally left to the JIT. |
| `g_emit_standalone_runner` | `arm64_to_c.h:310` (v) | Defaults to `true` for synthetic tests. The comment says the game exporter turns it off so game code runs only inside suyu against the user's files. |
| `g_emit_fastmem` / `g_emit_guard_gen` / `g_emit_fpx` / `g_emit_fpx_shadow` | `arm64_to_c.h:316-337` (v) | ABI 6 feature switches (FM1 page-table fastmem, GG1 generation guard, FPX1 exact native FP, FPX shadow instrumentation). FM1 remains on unless `SUYU_AOT_FASTMEM=0`; the other emitter switches retain their existing defaults. |
| `g_emit_fm_inline` / `EmitFmInline()` | `arm64_to_c.h:346-348` (v) | FMI is on unless `SUYU_AOT_FM_INLINE=0`; it is emitted only when FM1 is also enabled. |
| `FastmemInlineH()` / `BuildRuntimeH()` | `arm64_to_c.h:6668`, `:7241` (v, 2026-10-12) | Appends the FMI static inline fast paths, including the GG1 watch-word store entry, to generated `recomp_runtime.h`; variants are selected with FM1, GG1, and FPX settings. |
| `recomp_fmi_entry()` / `recomp_fm_entry()` | `arm64_to_c.h:6676`, `:7698` (v, 2026-10-12) | Generated FMI and FM1 page-table entry checks used by the fastmem accessors. |
| `FmInlineRuntimeC()` / `RuntimeC()` | `arm64_to_c.h:8111`, `:8116-8131` (v) | Renames generated-unit fastmem calls to FMI functions while keeping `recomp_runtime.c` definitions under their original names. |
| `TranslateAllForExport()` | `arm64_to_c.h:340` (v) | Strict static exports translate everything. |
| `kChainBudget` | `arm64_to_c.h:351` (v) | Blocks run before control returns to the dispatcher. |
| `ChainTo` / `ChainIndirect` | `arm64_to_c.h:299-302`, `:409-440` (v) | Default-on direct B/BL and independent indirect RET/BR/BLR tail-chain emitters; set the corresponding environment option to `0` to disable; indirect lookup subtracts the module base and preserves budget on misses. |
| `XmodTypesText` / `EmitCrossChain` / `ChainIndirect` and `recomp_image_xmod_v1` | `arm64_to_c.h:409-419`, `:505-507`, `:6172-6177` (v, 2026-10-11) | XMOD1 generated table types, opt-in cross-module lookup fallback for indirect chains, and the image export. |
| Generated `recomp_lookup()` / `_lookup_search()` | `arm64_to_c.h:5733`, `:5789` (v, 2026-10-13) | Emits the segment-search fallback and flat-index lookup used by each generated module; the search runs only when index allocation fails. |
| Generated 32-bit index | `arm64_to_c.h:426-442`, `:5733-5808`, `:6121`, `:6253-6264` (v, 2026-10-13) | `_build_idx_for`, `recomp_lookup`, `Index32TypesText`, and the `recomp_image_index32` export text; failed builds retain a reason and use segment search. |
| `ChainIndirect()` / `SetFlags()` | `arm64_to_c.h:505`, `:521` (v, 2026-10-12) | Indirect RET/BR/BLR chaining and ARM64 condition-flag emission. |
| `g_emit_direct_chain` / `g_emit_indirect_chain` | `arm64_to_c.h:367-374` (v) | Independent default-on chain switches controlled by `SUYU_AOT_DIRECT_CHAIN=0` and `SUYU_AOT_INDIRECT_CHAIN=0`. |
| `g_emit_chain_start` / `EmitChainStart()` | `arm64_to_c.h:401-405` | Default-on chain-start switch (`SUYU_AOT_CHAIN_START=0` disables); effective only when direct chaining is enabled. |
| Chain-start target set and `_s` emission | `arm64_to_c.h:5428-5510` and `:5568-5578` | Finds direct B/BL targets at most `kChainStartMaxInsns` instructions, selects `_s` from `ChainTo`, and shares the expected-word array and GG1 slot. |
| `chain_start_budget.h` | `src/core/recompiler/chain_start_budget.h:14-53` | Estimates start-entry host bytes and projects guest text plus entries per linked image under the 2 GiB limit with a 128 MiB margin. |
| `RECOMP_TAIL_CALL` | `arm64_to_c.h:5258-5273` (v) | Generated-unit macro: clang uses `musttail`; other compilers use a bounded plain call/return; emitted only when a chain flag is enabled. |
| `EmitProject()` | `arm64_to_c.h:5084` (v) | Per-module entry point. Emits C sources, `GuestContext`, the module `CMakeLists.txt`, and returns `RecompileStats`. |
| `SUYU_RECOMP_STRICT` read in emitted code | `arm64_to_c.h:7374` (v) | Generated runtime honours strict mode. |
| `kDefaultUnitSourceBytes` / `kDefaultUnitBlocks` | after `kChainBudget`, ~`arm64_to_c.h:357` (v, 2026-10-05) | Generated-unit limits (8 MiB / 5,000 blocks). `EmitProject()` takes `unit_source_limit` and `unit_block_limit`; split condition ~`:5328`. |
| Generated module CMake job pool | ~`arm64_to_c.h:5574-5615` (v, 2026-10-05) | `RECOMP_JOBS` from RAM minus 4 GB, 8 GB per cl.exe or 1.5 GB per clang compile; `SUYU_RECOMP_JOBS`, `SUYU_RECOMP_JOB_MEMORY_MB`, `-DRECOMP_JOBS` overrides; Ninja pool `recomp_compile`. |
| AdvSIMD shift-by-immediate emitters | ~`arm64_to_c.h:1719-1830` (v, 2026-10-05) | `SSHR`/`USHR`/`SHRN` at ~`:1795`. `SHL` (vector, immediate) is implemented in the non-saturating and dedicated emitters but gated by `g_translate_all` for Hybrid exports (see `OLD_HISTORY.md`). |
| FPX1 runtime text and emitters | `arm64_to_c.h:FpxH()` / `FpxH64()` ~`:6416` / `:6627`; `EmitFPNativeValue` / `EmitFPNativeSuffix` ~`:492` / `:505` (v) | `RECOMP_FPX_OPEN` uses `RECOMP_FPX_FPCR_MASK 0x06C8FF07`; `RECOMP_FPX_FZ` passes guest FZ to `recomp_fpx_<op>{32,64}(..., int fz, ...)` and applies the FZ-safe keep rules. |
| Shared exact FP body generators | `arm64_to_c.h:600`, `:645`, `:761`, `:841-924`, `:952-978`, `:7400-7412`, `:8354-8364` (v, 2026-10-09) | `EmitFPAddSubBody`, `EmitFPDivideBody`, `EmitFPMulAddBody`, step/SIMD/sqrt helpers, `kFpeOps`, `FpeH`/`FpeRuntimeC`, and `RuntimeH`/`RuntimeC` produce one exact FPX1 body per module when `SUYU_AOT_FP_SHARED` is enabled. |
| `g_emit_fpx_vec` / `EmitFpxVec()` and vector FADD/FSUB emitter | `arm64_to_c.h:359-361`, `:2399-2403` | FPXV is enabled only with FPX1 and outside shadow mode; 4S/2S vector FADD/FSUB first try `recomp_fpx_vaddsub32`. |
| `FpxVecH()` / `RuntimeH()` cache key | `arm64_to_c.h:7425-7486` | Inserts the SSE2 all-lane helper into generated runtime text; the runtime variant is selected by base, FP_SHARED, and FPXV. |

### 2.2 Exporter (Qt)

`src/suyu/game_export.cpp` (about 7,700 lines) and `game_export.h` (about 300 lines).

| Persistent AOT cache | Location | Purpose |
|---|---|---|
| Cache key, layout, lock, staging, publish | `src/suyu/aot_export_cache.h`, `src/suyu/aot_export_cache.cpp` (v, 2026-10-07) | Standard-library-only authoritative entry naming/layout (`aot_export_cache/<title>_<platform>_<backend>_<split\|all-static>`), producer record, non-waiting per-entry lock, interrupted-publish recovery, validation, atomic replacement, and size reporting. |

| Symbol | Location | Purpose |
|---|---|---|
| `GameExportDialog` | `game_export.h:44` | Export dialog and state. |
| FPXV option, manifest, and bundle identity | `game_export.cpp:3507-3546`, `:4235-4247`, `:4434-4478`, `:6282` | Parses `SUYU_AOT_FPX_VEC`, records `fpx_vec`, checks it on cache reuse, and adds it to split bundle identity only when enabled. |
| Chain-start option and image budget | `game_export.cpp:4258-4269`, `:4989-4992` | Default-on `SUYU_AOT_CHAIN_START` (`=0` disables), requires direct chaining, warns only for explicit `=1` when direct chaining is off, and refuses projected per-image size over the 2 GiB safety margin. |
| `TargetPlatform`, `RecompileBackend` enums | `game_export.h:134`, `:140` | Windows/Linux; `SuyuStatic` / `Hybrid` / `Dynarmic`. |
| `FindBestCmakeExecutable()` | `game_export.cpp:2931` (v) | Finds CMake on PATH, then in VS-bundled locations. |
| `WantsCompiledOutput()` | `game_export.cpp:3139` (v) | Build (compile) versus Source-only output. |
| `RunAotPrecompile()` | `game_export.cpp:3355` (v) | Block discovery and translation orchestration. |
| `AotBackendName()` / `ResolveAotCacheTarget()` | `game_export.cpp:3477`, `:3846` (v, 2026-10-07) | Share one backend name between manifest/key and resolve the test-driven or user-data cache target. |
| `CopyCacheArtifacts()` / `PublishAotCacheEntry()` | `game_export.cpp:3877`, `:3906` (v, 2026-10-07) | Define the single artifact list used for publish and reuse; publish a valid entry after AOT precompile and before packaging. |
| `VerifyCachedLauncher()` | `game_export.cpp:3799` (v, 2026-10-07) | Require and verify manifest launcher SHA-256 and size before reuse. |
| `recomp_modules.cmake` / `recomp_registration.c` generation | `game_export.cpp:3950-4030` (v: `:3991`, `:4019`) | Module list in NSO load order and `suyu_recomp_static_modules_v4()` registration table. **The object involved in the earlier `LNK1181` failure is built from this file.** |
| `Common::EnvFlagDefaultOn`, `Common::ParseEnvChoice` / `EnvChoice`, `ParseEnvIntInRange` | `src/common/env_option.h:16-59` **(v, 2026-10-20)** | Single definition for default-on flags, validated default/0/1 environment choices, and bounded integer parsing. |
| Split option and bundle identity | `game_export.cpp:3416-3551`, `:4632`, `:6886-6890` (v, 2026-10-09) | Resolves `SUYU_AOT_SPLIT_MAIN` once per export; unset/empty defaults to binding `main` as a DLL wherever supported, `0` disables it, `1` requests it explicitly, and other values are errors. Computes the versioned canonical bundle id, emits registry-v1/topology data, and logs each image's PE `SizeOfImage`; split cache and package records include topology, bindings, DLL hashes, and launcher size. |
| Prebuild DLL-bound module validation | `src/suyu_cmd/recomp_modules/CMakeLists.txt:26-126` (v, 2026-10-06) | Reads topology DLL bindings, checks version, bundle ID/bytes, module membership, target existence/type, and effective file name, then builds the shared image with identity definitions. |
| Shared DLL target helper | `src/suyu_cmd/recomp_modules/RecompDllTarget.cmake:4-17` (v, 2026-10-06) | Shared verification/setup helper used by prebuild and in-tree CMake; checks topology-bound target identity and writes DLLs to the launcher-side `recomp_dll/` directory. |
| Generated-tree CMake configure/build | around `game_export.cpp:4229` (v) | Invokes CMake on the generated tree. |
| Pruning of registration entries | around `game_export.cpp:4701` (v) | Comment on modules dropped from the registration. |
| `PackageNativeExport()` | `game_export.cpp:5960` (v) | Stages the final export package. |
| `ParseSplitMainOption()` / `ResolveSplitMainOption()` | `game_export.cpp:3432`, `:3805` (v, 2026-10-06) | Parse the per-export split option and resolve it with a test-driven refusal status. |
| `IsKnownFailStage()` / `ShouldInjectFailure()` | `game_export.cpp:3786`, `:3794` (v, 2026-10-06) | Validate and apply test-driven clang-route export failure injection. |
| `ReadCachedSplitExport()` | `game_export.cpp:3633` (v, 2026-10-07) | Reads a completed split cache record; its reuse path is now reachable through the persistent cache. |
| Gap-seeded discovery | around `game_export.cpp:220`, `3220-3330` | Reads `<user dir>/recomp/gaps/<title>.json` to seed blocks at Hybrid gap addresses. |
| Runtime DLL list | around `game_export.cpp:193-200` | FFmpeg, DXC, OpenSSL, and Discord RPC copied into packages. |
| Unit-limit environment overrides | `ReadPositiveSizeFromEnv()` ~`game_export.cpp:124`; read before `EmitProject()` ~`:3828` (v, 2026-10-05) | `SUYU_RECOMP_UNIT_BLOCKS`, `SUYU_RECOMP_UNIT_KB`. |
| FMI export switch and manifest/cache rule | `game_export.cpp:3460-3469`, `:3548-3570`, `:4977` (v, 2026-10-09) | `SUYU_AOT_FM_INLINE` is on unless exactly `0`; manifests record `fm_inline`, and cache reuse requires a matching value (missing field means FMI off). |
| Direct/indirect chain flags and manifest/cache identity | `game_export.cpp:3492-3528`, `:4172-4181`, `:4243`, `:4330-4335`, `:4396-4397`, `:6197-6198` (v, 2026-10-09) | `SUYU_AOT_DIRECT_CHAIN`/`SUYU_AOT_INDIRECT_CHAIN` are on unless exactly `0`; both fields affect generated code, `ComputeBundleId` inputs, manifest output, and reuse checks. |
| Cross-chain option and manifest/cache wiring | `src/suyu/game_export.cpp:4268-4290`, `:5167` (v, 2026-10-12) | `enabled("SUYU_AOT_CROSS_CHAIN")` is default-on; enforces FM1/indirect-chain/split prerequisites, sets XMOD1, and records `cross_chain` in registry/cache identity. |
| clang-cl module build and launcher link | ~`game_export.cpp:4400-4560` (v, 2026-10-05) | Configures `<build-dir>/recomp_clang`, builds `recomp_static_<module>`, then reconfigures the source build tree with `SUYU_CMD_RECOMP_DIR` etc. and links `suyu-cmd-static` at `--parallel 2`. Running from a source build therefore rebuilds and pollutes that tree. |
| Recorded compiler name | `last_recomp_compiler`, ~`game_export.cpp:4560`, `:4790`, `:4985`, `:5370` | Known defect: reports record `O2` instead of the compiler. |

Main window hooks: the export menu is wired at `src/suyu/main.cpp:1739` and `:1788` (v). `SUYU_RECOMP_DIR` auto-loads a recompiled image at `main.cpp:746-782` and `:2109` (v).

### 2.3 Recompiled-code runtime

`src/core/arm/recomp/`:

| File | Purpose |
|---|---|
| `arm_recomp.h/.cpp` (~2,400 lines) | `ArmRecomp : ArmInterface`. Block lookup/dispatch loop, context marshalling (`GuestContextView`, `RecompHostMem` with static_assert-locked offsets), registration hooks (`SetRecompLookup:52`, `SetRecompBindCallback:130`, `BindRecompProcess:135`, `SetRecompPrepareCallback`, `SetRecompBaseSetter`, `PrepareRecompProcess`). |
| `src/core/arm/recomp/arm_recomp.h:176`, `arm_recomp.cpp:750` **(v)** | `Core::GetRecompModules()` returns a thread-safe copy of the recompiled module map for overlay diagnostics. |
| `src/core/arm/recomp/recomp_thread_time.h:21` **(v)** | `RecompThreadTimeLedger` records per-guest-thread `RunThread` wall time for the opt-in diagnostic. |
| `src/core/hle/kernel/guest_time_ledger.h` | `GuestTimeLedger`: `BeginRun`/`EndRun`/`AddIdle`/`TakeReport`, single-writer per core. (v, 2026-10-11) |
| `src/common/cpu_budget.h` | OS-free CPU-budget math: frame interval recording, thread matching/window aggregation, percentiles, report formatting, and window parsing. (v, 2026-10-19) |
| `src/core/cpu_budget_sampler.{h,cpp}` | Windows process/thread CPU sampler using Toolhelp32, `GetThreadTimes`, `GetThreadDescription`, and `GetProcessTimes`; owns the `CpuBudget` sampler thread. (v, 2026-10-19) |
| `src/core/perf_stats.{h,cpp}` | `PerfStats` game-frame recording hook (`EnableGameFrameRecording`/`TakeGameFrameWindow`) used by CPU-budget diagnostics. (v, 2026-10-19) |
| `tests/recompiler_smoke/cpu_budget_unit.cpp` | Standalone CPU-budget math and formatting unit, registered as `smoke_cpu_budget_unit`. (v, 2026-10-19) |
| `src/core/host_timing_wait.h:20-130` **(v, 2026-10-20)** | `PlanHostTimingWait` splits long waits into an OS-blocked portion and a two-resolution fine-grained tail; `HostTimingWaitStats` reports wait lengths, wake lateness, OS-blocked, and signalled counts. |
| `src/core/host_timing_wait.h:132-176` **(v, 2026-10-20)** | `HostTimingEventStats` records callback counts and average microseconds by event name for the opt-in HostTiming report. |
| `src/core/core_timing.cpp:250-255` **(v, 2026-10-20)** | `CoreTiming::RecordEventStats` records callback count and duration when `SUYU_HOST_TIMING_STATS=1`. |
| `src/core/core_timing.cpp:347-382` **(v, 2026-10-20)** | `CoreTiming::WaitForNextEvent` applies the HostTiming plan, using `WaitUntil` for the OS portion and `WaitFor` for the tail. |
| `src/hid_core/resource_manager.cpp:49,56-62` **(v, 2026-10-20)** | `kDefaultNpadUpdateMs`/`NpadUpdatePeriod` set the 4 ms Npad default and parse the validated `SUYU_HID_NPAD_UPDATE_MS` override. |
| `src/common/thread.cpp:177-259` **(v, 2026-10-20)** | `Event::WaitFor` returns false on timeout and true only after consuming a signal across WAITPKG/MONITORX and fallback paths. |
| `tests/recompiler_smoke/host_timing_wait_unit.cpp:24-122` **(v, 2026-10-20)** | Standalone HostTiming wait-split/statistics unit with negative controls; registered as `smoke_host_timing_wait_unit`. |
| `src/core/hle/kernel/physical_core.cpp` | `kGuestTime`, `SteadyNowNs`, `BeginRun` + report around `interface->RunThread` in `PhysicalCore::RunThread`, `EndRun` after the halt reason is known, `AddIdle` in `PhysicalCore::Idle`; verify current lines (about :138-143, :250-262, :283, :396-402). (v, 2026-10-11) |
| `src/core/hle/kernel/physical_core.h` | Member `m_guest_time`. (v, 2026-10-11) |
| `src/core/arm/recomp/recomp_sharded_counters.h:17` **(v, 2026-10-10)** | `RecompShardedCounters` provides per-thread cache-line-aligned slots, single-writer increments, and aggregate counter reads for dispatch-path diagnostics. |
| `src/core/arm/recomp/recomp_sharded_counters.h:32` **(v, 2026-10-11)** | `RecompShardedCounters::Add` adds an amount to a selected counter slot. |
| `src/core/arm/recomp/recomp_lookup_stats.h:20-27` **(v, 2026-10-11)** | `DispatchReturn` and `ClassifyReturn` classify dispatcher exits; `RecompLookupStats` begins at line 38, with per-thread windows, module indexes, and top exit pairs. |
| `src/core/arm/recomp/recomp_pc_description.h` **(v, 2026-10-21)** | `RecompModuleSnapshot` and `DescribeRecompPc` format a guest PC as its recompiler module and offset when the snapshot contains it. |
| `src/core/arm/recomp/recomp_exit_targets.h:21` **(v, 2026-10-11)** | `RecompExitTargets` records per-thread exit targets in bounded hash tables and reports top targets, overflow, and rebase state. |
| `src/core/arm/recomp/arm_recomp.cpp:403-409, 345, 438` **(v, 2026-10-11)** | `kLookupStats`/`kLookupTargets` parse diagnostic flags; `g_module_generation` and `CurrentModuleSnapshot` provide per-thread module snapshots for dispatch-loop reports. |
| `src/core/arm/recomp/arm_recomp.cpp:279-305`, `:1725`, `:1972-1976` **(v)** | `kThreadTime`, `ThreadTimeScope`, one ledger per `Impl`, and the scope at the top of `ArmRecomp::RunThread`; enabled only by `SUYU_RECOMP_THREAD_TIME=1`. |
| `src/core/arm/recomp/arm_recomp.cpp:221-245, 815-817, 1172-1176, 2466-2467` **(v, 2026-10-10)** | `kShortSliceChainBudget`/`kLongSliceChainBudget`, startup `kChainBudgetOverride`, `RecompChainBudget()`, `SetRecompLongSlices()`, and AOT `HostReadCntpct()`; the override reads `SUYU_RECOMP_CHAIN_BUDGET` once (valid 1-8192), and CNTPCT uses `CoreTiming().GetClockTicks()`. |
| `src/core/arm/recomp/recomp_exclusive_stats.h:20-22` | `RecompExclusiveStats` for opt-in callback timing. |
| `src/core/arm/recomp/arm_recomp.cpp:423-430, 942-949, 1041-1067, 2204-2208` | `kExclusiveStats`, timed bridge wrappers (`HostExclusiveLoadTimed` and peers), and the `excl-stats` report hook. |
| `src/core/arm/recomp/recomp_access_stats.h` | `RecompAccessStats`: `Reason`, `Classify` (tagged, out_of_range, unmapped, debug, rasterizer, page_cross, watched, other), `Count`, `TakeReport`; attribute bits are pinned to `Common::PageType` by a `static_assert` in `arm_recomp.cpp`. (v, 2026-10-16) |
| `src/core/arm/recomp/recomp_access_sites.h` | `RecompAccessSites`: per-thread tables keyed by three host return addresses, `Count`, `Rebase`, `TakeTop`. (v, 2026-10-16) |
| `src/core/arm/recomp/recomp_host_frames.h`, `.cpp` | `CaptureCallerFrames(anchor, out, count)` and `DescribeHostAddress`; listed in `src/core/CMakeLists.txt`. (v, 2026-10-16) |
| `src/core/arm/recomp/arm_recomp.cpp` (access diagnostics) | `kAccessStats`, `kAccessSites`, `ClassifyAccess`, `NoteAccessSite`, `HostLoadCounted`, `HostStoreCounted` (installed in `bridge.load/store` only when enabled) and the periodic report block next to the exclusive-stats report. (v, 2026-10-16) |
| `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp:27-140` | `EmitSpinLockLock/Unlock`, TTAS branch, shared lock/unlock stub, and `SpinLockUsesTtas` (`SUYU_SPINLOCK_TTAS=0` selects legacy); the inline JIT uses the same emitter at `backend/x64/emit_x64_memory.h:353-363` but keeps its existing call. The global monitor lock is the single `SpinLock lock` member of `Dynarmic::ExclusiveMonitor`. (v, 2026-10-16) |
| `src/core/arm/dynarmic/dynarmic_exclusive_access.h:23-37` | `ReadMarked` lock-free parameter and dispatch to `ReadAndMarkLockFree`. |
| `src/core/arm/dynarmic/dynarmic_exclusive_monitor.cpp:4-8,21-24,62-69` | `kLockFreeMarks` uses `Common::EnvFlagDefaultOn` (default-on; `SUYU_EXCL_LOCKFREE_MARK=0` disables) and feeds reservation read wiring. |
| `src/dynarmic/src/dynarmic/interface/exclusive_monitor.h:70-80` | `ReadAndMarkLockFree`; stores retain saved-value comparison. |
| `src/dynarmic/src/dynarmic/backend/x64/exclusive_monitor.cpp:19-27, 45-68` | Atomic reservation slot helpers `LoadSlot`/`StoreSlot` and locked slot accesses. |
| `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp:27-42,105-140` | `EmitSpinLockLock` TTAS stub branch and shared-stub option/reporting (`SpinLockUsesTtas`). |
| `src/core/arm/dynarmic/dynarmic_exclusive_monitor.h/.cpp` | Dynarmic exclusive-monitor fast path: resolves plain-page host pointers before the monitor lock, retains the locked Memory path for special/cross-page accesses, and conditionally reports writes. |
| `src/core/arm/native_exclusive_state.h` **(v, 2026-10-21)** | Dynarmic-free reservation state: per-core cache-line-separated slots, TTAS locking, lock-free or locked read-and-mark, locked store callbacks, and host load/CAS/store helpers for the standalone monitor. |
| `src/core/arm/standalone_exclusive_monitor.h/.cpp` **(v, 2026-10-21)** | Dynarmic-free exclusive-monitor glue; resolves plain-page host pointers before the native state operation and retains locked `Memory` accesses for other pages. |
| `src/core/memory.h/.cpp` | `Memory::GetPlainExclusivePointer` (`memory.h:353`, `memory.cpp:1001`) and `Memory::NoteExclusiveWrite` (`memory.h:360`, `memory.cpp:1005`) support the lock-light U1 path. |
| `src/dynarmic/src/dynarmic/backend/x64/emit_x64_memory.cpp.inc` | `EmitExclusiveReadMemoryInline`/`EmitExclusiveWriteMemoryInline`: inline spin lock, reservation record, fastmem access, `lock cmpxchg`, no write notification. (v, 2026-10-11) |
| `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp:27-42,105-140` | `EmitSpinLockLock` and the shared x64 lock stub emitter; TTAS is host-stub-only and selected by `SUYU_SPINLOCK_TTAS`. |
| `recomp_bundle.h/.cpp` | Production v1 bundle loading and transactional DLL validation (`LoadRecompBundleV1:444`, `BindRecompBundleToInventory`, `ApplyRecompBundleBases`, `GetRecompExecutableDirectory`); used by the v1 launcher and smoke tests. |
| `recomp_index_view.h` | `src/core/arm/recomp/recomp_index_view.h:18-82` (v, 2026-10-13) | `RecompIndexView` and `AcquireRecompIndexView` prefer a valid 32-bit export, then legacy 64-bit, then the caller's `lookup`, with decline notes. |
| `DescribeRecompBlockIndexes` | `src/core/arm/recomp/recomp_bundle.cpp:652-680` and `src/suyu_cmd/suyu.cpp:2233-2235` (v, 2026-10-13) | Reports served, declined, absent, and out-of-range module indexes after bundle bases are applied. |
| `main.cpp` index view use | `src/suyu/main.cpp:7101-7162`, `:7538-7549`, `:7661-7692` (v, 2026-10-13) | Binds image index callbacks and uses `RecompIndexView::Covers`/`BlockAt` at both dispatch sites. |
| `SuyuRecompXmodTableV1` / `xmod_v1` | `src/core/recompiler/recomp_registry_v1.h:39,57-63,82` (v, 2026-10-11) | XMOD1 export name, immutable table layout, and registry-v1 image callback. |
| `index32_v1` registry member/emission | `src/core/recompiler/recomp_registry_v1.h:76` and `src/core/recompiler/recomp_topology.h:342-343` (v, 2026-10-13) | Optional last registry member and generated extern/initializer for every static image. |
| `PublishRecompXmod` / `UnpublishRecompXmod` | `src/core/arm/recomp/recomp_bundle.h:124-129`, `recomp_bundle.cpp:580-648` (v, 2026-10-11) | Verify and publish one host table to every image, or clear image tables on refusal; unpublish while guest threads are stopped. |
| `CrossModule1` / `kLegacyLoaderSupported` | `src/core/arm/recomp/recomp_image_features.h:30-37` (v, 2026-10-11) | XMOD1 feature bit `0x8`, host-supported features, and the legacy-loader mask that excludes XMOD1. |
| `ActiveRecompMissHookName` | `src/core/arm/recomp/arm_recomp.h:143`, `arm_recomp.cpp:892` (v, 2026-10-11) | Names the active force-miss or main-hole hook that suppresses XMOD1 publication. |
| `recomp_bridge_required.h` | Shared required host-bridge callback X-macro list and first-missing-callback helper (v, 2026-10-07). |
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
- Chain budget: defaults `kShortSliceChainBudget = 32` and `kLongSliceChainBudget = 256`, `arm_recomp.cpp:221-222` (v, 2026-10-10). `SetRecompLongSlices()` selects between them at `:815-817`; `RecompChainBudget()` is at `:242-245`; `kChainBudgetOverride` reads `SUYU_RECOMP_CHAIN_BUDGET` once at startup at `:232-239` (valid 1-8192). AOT `HostReadCntpct` is at `:1172-1176`.
- `RecompGuardGen::OnJitFallback()`, `recomp_guard_gen.cpp:481` (v, 2026-10-05), pins every module to verify-always the first time a JIT fallback is created. Pin causes are logged by `EnsureGuardGenPinLogger()` in `arm_recomp.cpp` (~`:815`). The cited design document (`diagnostics/suyu-guard-generation-20260923/DESIGN.md`) is not in this repository.
- Bound split processes log each core's canonical inventory at `arm_recomp.cpp:1839` and one process-scoped pre-relocation module list at `:2003`; these are diagnostics only and do not change execution behavior.
- Exported packages write `user/log/recomp_coverage.txt` (static/JIT transitions, unimplemented opcodes, SVC counts) and `recomp_gaps.json`.
- `suyu_cmd` sets `SUYU_RECOMP_STRICT` explicitly, at `src/suyu_cmd/suyu.cpp:1351-1359` (v).

### 2.4 End-to-end flows (*inference* from survey; verify before relying on details)

**Export:** ExeFS extraction → NSO parsing and block discovery (optionally seeded from gap JSON) → `EmitProject()` per module → top-level `CMakeLists.txt`, `recomp_modules.cmake`, `recomp_registration.c` → (Build output only) CMake configure/build with clang-cl or MSVC → link against the export build kit into a launcher → `PackageNativeExport()`.

**Run:** load NSO modules → `PrepareRecompProcess()` via frontend callback → ABI 6 handshakes (fastmem/guard/FPX) → `SetRecompLookup()` / base setter → `KProcess::InitializeInterfaces()` creates `ArmRecomp` → loop of `lookup(pc)` → block → SVC/interrupt handling. On a miss, either JIT fallback (Hybrid) or fatal (strict).

## 3. Core runtime (`src/core`, excluding recomp)

| Area | Key locations |
|---|---|
| `System` | `core/core.h:145`. Boot in `core/core.cpp`: `InitializeKernel` (~:263), `SetupForApplicationProcess` (~:273), `Load` (~:303). The load path reads `ReadNSOModules:375` and invokes `BindRecompProcess:386` before process publication. |
| Boundary inventory (N0) **(v, 2026-10-21)** | `src/core/boundary_inventory.h`, `src/core/boundary_inventory_report.h`, `src/core/boundary_inventory_scan.h`; hook sites: `physical_core.cpp` `RunThread`/`RecordSvcCaller`, `service.cpp` `InvokeRequest`/`InvokeRequestTipc`, `nvdrv.cpp` `Module::Open`/`Close`/`Ioctl1-3`, `audio_renderer_manager.cpp` `OpenAudioRenderer`, `audio_renderer.cpp` `RequestUpdateAuto`, `service/am/service/library_applet_creator.cpp` `ILibraryAppletCreator`, `fsp_srv.cpp` opens, `fs_i_filesystem.cpp` `OpenFile`, `fs_i_file.cpp` `IFile::Read`, `fs_i_storage.cpp` `IStorage::Read`, `core.cpp` `ShutdownMainProcess`; `StashIpcCaller`/`RecordIpcCaller` in `boundary_inventory.h` are hooked at `RecordSvcCaller` and `RecordBoundaryIpc`; `ScanInventoryModules` in `physical_core.cpp` scans modules. |
| `src/core/boundary_inventory_scan.h` **(v, 2026-10-21)** | Bounded printable-string and dynsym scan helpers: `FindDynsym` and `ParseDynsym`. |
| `src/core/native_memory/native_address_space.h` **(v, 2026-10-22)** | Header-only, unconnected `Core::NativeMemory` mapper: `ComputeLayout` ports process-region placement and `AddressSpace` models Stage 1 query, mapping, protection, heap, physical/shared memory, TLS, and stack aliases, with `SetupForIpc` as a fail-fast stub. |
| `tools/structural_gate/structural_gate.py` **(v, 2026-10-21)** | Stdlib-only structural gate with the shared `DENY_LISTS` table, PE import parsing, matched PDB publics, baseline mode, and `--require-absent` mode; N1 uses `--require-absent N1`. |
| Timing / CPU threads | `core/core_timing.h:61` (`CoreTiming`), `core/cpu_manager.h:33` (`CpuManager`). |
| Memory | `core/device_memory.h:19` (guest DRAM backing), `core/memory.h:59` (per-process virtual memory); `Memory::GetPlainExclusivePointer` and `Memory::NoteExclusiveWrite` support the reviewed U1 exclusive-monitor path. |
| CPU interface | `core/arm/arm_interface.h:48` (`ArmInterface`); dynarmic glue in `core/arm/dynarmic/`. |
| Kernel | `hle/kernel/kernel.h:87` (`KernelCore`); `k_process`, `k_thread`, `k_scheduler`, `k_memory_manager`. |
| Services/IPC | `hle/service/service.h:49` (`ServiceFrameworkBase`), `hle_ipc.h`, `server_manager.h`, `sm/`. Major services include `acc`, `am`, `apm`, `audio`, `filesystem`, `hid`, `ns`, `nvdrv` (GPU driver ioctls), and `vi` (display). |
| Loading | `loader/loader.h:164` (`AppLoader`; NSP/XCI/NCA/NSO/NRO/KIP). Canonical NSO inventory comes from `loader/deconstructed_rom_directory.cpp:191-265` and `ReadNSOModules` ~`:334`; NCA/NAX/NSP/XCI loaders forward it. Standalone NSO records its file name in `loader/nso.cpp:242-252`. The launcher registration/DLL loader is `src/suyu_cmd/suyu.cpp:1889-2227` (v1 registry path, static registry v4 path, legacy DLL probe, base binding). `file_sys/patch_manager.h` handles updates. `crypto/key_manager.h` handles keys. |
| v1 launcher path | `src/suyu_cmd/suyu.cpp:1931-1950`, `:2216-2227` (macro-gated `SUYU_RECOMP_REGISTRY_V1`) | Loads and retains a validated bundle, publishes lookup/handshakes, and binds the canonical inventory before `Core::Load` publishes the process. |
| v1 CMake mode | `src/suyu_cmd/CMakeLists.txt:131-275` (v, 2026-10-06) | Cross-checks topology marker/DLL lists, builds in-tree DLLs under `recomp_dll/`, skips DLLs in the prebuilt path, imports the shared runtime only when static modules exist, and leaves all-static mode unchanged. |
| Shared libraries | `src/common` (settings, logging, `string_util` UTF-8 helpers, `fs/` paths), `src/hid_core`, `src/audio_core`, `src/input_common`, `src/network`. |

`src/common/package_policy.h:187` adds `LocalExportExpectation::module_dlls`; `package_policy.cpp:1097-1108` validates the required split DLL basenames and `:1187`, `:1268-1281` rejects collisions or unlisted DLLs.

**Boot sequence (*inference*):** `System::Load` → loader → `CreateApplicationProcess` → `ReadNSOModules` → recomp bind (if registered) → recomp prepare (if registered) → `InitializeInterfaces` chooses the backend → GPU/audio/services setup → `AppletManager` starts the main thread → `System::Run`.

## 4. GPU and shaders (`src/video_core`, `src/shader_recompiler`)

| Area | Key locations |
|---|---|
| GPU front | `video_core/gpu.h:138` (`GPU`), `control/scheduler.h:23`, `control/channel_state.h:35`, `dma_pusher.h`, `gpu_thread.h:105`. |
| Engines | `engines/maxwell_3d.h:56`, `engines/draw_manager.h:20`, `fermi_2d`, `kepler_compute`, `maxwell_dma`, `kepler_memory`. |
| GPU memory | `memory_manager.h:36`. |
| Renderer contract | `rasterizer_interface.h:38`, `renderer_base.h:32`. |
| Backends | `renderer_vulkan/renderer_vulkan.h:40`, `renderer_opengl/renderer_opengl.h:37`, plus null. |
| Vulkan overlay extension | `src/core/frontend/vulkan_overlay.h` and `src/core/frontend/emu_window.h:82` **(v)** | `Core::Frontend::VulkanOverlay`, `VulkanOverlayContext`, and `EmuWindow::GetVulkanOverlay()` provide the optional renderer/window extension point. |
| Vulkan overlay rendering | `src/video_core/renderer_vulkan/renderer_vulkan.cpp:187` **(v)**; `src/video_core/renderer_vulkan/present/window_adapt_pass.cpp:37` **(v)** | `RendererVulkan::InitializeOverlay` owns setup/teardown; `WindowAdaptPass::Draw` records the overlay before ending the render pass. |
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
| `src/suyu_cmd/overlay/debug_overlay.{h,cpp}` | Dear ImGui Vulkan overlay UI: command bar, View menu, Debug window sections, Mods/folders (`BuildModsFoldersWindow` at `debug_overlay.cpp:616` (v, 2026-10-18)), Game section (`BuildGameSection` at `:716` (v, 2026-10-18)), build identity, native status, recompiled CPU, GPU diagnostics, and `BuildPerfHud` (`debug_overlay.cpp:372-390`) with the bold-font fallback. |
| `src/suyu_cmd/overlay/overlay_style.{h,cpp}` | Single source for ImGui overlay scale, theme colors, window rounding, fixed-size scaling, embedded-font loading, and `OverlayBoldFont` (`overlay_style.h:79-92`, `overlay_style.cpp:29-68`). |
| `src/suyu_cmd/overlay/perf_hud.h` **(v, 2026-10-18)** | ImGui/OS-free HUD math, formatting, visibility, `kPerfHudSampleIntervalMs`, `kPerfHudTextAlpha`, and `kPerfHudTextScale` (`perf_hud.h:18-23`). |
| `src/suyu_cmd/overlay/perf_hud_sampler.{h,cpp}` **(v, 2026-10-18)** | Once-per-second, visible-only HUD sampling thread and latest-sample publication; joins on destruction. |
| `src/suyu_cmd/overlay/mods_folders.h:92-194` **(v)** | Bounded mod/cheat scan and background `ModsScanner`, including runtime folder snapshots and error reporting. |
| `src/suyu_cmd/fullscreen_toggle.h:13-73` **(v, 2026-10-18)** | SDL-free F11/Alt+Enter, F12 HUD, and F9/F10 inventory hotkey routes, defaulted `FullscreenKeys::perf_hud`, `kNoScancode`, and hotkey ownership contract. |
| `src/suyu_cmd/overlay/inventory_session.h` **(v, 2026-10-21)** | ImGui/SDL-free segment state, step labels, state line, and `PendingSegmentFiles` retry queue. |
| `src/suyu_cmd/overlay/inventory_host.h` **(v, 2026-10-21)** | Event-thread glue for Mark/Pause/Skip/Write/Label and `Finalize`. |
| `SuyuCmd::AcquireDebugOverlay` | `src/suyu_cmd/overlay/debug_overlay.cpp:154` **(v, 2026-10-18)** | Process-wide overlay acquisition; intentionally never destroys the object because the renderer retains a raw pointer and owns shutdown. |
| `src/suyu_cmd/overlay/overlay_logic.h:96-256` **(v)** | ImGui-free command-bar visibility plus bounded mouse, keyboard, and UTF-8 text-input routing. |
| `src/suyu_cmd/overlay/overlay_actions.h:21-41` **(v)** | Bounded FIFO UI action/status channel bound to the event thread. |
| `src/suyu_cmd/overlay/graphics_settings.h:18-26` **(v)** | Save-only Resolution Scale action rule and hooks. |
| `src/suyu_cmd/overlay/controller_settings.h:23-148` **(v)** | Player 1 controller snapshot, binding rules, and request helpers. |
| `src/suyu_cmd/overlay/rebind_session.h:18-111` **(v)** | Non-blocking five-second controller rebind state machine and hooks. |
| `src/suyu_cmd/resolution_scale_host.h:13-25` **(v)** | Event-thread Resolution Scale host hooks. |
| `src/common/resolution_scale_table.h:14-50` **(v)** | Shared 13-entry Resolution Scale table and enum-order validation. |
| `src/suyu_cmd/emu_window/emu_window_sdl2.h:82-92,156`; `emu_window_sdl2.cpp:306-309,438-466` **(v, 2026-10-18)** | `HasPerfHud`/`TogglePerfHud` virtuals, `ActiveHotkeys`, and F12 key-down/up ownership before game dispatch; also fullscreen routing and overlay event/action/text-input routing. |
| `src/suyu_cmd/emu_window/emu_window_sdl2_vk.h:46-48` **(v)**; `emu_window_sdl2_vk.cpp:40-41,252-286` **(v, 2026-10-21)** | `InstallOverlayModsFolders`, `InstallOverlayFullscreen`, `InstallOverlayInventory`, `InventoryHotkey`, and Vulkan SDL overlay callbacks. |
| `src/suyu_cmd/controller_assign.h:27,135` | `PadBinding` and `Slots`, the controller-slot and Joy-Con assignment definitions shared by automatic assignment and the overlay Controller window. |
| `src/suyu_cmd/overlay/overlay_stats.h` | Frame-time history, guest-PC/module placement, fallback counters/verdict, execution-mode text, and timing helpers. |
| `src/suyu_cmd/overlay/build_identity.{h,cpp}` | Parses packaged `aot_manifest.json` display fields and stores the launcher build identity. |
| `docs/Aevora/IMGUI_OVERLAY.md` | ImGui Vulkan overlay architecture, input routing, UI contracts, tests, and extension guide. |
| `docs/Aevora/N0_INVENTORY.md` **(v, 2026-10-21)** | Supported-path manual check list, aggregate inventory summary, and structural-gate first report. |
| `src/core/boundary_inventory.h:141-171,405-409,637-651` **(v, 2026-10-22)** | `[memory]` recorder helpers `SizeClass`, `RegionName`, `MemorySvcKey`, and `Recorder::RecordMemory`; paused operations go to cumulative `[memory_paused]`, printed in every report. |
| `src/core/hle/kernel/physical_core.cpp:265-274,292-343` **(v, 2026-10-22)** | `ProcessRegions` checks heap, alias, stack, kernel map, then code; `RecordMemorySvc` records inventoried memory SVC argument shapes, regions, sizes, and flags. |
| `src/core/hle/kernel/k_server_session.cpp:516-519`; `k_thread_local_page.cpp:29-30`; `k_shared_memory.cpp:34-36`; `k_process.cpp:1278-1281` **(v, 2026-10-21)** | `[memory]` hook sites for IPC map-alias descriptors, TLS pages, shared-memory creation, and process layout/ranges. |
| `tests/recompiler_smoke/boundary_inventory_unit.cpp:676-826` **(v, 2026-10-21)** | Standalone `[memory]` recorder tests, including size-class and region-end negative controls. |
| `tests/recompiler_smoke/native_memory_unit.cpp` **(v, 2026-10-22)** | `smoke_native_memory_unit`: observed-layout, randomized-layout, mapping/accounting, and capacity checks for the Stage 1 native mapper; 12 header-mutant targets provide negative controls. |
| `docs/Aevora/N2_DESIGN.md` **(v, 2026-10-22)** | N2 native guest memory design, observed seams, FM1 contract, Stage 0 test4 results, risks, and open decisions. |
| `D:\HisuiWakeBuild\diag\n0\merge_memory.py`; `summarize_memory.py` **(v, 2026-10-22)** | Local test4 evidence helpers: merge `[memory]` by sum and cumulative `[memory_paused]` by maximum, then summarize SVC totals and region/state counts. |
| `src/suyu_cmd/emu_window/emu_window_sdl2.h:76-106`, `emu_window_sdl2.cpp:390-466,630-773,810` **(v, 2026-10-18)**; `emu_window_sdl2_vk.{h,cpp}:190-367` **(v)** | `RouteOverlayEvent`/`WaitEvent`/`RunOverlayActions`/`SyncOverlayTextInput`, controller snapshot/request/rebind host methods, and Vulkan SDL overlay translation route overlay input before game dispatch. |
| `src/suyu_cmd/native_status.h` **(v, 2026-10-21)** | Native status title naming: `Aevora Static AOT` with `(JIT-capable)` or `(no-JIT build)` qualifier and JIT-transition suffix only when available. |
| `src/suyu_cmd/native_status.h:35-43`; `native_status.cpp:131-132` **(v, 2026-10-18)** | `NativeStatusSnapshot::guest_memory_used_bytes/guest_memory_total_bytes` and `SampleNativeStatus` application physical-memory sampling. |
| `src/core/frontend/vulkan_overlay.h:12-21`; `src/video_core/renderer_vulkan/renderer_vulkan.cpp:192-201` **(v, 2026-10-18)** | `VulkanOverlayContext::memory_budget`, set from `Device::CanReportMemoryUsage()` for the overlay. |
| Qt configuration tabs | `src/suyu/configuration/` (`configure_dialog.cpp`, `configure_per_game.cpp`, `configure_graphics_extensions.cpp` for `Category::RendererExtensions`). Labels and tooltips come from `src/suyu/configuration/shared_translation.cpp`; `src/qt_common/config/shared_translation.cpp` is a separate copy. |
| `src/qt_common`, `src/frontend_common` | Shared Qt utilities; cross-frontend config, content/firmware/mod/playtime managers. |
| `src/libretro_core` | Libretro core (`SUYU_BUILD_LIBRETRO_CORE`, default OFF). |
| `src/nintendo_library` | `Nintendo::Library` with authentication/purchase-history types. Fork-specific; no confirmed UI use. Review before any release. |
| `src/web_service`, `src/dedicated_room`, `src/yuzu_room_standalone`, `src/network` | Upstream online/room features. |

## 6. Build system and dependencies

- Root `CMakeLists.txt` requires CMake 3.22 and C++20. Main options include `ENABLE_QT`, `ENABLE_SDL3`, `YUZU_USE_BUNDLED_{QT,FFMPEG,SDL3}`, `YUZU_CMD`, `SUYU_BUILD_LIBRETRO_CORE`, `SUYU_NO_JIT`, `YUZU_TESTS`, `YUZU_ROOM`, and `ENABLE_WEB_SERVICE`.
- `externals/CMakeLists.txt:442-465` **(v)** builds the forced-bundled `Aevora::imgui`; `cpmfile.json:120-125` **(v)** pins ImGui v1.92.9b with a SHA-512 hash.
- CPM (`CMakeModules/CPMUtil.cmake` + `cpmfile.json`, about 40 entries such as boost 1.90.0, fmt 12.1.0, openssl 3.6.2, SDL3 3.4.8, sirit v1.0.5, Vulkan 1.4.x, catch2 v3.13.0) is the main acquisition path. The cache defaults to `.cache/cpm` and can be moved with `-DCPM_SOURCE_CACHE`. Archives are extracted with `cmake -E tar` (`file(ARCHIVE_EXTRACT)` fails on non-ASCII entries under a non-UTF-8 ANSI code page). Patching needs Git for Windows (`patch`, found relative to `GIT_EXECUTABLE`); host shaders need `glslangValidator` (`src/video_core/host_shaders/CMakeLists.txt`). `SUYU_USE_LOCAL_EXTERNALS` (`LocalDependencies.cmake`) switches to local checkouts, in which case `.patch/` may not apply.
- `CMakeModules/ExportBuildKit.cmake`: `SUYU_EXPORT_BUILD_KIT=ON` (Windows, MSVC, Ninja, Release only; needs Python 3) produces the export build kit. See `tools/export_build_kit/` (`package.py`, `registry_probe.c`, `README.md`; kit revision string `suyu-aot-kit-abi6-fm1-gg1-fpx1-control-r3` per survey).
- `CMakeModules/EmbedBinary.cmake` generates embedded binary source data for the overlay fonts at build time. The `src/suyu_cmd/CMakeLists.txt:341-344` and `tests/recompiler_smoke/CMakeLists.txt:145-147` loops generate `aevora_overlay_font_latin_*` from `dist/fonts/Roboto/Roboto-VariableFont.ttf`, `aevora_overlay_font_latin_bold_*` from `dist/fonts/Roboto/Roboto-Bold.ttf`, and `aevora_overlay_font_japanese_*` from `dist/fonts/BIZUDPGothic/BIZUDPGothic-Regular.ttf`; `dist/fonts/Roboto/` and `dist/fonts/BIZUDPGothic/` each contain the TTF and its OFL notice.
- Version string: `GenerateSCMRev.cmake` (`HEAD-<hash>-<ref>` or the tag). Note the hard-coded `v0.0.11 (mk8-recomp)` full name above.
- Bundled Qt on MSVC is `AddQt(Eden-CI/Qt 6.11.1)` (root `CMakeLists.txt` ~`:638`), verified on 2026-10-04 as a static build with the `/MD` CRT and without Svg/Test modules.
- Targets (`src/CMakeLists.txt`): `common`, `network`, `core`, `audio_core`, `video_core`, `hid_core`, `input_common`, `frontend_common`, `shader_recompiler`, `nintendo_library`, `dynarmic` (unless `SUYU_NO_JIT`), `suyu`, `suyu_cmd`, `tests`, optional `libretro_core`, `web_service`, and room servers.

Open build questions are tracked in WORK_PLAN Milestone 1: actual fetch success, Qt source and pinning, the dynarmic `mk8-recomp` revision, and whether vcpkg/conan are unused.

## 7. Tests

| Location | Content |
|---|---|
| `src/tests/` | Catch2 `tests` target covering common utilities, core timing, crypto, network, video_core memory tracker, and input calibration. |
| `tests/recompiler_smoke` | Translator golden-hash/smoke tests (synthetic code), including ABI6FMI and GGFMI variants; topology/identity units, production bundle-loading and mixed-image checks, the three-module xmod fixture, loader stub and lookup-boundary cases, `check_prebuild` split consumer checks, and `smoke_env_option_unit`; MSVC golden hashes require `/utf-8`, matching the real build. |
| `tests/recompiler_smoke/overlay_logic_unit.cpp`, `overlay_actions_unit.cpp`, `graphics_settings_unit.cpp`, `controller_settings_unit.cpp`, `build_identity_unit.cpp`, `mods_folders_unit.cpp`, `fullscreen_toggle_unit.cpp` | Standalone tests for overlay command/input, action queue, graphics/controller settings, build identity, mods scanning, and fullscreen; `fullscreen_toggle_unit.cpp` covers F12 and F9/F10; mods/fullscreen are registered at `CMakeLists.txt:124-130` and run at `run.py:908-911` (v). |
| `tests/recompiler_smoke/boundary_inventory_unit.cpp` **(v, 2026-10-21)** | `smoke_boundary_inventory_unit`. |
| `tests/recompiler_smoke/inventory_session_unit.cpp` **(v, 2026-10-21)** | `smoke_inventory_session_unit`. |
| `tests/structural_gate/test_structural_gate.py` **(v, 2026-10-21)** | Structural-gate tests with an independent oracle and mutation coverage. |
| `tests/recompiler_smoke/overlay_style_unit.cpp` | Both embedded font byte streams, theme RGB/alpha rules, and 125% scale checks; each font has flipped-byte and truncated-copy negative controls; registered as `smoke_overlay_style_unit`. |
| `tests/recompiler_smoke/perf_hud_unit.cpp` **(v, 2026-10-18)** | `smoke_perf_hud_unit`: HUD formatting/visibility, core scaling, and failed-CPU-read negative controls. |
| `tests/recompiler_smoke/thread_time_unit.cpp` **(v)** | Standalone `RecompThreadTimeLedger` unit; registered as `smoke_thread_time_unit` and invoked by `run.py`. |
| `tests/recompiler_smoke/exclusive_stats_unit.cpp` | `smoke_exclusive_stats_unit`: exclusive callback timing counters. |
| `tests/recompiler_smoke/pc_description_unit.cpp` **(v, 2026-10-21)** | `smoke_pc_description_unit`: module-and-offset formatting and module-count bounds checks for recompiler diagnostics. |
| `tests/recompiler_smoke/native_exclusive_unit.cpp` **(v, 2026-10-21)** | `smoke_native_exclusive_unit`: reservation semantics, notification wiring, callback mutual exclusion, lock modes, same-value races, and u32/u64/u128 counter stress; six header mutants fail fast. |
| `tests/recompiler_smoke/guest_time_unit.cpp` | `smoke_guest_time_unit`, registered in `CMakeLists.txt:115-117` and `run.py:854`. (v, 2026-10-11) |
| `tests/recompiler_smoke/sharded_counters_unit.cpp` **(v, 2026-10-10)** | Standalone `RecompShardedCounters` unit; registered as `smoke_sharded_counters_unit` and invoked by `run.py`, with a race-free shared-slot negative control. |
| `tests/recompiler_smoke/lookup_stats_unit.cpp` **(v, 2026-10-11)** | Dispatcher-return, module-index, per-window, and exit-pair unit; registered as `smoke_lookup_stats_unit`. |
| `tests/recompiler_smoke/exit_targets_unit.cpp` **(v, 2026-10-11)** | Exit-target hash-table, overflow, rebase, and concurrency unit; registered as `smoke_exit_targets_unit`. |
| `tests/recompiler_smoke/access_stats_unit.cpp`, `access_sites_unit.cpp` **(v, 2026-10-16)** | Access-stats classifier, weakened-classifier negative controls and window report; access-sites table, top-N, overflow and thread checks with a first-frame-only negative control; registered as `smoke_access_stats_unit` and `smoke_access_sites_unit`. |
| `tests/recompiler_smoke/run.py` | The build-identity test requires `nlohmann/json.hpp`; pass `--nlohmann-include`, set `NLOHMANN_INCLUDE_DIR`, or use a `CPM_SOURCE_CACHE` containing `nlohmann_json/*/include`. |
| `tests/recompiler_smoke/lookup_code.h` / `lookup_c.c` | `lookup_code.h:1-15`, `lookup_c.c:1-170` (v, 2026-10-13) | Eight two-instruction blocks with zero-word gaps, signed offset/range and allocation-failure cases, plus exhaustive comparison of `recomp_lookup` and `_lookup_search`. |
| `tests/recompiler_smoke/export.cpp --lookup` | `export.cpp:94-117` (v, 2026-10-13) | Exports the multi-segment lookup fixture with two blocks per generated unit. |
| `tests/recompiler_smoke/CMakeLists.txt` `LOOKUP_DIR` / `smoke_lookup` | `CMakeLists.txt:3`, `:41-51` (v, 2026-10-13) | Builds the generated lookup fixture executable. |
| `tests/recompiler_smoke/run.py` `check_lookup_index` | `run.py:785-797`, target `smoke_lookup` (v, 2026-10-13) | Runs the `--lookup` export and checks the generated index against the segment search. |
| `tests/recompiler_smoke/index_view_unit.cpp` / `index_report_unit.cpp` | `index_view_unit.cpp:116-247`, `index_report_unit.cpp:67-93` (v, 2026-10-13) | Host preference/fallback and launcher report unit tests, including refusal reasons, absent exports, subsets, and bounds checks. |
| `tests/recompiler_smoke` 32-bit lookup fixtures | `lookup_c.c:23-170`, `bundle_host.cpp:312`, `run.py:404,903-904` (v, 2026-10-13) | Generated lookup/offset and real-DLL optional-export cases, including `dll-without-index-export`, plus registered smoke units. |
| `tests/recompiler_smoke/xmod_code.h` | Three-module fixture instruction words and scenario addresses (v, 2026-10-07). |
| `tests/recompiler_smoke/xmod_exec.c` | Cross-image reservation, counter, FPX, guard, bridge-null, and lookup-boundary fixture execution (v, 2026-10-07). |
| `tests/recompiler_smoke/xmod_host.cpp` | Production bundle host for xmod module loading, handshakes, and per-image checks (v, 2026-10-07). |
| `tests/recompiler_smoke/xmod_exec.c` ring cases | `xmod_exec.c:476-582,748-778` (v, 2026-10-11) | Three-module A→B→C→A chain, cross-off behavior, and chained-module guard probe scenarios. |
| `tests/recompiler_smoke/xmod_host.cpp` cases | `xmod_host.cpp:142-249` (v, 2026-10-11) | `cross`, `cross-off`, `cross-refuse`, and `cross-guard` host cases, including table refusal validation. |
| `smoke_xmod_table_unit` | `tests/recompiler_smoke/CMakeLists.txt:137-140`, `run.py:884` (v, 2026-10-11) | Standalone XMOD1 table publication, validation, and unpublish unit. |
| Export AOT cache cross-chain matrix block | `tests/export_integration/run_export_integration.py:941-969` (v, 2026-10-11) | Verifies cross-chain manifest/cache identity, prerequisite handling, export/run, and switching the option off. |
| `tests/recompiler_smoke/bundle_stub_image.c` | Shared-image test stub without the required bundle entry points (v, 2026-10-07). |
| `tests/recompiler_smoke/export.cpp --xmod` | Emits the synthetic static/DLL/static xmod images (v, 2026-10-07). |
| `tests/recompiler_smoke/run.py --only-xmod` | Runs only the xmod export and host scenarios (v, 2026-10-07). |
| `tests/recompiler_fastmem`, `recompiler_fpx`, `recompiler_partition` | ABI 6 feature and translation-unit partitioning tests. Fastmem covers FM1 and FMI legs plus the GG1 watched-page differential executable. `recompiler_fpx` differentially checks FPX1 against the exact body over seven FPCR settings; `fpxs` checks shared exact bodies, and `fpxv`/`fpxvb`/`fpxvz` cover FPXV plus non-finite/FZ negative controls (`driver.c:10-12, 46-48, 105-107`). Partitioning is relevant to the `C1002` investigation. |
| `tests/exclusive_access/run.py:60-68`, `exclusive_access_test.cpp:220-353` | Standalone reservation/CAS test; covers legacy/TTAS byte emission, plain-load probe, exclusion, exact u8/u16/u32/u64/u128 counters, env selection across unset/0/1/2, reservation semantics, upper-half comparison, attempted reporting, and negative controls (v, 2026-10-16). |
| `tests/recompiler_smoke/bl_code.h`, `bl_c.c`, `export.cpp --bl` | Direct/indirect chain execution fixture, including RET/BR middle-block targets, non-zero module bases, budget exhaustion, and BLR x30 ordering. |
| `tests/recompiler_smoke/run.py` | `check_direct_chain:595`, `check_direct_chain_bl:625`, and variants `abi6dc`/`ggdc`/`abi6ic`/`ggdcic` (`:57-64`, `:742-746`). |
| `tests/recompiler_smoke/run.py` | `fpxv`/`abi6fv` variants (`:63-65`), `check_fpx_vec:631`, and invocation of the structural check (`:819`). |
| `tests/export_integration/make_multimodule_fixture.py` | `write_multimodule_fixture()` (v, 2026-10-06) | Generates sparse canonical multi-module ExeFS fixtures with cross-module calls, markers, unreadable paths, and code-less options. |
| `tests/export_integration/run_export_integration.py` | `multimodule_split_scenarios()`, `split_source_failure_scenarios()`, `split_failure_injection_scenarios()`, `split_option_scenarios()` (v, 2026-10-06) | Exercises real split static/Hybrid exports, source/analysis refusals, injected build/package failures, per-export option handling, and retries. |
| `tests/export_integration/run_export_integration.py` | `default_option_scenarios()` and `--only-defaults` (v, 2026-10-09) | Verifies unset default-on FMI/direct/indirect options, default supported split binding, quiet unsupported-default skips, and `=0` all-off/all-static output. |
| `src/suyu/main.cpp` RPC export options | `main.cpp:6173-6179` (v, 2026-10-07) | Test-driven export RPC accepts `aot_cache_root`, `aot_producer_revision`, and `aot_producer_build_id`; status fields are returned around `:5789-5795`. |
| `tests/recompiler_smoke/aot_cache_unit.cpp` | `smoke_aot_cache_unit` (v, 2026-10-07) | Covers naming, producer seam, lock, staging, publish/replace, interrupted-publish restore, launcher requirement, and non-ANSI cache roots. |
| AOT cache backend identity | `src/suyu/aot_export_cache.h`; `tests/recompiler_smoke/aot_cache_unit.cpp` | `BackendName` uses `suyu-static`, `suyu-static-nojit`, and `suyu-hybrid` without underscores; names are part of cache identity. (v, 2026-10-21) |
| `tests/export_integration/run_export_integration.py` | `aot_cache_publish_scenarios()`, `aot_cache_matrix_scenarios()`, `--only-aot-cache`, `--only-aot-cache-matrix` (v, 2026-10-07) | Covers publish/failure preservation/reuse/producer changes/lock and the named artifact, manifest-input, backend/topology, source, and package-exclusion matrix. |
| `tests/export_integration/run_export_integration.py` | `aot_cache_matrix_scenarios()` chain cases (`:702-706`, env-driven scenario `:767-768`) | Mutates direct/indirect manifest fields and restarts suyu with chain environment variables to verify naming, manifest/bundle-id changes, reuse, and refusal when switching off. |
| `tests/export_integration/run_export_integration.py` | default checks `:421-457`; chain-start matrix `:905-941` | Verifies chain-start default-on and `=0` behavior, manifest mutation, direct-chain prerequisite, bundle-id/reuse behavior, split and non-split per-image refusal injection, and successful export without the injected refusal. |
| `tests/recompiler_smoke/chain_start_budget_unit.cpp` | `smoke_chain_start_budget_unit` | Checks estimator boundaries, PLA calibration fit, and separate versus combined image projections. |
| `tests/export_integration`, `export_regression`, `appdata_path`, `package_policy`, `export_build_kit` | Export pipeline, path, and packaging-policy tests; export integration covers AOT-cache publish/reuse and binding mismatches; export build-kit tests reject split modules at configure time; export-regression fixture execution remains blocked by its Qt-root layout lookup. |
| `research/tests/` | Python regression scripts (`test_build_regressions.py`, `test_fixed_point_conversions.py`). |

## N1 build trees

| Tree | Purpose |
|---|---|
| `D:\HisuiWakeBuild\msvc-release` | JIT-capable host/frontend tree. |
| `D:\HisuiWakeBuild\msvc-release-nojit` | `SUYU_NO_JIT=ON` no-JIT host/frontend tree; its frontend exports strict static AOT only. Reconfigure both trees after every commit so embedded `GIT_REV` matches HEAD. |

Local analysis helpers for session 24 live outside the repository in `D:\HisuiWakeBuild\tmp_gt` and `D:\HisuiWakeBuild\diag\hotrva` (see WORK_PLAN).

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
| Exclusive-access overhead (WORK_PLAN Group B) | `spin_lock_x64.cpp`, `interface/exclusive_monitor.h`, `backend/x64/exclusive_monitor.cpp`, `src/core/arm/dynarmic/dynarmic_exclusive_monitor.cpp`, `dynarmic_exclusive_access.h`, `arm_recomp.cpp` (`HostExclusive*`). |
| Host memory callbacks (GPU-tracked pages) | `recomp_access_stats.h`, `recomp_access_sites.h`, `Core::Memory::HandleRasterizerWrite/Download` in `src/core/memory.cpp`. |
| Arena fastmem design record | OLD_HISTORY.md "Session 29 investigation record"; JIT side: `arm_dynarmic_64.cpp` `MakeJit`, `emit_x64_memory.cpp.inc`, `exception_handler_windows.cpp`; host side: `memory.cpp` (`SetCurrentPageTable`, `Protect*`), `common/host_memory.cpp`. |
