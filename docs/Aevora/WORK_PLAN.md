# Aevora Work Plan

Handoff prepared: 2026-10-04. HEAD: `a3063a2` (docs commit after `c2c921e` `v0.0.1-dev9`). Last session handoff: 2026-10-05, seventh session (see [Session handoff](#session-handoff-2026-10-05-seventh-session-complete)).
Older history (2026-10-04 handoff through the fourth session close on 2026-10-05, plus earlier export evidence) is archived in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop the requested rendering and gameplay improvements on a verified runtime. Keep the code understandable and maintainable by humans, with human review and hands-on testing.

Start with a reproducible Windows x64 build and a correctly running game. Proceed to graphics improvements after the execution path is understood. Fully static PLA compatibility remains uncertain.

## Handoff status

| Item | Evidence/status |
|---|---|
| Source repository | Root contains the Suyu-derived source, build files, dependency metadata, and archived upstream documentation. |
| Git baseline | Initial commit `9f932d5`. As of 2026-10-05, HEAD is `a3063a2`; the user commits and pushes. |
| Configured origin | `https://github.com/Azuloma/Aevora.git`; remote publication was not independently verified during this handoff. |
| README | English draft describes feasibility status, planned features, support scope, versioning, and the hobby/no-donations policy. |
| License | Root `LICENSE.txt` is copied unchanged from the archived upstream license; README specifies `GPL-3.0-or-later`. Existing notices are retained. |
| Documentation | Project documents belong in `docs/Aevora/`; upstream documents are archived in `docs/Suyu/`. |
| Project name | Renamed from HisuiWake to Aevora on 2026-10-05 (README in `a3063a2`, other documents in this change); the local repository folder is still named HisuiWake. |
| Dependencies | Acquired by CPMUtil from `cpmfile.json` (bundled Qt 6.11.1, SDL3, FFmpeg, OpenSSL on MSVC); `.gitmodules` is not the acquisition path. Verified on 2026-10-04. |
| Host source build | Verified: MSVC 14.51 (Build Tools 2026) + Ninja Release; `suyu.exe`, `suyu-cmd.exe`, and the export build kit build and start. `docs/Aevora/BUILD.md` is not written yet. |
| PLA export/run | JIT: 30 FPS. Hybrid (dev6-era, translate-all, budget 256): 15-20 FPS. v0.0.1-dev8 static AOT + Build (clang-cl): 25-30 FPS with no fallback in the observed overworld scene; conditions were not fully matched. Dev9 FMI export failed to link: image about 2.19 GB exceeds the 2 GB x64 limit (LNK1248). |
| Proposed path patch | Superseded. The observed non-ASCII failure was the launcher's window-title conversion (fixed in v0.0.1-dev5). Filename sanitization was not implemented; the package name still contains `é`. |

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

## Milestone 4a: Main-module DLL split (design plan)

**Goal:** Build `main` as its own DLL image loaded by the static launcher while `rtld`, `sdk`, `subsdk0`, and `subsdk1` stay statically linked, so each image stays under the 2 GB MSVC x64 limit, including with FMI. Guest CPU fallback must remain disabled; the export must still run without JIT.

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

1. Code Explorer (Orca): enumerate globals/TLS and every exe<->module symbol dependency for D1/D2; confirm the launcher DLL loader path and ABI6 handshake details for D3.
2. Parent session: write the design note (sections D1-D7, data flow at load time, failure modes, test plan) as a new subsection of this milestone; Code reviewer reviews the design against CLAUDE.md before any code change.
3. Prototype measurement before full implementation: link main alone as a DLL from the existing dev8 module objects (local, outside the repository) to measure its real image size and confirm the 2 GB margin; record it.
4. Implementation (code-quick-implementer), in reviewable steps: (a) generator/CMake mixed mode, (b) registry + launcher loading, (c) exporter packaging/manifest/wording, (d) build-kit decision. Code reviewer reviews each step.
5. Tests: extend `tests/recompiler_smoke` with a mixed static+DLL variant (synthetic modules) covering load order, base routing, cross-module dispatch, guard checks, GG1, and FPX; keep existing goldens unchanged for non-split exports; run the fastmem and FPX harnesses.
6. PLA verification: export static AOT (fallback disabled) without FMI, then with FMI; record per-image sizes, link time, zero JIT transitions, FPS in the same overworld scene, and sampling; compare with dev8 (25-30 FPS) and JIT (30 FPS).

**Acceptance:** design note reviewed with no unresolved findings; main DLL and launcher each link under 2 GB for PLA with FMI; strict static mode shows zero JIT transitions and no lookup misses in the exercised scene; synthetic mixed-mode tests pass; packaging, manifest, and documentation describe the two-file layout; remaining limits (main margin, build-kit support) are documented.

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



## Progress record

After each meaningful change, add a dated entry with:

- Milestone and source revision.
- Problem and supporting evidence.
- Files changed and reason for the chosen approach.
- Exact build/test commands and relevant settings.
- Observed result, comparison baseline, and untested behavior.
- Next action or blocking information.

Keep raw logs, machine-specific paths, and game-derived artifacts local. Link only to sanitized, shareable evidence from public documentation.


- **Archive rule:** At each session close, move progress entries older than the previous session (and their session-close entry) verbatim to OLD_HISTORY.md, and add any still-relevant fact to Key facts from archived history.





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
