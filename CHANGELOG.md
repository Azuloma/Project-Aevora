# Changelog

All notable changes to Aevora are recorded here. Versions follow the scheme in [README.md](README.md#releases-and-versioning): release versions `vX.Y.Z`, development versions `vX.Y.Z-devN`. Development entries are not releases and are not recommended for general use.

## v0.0.1-dev16 - Unreleased

*Development entry 16 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the synthetic `xmod` three-module static/DLL/static fixture covering cross-image state, guards, FPX, and bridge behavior.
- Added the shared required host-bridge callback list and its generated-runtime drift check.
- Added 14 loader and bundle-boundary smoke cases covering feature/handshake refusals, missing pointers/exports, paths, and lookup misses.
- Added an AOT-cache integration case for reuse after a pre-promotion validation failure, preserving the earlier package and cache entry and leaving no staging or cache-root residue.
- Added the reverse module-binding matrix case, rebuilding and republishing when `main` changes from DLL to static.
- Added an export build-kit test that rejects split modules at CMake configure time without creating a build tree.

### Changed
- `ArmRecomp::CheckBridgeComplete` now uses the shared required callback list.
- U4 regression runs completed successfully: the full smoke suite exited 0 with unchanged ABI5/FM1/FPX golden hashes and all listed variants passing; fastmem reported 0 mismatches across 5 fmdiff and 4 GG1 legs; reduced FPX reported 1,786,000 cases with 0 mismatches, and full FPX reported 100,587,904 plus 30,320,000 cases with 0 mismatches.
- The AOT-cache integration run passed 272 tests with 0 failures; the export build-kit package suite passed 14 tests. The host tree was rebuilt successfully with both `suyu.exe` and `suyu-cmd.exe` linked.

## v0.0.1-dev15 - Unreleased

*Development commit 15 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added a persistent user-data AOT cache for Windows Build exports with authoritative entry layout, producer revision/build-id records, per-entry locking, atomic publish/replace, interrupted-publish recovery, and failed-package preservation.
- Added full-match reuse and named mismatch reasons for manifest inputs, split topology, artifacts, and launcher SHA-256/size records, plus test-only cache and producer seams and export status fields.
- Added the cache unit test and publish/reuse/invalidation integration matrix; the final synthetic-fixture run passed 262 tests with 0 failures.

### Changed
- Build packages continue to exclude `aot_cache`; Source-format, non-Windows, and lock-busy exports do not use or publish the persistent cache.

## v0.0.1-dev14 - Unreleased

*Development commit 14 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added a sparse multi-module fixture generator and integration scenarios for split static/Hybrid exports, linked launcher execution, and unchanged all-static output.
- Added a test-driven per-export split option and build-stage failure injection, including tampered or missing DLL packaging checks and rejection reasons in test-driven status.

### Fixed
- Launcher LNK1248 failures no longer block automated runs on a modal dialog; test-driven exports report the rejection while interactive users retain the dialog.

The persistent cache was subsequently implemented in v0.0.1-dev15; this entry records the pre-cache state.

## v0.0.1-dev13 - Unreleased

*Development commit 13 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added exporter, cache, manifest, package-policy, and launcher wiring for the opt-in `SUYU_AOT_SPLIT_MAIN` main-module DLL topology, including a versioned canonical bundle id and registry-v1 marker.
- Added shared PE `SizeOfImage` validation, canonical NSO module names, shared DLL-target verification, per-image manifest metadata, and package staging/verification for split exports.
- Added bound-inventory and pre-relocation INFO diagnostics, plus topology, PE, policy, and prebuild regression coverage; export-regression fixture stubs were updated but could not be run because their Qt-root lookup does not match the CPM Qt package layout.

### Changed
- The v1 launcher and in-tree/prebuilt CMake paths now build and collect split DLLs without importing them into the launcher; all-static v4 output remains unchanged.
- Split exports refuse missing or failed canonical modules, validate cache/package topology and DLL identities, and report image sizes against the 2 GiB Windows x64 limit.

Split export wiring is implemented, but exporter end-to-end execution, linked launcher guest execution, and the remaining Milestone 4a step 4(c) acceptance coverage are not yet verified (part 2).

## v0.0.1-dev12 - Unreleased

*Development commit 12 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the single-definition registry v1 layout and topology writers for mixed static/DLL recompiled bundles.
- Added production bundle loading with transactional DLL handling and identity, ABI, feature, and handshake validation.
- Added inventory-based binding before process publication, including canonical module matching and base application.
- Added host bridge completeness checks for exclusive, pair, clear-exclusive, and counter callbacks.
- Added the v1 launcher path behind `SUYU_RECOMP_REGISTRY_V1`.
- Added production bundle-loading, inventory-binding, and mixed-image execution smoke tests.

Split exports remain disabled until Milestone 4a step 4(c) connects the exporter, packaging, and linked launcher.

## v0.0.1-dev11 - Unreleased

*Development commit 11 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added a single authoritative module-topology header, an image-identity contract, and an opt-in identity export for shared images. The identity contract returns the canonical module name and raw 32-byte SHA-256 bundle digest.
- Added prebuild support for topology-bound DLL modules with build-time checks for topology version, bundle identity, target type, module membership, and effective DLL file name. Split exports are not usable end to end yet; registry v1, launcher loading, and the exporter option are later steps.
- Added fail-fast rejection of split exports in the in-tree launcher configuration without the registry marker and in the export build kit, plus topology, identity, and prebuild consumer smoke tests.

### Changed
- The exporter now writes `recomp_modules.cmake` and the module load order from the shared topology definition. All-static output remains byte-identical to the previous output.

## v0.0.1-dev10 - Unreleased

*Development commit 10 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- A static Build export whose clang-cl link fails with LNK1248 (image over the 2 GB Windows x64 limit) now stops with an explicit error instead of recompiling every module with MSVC, which links the same code into the same single image and was estimated at about 20 hours for PLA's main module. The existing Hybrid retry that routes the largest module to JIT when fallback is enabled and more than one module remains is unchanged; other link failures still fall back to MSVC.
- Renamed the project from HisuiWake to Aevora across the documentation; project documents moved from `docs/HisuiWake/` to `docs/Aevora/`, and `OLD_HISTORY_HW.md` became `OLD_HISTORY.md`.

### Added
- Failed compile and link output (clang-cl and MSVC paths) is appended to `export_build_failure.log` in suyu's log directory, because `suyu_log.txt` rotates on restart and the exporter deletes its work directory. Write and flush errors are checked; the file is truncated before appending once it exceeds 8 MiB. Only the captured tail of each process output (about 0.5-1 M characters) is kept.

### Documentation
- Added the main-module DLL split design plan (Milestone 4a) to the work plan and archived the sixth-session progress entries to `docs/Aevora/OLD_HISTORY.md`.
- Recorded the reviewed Milestone 4a main-module DLL split design note (D1-D7) in the work plan and closed the eighth session.
- Recorded the Milestone 4a step 3 measurement: the PLA main module built as a DLL is 1,459.1 MiB without FMI and 1,783.7 MiB with FMI (SizeOfImage), under the 2 GB image limit, exporting only the image ABI and depending only on CRT/OS DLLs.
- Added the "Modding and QoL readiness" rule to `CLAUDE.md`: recompiler changes must keep mods and QoL additions easy to add, resolve references from one authoritative source, and fail fast on mismatches so a small change cannot break the recompiler tool; the Code reviewer role now checks it.
- Disabled Claude Code auto memory for the project (`.claude/settings.json`) and moved the Orca worker operating notes that existed only in agent memory (spec preamble, wait timeout, worker release and acknowledgement, unsubmitted-spec recovery, re-checking `git status`) into `CLAUDE.md`.

## v0.0.1-dev9 - Unreleased

*Development commit 9 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the opt-in inline FM1 fast path (FMI), enabled by setting `SUYU_AOT_FM_INLINE=1` in the environment of the export. FMI requires FM1, needs a new export, leaves default output unchanged, records `"fm_inline"` in the manifest, and requires cache reuse to match that field; manifests without the field count as FMI off.

### Tests
- Added FMI fast-path and fallback legs to the fastmem differential tests, plus the GG1 watched-page differential executable.
- Added `abi6fmi` and `ggfmi` variants to the recompiler smoke tests; default output remains byte-identical.

### Known issues
- FMI on PLA made the static launcher image about 2.19 GB, over the 2 GB x64 image limit (LNK1248), so FMI cannot currently be used for a PLA static export. After the link failure, fallback-disabled export takes the existing MSVC per-module path (one `cl.exe` at a time on 16 GB hosts, about 20 h for main), also bound to fail; FPS and sampling remain unmeasured. Fixed in v0.0.1-dev10: the export now stops at the LNK1248 link failure.
- Whole-helper inlining was measured worse on MK8D (image size 100 to 222 MB and 14% lower FPS); FMI only inlines the FM1 fast path.
- Per-instruction entry labels that store and reload guest registers through `GuestContext` are not addressed by FMI.

## v0.0.1-dev8 - Unreleased

*Development commit 8 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Recompiled code now uses the native floating-point fast path (FPX1) when the guest runs with flush-to-zero (FPCR.FZ), as Pokemon Legends: Arceus does (FZ set in about 96% of sampled thread switches). Previously FZ closed the fast path, so every covered FP operation ran a software IEEE implementation. Under FZ a native result is kept only when no input is subnormal and the result is a normal number larger than the smallest normal (or, for add/subtract, exactly zero; for multiply/divide/FMA, the existing exact zero-operand cases), so results and FPSR stay bit-identical to the architecture. DN, AHP, FZ16, non-nearest rounding and trap enables still close the fast path. Requires a new export.

### Fixed
- The recompiler smoke suite's golden hashes were stale (they no longer matched after the v0.0.1-dev4 generated-CMake change) and the suite's exporter was built without /utf-8 on MSVC, so its output differed from real exports (which suyu builds with /utf-8) in UTF-8 comments and messages. The exporter test target now builds with /utf-8 and the goldens were recomputed; MSVC and clang-cl builds now give identical hashes.

### Known issues
- PLA static AOT overworld performance was observed at about 25-30 FPS, up from about 16-20 FPS with dev6 static AOT and versus 30 FPS with the JIT baseline, in one user-observed run with zero fallback. The FP differential harness compares against the software implementation on this host; the hardware-golden leg (L5) needs an AArch64 host and was not run.

### Documentation
- Split `WORK_PLAN.md`, archived older history verbatim in `docs/Aevora/OLD_HISTORY.md`, and added a key-facts summary plus archive rule.
- Updated README status to the dev8 evidence and corrected the stale SHL note in `CODEBASE_INDEX.md`.

## v0.0.1-dev7 - Unreleased

*Development commit 7 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- An opt-in FPCR diagnostic, enabled when `SUYU_FPCR_STATS` has any non-empty value other than `0`, tallies the guest FPCR of application threads at each thread switch and logs each newly seen value (decoded RMode/FZ/DN/AHP/FZ16/trap bits) plus a process-wide summary every 65,536 context saves, including how often FPSR.IXC is set. It works with both the JIT and recompiled CPU backends. The diagnostic is off by default; when enabled it takes a lock per context save, so FPS from such runs is not a performance measurement.

### Known issues
- Counts are cumulative per host process; restart suyu between comparisons.
- At most 16 distinct values are tracked; overflow is logged.
- The diagnostic samples context saves, not individual FP operations, so it does not give the per-operation FPX1 gate-open rate.

## v0.0.1-dev6 - Unreleased

*Development commit 6 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Recompiled code now returns to the dispatcher every 256 CPU blocks instead of 4096 on current export images (ABI 6 with `run_slice` enabled). The CPU core answers rescheduling requests sooner, reducing the time other cores spend waiting. In a Pokemon Legends: Arceus Hybrid export this reduced contention in the fiber scheduler and raised overworld FPS from about 14 to 15-20. The override `SUYU_RECOMP_CHAIN_BUDGET` (1-8192) still takes precedence.

### Known issues
- The reduction needs a new export to take effect; existing packages continue to use the 4096-block budget.
- Hybrid performance is still below the JIT baseline (30 FPS). Profiling points at generated-code quality (flag computation, out-of-line memory accesses, context-pointer reloads) as a possible cause; this is not yet confirmed.
- The new default was chosen from environment-variable runs on one game and one PC and has not yet been confirmed in a rebuilt export.

## v0.0.1-dev5 - Unreleased

*Development commit 5 toward v0.0.1. Not a release; no build artifacts are published.*

### Fixed
- Exported launchers whose file name contains characters outside the Windows ANSI code page (for example `é` in "Pokémon" on a Japanese system) exited immediately with "No mapping for the Unicode character exists in the target multi-byte code page". The window title and Discord title are now converted from paths as UTF-8.

### Known issues
- The fix needs a new export to take effect; existing packages cannot be relinked.
- When run from a source build, the exporter leaves export-specific CMake cache entries in the build tree; they must be cleared before packaging an export build kit.

## v0.0.1-dev4 - Unreleased

*Development commit 4 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Hybrid and static exports now compile generated game code in parallel according to available memory and the compiler. Previously the build allowed one compile per 8 GB of RAM, so a 16 GB PC compiled one file at a time (about 3.4 hours of compilation for Pokemon Legends: Arceus). With clang-cl the budget is 1.5 GB per compile with 4 GB reserved (7 parallel compiles on 16 GB); MSVC keeps 8 GB per compile.
- Generated code is split into smaller files (at most 5,000 blocks or 8 MiB each, previously 20,000 blocks or 32 MiB) so that more compiles fit in memory.
- New environment overrides for tuning: `SUYU_RECOMP_JOBS`, `SUYU_RECOMP_JOB_MEMORY_MB`, `SUYU_RECOMP_UNIT_BLOCKS`, `SUYU_RECOMP_UNIT_KB`.

### Known issues
- The new limits are not yet measured on a full export; export time, memory use, and in-game performance still need to be compared with the previous settings.
- The Linux/macOS Source-export build script still limits jobs to one per 8 GB of RAM.
- A Pokemon Legends: Arceus Hybrid export (clang-cl) now builds and links with no module left to JIT, but the resulting launcher (about 1.87 GB) does not start. Under investigation.
- Export reports record the compiler as "O2" instead of the compiler name.

## v0.0.1-dev3 - Unreleased

*Development commit 3 toward v0.0.1. Not a release; no build artifacts are published.*

### Fixed
- Graphics pipeline libraries (Vulkan GPL) are now disabled by default. With GPL enabled, some objects in Pokemon Legends: Arceus rendered with wrong or missing textures, and the affected objects changed after clearing the pipeline cache. The same defect occurs in Suyu v0.0.12.
- The Qt frontend now shows the renderer extension settings (graphics pipeline libraries, Extended Dynamic State, Vertex Input Dynamic State, Sample Shading) in a new GraphicsExtensions tab and in per-game configuration. These settings were previously only editable in the configuration file.

### Known issues
- The underlying GPL defect is not fixed; enabling GPL can still cause rendering errors.

## v0.0.1-dev2 - Unreleased

*Development commit 2 toward v0.0.1. Not a release; no build artifacts are published.*

### Fixed
- `suyu.exe` (Qt frontend) now links with the default bundled static Qt 6.11.1 on Windows x64. The VulkanMemoryAllocator implementation (`video_core/vulkan_common/vma.cpp`) is no longer compiled into `video_core`; each executable compiles it, and the Qt frontend uses Qt's built-in copy when Qt is static (except on Apple). Approach follows Eden PR #2994.

### Known issues
- With static Qt, the Qt frontend uses Qt's VMA 3.2.1 (built with `VMA_DEDICATED_ALLOCATION 0`) instead of the bundled VMA 3.3.0; runtime behavior has not been tested.
- Application startup, game loading, export build kit, and libretro builds have not been tested.
- Export build kits generated before this change must be regenerated.

## v0.0.1-dev1 - 2026-10-04

*Development commit 1 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Agent guide (`CLAUDE.md`) and project subagent definitions (`.claude/agents/`) for AI-assisted development.
- Work plan with milestones, acceptance criteria, and a dated progress record ([docs/Aevora/WORK_PLAN.md](docs/Aevora/WORK_PLAN.md)).
- Read-only codebase survey ([docs/Aevora/CODEBASE_INDEX.md](docs/Aevora/CODEBASE_INDEX.md)).

### Fixed
- Dependency extraction on Windows systems whose ANSI code page is not UTF-8 (e.g. Japanese, code page 932): `CMakeModules/CPMUtil.cmake` now extracts downloaded archives with `cmake -E tar` instead of `file(ARCHIVE_EXTRACT)`, which failed on non-ASCII entry names in the Boost 1.90.0 archive (`Invalid empty pathname`).

### Known issues
- `suyu.exe` (Qt frontend) does not link with the default bundled Qt 6.11.1 on Windows x64: the static Qt build and `video_core` both define VulkanMemoryAllocator symbols (`LNK2005`/`LNK1169`). The resolution is pending.
- `suyu-cmd.exe` builds and links with MSVC 14.51 (Release), but application startup, PLA loading, and game export have not been tested.
- Windows builds require Git for Windows (for `patch`) and the Vulkan SDK (for `glslangValidator`) in addition to the compiler toolchain.
- The build's embedded version string still comes from the inherited `CMakeModules/GenerateSCMRev.cmake` and does not yet reflect Aevora versioning.

### Notes
- Verified host build setup and detailed evidence are recorded in the work plan's progress record.
