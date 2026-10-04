# HisuiWake Work Plan

Handoff prepared: 2026-10-04. This is a planning baseline, not a successful-build report.

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop the requested rendering and gameplay improvements on a verified runtime. Keep the code understandable and maintainable by humans, with human review and hands-on testing.

Start with a reproducible Windows x64 build and a correctly running game. Proceed to graphics improvements after the execution path is understood. Fully static PLA compatibility remains uncertain.

## Handoff status

| Item | Evidence/status |
|---|---|
| Source repository | Root contains the Suyu-derived source, build files, dependency metadata, and archived upstream documentation. |
| Git baseline | Local HEAD was `9f932d5` (`Initial commit`); working tree was clean before these handoff files were created. |
| Configured origin | `https://github.com/Azuloma/HisuiWake.git`; remote publication was not independently verified during this handoff. |
| README | English draft describes feasibility status, planned features, support scope, versioning, and the hobby/no-donations policy. |
| License | Root `LICENSE.txt` is copied unchanged from the archived upstream license; README specifies `GPL-3.0-or-later`. Existing notices are retained. |
| Documentation | Project documents belong in `docs/HisuiWake/`; upstream documents are archived in `docs/Suyu/`. |
| Dependencies | Many `externals/` directories are empty. CPM acquisition and exact dependency revisions need verification. |
| Host source build | Not verified in this project during the handoff. |
| PLA export/run | Earlier Hybrid + Build failed. No successful Hybrid gameplay or fully static PLA run has been confirmed. |
| Proposed path patch | An earlier ASCII-name patch was drafted outside this repository. It has not been built or validated and is not an implemented fix. |

The cleanup already removed upstream automation, temporary material, old screenshots, and an isolated `tools/static_recompiler` demo. Do not perform another broad deletion pass before a working baseline identifies the files actually required.

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

## Milestone 1: Establish a reproducible host build

- [ ] Record the checkout revision, Windows version, CPU, RAM, GPU/driver, compiler, SDK, CMake, Ninja, and Qt versions.
- [ ] Verify the source lineage using the README, archived provenance, and upstream revision information. Do not assume the new Git history contains Suyu's original commits.
- [ ] Inspect `CMakeLists.txt`, `CMakeModules/`, `cpmfile.json`, `.gitmodules`, and package configuration to identify the actual dependency acquisition paths and pinned versions.
- [ ] Obtain required dependencies through the build's intended mechanism. Record downloads, patches, versions, and failures; do not substitute arbitrary latest revisions.
- [ ] Configure and build the Suyu-derived application in a local ASCII-only build path. Save the exact commands and options.
- [ ] Build or locate the matching export build kit if required by the selected exporter path. Verify object/library/compiler compatibility.
- [ ] Confirm the application starts and can load PLA through its ordinary JIT path with the user's local inputs. Record the game update/module build IDs and settings without publishing game material or keys.
- [ ] Write the successful setup in `docs/HisuiWake/BUILD.md` when verified. Repair relevant archived documentation links as needed.

**Acceptance:** a documented source build produces a launchable application; required dependencies and the export kit can be reproduced; PLA's JIT behavior is recorded as the comparison baseline. A configure failure or build-only result does not satisfy the gameplay check.

## Milestone 2: Resolve export build failures

- [ ] Inspect `src/suyu/game_export.cpp` and its current generator/build helpers. Reproduce the failure with a fixed source revision, game update, backend, and toolchain.
- [ ] Export Source separately when useful to inspect generated inputs. Distinguish source generation, compilation, linking, packaging, and execution failures.
- [ ] Check whether `recomp_registration.c.obj` exists, where it is written, and which command/response file references it.
- [ ] Trace QString/filesystem conversions, process arguments, working directories, and response-file encoding. Preserve display titles independently of generated filenames.
- [ ] Compare ASCII-only paths, paths with spaces, an accented game title, and a Japanese parent path. Use synthetic filenames where possible.
- [ ] Implement the smallest supported fix. Test filename collisions and empty sanitized names if sanitization is used.
- [ ] Diagnose `C1002` independently. Inspect compiler bitness, translation-unit sizes, memory use, optimization settings, and existing generator partitioning before choosing a fix.
- [ ] If unit splitting is required, make it deterministic and verify symbol linkage, registration completeness, and execution behavior. Do not hand-edit generated PLA files as the lasting solution.
- [ ] Re-export with the modified application and matching build kit; record whether either compiler path still fails or leaves modules to JIT.

**Acceptance:** the affected export can compile, link, and produce a package with documented toolchain settings. The missing-object and compiler-memory issues each have evidence of resolution or an explicit remaining limitation. Packaging success alone does not establish gameplay correctness.

## Milestone 3: Establish Hybrid runtime behavior

- [ ] Run the new Hybrid package with the same PLA update and comparable settings used for the JIT baseline.
- [ ] Confirm it loads the intended generated code and matching runtime. Identify any modules compiled entirely through JIT fallback.
- [ ] Exercise boot, menus, opening gameplay, movement, combat, area transitions, saving/loading, and a longer play session.
- [ ] Record crashes, visual/audio errors, save behavior, timing, frame rate, and memory use.
- [ ] Collect local recompilation gap/guard diagnostics. Separate unsupported CPU operations, indirect branches, service/runtime failures, and GPU issues.
- [ ] Use fallback frequency and exercised code paths to prioritize fixes. Record which gameplay areas remain untested.

**Acceptance:** Hybrid passes a documented representative gameplay checklist, with fallback behavior and known failures reported. It remains a compatibility investigation milestone.

## Milestone 4: Evaluate fully static execution

- [ ] Classify the gaps exposed by Hybrid and implement supported fixes in the generator or runtime, with targeted regression checks.
- [ ] Re-generate and compile a static AOT export against the same game/update and runtime revision.
- [ ] Inspect configuration and diagnostics to verify that guest CPU execution uses no JIT or interpreter fallback. Do not infer this solely from the UI label or executable name.
- [ ] Repeat the representative gameplay checklist and add coverage across regions, battles, transitions, and save/load cycles.
- [ ] Compare correctness and performance with JIT and Hybrid under matched conditions.
- [ ] If static execution remains blocked, document the specific unsupported behavior, evidence, and remaining work. Do not promise that full compatibility is guaranteed.

**Acceptance:** the tested scenarios run correctly with guest CPU fallback disabled, and remaining coverage limits are disclosed. Passing those scenarios is evidence for those scenarios, not proof that every game path works. GPU shaders and runtime services remain separate architectural concerns.

## Milestone 5: Implement improvements one at a time

For each feature, identify the responsible layer, add a clear setting where appropriate, preserve a baseline/default mode, and record gameplay correctness and performance before and after the change.

| Feature | Required checks |
|---|---|
| Up to 4K | Render-target/scaling behavior, UI readability, post-processing, GPU memory, and frame-time cost. |
| Ultrawide | Camera projection, UI placement, menus, cutscenes, and field-of-view behavior without stretching. |
| 60 FPS | Simulation and animation speed, physics, input, timers, scripted events, and cutscenes across variable frame rates. |
| Ambient occlusion | Depth/normal availability, shader integration, temporal artifacts, quality settings, and GPU cost. |
| LoD/draw distance | Identify actual culling/LoD controls; check transitions, streaming, CPU/GPU load, and memory. |
| Additional vegetation | Determine whether this requires game-data changes; inspect placement, collision, streaming, asset provenance, and performance. |

**Acceptance:** each completed feature has repeatable evidence and documented limits. Keep independent features in separate coherent changes and leave unverified features labeled as planned.

## Milestone 6: Prepare a reproducible public release

- [ ] Verify build/setup instructions from a clean checkout using the documented dependency mechanism.
- [ ] Inspect tracked files and release contents for game inputs, generated game code/assets, keys, firmware, private paths, and credentials. Ignore rules do not remove already tracked files.
- [ ] Preserve upstream and third-party notices and provide corresponding source/build instructions for GPL-covered binaries.
- [ ] Publish only supported software artifacts; keep local game exports out of public repository and release contents.
- [ ] Define how `devN` counts development commits for a target version, then automate it if useful. Do not label an unverified build stable.
- [ ] Update the README, compatibility notes, known issues, and changelog to match the verified implementation.
- [ ] Prepare a concrete release for user review before publishing it unless publication is separately authorized.

**Acceptance:** a clean checkout reproduces the documented software build, release contents match the license/input policy, and support claims match testing evidence. Reaching `v1.0.0` requires an explicitly agreed stable scope.

## Immediate next action

Begin Milestone 1. Inspect the build configuration and dependencies, record the actual environment, and establish a source-built JIT baseline before applying the unverified path patch or attempting graphics changes.

Useful entry points: [agent guide](../../CLAUDE.md), [project README](../../README.md), [upstream build guide](../Suyu/Build.md), [upstream export guide](../Suyu/user/GameExport.md), [exporter source](../../src/suyu/game_export.cpp), and [dependency manifest](../../cpmfile.json). Archived guides are reference material; verify commands against current code.

## Progress record

After each meaningful change, add a dated entry with:

- Milestone and source revision.
- Problem and supporting evidence.
- Files changed and reason for the chosen approach.
- Exact build/test commands and relevant settings.
- Observed result, comparison baseline, and untested behavior.
- Next action or blocking information.

Keep raw logs, machine-specific paths, and game-derived artifacts local. Link only to sanitized, shareable evidence from public documentation.

### 2026-10-04: Handoff documentation

Created the root agent guide and this work plan. Verified the existing repository layout, README, local Git baseline/origin, exporter naming code, and CMake dependency entry points. Documentation creation does not constitute a host build, export fix, or successful PLA run. No source-code fix was applied during this handoff.

### 2026-10-04: Codebase index

- Milestone: preparation for Milestones 1-2. Revision `9f932d5` plus untracked handoff docs.
- Added [CODEBASE_INDEX.md](CODEBASE_INDEX.md), a read-only survey of the recompiler, exporter, runtime, GPU, build system, frontends, and tests. Key references were checked against source and marked "(v)".
- Findings relevant to later milestones:
  - The ARM64→C translator is `src/core/recompiler/arm64_to_c.h` (`EmitProject`).
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
- **Problem: Boost extraction failure.** `file(ARCHIVE_EXTRACT)` (CMake 4.4.3) aborted on `boost-1.90.0-cmake.tar.xz` with `archive_write_header(): Invalid empty pathname`. Evidence: extraction stopped just before entry `libs/wave/test/testwave/testfiles/utf8-test-ßµ™∃/`; system ANSI code page is 932; a minimal `cmake -P` script reproduced the failure; `cmake -E tar xf` extracted the full archive including that directory under both console code pages 932 and 65001. A path-length hypothesis (longest entry 278 characters under the original cache path; LongPathsEnabled=1) was tested by moving the CPM cache to a short path and ruled out.
- **Fix:** `CMakeModules/CPMUtil.cmake` `fetch_package` now extracts with `execute_process(${CMAKE_COMMAND} -E tar xf ...)` in the destination directory and calls `fatal` with archive path, destination, exit result, and stderr on failure. Other `file(ARCHIVE_EXTRACT)` uses (`externals/nx_tzdb/CMakeLists.txt`, `tools/cpm/cmake/update.cmake`) were not changed; nx_tzdb extracted without error in this run. The short CPM cache path was kept for an ASCII-only reproduction.
- **Configure result:** success in 167 s. All CPMUtil packages resolved as bundled, matching `cpmfile.json` (e.g. OpenSSL v4.0.0-11b7b6ea3b CI build, Boost 1.90.0, fmt 12.1.0, SDL3 3.4.8, FFmpeg 8.0.1, sirit v1.0.5, Vulkan-Headers v1.4.345, Qt6 6.11.1 bundled). Warnings: optional Qt components Svg and Test missing from the bundled Qt; `windeployqt` not found (`src/suyu/CMakeLists.txt:532`); RenderDoc and Doxygen not found (optional).
- **Build command:** same environment, `cmake --build <build-dir> --target suyu suyu-cmd -j 8` (parallelism limited for a 16 GB host).
- **Build result:** all compilation steps succeeded; `suyu-cmd.exe` linked (about 31.6 MB). `suyu.exe` failed to link with `LNK1169` after many `LNK2005` errors: VMA (VulkanMemoryAllocator) symbols such as `VmaAllocator_T::VmaAllocator_T` are defined both in bundled `Qt6Gui.lib(qrhivulkan.cpp.obj)` and in `video_core.lib(vma.cpp.obj)` (`src/video_core/vulkan_common/vma.cpp` defines `VMA_IMPLEMENTATION`). The bundled Qt 6.11.1 for windows-amd64 is a static build (no Qt DLLs; `Qt6Gui.lib` about 33 MB) using the `/MD` CRT, matching the project. A `LNK4098` LIBCMT defaultlib conflict warning also appeared (not yet investigated). Linker messages were emitted in Japanese and appear garbled in the UTF-8 log because of console code-page mismatch.
- **Not yet verified:** application startup, PLA loading, export kit.
- **Next / decision needed:** choose how to resolve the VMA duplicate definition — e.g. build against a shared (DLL) Qt with `-DYUZU_USE_BUNDLED_QT=OFF` (the archived predecessor README documents Qt 6.9.3 msvc2022_64), versus a source-level change. Linker options such as `/FORCE:MULTIPLE` would hide an ODR conflict and are not preferred.
