# Changelog

All notable changes to HisuiWake are recorded here. Versions follow the scheme in [README.md](README.md#releases-and-versioning): release versions `vX.Y.Z`, development versions `vX.Y.Z-devN`. Development entries are not releases and are not recommended for general use.

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
