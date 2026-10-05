# HisuiWake Work Plan

Handoff prepared: 2026-10-04. HEAD: `70336e7` (docs commit after `2662ef5` `v0.0.1-dev8`). Last session handoff: 2026-10-05, fifth session (see [Session handoff](#session-handoff-2026-10-05-end-of-fifth-session)).
Older history (2026-10-04 handoff through the fourth session close on 2026-10-05, plus earlier export evidence) is archived in [OLD_HISTORY_HW.md](OLD_HISTORY_HW.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop the requested rendering and gameplay improvements on a verified runtime. Keep the code understandable and maintainable by humans, with human review and hands-on testing.

Start with a reproducible Windows x64 build and a correctly running game. Proceed to graphics improvements after the execution path is understood. Fully static PLA compatibility remains uncertain.

## Handoff status

| Item | Evidence/status |
|---|---|
| Source repository | Root contains the Suyu-derived source, build files, dependency metadata, and archived upstream documentation. |
| Git baseline | Initial commit `9f932d5`. As of 2026-10-05, HEAD is `70336e7` (docs commit after `2662ef5` v0.0.1-dev8); the user commits and pushes. |
| Configured origin | `https://github.com/Azuloma/HisuiWake.git`; remote publication was not independently verified during this handoff. |
| README | English draft describes feasibility status, planned features, support scope, versioning, and the hobby/no-donations policy. |
| License | Root `LICENSE.txt` is copied unchanged from the archived upstream license; README specifies `GPL-3.0-or-later`. Existing notices are retained. |
| Documentation | Project documents belong in `docs/HisuiWake/`; upstream documents are archived in `docs/Suyu/`. |
| Dependencies | Acquired by CPMUtil from `cpmfile.json` (bundled Qt 6.11.1, SDL3, FFmpeg, OpenSSL on MSVC); `.gitmodules` is not the acquisition path. Verified on 2026-10-04. |
| Host source build | Verified: MSVC 14.51 (Build Tools 2026) + Ninja Release; `suyu.exe`, `suyu-cmd.exe`, and the export build kit build and start. `docs/HisuiWake/BUILD.md` is not written yet. |
| PLA export/run | JIT: 30 FPS. Hybrid (dev6-era, translate-all, budget 256): 15-20 FPS. v0.0.1-dev8 static AOT + Build (clang-cl): 25-30 FPS with no fallback in the observed overworld scene; conditions were not fully matched. |
| Proposed path patch | Superseded. The observed non-ASCII failure was the launcher's window-title conversion (fixed in v0.0.1-dev5). Filename sanitization was not implemented; the package name still contains `é`. |

The cleanup already removed upstream automation, temporary material, old screenshots, and an isolated `tools/static_recompiler` demo. Do not perform another broad deletion pass before a working baseline identifies the files actually required.


## Key facts from archived history

- **Host/tool baseline:** Windows 11 Home 10.0.26300, Intel Core i5-12400F, 15.8 GB RAM, RTX 2060 SUPER; MSVC 14.51 via Build Tools 2026 `vcvars64`, Ninja 1.13.2, CMake 4.4.3, LLVM/clang-cl 22.1.8, Python 3.14.7, Git for Windows 2.56.0 `patch`, Vulkan SDK 1.4.363.0 `glslangValidator`; use a short ASCII `CPM_SOURCE_CACHE`. ([details](OLD_HISTORY_HW.md#2026-10-04-milestone-1-environment-and-first-configure-attempt))
- **Configure/build:** use the recorded CMake configure with explicit Git/glslang paths and `cmake --build <build-dir> --target suyu suyu-cmd -j 8`. ([details](OLD_HISTORY_HW.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **Dependencies:** CPMUtil uses `cpmfile.json`; extraction uses `cmake -E tar xf` because `file(ARCHIVE_EXTRACT)` failed on non-ASCII entries. ([details](OLD_HISTORY_HW.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **VMA:** implementation moved to executable targets to avoid static-Qt duplicate symbols. ([details](OLD_HISTORY_HW.md#2026-10-04-vma-implementation-moved-to-executables-suyuexe-links))
- **JIT/GPL:** PLA 1.1.1 ran at 30 FPS; the GPL rendering defect was isolated and GPL disabled by default. ([details](OLD_HISTORY_HW.md#2026-10-04-first-pla-jit-run-on-the-source-build))
- **Export/build:** the export kit was produced; clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before packaging. ([details](OLD_HISTORY_HW.md#2026-10-04-export-build-kit-produced))
- **Generated-code scaling:** job-pool sizing and unit splitting use 8 MiB/5,000-block defaults and compiler-aware budgets. ([details](OLD_HISTORY_HW.md#2026-10-04-generated-code-build-parallelism-milestone-2))
- **Launcher/Hybrid:** the non-ASCII crash was fixed in dev5; SHL was implemented but gated off for Hybrid; Hybrid results were 17, 14, and 15-20 FPS at budget 256. ([details](OLD_HISTORY_HW.md#2026-10-05-shl-already-implemented-but-gated-off-for-hybrid))
- **Chain budget:** 256 improved long-slice behavior from about 14 FPS at 4096 to 15-20 FPS. ([details](OLD_HISTORY_HW.md#2026-10-05-long-slice-chain-budget-lowered-to-256-v001-dev6-unverified-in-export))
- **AOT conclusion:** Hybrid executed AOT-recompiled native application code in the observed run, while runtime services and GPU macro JIT/shader compilation remain; static AOT with fallback disabled is the stronger check. ([details](OLD_HISTORY_HW.md#2026-10-05-is-the-hybrid-cpu-path-ahead-of-time-static-recompilation))

## Milestone 1: Establish a reproducible host build

- [x] Record the checkout revision, Windows version, CPU, RAM, GPU/driver, compiler, SDK, CMake, Ninja, and Qt versions.
- [ ] Verify the source lineage using the README, archived provenance, and upstream revision information. Do not assume the new Git history contains Suyu's original commits.
- [x] Inspect `CMakeLists.txt`, `CMakeModules/`, `cpmfile.json`, `.gitmodules`, and package configuration to identify the actual dependency acquisition paths and pinned versions.
- [x] Obtain required dependencies through the build's intended mechanism. Record downloads, patches, versions, and failures; do not substitute arbitrary latest revisions.
- [x] Configure and build the Suyu-derived application in a local ASCII-only build path. Save the exact commands and options.
- [x] Build or locate the matching export build kit if required by the selected exporter path. Verify object/library/compiler compatibility.
- [ ] Confirm the application starts and can load PLA through its ordinary JIT path with the user's local inputs. Record the game update/module build IDs and settings without publishing game material or keys. (Startup, PLA 1.1.1 gameplay, and settings are recorded; module build IDs are not.)
- [ ] Write the successful setup in `docs/HisuiWake/BUILD.md` when verified. Repair relevant archived documentation links as needed.

**Acceptance:** a documented source build produces a launchable application; required dependencies and the export kit can be reproduced; PLA's JIT behavior is recorded as the comparison baseline. A configure failure or build-only result does not satisfy the gameplay check.

## Milestone 2: Resolve export build failures

- [x] Inspect `src/suyu/game_export.cpp` and its current generator/build helpers. Reproduce the failure with a fixed source revision, game update, backend, and toolchain.
- [ ] Export Source separately when useful to inspect generated inputs. Distinguish source generation, compilation, linking, packaging, and execution failures.
- [ ] Check whether `recomp_registration.c.obj` exists, where it is written, and which command/response file references it.
- [ ] Trace QString/filesystem conversions, process arguments, working directories, and response-file encoding. Preserve display titles independently of generated filenames.
- [ ] Compare ASCII-only paths, paths with spaces, an accented game title, and a Japanese parent path. Use synthetic filenames where possible.
- [ ] Implement the smallest supported fix. Test filename collisions and empty sanitized names if sanitization is used.
- [ ] Diagnose `C1002` independently. Inspect compiler bitness, translation-unit sizes, memory use, optimization settings, and existing generator partitioning before choosing a fix.
- [x] If unit splitting is required, make it deterministic and verify symbol linkage, registration completeness, and execution behavior. Do not hand-edit generated PLA files as the lasting solution.
- [x] Re-export with the modified application and matching build kit; record whether either compiler path still fails or leaves modules to JIT.

**Acceptance:** the affected export can compile, link, and produce a package with documented toolchain settings. The missing-object and compiler-memory issues each have evidence of resolution or an explicit remaining limitation. Packaging success alone does not establish gameplay correctness.

## Milestone 3: Establish Hybrid runtime behavior

- [ ] Run the new Hybrid package with the same PLA update and comparable settings used for the JIT baseline.
- [x] Confirm it loads the intended generated code and matching runtime. Identify any modules compiled entirely through JIT fallback.
- [ ] Exercise boot, menus, opening gameplay, movement, combat, area transitions, saving/loading, and a longer play session.
- [ ] Record crashes, visual/audio errors, save behavior, timing, frame rate, and memory use.
- [x] Collect local recompilation gap/guard diagnostics. Separate unsupported CPU operations, indirect branches, service/runtime failures, and GPU issues.
- [ ] Use fallback frequency and exercised code paths to prioritize fixes. Record which gameplay areas remain untested.

**Acceptance:** Hybrid passes a documented representative gameplay checklist, with fallback behavior and known failures reported. It remains a compatibility investigation milestone.

## Milestone 4: Evaluate fully static execution

- [ ] Classify the gaps exposed by Hybrid and implement supported fixes in the generator or runtime, with targeted regression checks.
- [x] Re-generate and compile a static AOT export against the same game/update and runtime revision. (The v0.0.1-dev6 export completed with five modules and was packaged successfully.)
- [x] Inspect configuration and diagnostics to verify that guest CPU execution uses no JIT or interpreter fallback. Do not infer this solely from the UI label or executable name. (Strict static mode, zero static/JIT transitions, zero lookup misses, zero unimplemented opcodes, and no guard bumps were recorded in the exercised run.)
- [ ] Repeat the representative gameplay checklist and add coverage across regions, battles, transitions, and save/load cycles. (The run reached gameplay, but the complete checklist was not confirmed.)
- [ ] Compare correctness and performance with JIT and Hybrid under matched conditions. (Dev8 observations were about 25-30 FPS static AOT, 15-20 FPS Hybrid at budget 256, and 30 FPS JIT; conditions were not fully matched.)
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


## Session handoff (2026-10-05, end of fifth session)

### State at handoff

- **Repository:** HEAD is `70336e7` (docs commit after `2662ef5` `v0.0.1-dev8`), after user commits `2662ef5`, `ff44909`, `d0be6ce`, `a6b8690`, `28e6caf`, and `6f4dffd`. The dev8 result in `CHANGELOG.md` is committed. The uncommitted Markdown is this archive split, the `CLAUDE.md`/`CODEBASE_INDEX.md` updates, and `README.md`.
- **Local environment:** `suyu.exe` and `suyu-cmd.exe` in the local build directory are built from dev8. The latest local PLA package is a dev8 static AOT Build export (`suyu-static`, fallback disabled, translate-all, ABI 6, features 7); a dev6-era Source-format export also exists for hot-block inspection. Exports are outside the repository; package names containing `é` work.
- **Diagnostics:** the local diag directory outside the repository contains `RipSampler.cs`, `RipSampler2.cs`, and `sample_static.ps1`; launch from a non-elevated shell. Use `powershell -File sample_static.ps1 -ProcId <launcher PID> -Out <file>`.
- **Build/test:** in the Build Tools 2026 `vcvars64` environment, run `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls` (about 100.6 M cases; reduced: add `--cases 1000 --adv 2000 --legs 13`) and `python -B tests/recompiler_smoke/run.py`, also with clang-cl. Both passed at dev8; partition was not rerun. `rebuild-suyu.bat` clears export variables and builds with `-j 8`; incremental builds use `cmake --build <build-dir> --target suyu suyu-cmd -j 8`.
- **Switches:** `SUYU_FPCR_STATS=1` logs guest FPCR and adds a lock per save; never use it for FPS. `SUYU_RECOMP_CHAIN_BUDGET` is 1-8192, default 256. `SUYU_AOT_TRANSLATE_ALL=1` applies to Hybrid; static AOT forces translate-all and strict mode.

### What this session did

1. [Dev6 static AOT export](#2026-10-05-first-static-aot-export-v001-dev6-runs-with-no-fallback-16-20-fps): no fallback, 16-20 FPS.
2. [RIP sampling](#2026-10-05-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld): hot block `blk_main_0000000002d1e290` was 15-19% of busy-core samples.
3. [Source inspection](#2026-10-05-hot-block-c-inspection-fz-closes-the-fpx1-native-fp-gate-hypothesis): 49 vector FP operations; FPCR.FZ closed FPX1 and selected software IEEE bodies.
4. [FPCR measurement](#2026-10-05-guest-fpcr-measured-pla-runs-with-fz-set-fpx1-gate-closed): FZ was set in about 96% of JIT context saves.
5. [Dev8 FZ-aware FPX1 gate](#2026-10-05-fz-aware-fpx1-gate-v001-dev8-unverified-in-pla): 0 mismatches over 100.6 M cases; FZ hit rate 64.5%; smoke goldens fixed.
6. [Dev8 static AOT export](#2026-10-05-fz-aware-fpx1-gate-verified-in-pla-static-aot-25-30-fps): 25-30 FPS, zero fallback.

### Where things stand

| Observation | Result |
|---|---|
| JIT baseline | 30 FPS. |
| Hybrid first / translate-all / budget 256 | 17 / 14 / 15-20 FPS. |
| Dev6 static AOT | 16-20 FPS, no fallback. |
| Dev8 static AOT | 25-30 FPS, no fallback. |
| FPCR measurement | FZ set in about 96% of 3,473,408 JIT context saves; do not measure FPS with `SUYU_FPCR_STATS=1`. |
| Sampling profile | Hot block was 15-19% of busy-core RIP samples; `ScheduleImplFiber` was 10-12.5%. The FZ finding is confirmed, not a hypothesis. |

Key conclusions: static AOT ran without fallback in the exercised scenario; Dynarmic remains linked for Hybrid and GPU macro JIT/shader compilation remain runtime generation. The SHL fallback hypothesis was refuted, and budget 256 addressed long-slice interrupt delay. The FZ-closed FPX1 gate was the main remaining static AOT cost; the smoke golden mismatch came from MSVC without `/utf-8` in the test only, so real exports were unaffected. Build exports delete `aot_cache`; use Source format to inspect generated C. The FPS comparison is not under fully matched conditions.

### Open issues, in priority order

1. Per-thread CPU and RIP sampling on the dev8 static AOT launcher in the overworld; check `recomp_load*`/`store*`/`ldp`/`stp`, per-instruction entry labels, `ScheduleImplFiber`, `HostTiming`, and `recomp_lookup_*`.
2. Code review of `a6b8690` by the Code reviewer.
3. Investigate stale `export-package.json` producer revision `d0be6ce` in dev8-era builds.
4. Later: race-free `SignalInterrupt` slice ending; revisit verify-always pin; optional `SUYU_NO_JIT`; optional DN acceptance in FPX1 (safe because NaN results are never kept, but PLA does not set DN).
5. Existing investigations: export pipeline cost, GPL root cause, and smaller items (8 unresolved rtld GOT/PLT symbols, O2 compiler label, `BUILD.md`, version string, `egrep` hook, `getopt` LGPL warning, kit MSVC baseline).

### Immediate next action

The user commits the pending Markdown (this change), then starts the dev8 static AOT launcher from a non-elevated shell, stops in the overworld, runs `sample_static.ps1`, and compares with the dev6 static sample.

### How to work

- Measure FPS in the same overworld scene and record backend, settings, cache state, and chain-budget condition; never use `SUYU_FPCR_STATS` during FPS measurements.
- Use the test commands above and Source format when generated C must be inspected. Keep build/export paths and diagnostics outside the repository.
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



## Progress record

After each meaningful change, add a dated entry with:

- Milestone and source revision.
- Problem and supporting evidence.
- Files changed and reason for the chosen approach.
- Exact build/test commands and relevant settings.
- Observed result, comparison baseline, and untested behavior.
- Next action or blocking information.

Keep raw logs, machine-specific paths, and game-derived artifacts local. Link only to sanitized, shareable evidence from public documentation.


- **Archive rule:** At each session close, move progress entries older than the previous session (and their session-close entry) verbatim to OLD_HISTORY_HW.md, and add any still-relevant fact to Key facts from archived history.
### 2026-10-05: First static AOT export (v0.0.1-dev6) runs with no fallback, 16-20 FPS

- **Milestone:** 4 (fully static execution evaluation). The `suyu.exe` exporter was built from v0.0.1-dev6. The export package recorded backend `static`, format `build`, producer revision `a6b869025143832cd6f6488f9c4abb25ad8e8568` (dev6), and the existing `suyu v0.0.11 (mk8-recomp)` build string. The parent output directory was ASCII-only; the package folder and launcher name contained `é`, and the launcher started successfully.
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
- **Proposed next steps:** (1) verify guest FPCR and gate-open rate at runtime with a diagnostic counter or shadow build; (2) if confirmed, extend the FPX1 gate to accept FZ only when no input is denormal and the result is a normal finite value (or an exact zero case), so FZ cannot change the value or add IDC/UFC. An FZ-aware sum rule is needed because “any finite sum” may be tiny. Add targeted tests, expect generated output and `tests/recompiler_smoke` hashes to change, re-export, and measure FPS.

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
