# Changelog

All notable changes to HisuiWake are recorded here. Versions follow the scheme in [README.md](README.md#releases-and-versioning): release versions `vX.Y.Z`, development versions `vX.Y.Z-devN`. Development entries are not releases and are not recommended for general use.

## v0.0.1-dev8 - Unreleased

*Development commit 8 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Recompiled code now uses the native floating-point fast path (FPX1) when the guest runs with flush-to-zero (FPCR.FZ), as Pokemon Legends: Arceus does (FZ set in about 96% of sampled thread switches). Previously FZ closed the fast path, so every covered FP operation ran a software IEEE implementation. Under FZ a native result is kept only when no input is subnormal and the result is a normal number larger than the smallest normal (or, for add/subtract, exactly zero; for multiply/divide/FMA, the existing exact zero-operand cases), so results and FPSR stay bit-identical to the architecture. DN, AHP, FZ16, non-nearest rounding and trap enables still close the fast path. Requires a new export.

### Fixed
- The recompiler smoke suite's golden hashes were stale (they no longer matched after the v0.0.1-dev4 generated-CMake change) and the suite's exporter was built without /utf-8 on MSVC, so its output differed from real exports (which suyu builds with /utf-8) in UTF-8 comments and messages. The exporter test target now builds with /utf-8 and the goldens were recomputed; MSVC and clang-cl builds now give identical hashes.

### Known issues
- PLA static AOT overworld performance was observed at about 25-30 FPS, up from about 16-20 FPS with dev6 static AOT and versus 30 FPS with the JIT baseline, in one user-observed run with zero fallback. The FP differential harness compares against the software implementation on this host; the hardware-golden leg (L5) needs an AArch64 host and was not run.

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
- Work plan with milestones, acceptance criteria, and a dated progress record ([docs/HisuiWake/WORK_PLAN.md](docs/HisuiWake/WORK_PLAN.md)).
- Read-only codebase survey ([docs/HisuiWake/CODEBASE_INDEX.md](docs/HisuiWake/CODEBASE_INDEX.md)).

### Fixed
- Dependency extraction on Windows systems whose ANSI code page is not UTF-8 (e.g. Japanese, code page 932): `CMakeModules/CPMUtil.cmake` now extracts downloaded archives with `cmake -E tar` instead of `file(ARCHIVE_EXTRACT)`, which failed on non-ASCII entry names in the Boost 1.90.0 archive (`Invalid empty pathname`).

### Known issues
- `suyu.exe` (Qt frontend) does not link with the default bundled Qt 6.11.1 on Windows x64: the static Qt build and `video_core` both define VulkanMemoryAllocator symbols (`LNK2005`/`LNK1169`). The resolution is pending.
- `suyu-cmd.exe` builds and links with MSVC 14.51 (Release), but application startup, PLA loading, and game export have not been tested.
- Windows builds require Git for Windows (for `patch`) and the Vulkan SDK (for `glslangValidator`) in addition to the compiler toolchain.
- The build's embedded version string still comes from the inherited `CMakeModules/GenerateSCMRev.cmake` and does not yet reflect HisuiWake versioning.

### Notes
- Verified host build setup and detailed evidence are recorded in the work plan's progress record.
