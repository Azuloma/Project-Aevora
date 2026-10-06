# Aevora Work Plan

Handoff prepared: 2026-10-06. HEAD: `aa8c2aa` (README-only commits on top of `d764d8e`, v0.0.1-dev13). Last session handoff: 2026-10-06, ninth session, complete (see [Session handoff](#session-handoff-2026-10-06-ninth-session-complete)). The user commits and pushes; older detailed history is in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Keep evidence limited to tested scenarios: successful generation, compilation, or startup is not gameplay compatibility.

## Handoff status

| Item | Evidence/status |
|---|---|
| Source repository | Root contains the Suyu-derived source, build files, dependency metadata, and archived upstream documentation. |
| Git baseline | Initial commit `9f932d5`. As of 2026-10-06, HEAD is `aa8c2aa` (README-only commits on top of `d764d8e`, v0.0.1-dev13); the user commits and pushes. |
| Milestone 4a | Steps 1-3, 4(a), 4(b), and 4(c) part 1 are implemented, reviewed, and committed through `d764d8e`; 4(c) part 2, 4(d), tests, and PLA verification remain open. |
| Configured origin | `https://github.com/Azuloma/Aevora.git`; remote publication was not independently verified during this handoff. |
| README | English draft describes feasibility status, planned features, support scope, versioning, and the hobby/no-donations policy. |
| License | Root `LICENSE.txt` is copied unchanged from the archived upstream license; README specifies `GPL-3.0-or-later`. Existing notices are retained. |
| Documentation | Project documents belong in `docs/Aevora/`; upstream documents are archived in `docs/Suyu/`. |
| Project name | Renamed from HisuiWake to Aevora on 2026-10-05 (README in `a3063a2`, other documents in this change); the local repository folder is still named HisuiWake. |
| Dependencies | Acquired by CPMUtil from `cpmfile.json` (bundled Qt 6.11.1, SDL3, FFmpeg, OpenSSL on MSVC); `.gitmodules` is not the acquisition path. Verified on 2026-10-04. |
| Host source build | Verified: MSVC 14.51 (Build Tools 2026) + Ninja Release; `suyu.exe`, `suyu-cmd.exe`, and the export build kit build and start. `docs/Aevora/BUILD.md` is not written yet. |
| PLA export/run | JIT: 30 FPS. Hybrid (dev6-era, translate-all, budget 256): 15-20 FPS. v0.0.1-dev8 static AOT + Build (clang-cl): 25-30 FPS with no fallback in the observed overworld scene; conditions were not fully matched. Dev9 FMI export failed to link: image about 2.19 GB exceeds the 2 GB x64 limit (LNK1248). Steps 1-3, 4(a), 4(b), and 4(c) part 1 are committed through `d764d8e`; split export and linked v1 execution remain unverified. |
| Proposed path patch | Superseded. The observed non-ASCII failure was the launcher's window-title conversion (fixed in v0.0.1-dev5). Filename sanitization was not implemented; the package name still contains `ﾃｩ`. |

The cleanup already removed upstream automation, temporary material, old screenshots, and an isolated `tools/static_recompiler` demo. Do not perform another broad deletion pass before a working baseline identifies the files actually required.

## Key facts from archived history

- **Host/tool baseline:** Windows 11 Home 10.0.26300, Intel Core i5-12400F, 15.8 GB RAM, RTX 2060 SUPER; MSVC 14.51 via Build Tools 2026 `vcvars64`, Ninja 1.13.2, CMake 4.4.3, LLVM/clang-cl 22.1.8, Python 3.14.7, Git for Windows 2.56.0 `patch`, Vulkan SDK 1.4.363.0 `glslangValidator`; use a short ASCII `CPM_SOURCE_CACHE`. ([details](OLD_HISTORY.md#2026-10-04-milestone-1-environment-and-first-configure-attempt))
- **Configure/build:** use the recorded CMake configure with explicit Git/glslang paths and `cmake --build <build-dir> --target suyu suyu-cmd -j 8`. ([details](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **Dependencies:** CPMUtil uses `cpmfile.json`; extraction uses `cmake -E tar xf` because `file(ARCHIVE_EXTRACT)` failed on non-ASCII entries. ([details](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **VMA:** implementation moved to executable targets to avoid static-Qt duplicate symbols. ([details](OLD_HISTORY.md#2026-10-04-vma-implementation-moved-to-executables-suyuexe-links))
- **JIT/GPL:** PLA 1.1.1 ran at 30 FPS; the GPL rendering defect was isolated and GPL disabled by default. ([details](OLD_HISTORY.md#2026-10-04-first-pla-jit-run-on-the-source-build))
- **Export/build:** the export kit was produced; clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before packaging. ([details](OLD_HISTORY.md#2026-10-04-export-build-kit-produced))
- **Generated-code scaling:** job-pool sizing and unit splitting use 8 MiB/5,000-block defaults and compiler-aware budgets. ([details](OLD_HISTORY.md#2026-10-04-generated-code-build-parallelism-milestone-2))
- **Launcher/Hybrid:** the non-ASCII crash was fixed in dev5; SHL was implemented but gated off for Hybrid; Hybrid results were 17, 14, and 15-20 FPS at budget 256. ([details](OLD_HISTORY.md#2026-10-05-shl-already-implemented-but-gated-off-for-hybrid))
- **Chain budget:** 256 improved long-slice behavior from about 14 FPS at 4096 to 15-20 FPS. ([details](OLD_HISTORY.md#2026-10-05-long-slice-chain-budget-lowered-to-256-v001-dev6-unverified-in-export))
- **AOT conclusion:** Hybrid executed AOT-recompiled native application code in the observed run, while runtime services and GPU macro JIT/shader compilation remain; static AOT with fallback disabled is the stronger check. ([details](OLD_HISTORY.md#2026-10-05-is-the-hybrid-cpu-path-ahead-of-time-static-recompilation))
- **Dev6 static AOT:** The fallback-disabled export reached 16-20 FPS in the observed scene. ([details](OLD_HISTORY.md#2026-10-05-first-static-aot-export-v001-dev6-runs-with-no-fallback-16-20-fps))
- **Static hot block:** The dev6 hot block contributed 15-19% of busy-core samples, and guest FPCR.FZ was set in about 96% of JIT context saves. ([details](OLD_HISTORY.md#2026-10-05-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld)) ([details](OLD_HISTORY.md#2026-10-05-guest-fpcr-measured-pla-runs-with-fz-set-fpx1-gate-closed))
- **Dev8 FZ-aware FPX1 gate:** The harness recorded 0 mismatches over 100.6 M cases. ([details](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-v001-dev8-unverified-in-pla))
- **Dev8 static AOT:** The fallback-disabled export reached 25-30 FPS with zero fallback in the observed scene. ([details](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-verified-in-pla-static-aot-25-30-fps))
- **Dev8 static sampling:** The dominant hot block fell from 15-19% to 2-3.5% of busy-core samples, and CPU cores waited 35-49% of samples. ([details](OLD_HISTORY.md#2026-10-05-dev8-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld))
- **Same-scene JIT comparison:** JIT process CPU was 275.7% versus 320.3% dev8 static; HostTiming, GPU-thread load, and `NtGdiDdDDIQueryAdapterInfo` are not AOT-specific; the remaining gap is guest CPU execution cost (static about 1.5-2x JIT CPU per core, estimate). ([details](OLD_HISTORY.md#2026-10-05-same-scene-jit-sampling-remaining-gap-is-guest-cpu-cost))
- **FMI (dev9):** Opt-in `SUYU_AOT_FM_INLINE=1` inlines the FM1 fast path; targeted tests passed (0 mismatches); out-of-line memory helpers were about 11% of busy-core samples. ([details](OLD_HISTORY.md#2026-10-05-source-inspection-of-memory-access-and-the-opt-in-fmi-inline-fast-path-v001-dev9-unmeasured-in-pla))
- **FMI LNK1248:** The PLA FMI static launcher reached image size `0x8C0E0000` (about 2.19 GB) and failed LNK1248; the MSVC fallback path could not help and diagnostics were lost (addressed in dev10). ([details](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248))

## Milestone 1: Establish a reproducible host build

- [x] Record the checkout revision, Windows version, CPU, RAM, GPU/driver, compiler, SDK, CMake, Ninja, and Qt versions.
- [ ] Verify the source lineage using the README, archived provenance, and upstream revision information. Do not assume the new Git history contains Suyu's original commits.
- [x] Inspect `CMakeLists.txt`, `CMakeModules/`, `cpmfile.json`, `.gitmodules`, and package configuration to identify the actual dependency acquisition paths and pinned versions.
- [x] Obtain required dependencies through the build's intended mechanism. Record downloads, patches, versions, and failures; do not substitute arbitrary latest revisions.
- [x] Configure and build the Suyu-derived application in a local ASCII-only build path. Save the exact commands and options.
- [x] Build or locate the matching export build kit if required by the selected exporter path. Verify object/library/compiler compatibility.
- [ ] Confirm the application starts and can load PLA through its ordinary JIT path with the user's local inputs. Record the game update/module build IDs and settings without publishing game material or keys. (Startup, PLA 1.1.1 gameplay, and settings are recorded; module build IDs are not.)
- [ ] Write the successful setup in `docs/Aevora/BUILD.md` when verified. Repair relevant archived documentation links as needed.

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

## Milestone 4a: Main-module DLL split

**Goal:** Build `main` as a DLL while `rtld`, `sdk`, `subsdk0`, and `subsdk1` remain static, keeping every image below the MSVC x64 2 GB limit with fallback disabled.

| Step | Status | Revision/evidence |
|---|---|---|
| 1 survey | Done | Code Explorer survey; archived design note |
| 2 design | Done | Accepted draft 3; full note archived |
| 3 image measurement | Done | Main DLL 1,459.1 / 1,783.7 MiB without/with FMI |
| 4(a) topology/identity/prebuild | Done | `1d9ba61` / dev11 |
| 4(b) registry, bundle loader, inventory binding | Done | `e38f7ad` / dev12 |
| 4(c) part 1 exporter/CMake/cache/package | Done | `d764d8e` / dev13; unit checks passed |
| 4(c) part 2 execution evidence | Open | Real split export, launcher, failure/retry/cache and runtime seams |
| 4(d) build-kit/runtime fixture | Open | Three-module production-dispatch fixture and carried criteria |
| 5 tests | Open | Mixed synthetic coverage and regressions |
| 6 PLA verification | Open | Per-image sizes, link time, zero transitions, same-scene FPS |

### Accepted design decisions

- D1: DLL-local runtime copy; no host symbol imports beyond the image ABI.
- D2: image/TLS state is classified per image; shared process state stays in host callbacks/context.
- D3: versioned registry v1 and topology marker are authoritative; loading is transactional and fails fast.
- D4: mixed in-tree and clang-cl prebuild modes use canonical target/file names and `/MD`.
- D5: package, manifest, cache, and image-size checks derive from the canonical topology.
- D6: the kit remains static-only until split support is explicitly implemented and verified.
- D7: split selection is an explicit per-export option; all-static v4 remains unchanged.
- Modifiability: one exporter-owned topology defines module order, bindings, DLL names, bundle id, ABI, and features; generated code is never edited.

The full D1-D7 design note, data flow, failure modes, test plan, and step-3 measurement are preserved in [OLD_HISTORY.md](OLD_HISTORY.md#design-note-accepted-2026-10-06-draft-3).

### Carried acceptance criteria

- 4(c): real split export and linked v1 launcher with fallback disabled; unreadable middle-module path; complete inventories and relocation input; named `Core::Load` refusals with zero guest execution; unregister/rebind; unchanged all-static v4 regression.
- 4(d): three-or-more-module static/DLL/static fixture; GG1, guard, FPX, cross-image reservation, STXR, CLREX, LDXP/STXP, counters, bridge-null and strict lookup refusals; remaining loader/handshake, kit/package/cache/path, golden, fastmem, and FPX checks.

### Step 4(c) part 2 acceptance

- Drive synthetic split static and Hybrid exports through `tests/export_integration`, run a linked v1 launcher with JIT fallback disabled, and inspect registry, marker, selection, DLL collection, manifests, hashes, and per-image `SizeOfImage`.
- Cover VFS/deconstructed-source failures: zero-text data, unreadable text, decompression failure, bad/short NSO header, missing `main`, all analyses failing, and analyzable non-canonical NSO refusal; ancillary files remain allowed.
- Inject emission, compiler, DLL-link, and launcher LNK1248 failures, including clang-to-MSVC retry; failed packages must not remove modules or finalize.
- Exercise option unset/empty/`0`/`1`/invalid across Dynarmic, Hybrid, strict static, non-Windows, Source, and kit-only paths; verify snapshot and re-entry protection.
- Cover successful split then option-off retry, and collection failure then JIT/all-static retry, with no stale split state.
- Verify split-cache reuse/invalidation for ancillary files, bindings, selection, identity, topology, DLL file/size/hash, and missing DLL; all-static and split caches never cross-reuse.
- Cover main-only, all-DLL, and mixed clang prebuild/in-tree MSVC paths and runtime-import rules.
- Complete inventory, pre-relocation, named refusal, unregister/rebind, two-lifetime, completion-dialog, all-static, regression, path, and PE-header checks.

### Part 2 planning notes (to verify)

Use a per-export seam or separate frontend process because the MCP harness cannot mutate the split option within one frontend. Build sparse canonical ExeFS fixtures with contiguous page-aligned bases and seed cross-module targets as discovery roots. Keep inventory binding independent of `FindModules`, which can drop a module when its debug path read fails. Use an isolated `Core::System` host seam for two lifetimes; add process-runner/toolchain injection for compiler/link failures. Fix `export_regression` Qt lookup against the CPM package root rather than the build-tree layout.

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

## Session handoff (2026-10-06, ninth session, complete)

### State at handoff

- **Current local state:** The host build tree is built from the dev13 sources with export variables cleared; no working static Build export exists locally. Clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before host builds, then re-export without FMI to obtain a package; the dev8 module libraries remain available for image inspection outside the repository.

- **Repository:** HEAD is `aa8c2aa` (README-only commits on top of `d764d8e`, v0.0.1-dev13); the project was renamed from HisuiWake to Aevora on 2026-10-05, and the other documents now follow the rename. The local repository folder remains named HisuiWake. Uncommitted: documentation and role-rule changes remain user-owned.
- **Local environment:** The dev8 module libraries built for the image measurement are in a local directory outside the repository; the host build tree is built from the dev13 sources with export variables cleared.
- **Diagnostics:** the local diag directory outside the repository contains `sample_static.ps1` and `RipSampler2.cs`, with `asm_probe` and `fmi_probe` material. Launch from a non-elevated shell; use `powershell -File sample_static.ps1 -ProcId <launcher PID> -Out <file>`.
- **Build/test:** in the Build Tools 2026 `vcvars64` environment, run `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls` (about 100.6 M cases; reduced: add `--cases 1000 --adv 2000 --legs 13`), `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`, and `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl`. Python must read generated files as UTF-8. Run helper `.cmd` files that set up `vcvars64` through PowerShell `cmd /c`, not from Git Bash where `vswhere` is not on PATH. The `tests/package_policy` unittest wrapper fails on cp932 decoding of `cl` output, so build `tests/package_policy/policy_unit.cpp` by hand. `tests/export_regression/run_fixture.py` cannot run until its Qt-root lookup accepts the CPM Qt package layout. `rebuild-suyu.bat` clears export variables and builds with `-j 8`; incremental builds use `cmake --build <build-dir> --target suyu suyu-cmd -j 8`.
- **Switches:** `SUYU_FPCR_STATS=1` logs guest FPCR and adds a lock per save; never use it for FPS. `SUYU_RECOMP_CHAIN_BUDGET` is 1-8192, default 256. `SUYU_AOT_TRANSLATE_ALL=1` applies to Hybrid; static AOT forces translate-all and strict mode. `SUYU_AOT_FM_INLINE=1` enables FMI at export only when FM1 is enabled, but currently breaks the PLA static export with LNK1248 and remains off.

### What this session did

1. Replaced the abolished quick-implementer role with main-agent implementation and verification; retained Orca Code reviewer, Code Explorer, and md Implementer roles.
2. Completed 4(a): first review found 0 blocker / 2 major / 1 minor, then 0 / 0 / 2; findings were fixed and the unit was accepted.
3. Completed 4(b): first review found 0 / 3 / 1, then 0 / 0 / 0; F3 was carried as an explicit intermediate criterion.
4. Completed 4(c) part 1: reviews found 0 / 5 / 3, then 0 / 1 / 2, then 0 / 0 / 0; exporter/CMake/cache/package wiring was accepted and committed through dev13.
5. Updated current-status and resume rules, corrected dev13/HEAD facts, and condensed this plan while preserving detailed history in OLD_HISTORY.
6. Applied usage-reduction rules: one behavior per implementation unit, one verification run when evidence is unchanged, and batched Markdown updates at milestones.

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

1. Complete 4(c) part 2 acceptance with synthetic exports, linked v1 launcher execution, failure/retry/cache paths, and runtime seams.
2. Exercise the dev10 failure paths synthetically.
3. Remaining items from the old list item 5: code review of `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, the pre-existing GG1 `g_recomp_gg_word` link failure, and existing export/GPL/rtld/BUILD.md/version investigations.
4. Complete 4(d) three-module fixture and decide build-kit support; then run mixed recompiler/export regressions and host verification.
5. Repeat PLA static AOT without/with FMI after the split and complete the gameplay checklist.

### Immediate next action

Milestone 4a step 4(c) part 2 proposed plan (proposed; adjust after the first unit):

1. Add a per-export split-option seam so one frontend instance can run split and non-split exports in sequence, including invalid values; verify the `tests/export_integration` scenarios.
2. Add a multi-module synthetic ExeFS fixture generator with a sparse canonical set, page-aligned contiguous bases, a cross-module call target seeded as a discovery root, and an optional unreadable embedded path in a middle module; verify it with a JIT/all-static run of the new fixture.
3. Drive that fixture through a split static export using the real exporter and run the linked v1 launcher with JIT fallback disabled; inspect the registry, marker, selection, DLL collection, manifests, hashes, per-image `SizeOfImage`, the `recomp: bound inventory` and `recomp: pre-relocation modules` log lines, and the guest marker. Also verify the all-DLL (main-only) topology on the clang prebuilt route.
4. Add split Hybrid export coverage, source/analysis failure cases (code-less module, missing `main`, and non-canonical NSO), and failure/retry injection for emission, compiler, DLL link, and LNK1248 through a process-runner or toolchain seam.
5. Add the cache reuse/invalidation matrix and package checks for missing or tampered DLLs, extra DLLs, and unchanged all-static v4 artifacts.
6. Add a core-level seam for two application lifetimes: bind `RecompProcess` twice, unregister on `SetRecompLookup(nullptr)`, and verify named `Core::Load` refusals with zero guest execution; first verify the feasibility of the smallest test-only host around `Core::System` allowed by the dependencies.
7. Fix the `export_regression` Qt-root lookup so the fixture runs again.

After this group, obtain Code reviewer review. Then continue with step 4(d) and the remaining criteria in the open issues list. Start with the per-export seam and synthetic multi-module fixture.

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

## Progress record

### 2026-10-06: Milestone 4a step 4(c) part 1: split export wiring in exporter, CMake, cache, and package (v0.0.1-dev13)

- **Milestone and revision:** Milestone 4a step 4(c), part 1; base HEAD `e38f7ad` (v0.0.1-dev12); implementation remained uncommitted.
- **Problem and evidence:** The main-module DLL split needed exporter ownership of the option, bundle identity, canonical topology, cache/package records, and linked launcher/CMake handling. `SUYU_AOT_SPLIT_MAIN` is resolved once per export; split mode emits registry v1 and its marker, binds every canonical module, collects and verifies DLLs, and records per-image PE `SizeOfImage`; all-static v4 remains unchanged.
- **Files changed and reasons:** `src/suyu/game_export.cpp` adds split validation, bundle-id/cache/manifest/package wiring, image-size diagnostics, and completion wording; `src/core/recompiler/pe_image_size.h` adds a validated PE32+ reader; `src/core/loader/nso_module_names.h` centralizes the canonical list; `src/suyu_cmd/CMakeLists.txt` and `src/suyu_cmd/recomp_modules/RecompDllTarget.cmake` implement v1 in-tree/prebuild DLL handling; `src/common/package_policy.*` records required module DLLs; `src/core/arm/recomp/arm_recomp.cpp` adds bound-inventory and process-scoped pre-relocation diagnostics; packaging and test files cover the corresponding checks.
- **Commands/settings:** Smoke (including PE/canonical-list units, 21 bundle cases, and all-DLL prebuild), fastmem, reduced FPX, package-policy, export-build-kit, host `suyu`/`suyu-cmd` build with export variables cleared, and split/all-static in-tree/prebuilt configure/link checks were run under Build Tools 2026 `vcvars64`; exact local logs remain outside the repository.
- **Result and untested behavior:** Smoke, fastmem, reduced FPX, package policy, kit tests, host build, and configure/link checks passed. Exporter end-to-end execution, linked v1 launcher guest execution, loader behavior against a game, and `export_regression` remain unverified; part 2 acceptance is still open.
- **Next action:** Plan and implement Milestone 4a step 4(c) part 2 from the acceptance list above.

### Older progress entries

- [4(c) part 1 archived in OLD_HISTORY](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4c-part-1-split-export-wiring-in-exporter-cmake-cache-and-package-v001-dev13)
- [4(b) archived in OLD_HISTORY](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4b-registry-v1-bundle-loading-and-inventory-binding-v001-dev12)
- [4(a) archived in OLD_HISTORY](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4a-module-topology-identity-export-and-prebuild-dll-binding-v001-dev11)
- [Step 3 archived in OLD_HISTORY](OLD_HISTORY.md#2026-10-06-milestone-4a-step-3-main-module-dll-image-measured-without-and-with-fmi)
- [Design review archived in OLD_HISTORY](OLD_HISTORY.md#2026-10-06-milestone-4a-design-note-reviewed-and-accepted-steps-1-2)
- [Image budget archived in OLD_HISTORY](OLD_HISTORY.md#2026-10-05-static-launcher-image-budget-dev8-no-fmi-versus-fmi-section-totals)
- [Dev10 LNK1248 archived in OLD_HISTORY](OLD_HISTORY.md#2026-10-05-exporter-stops-on-lnk1248-and-keeps-build-failure-logs-v001-dev10-unverified-by-export)
