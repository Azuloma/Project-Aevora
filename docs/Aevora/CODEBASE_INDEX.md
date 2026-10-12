# Aevora Codebase Index

Snapshot of the source tree at `36a1a40` (2026-10-11), updated from a read-only survey. It is a navigation aid, not a verified architecture specification.

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
| Local, outside repository: `D:\HisuiWakeBuild\diag` | `buildsuyu.cmd`, `runsmoke.cmd`, and samplers. |

### Lineage markers found in the tree

- `.gitmodules:15` **(v)** pins `externals/dynarmic` to branch `mk8-recomp`.
- `CMakeModules/GenerateSCMRev.cmake:56` **(v)** hard-codes `BUILD_FULLNAME "${REPO_NAME} v0.0.11 (mk8-recomp)"`, although the README names v0.0.12 as the base.
- *Inference*: the recompiler work descends from an `mk8-recomp` fork rather than stock Suyu v0.0.12. Verify this before you describe the lineage publicly (WORK_PLAN Milestone 1).

## 2. Static recompilation pipeline (project-critical)

### 2.1 ARM64 → C translator

`src/core/recompiler/arm64_to_c.h` is a header-only translator of about 7,600 lines. It is the only code generator.

The shared topology and image identity contracts are defined in `src/core/recompiler/recomp_topology.h` (versioned module bindings, load ordering, image names, validation, bundle-byte conversion, and `recomp_modules.cmake` output; `kRecompTopologyVersion:31`, `BuildRecompTopology:160`, `WriteRecompModulesCMake:234`) and `src/core/recompiler/recomp_image_identity.h` (v1 symbol and raw 32-byte bundle digest contract; `RECOMP_IMAGE_IDENTITY_V1_SYMBOL:15`, `RecompImageIdentityV1Fn:24`). Registry v1 is defined once in `src/core/recompiler/recomp_registry_v1.h:64` (`SuyuRecompRegistryV1`, entries, ABI/features, and DLL export-name macros); `WriteRecompRegistryV1C:299` and `WriteRecompTopologyV1Marker:396` in `recomp_topology.h` emit the registry and marker.

`src/core/recompiler/pe_image_size.h:64` parses validated PE32+ headers and reads `SizeOfImage` from files (`:114`), rejecting truncated, zero-sized, or out-of-range headers. `src/core/loader/nso_module_names.h:19` defines the 13-name canonical NSO order shared by the loader, topology, and exporter.

| Symbol | Location | Purpose |
|---|---|---|
| `struct Block` | `arm64_to_c.h:60` (v) | Discovered code block (vaddr, size, count, entry flag). |
| `g_translate_all` | `arm64_to_c.h:303` (v) | Translate forms normally left to the JIT. |
| `g_emit_standalone_runner` | `arm64_to_c.h:310` (v) | Defaults to `true` for synthetic tests. The comment says the game exporter turns it off so game code runs only inside suyu against the user's files. |
| `g_emit_fastmem` / `g_emit_guard_gen` / `g_emit_fpx` / `g_emit_fpx_shadow` | `arm64_to_c.h:316-337` (v) | ABI 6 feature switches (FM1 page-table fastmem, GG1 generation guard, FPX1 exact native FP, FPX shadow instrumentation). FM1 remains on unless `SUYU_AOT_FASTMEM=0`; the other emitter switches retain their existing defaults. |
| `g_emit_fm_inline` / `EmitFmInline()` | `arm64_to_c.h:346-348` (v) | FMI is on unless `SUYU_AOT_FM_INLINE=0`; it is emitted only when FM1 is also enabled. |
| `FastmemInlineH()` / `RuntimeH()` | `arm64_to_c.h:6341`, `:7187-7201` (v) | Appends the FMI static inline fast paths, including the GG1 watch-word store entry, to generated `recomp_runtime.h`; variants are selected with FM1, GG1, and FPX settings. |
| `FmInlineRuntimeC()` / `RuntimeC()` | `arm64_to_c.h:8111`, `:8116-8131` (v) | Renames generated-unit fastmem calls to FMI functions while keeping `recomp_runtime.c` definitions under their original names. |
| `TranslateAllForExport()` | `arm64_to_c.h:340` (v) | Strict static exports translate everything. |
| `kChainBudget` | `arm64_to_c.h:351` (v) | Blocks run before control returns to the dispatcher. |
| `ChainTo` / `ChainIndirect` | `arm64_to_c.h:299-302`, `:409-440` (v) | Default-on direct B/BL and independent indirect RET/BR/BLR tail-chain emitters; set the corresponding environment option to `0` to disable; indirect lookup subtracts the module base and preserves budget on misses. |
| `XmodTypesText` / `EmitCrossChain` / `ChainIndirect` and `recomp_image_xmod_v1` | `arm64_to_c.h:409-419`, `:505-507`, `:6172-6177` (v, 2026-10-11) | XMOD1 generated table types, opt-in cross-module lookup fallback for indirect chains, and the image export. |
| `g_emit_direct_chain` / `g_emit_indirect_chain` | `arm64_to_c.h:367-374` (v) | Independent default-on chain switches controlled by `SUYU_AOT_DIRECT_CHAIN=0` and `SUYU_AOT_INDIRECT_CHAIN=0`. |
| `g_emit_chain_start` / `EmitChainStart()` | `arm64_to_c.h:400-404` | Opt-in chain-start switch; effective only when direct chaining is enabled. |
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
| Chain-start option, cache identity, and image budget | `game_export.cpp:4255-4263`, `:4454-4460`, `:4805-4807`, `:4942-4964`, `:6338` | Reads `SUYU_AOT_CHAIN_START=1`, requires direct chaining, records manifest/cache/bundle identity, and refuses projected per-image size over the safety margin. |
| `TargetPlatform`, `RecompileBackend` enums | `game_export.h:134`, `:140` | Windows/Linux/macOS; `SuyuStatic` / `Hybrid` / `Dynarmic`. |
| `FindBestCmakeExecutable()` | `game_export.cpp:2931` (v) | Finds CMake on PATH, then in VS-bundled locations. |
| `WantsCompiledOutput()` | `game_export.cpp:3139` (v) | Build (compile) versus Source-only output. |
| `RunAotPrecompile()` | `game_export.cpp:3355` (v) | Block discovery and translation orchestration. |
| `AotBackendName()` / `ResolveAotCacheTarget()` | `game_export.cpp:3477`, `:3846` (v, 2026-10-07) | Share one backend name between manifest/key and resolve the test-driven or user-data cache target. |
| `CopyCacheArtifacts()` / `PublishAotCacheEntry()` | `game_export.cpp:3877`, `:3906` (v, 2026-10-07) | Define the single artifact list used for publish and reuse; publish a valid entry after AOT precompile and before packaging. |
| `VerifyCachedLauncher()` | `game_export.cpp:3799` (v, 2026-10-07) | Require and verify manifest launcher SHA-256 and size before reuse. |
| `recomp_modules.cmake` / `recomp_registration.c` generation | `game_export.cpp:3950-4030` (v: `:3991`, `:4019`) | Module list in NSO load order and `suyu_recomp_static_modules_v4()` registration table. **The object involved in the earlier `LNK1181` failure is built from this file.** |
| `Common::EnvFlagDefaultOn`, `Common::ParseEnvChoice` / `EnvChoice` | `src/common/env_option.h:16-39` (v, 2026-10-09) | Single definition for default-on flags and validated default/0/1 environment choices. |
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
| `src/core/hle/kernel/physical_core.cpp` | `kGuestTime`, `SteadyNowNs`, `BeginRun` + report around `interface->RunThread` in `PhysicalCore::RunThread`, `EndRun` after the halt reason is known, `AddIdle` in `PhysicalCore::Idle`; verify current lines (about :138-143, :250-262, :283, :396-402). (v, 2026-10-11) |
| `src/core/hle/kernel/physical_core.h` | Member `m_guest_time`. (v, 2026-10-11) |
| `src/core/arm/recomp/recomp_sharded_counters.h:17` **(v, 2026-10-10)** | `RecompShardedCounters` provides per-thread cache-line-aligned slots, single-writer increments, and aggregate counter reads for dispatch-path diagnostics. |
| `src/core/arm/recomp/recomp_sharded_counters.h:32` **(v, 2026-10-11)** | `RecompShardedCounters::Add` adds an amount to a selected counter slot. |
| `src/core/arm/recomp/recomp_lookup_stats.h:20-27` **(v, 2026-10-11)** | `DispatchReturn` and `ClassifyReturn` classify dispatcher exits; `RecompLookupStats` begins at line 38, with per-thread windows, module indexes, and top exit pairs. |
| `src/core/arm/recomp/recomp_exit_targets.h:21` **(v, 2026-10-11)** | `RecompExitTargets` records per-thread exit targets in bounded hash tables and reports top targets, overflow, and rebase state. |
| `src/core/arm/recomp/arm_recomp.cpp:403-409, 345, 438` **(v, 2026-10-11)** | `kLookupStats`/`kLookupTargets` parse diagnostic flags; `g_module_generation` and `CurrentModuleSnapshot` provide per-thread module snapshots for dispatch-loop reports. |
| `src/core/arm/recomp/arm_recomp.cpp:279-305`, `:1725`, `:1972-1976` **(v)** | `kThreadTime`, `ThreadTimeScope`, one ledger per `Impl`, and the scope at the top of `ArmRecomp::RunThread`; enabled only by `SUYU_RECOMP_THREAD_TIME=1`. |
| `src/core/arm/recomp/arm_recomp.cpp:221-245, 815-817, 1172-1176, 2466-2467` **(v, 2026-10-10)** | `kShortSliceChainBudget`/`kLongSliceChainBudget`, startup `kChainBudgetOverride`, `RecompChainBudget()`, `SetRecompLongSlices()`, and AOT `HostReadCntpct()`; the override reads `SUYU_RECOMP_CHAIN_BUDGET` once (valid 1-8192), and CNTPCT uses `CoreTiming().GetClockTicks()`. |
| `src/core/arm/recomp/recomp_exclusive_stats.h:20-22` | `RecompExclusiveStats` for opt-in callback timing. |
| `src/core/arm/recomp/arm_recomp.cpp:423-430, 942-949, 1041-1067, 2204-2208` | `kExclusiveStats`, timed bridge wrappers (`HostExclusiveLoadTimed` and peers), and the `excl-stats` report hook. |
| `src/core/arm/dynarmic/dynarmic_exclusive_access.h:23-37` | `ReadMarked` lock-free parameter and dispatch to `ReadAndMarkLockFree`. |
| `src/core/arm/dynarmic/dynarmic_exclusive_monitor.cpp:20-24, 60-68` | `kLockFreeMarks` environment option and reservation read wiring. |
| `src/dynarmic/src/dynarmic/interface/exclusive_monitor.h:70-80` | `ReadAndMarkLockFree`; stores retain saved-value comparison. |
| `src/dynarmic/src/dynarmic/backend/x64/exclusive_monitor.cpp:19-27, 45-68` | Atomic reservation slot helpers `LoadSlot`/`StoreSlot` and locked slot accesses. |
| `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp:25-28` | `EmitSpinLockLock` stub emitter. |
| `src/core/arm/dynarmic/dynarmic_exclusive_monitor.h/.cpp` | Dynarmic exclusive-monitor fast path: resolves plain-page host pointers before the monitor lock, retains the locked Memory path for special/cross-page accesses, and conditionally reports writes. |
| `src/core/memory.h/.cpp` | `Memory::GetPlainExclusivePointer` (`memory.h:353`, `memory.cpp:1001`) and `Memory::NoteExclusiveWrite` (`memory.h:360`, `memory.cpp:1005`) support the lock-light U1 path. |
| `src/dynarmic/src/dynarmic/backend/x64/emit_x64_memory.cpp.inc` | `EmitExclusiveReadMemoryInline`/`EmitExclusiveWriteMemoryInline`: inline spin lock, reservation record, fastmem access, `lock cmpxchg`, no write notification. (v, 2026-10-11) |
| `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp:25` | `EmitSpinLockLock` and the shared x64 lock stub emitter used by JIT and the exclusive monitor. |
| `recomp_bundle.h/.cpp` | Production v1 bundle loading and transactional DLL validation (`LoadRecompBundleV1:444`, `BindRecompBundleToInventory`, `ApplyRecompBundleBases`, `GetRecompExecutableDirectory`); used by the v1 launcher and smoke tests. |
| `SuyuRecompXmodTableV1` / `xmod_v1` | `src/core/recompiler/recomp_registry_v1.h:39,57-63,82` (v, 2026-10-11) | XMOD1 export name, immutable table layout, and registry-v1 image callback. |
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
| Backends | `renderer_vulkan/renderer_vulkan.h:40`, `renderer_opengl/renderer_opengl.h:37`, plus null and (per survey) Metal. |
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
| `src/suyu_cmd/overlay/debug_overlay.{h,cpp}` | Dear ImGui Vulkan overlay UI: command bar, Debug window sections, build identity, native status, recompiled CPU, and GPU diagnostics; created by default unless `SUYU_DEBUG_OVERLAY=0`. |
| `SuyuCmd::AcquireDebugOverlay` | `src/suyu_cmd/overlay/debug_overlay.cpp:57` (v, 2026-10-09) | Process-wide overlay acquisition; intentionally never destroys the object because the renderer retains a raw pointer and owns shutdown. |
| `src/suyu_cmd/overlay/overlay_logic.h` | ImGui-free command-bar visibility and bounded cross-thread mouse-input router. |
| `src/suyu_cmd/overlay/overlay_stats.h` | Frame-time history, guest-PC/module placement, fallback counters/verdict, execution-mode text, and timing helpers. |
| `src/suyu_cmd/overlay/build_identity.{h,cpp}` | Parses packaged `aot_manifest.json` display fields and stores the launcher build identity. |
| `src/suyu_cmd/emu_window/emu_window_sdl2.h:75`, `emu_window_sdl2.cpp:1028` **(v)**; `emu_window_sdl2_vk.{h,cpp}:40,198` **(v)** | `EmuWindow_SDL2::RouteOverlayEvent` and Vulkan SDL event translation route overlay-owned mouse input before game dispatch. |
| `src/suyu_cmd/native_status.cpp:110-134` **(v)** | `SampleNativeStatus` publishes the current shader-build count in `NativeStatusSnapshot::shaders_building`. |
| Qt configuration tabs | `src/suyu/configuration/` (`configure_dialog.cpp`, `configure_per_game.cpp`, `configure_graphics_extensions.cpp` for `Category::RendererExtensions`). Labels and tooltips come from `src/suyu/configuration/shared_translation.cpp`; `src/qt_common/config/shared_translation.cpp` is a separate copy. |
| `src/qt_common`, `src/frontend_common` | Shared Qt utilities; cross-frontend config, content/firmware/mod/playtime managers. |
| `src/libretro_core` | Libretro core (`SUYU_BUILD_LIBRETRO_CORE`, default OFF). |
| `src/nintendo_library` | `Nintendo::Library` with authentication/purchase-history types. Fork-specific; no confirmed UI use. Review before any release. |
| `src/android` | Android frontend (upstream lineage). Outside the Windows x64 target. |
| `src/web_service`, `src/dedicated_room`, `src/yuzu_room_standalone`, `src/network` | Upstream online/room features. |

## 6. Build system and dependencies

- Root `CMakeLists.txt` requires CMake 3.22 and C++20. Main options include `ENABLE_QT`, `ENABLE_SDL3`, `YUZU_USE_BUNDLED_{QT,FFMPEG,SDL3}`, `YUZU_CMD`, `SUYU_BUILD_LIBRETRO_CORE`, `SUYU_NO_JIT`, `YUZU_TESTS`, `YUZU_ROOM`, and `ENABLE_WEB_SERVICE`.
- `externals/CMakeLists.txt:442-465` **(v)** builds the forced-bundled `Aevora::imgui`; `cpmfile.json:120-125` **(v)** pins ImGui v1.92.9b with a SHA-512 hash.
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
| `tests/recompiler_smoke` | Translator golden-hash/smoke tests (synthetic code), including ABI6FMI and GGFMI variants; topology/identity units, production bundle-loading and mixed-image checks, the three-module xmod fixture, loader stub and lookup-boundary cases, `check_prebuild` split consumer checks, and `smoke_env_option_unit`; MSVC golden hashes require `/utf-8`, matching the real build. |
| `tests/recompiler_smoke/overlay_logic_unit.cpp`, `build_identity_unit.cpp` | Standalone tests for overlay command/input/statistics logic and AOT manifest build-identity parsing. |
| `tests/recompiler_smoke/thread_time_unit.cpp` **(v)** | Standalone `RecompThreadTimeLedger` unit; registered as `smoke_thread_time_unit` and invoked by `run.py`. |
| `tests/recompiler_smoke/guest_time_unit.cpp` | `smoke_guest_time_unit`, registered in `CMakeLists.txt:115-117` and `run.py:854`. (v, 2026-10-11) |
| `tests/recompiler_smoke/sharded_counters_unit.cpp` **(v, 2026-10-10)** | Standalone `RecompShardedCounters` unit; registered as `smoke_sharded_counters_unit` and invoked by `run.py`, with a race-free shared-slot negative control. |
| `tests/recompiler_smoke/lookup_stats_unit.cpp` **(v, 2026-10-11)** | Dispatcher-return, module-index, per-window, and exit-pair unit; registered as `smoke_lookup_stats_unit`. |
| `tests/recompiler_smoke/exit_targets_unit.cpp` **(v, 2026-10-11)** | Exit-target hash-table, overflow, rebase, and concurrency unit; registered as `smoke_exit_targets_unit`. |
| `tests/recompiler_smoke/run.py` | The build-identity test requires `nlohmann/json.hpp`; pass `--nlohmann-include`, set `NLOHMANN_INCLUDE_DIR`, or use a `CPM_SOURCE_CACHE` containing `nlohmann_json/*/include`. |
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
| `tests/exclusive_access/run.py`, `exclusive_access_test.cpp` | Standalone reservation/CAS test; covers exact u8/u16/u32/u64/u128 counters, reservation semantics, isolation, upper-half comparison, attempted reporting, and a race-free negative control (v, 2026-10-08). |
| `tests/recompiler_smoke/bl_code.h`, `bl_c.c`, `export.cpp --bl` | Direct/indirect chain execution fixture, including RET/BR middle-block targets, non-zero module bases, budget exhaustion, and BLR x30 ordering. |
| `tests/recompiler_smoke/run.py` | `check_direct_chain:595`, `check_direct_chain_bl:625`, and variants `abi6dc`/`ggdc`/`abi6ic`/`ggdcic` (`:57-64`, `:742-746`). |
| `tests/recompiler_smoke/run.py` | `fpxv`/`abi6fv` variants (`:63-65`), `check_fpx_vec:631`, and invocation of the structural check (`:819`). |
| `tests/export_integration/make_multimodule_fixture.py` | `write_multimodule_fixture()` (v, 2026-10-06) | Generates sparse canonical multi-module ExeFS fixtures with cross-module calls, markers, unreadable paths, and code-less options. |
| `tests/export_integration/run_export_integration.py` | `multimodule_split_scenarios()`, `split_source_failure_scenarios()`, `split_failure_injection_scenarios()`, `split_option_scenarios()` (v, 2026-10-06) | Exercises real split static/Hybrid exports, source/analysis refusals, injected build/package failures, per-export option handling, and retries. |
| `tests/export_integration/run_export_integration.py` | `default_option_scenarios()` and `--only-defaults` (v, 2026-10-09) | Verifies unset default-on FMI/direct/indirect options, default supported split binding, quiet unsupported-default skips, and `=0` all-off/all-static output. |
| `src/suyu/main.cpp` RPC export options | `main.cpp:6173-6179` (v, 2026-10-07) | Test-driven export RPC accepts `aot_cache_root`, `aot_producer_revision`, and `aot_producer_build_id`; status fields are returned around `:5789-5795`. |
| `tests/recompiler_smoke/aot_cache_unit.cpp` | `smoke_aot_cache_unit` (v, 2026-10-07) | Covers naming, producer seam, lock, staging, publish/replace, interrupted-publish restore, launcher requirement, and non-ANSI cache roots. |
| `tests/export_integration/run_export_integration.py` | `aot_cache_publish_scenarios()`, `aot_cache_matrix_scenarios()`, `--only-aot-cache`, `--only-aot-cache-matrix` (v, 2026-10-07) | Covers publish/failure preservation/reuse/producer changes/lock and the named artifact, manifest-input, backend/topology, source, and package-exclusion matrix. |
| `tests/export_integration/run_export_integration.py` | `aot_cache_matrix_scenarios()` chain cases (`:702-706`, env-driven scenario `:767-768`) | Mutates direct/indirect manifest fields and restarts suyu with chain environment variables to verify naming, manifest/bundle-id changes, reuse, and refusal when switching off. |
| `tests/export_integration/run_export_integration.py` | chain-start matrix cases `:906-941` | Verifies manifest mutation, direct-chain prerequisite, bundle-id/reuse behavior, split and non-split per-image refusal injection, and successful export without the injected refusal. |
| `tests/recompiler_smoke/chain_start_budget_unit.cpp` | `smoke_chain_start_budget_unit` | Checks estimator boundaries, PLA calibration fit, and separate versus combined image projections. |
| `tests/export_integration`, `export_regression`, `appdata_path`, `package_policy`, `export_build_kit` | Export pipeline, path, and packaging-policy tests; export integration covers AOT-cache publish/reuse and binding mismatches; export build-kit tests reject split modules at configure time; export-regression fixture execution remains blocked by its Qt-root layout lookup. |
| `research/tests/` | Python regression scripts (`test_build_regressions.py`, `test_fixed_point_conversions.py`). |

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
