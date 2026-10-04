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
- **A/B results (user, PLA 1.1.1, same locations):** ASTC decoding Gpu→Cpu: no change. Clearing the per-title pipeline cache: the originally affected objects rendered correctly, but other objects broke instead (player character showed another texture in patches; a large black triangle covered part of the sky), i.e. the affected objects depend on pipeline build order. GPU accuracy changes: no change. Prebuilt Suyu v0.0.12: same defect, so it is inherited code, not caused by this build's toolchain or the VMA change. Setting `use_graphics_pipeline_library=false` (edited in qt-config.ini): rendering correct.
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
- **Next:** Milestone 2 — reproduce the PLA export with this application and kit in an ASCII-only export path, collect the export/build logs, and separate the missing-object (`recomp_registration.c.obj`) and compiler-memory (`C1002`) investigations.

### 2026-10-04: Generated-code build parallelism (Milestone 2)

- Milestone 2. Base revision `0ddf47d` (v0.0.1-dev3) plus uncommitted changes below.
- **Observation (PLA 1.1.1 Hybrid + Build export in progress, ASCII path, clang-cl 22.1.8):** only one `clang-cl` ran at a time; CPU load ~31% of 12 logical CPUs. The generated module CMake had `RECOMP_JOBS=1`, and its Ninja pool `recomp_compile` had depth 1. 223 generated units (main 174, sdk 18, subsdk1 18, subsdk0 11, rtld 2), median 22 MB / max 32 MB of C; 105 units took 94 min (~54 s each), projecting ~3.4 h for compilation. One clang-cl compile peaked at ~1.3 GB working set. The exporter configured the module build inside the source-build tree (`<build-dir>/recomp_clang`) with generated sources in the export's hidden `.suyu-export-<id>/aot_cache/exefs`.
- **Cause:** `arm64_to_c.h` emitted `RECOMP_JOBS = min(cores, total RAM MB / 8192)`; 16191 MB / 8192 = 1. The 8 GB budget came from cl.exe heap failures (C1060/C1002), not clang-cl. Units were limited mainly by the 20,000-block ceiling.
- **Change:**
  - Unit limits: new constants `kDefaultUnitSourceBytes` (8 MiB) and `kDefaultUnitBlocks` (5,000) replace the 32 MiB / 20,000-block production limits; `EmitProject` gains a `unit_block_limit` parameter (0 treated as 1). The exporter accepts `SUYU_RECOMP_UNIT_BLOCKS` and `SUYU_RECOMP_UNIT_KB` overrides (positive integers; invalid values are logged and ignored) and logs the effective limits once.
  - Job count in the generated CMake: per-compile budget 8192 MB for real MSVC cl.exe and 1536 MB for clang/clang-cl/GCC; 4096 MB reserved for the OS and emulator; jobs = clamp((RAM − reserve) / budget, 1, logical cores). Overrides: `SUYU_RECOMP_JOB_MEMORY_MB`, `SUYU_RECOMP_JOBS`, or `-DRECOMP_JOBS`.
  - Files: `src/core/recompiler/arm64_to_c.h`, `src/suyu/game_export.cpp`, `tests/recompiler_partition/{export.cpp,run.py,README.md}` (block-ceiling fixture now 5,001 blocks; baseline mode passes an explicit 20,000-block limit).
- **Verification:** `python -B tests/recompiler_partition/run.py --build <ascii-dir>` from a VS 2026 Build Tools x64 environment: PASS (exact bodies/dispatch entries, GG slots, single- and multi-module execution). Configuring the generated test project reports 1 job with cl.exe (8192 MB budget) and 7 jobs with clang-cl (1536 MB budget) on this 16 GB / 12-thread host; `SUYU_RECOMP_JOBS=3` yields 3.
- **Not verified yet:** suyu has not been rebuilt with this change (the running export locks `suyu.exe`); no PLA export with the new limits; actual peak memory of 8 MiB / 5,000-block units; total export time; link time with ~4x more objects; runtime performance (fewer cross-unit inlining opportunities may reduce FPS); `build_native_unix.sh` (Source export helper for Linux/macOS) still uses RAM / 8192 and was not changed.
- **Next:** after the current export finishes, record its result (link outcome, compiler, JIT-fallback modules, time), rebuild suyu, re-export, and measure time, peak memory per compile, and in-game FPS against the 1-job build; tune unit limits and budget from those measurements.

### 2026-10-05: First PLA Hybrid export completed; launcher does not start

- **Milestone 2.** Exporter: `suyu.exe` built from `0ddf47d` (v0.0.1-dev3) with the locally built export build kit; the generated-code parallelism change was not yet in this binary.
- **Settings (user):** PLA 1.1.1, CPU backend suyu Hybrid JIT + AOT, format Build, Dynarmic fallback allowed, ASCII-only output parent directory. Compiler: clang-cl 22.1.8 (LLVM). Duration about 4 hours (user), dominated by one-at-a-time compilation of 223 generated units.
- **Result:** export reported success. `aot_manifest.json`: `effective_backend` suyu-hybrid, `image_abi` 6, `total_modules` 5, `fallback_modules` empty (no module left to JIT), `recompiled_c_blocks` 4,131,332, `total_blocks_analyzed` 3,854,104, `total_instructions` 17,169,816, `total_text_bytes` 68,679,264, `requires_runtime_codegen` false. The single-file launcher is about 1.87 GB (1,870,654,976 bytes).
- **Earlier failures:** neither the `LNK1181` missing `recomp_registration.c.obj` nor the MSVC `C1002` heap failure reproduced in this run (clang-cl path, ASCII parent directory). This does not prove they are fixed: the earlier run used a non-ASCII-containing path, and the MSVC fallback path was not exercised.
- **Non-ASCII naming:** the package folder and launcher name are derived from the game title and contain a non-ASCII character (`Pokémon Legends_ Arceus - Hybrid AOT + JIT`), even with an ASCII output parent directory.
- **Defect found:** `README_NATIVE_EXPORT.txt` says "Game code compiled with: O2" and the manifest has `recomp_compiler` = `O2`; the compiler name (clang-cl) is not recorded correctly (`last_recomp_compiler` handling in `src/suyu/game_export.cpp`). Not yet investigated.
- **Launch failure (user):** double-clicking the launcher does not start the game. Not yet investigated; deferred until after the next commit at the user's request. Hypotheses to check: image size (~1.87 GB) and loader limits, the non-ASCII executable/package name, missing runtime files or keys/firmware lookup, and the package's own `user/` log.
- **Next:** commit the pending changes, then diagnose the launch failure from the package log and a console launch; afterwards re-export with the parallel build to measure time and compare.

### 2026-10-05: Exported launcher crash on non-ASCII name fixed

- **Milestone 2.** Base revision `6f4dffd` (v0.0.1-dev4) plus uncommitted changes below.
- **Evidence:** the package log (`user/log/suyu_log.txt` in the export) ended 0.4 s after start with `Frontend <Critical> suyu_cmd\suyu.cpp:2937:main: Unhandled fatal exception in suyu-cmd: No mapping for the Unicode character exists in the target multi-byte code page.` The last step logged before it was the SDL configuration write; `Window created, loading game...` was never logged.
- **Cause:** `src/suyu_cmd/emu_window/emu_window_sdl2_vk.cpp` (static-recompiled launcher builds) derived the window title with `exe.stem().string()`. On Windows, `std::filesystem::path::string()` converts through the ANSI code page; the launcher name contains `é`, which code page 932 cannot represent, so the conversion threw. The same class of conversion existed in `src/suyu_cmd/suyu.cpp` for the Discord title fallback (`std::filesystem::path(filepath).stem().string()`, which also interpreted UTF-8 as the ANSI code page). Other `.string()` uses in `suyu_cmd` are in non-Windows branches.
- **Fix:** both sites now use `Common::FS::PathToUTF8String` (and `Common::FS::ToU8String` for the UTF-8 input path). Files: `src/suyu_cmd/emu_window/emu_window_sdl2_vk.cpp`, `src/suyu_cmd/suyu.cpp`.
- **Verification:** rebuilt `suyu`, `suyu-cmd`, and `export-build-kit`. Copied the rebuilt `suyu-export-host-hybrid.exe` (built with `SUYU_CMD_STATIC_RECOMP`) to a portable folder under a name containing `é`, with an empty `exefs/main` so startup proceeds past ROM discovery: the log reached `Window created, loading game...` and then failed to load the dummy ROM (Error 30), as expected. A full PLA launch still requires a new export, because the existing package cannot be relinked: the export's hidden working directory (generated sources and `recomp_registration.c`) is deleted after export.
- **Exporter side effects found:** when `suyu.exe` runs from a source build, the exporter configures the source build tree itself with `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, leaves those cache entries pointing at the deleted hidden directory, links `bin/suyu-cmd-static.exe` (game-derived, ~1.87 GB) in the build tree, and builds generated modules in `<build-dir>/recomp_clang`. With those entries set, `package.py` refuses to package an export build kit. They were removed with `cmake -U ...`, which then rebuilt the whole tree (1,740 steps) because compile definitions changed. Treat the build tree's `bin/suyu-cmd-static.exe` and `recomp_clang/` as local game-derived output.
- **Kit note:** the regenerated kit records `producer_source_revision` `6f4dffd` although it was built with uncommitted changes; the manifest records HEAD only.
- **Next:** user commits; then re-export PLA (Hybrid, Build, ASCII parent directory) with the rebuilt `suyu.exe` to verify launch and measure export time, peak memory per compile, and job count with the new parallel limits.
