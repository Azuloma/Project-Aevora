# Aevora Work Plan

Handoff prepared: 2026-10-07. HEAD: `7db8f01` (`v0.0.1-dev16`); the user commits, tags, and pushes. Start at [Session handoff](#session-handoff-2026-10-07-end-of-twelfth-session); archived detail is in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Evidence remains scenario-specific: generation, compilation, or startup does not establish game compatibility.

## Handoff status

| Item | Evidence/status |
|---|---|
| Git baseline | HEAD `7db8f01`; U1 `33ed262`, U2 `3dc53c7`, and U3a `71b5fd6` are committed; the current test additions remain uncommitted. |
| Milestone 4a | Steps 1-3, 4(a)-(c) part 1, 4(d) U1-U4, and Step 5's covered synthetic-fixture cases are done. Remaining Step 5 cases, the two-application-lifetime seam, and PLA verification are open. |
| Persistent cache | U1-U4 and Step 5 cache cases verified on the synthetic fixture only: `--only-aot-cache` 272 passed / 0 failed; Build/Windows only. |
| Host and PLA | Windows x64 source build verified. Historical JIT/static measurements are retained below and are not a matched comparison. |

## Key facts

- Windows baseline: MSVC 14.51/Build Tools 2026, Ninja 1.13.2, CMake 4.4.3, clang-cl 22.1.8, Python 3.14.7, Vulkan SDK 1.4.363.0; use a short ASCII `CPM_SOURCE_CACHE`. ([details](OLD_HISTORY.md#2026-10-04-milestone-1-environment-and-first-configure-attempt))
- CPM is the active dependency path; extraction uses `cmake -E tar xf` for the non-ASCII archive issue. ([details](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- Runtime baseline: JIT 30 FPS; Hybrid 15-20 FPS; dev6 static 16-20 FPS; dev8 static 25-30 FPS with zero fallback in the observed scene. These are historical, unmatched measurements. ([JIT](OLD_HISTORY.md#2026-10-04-first-pla-jit-run-on-the-source-build), [Hybrid](OLD_HISTORY.md#2026-10-05-long-slice-chain-budget-lowered-to-256-v001-dev6-unverified-in-export), [dev8](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-verified-in-pla-static-aot-25-30-fps))
- FMI targeted tests pass, but the PLA launcher reached 2.19 GB and failed LNK1248; main DLL `SizeOfImage` was 1,459.1 MiB without FMI / 1,783.7 MiB with FMI. ([FMI](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248), [measurement](OLD_HISTORY.md#2026-10-06-milestone-4a-step-3-main-module-dll-image-measured-without-and-with-fmi))

## Milestones 1-3

Milestone 1 recorded the host/tool/dependency inventory, source build, export kit, and JIT baseline; lineage, module IDs, `BUILD.md`, and complete gameplay remain open. Milestone 2 recorded exporter/build issues, deterministic splitting, and a modified export; Source/path/compiler-route investigations remain open. Milestone 3 verified generated-code loading and gap/guard diagnostics; representative gameplay, matched comparisons, and fallback coverage remain open.

## Milestone 4: Evaluate fully static execution

Static AOT generation and strict no-fallback diagnostics are verified in the exercised scenario. Gap fixes, representative gameplay, matched comparisons, and unsupported-behavior documentation remain open; GPU shaders and runtime services are separate concerns.

## Milestone 4a: Main-module DLL split

**Goal:** Keep the `main` DLL and static images below the MSVC x64 2 GB limit with fallback disabled.

| Step | Status |
|---|---|
| 1 survey; 2 design; 3 image measurement | Done; design and measurements archived. |
| 4(a) topology/identity/prebuild; 4(b) registry/loader/inventory | Done. |
| 4(c) part 1 exporter/CMake/cache/package | Done; `d764d8e` / dev13. |
| 4(c) part 2 execution evidence | Units 1-5 done, including U1-U4 cache; listed cases remain. |
| 4(d) build-kit/runtime fixture | U1, U2, U3a, and U4 done on the synthetic fixture; U3b is on hold. Real `ArmRecomp` checks remain open under issue 1. |
| 5 tests | Covered: cache reuse in both binding directions, failure after DLL verification and before promotion, and kit-only split rejection. Remaining: (a) MSVC-route LNK1248 / `launcher_link` injection pairs; (b) NSO/NSP/XCI real-loader paths and real `ArmRecomp` null-callback/strict-miss refusals; (c) a stub image missing only the identity export; (d) static-module compile/link injection and generic `launcher_link` fallback to MSVC. |
| 6 PLA verification | Not started. |

### Accepted decisions D1-D7

D1 DLL-local runtime copy; D2 image/TLS state per image and shared state in host callbacks/context; D3 versioned registry v1/topology marker are authoritative and loading is transactional; D4 mixed in-tree/clang-cl modes use canonical names and `/MD`; D5 package, manifest, cache, and image checks derive from canonical topology; D6 kit stays static-only until split support is verified; D7 split is an explicit per-export option and all-static v4 is unchanged. One exporter-owned topology defines bindings, names, bundle, ABI, and features; generated code is never edited. ([full design](OLD_HISTORY.md#design-note-accepted-2026-10-06-draft-3))

### Carried acceptance criteria

4(c) requires real split export and linked v1 launcher with fallback disabled, unreadable middle-module path, complete inventories/relocation input, named `Core::Load` refusals with zero guest execution, unregister/rebind, and unchanged all-static v4. 4(d) U1-U4 verify the synthetic three-module static/DLL/static fixture, loader refusals, bridge-list and lookup-miss model, and regression suites. Step 5 covers cache reuse in both binding directions, failure after DLL verification and before promotion, and kit-only split rejection; the remaining test cases and real `ArmRecomp` seam remain open.

### Part 2 acceptance list

- [x] Synthetic split static/Hybrid exports, linked v1 launcher without fallback, inventories, relocation diagnostics, and main-only all-DLL topology.
- [x] Source/analysis refusals, including code-less module refusal; earlier ExeFS validation stops missing `main` and non-canonical NSO.
- [x] Injected emit/compile/DLL-link/launcher-LNK1248/tamper/missing-DLL failures and retry reporting.
- [x] All-static v4 regression and persistent AOT cache reuse/invalidation matrix; Build packages exclude `aot_cache`.
- [ ] Remaining cases: MSVC-route LNK1248 / `launcher_link` injection pairs; NSO/NSP/XCI real-loader paths; real `ArmRecomp` null-callback and strict-miss refusals; a stub image missing only the identity export; static-module compile/link injection; and generic `launcher_link` fallback to MSVC.

### Persistent AOT cache (implemented, U1-U4)

- Defaults: user-data `aot_export_cache`, Build format only, producer = suyu revision + build id.
- Entry key/layout authority: `src/suyu/aot_export_cache.h`; launcher, manifest, split module selection, and producer record are stored per entry.
- Publish occurs after precompile and before packaging; reuse copies only on a full match.
- Mismatches are named; a busy lock means no reuse and no publish; entries replace atomically.
- Build/Windows only; packages contain no `aot_cache`.
- Test seams and status fields cover cache root/producer identity, reuse, mismatch, busy, publish, and not-applicable outcomes.
- Synthetic fixture final run: `--only-aot-cache` 272 passed / 0 failed; full design/evidence is in [OLD_HISTORY.md](OLD_HISTORY.md#2026-10-07-persistent-aot-cache-u1-u4-design-decisions-implemented-behavior-matrix-evidence).

## Milestone 5: Implement improvements one at a time

For each feature, identify the responsible layer, preserve a baseline/default mode, and record repeatable correctness and performance evidence. Planned features are up to 4K, ultrawide, correctly timed 60 FPS, ambient occlusion, increased LoD/draw distance, and additional vegetation; none is accepted yet.

## Milestone 6: Prepare a reproducible public release

Verify a clean-checkout build, inspect release contents for private/game inputs, preserve GPL notices/source instructions, define `devN`, and align README/compatibility/changelog with evidence. Do not label an unverified build stable.

## Session handoff (2026-10-07, end of twelfth session)

### State

HEAD is `7db8f01` (`v0.0.1-dev16`); U1 `33ed262`, U2 `3dc53c7`, and U3a `71b5fd6` are committed, while the current test additions remain uncommitted. The final regression evidence is synthetic-fixture only; logs and work folders stay outside the repository.

### What was done this session

Completed 4(d) U1-U4: the xmod three-module static/DLL/static fixture, the required bridge callback list and bundle-boundary lookup-miss model, loader-side refusals and path cases, and full/targeted regression runs. Full smoke exited 0 with unchanged ABI5/FM1/FPX golden hashes; fastmem had 0 mismatches across 5 fmdiff and 4 GG1 legs; reduced FPX had 1,786,000 cases with 0 mismatches, and full FPX had 100,587,904 plus 30,320,000 cases with 0 mismatches. Step 5 added and verified cache reuse in both binding directions, failure after DLL verification and before promotion, and kit-only split rejection; the integration run passed 272 tests with 0 failures and the build-kit package suite passed 14 tests. The host tree was rebuilt successfully, linking both `suyu.exe` and `suyu-cmd.exe`. The bridge is a test model and FPX evidence shows value/flag agreement only, not native-path selection. Review findings for U1-U3a were fixed; no re-review of U1 fixes was run.

### Open issues, in priority order

1. Two-application-lifetime seam, including the real `ArmRecomp` null-callback refusal and strict lookup-miss refusal; the synthetic bridge/list and lookup model does not cover these.
2. Fix `tests/export_regression` Qt-root lookup for the CPM Qt layout so `run_fixture.py` runs again.
3. Complete remaining Step 5 cases: MSVC-route LNK1248 / `launcher_link` injection pairs; NSO/NSP/XCI real-loader paths; real `ArmRecomp` null-callback and strict-miss refusals; a stub image missing only the identity export; static-module compile/link injection; and generic `launcher_link` fallback to MSVC.
4. On PLA, repeat static AOT without/with FMI, run the gameplay checklist, and measure cache entry size (~3 GB expected), multi-GB launcher hash time, publish/copy time, and Hybrid publish/reuse; Step 6 is not started.
5. Add extra cache cases: ACL-unreadable manifest, post-match copy failure, rename/restore publish failure, two-process lock contention, and dirty-tree-only producer difference.
6. Older investigations: `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, pre-existing GG1 `g_recomp_gg_word` link failure, and export/GPL/rtld/BUILD.md/version investigations.

### Immediate next action

Start item 1: design the lifetime/unregister contract, verify the smallest host seam, obtain Code reviewer review, and update Markdown only after the unit loop is complete.

### How to work

- Never run two `rebuild-suyu.bat`/ninja invocations on one build tree. Run `cmd /c <local>\rebuild-suyu.bat` from PowerShell, clear the three export variables after exports, and keep logs outside the repository.
- The exporter log tail is not evidence; use `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused`. Use a short ASCII `D:\hwk\<name>` work folder.
- After `worker-start`, ensure `turnStart` is `observed`; otherwise send `orca terminal send --terminal <handle> --enter`. Run the full integration suite once per finished review-fix set; a test-only expectation fix needs syntax plus the affected case.
- Useful commands: `python tests/export_integration/run_export_integration.py --suyu-dir <build>/bin --work <new short folder> --only-aot-cache` and `--only-aot-cache-matrix`. Switches: `SUYU_FPCR_STATS`, `SUYU_RECOMP_CHAIN_BUDGET`, `SUYU_AOT_TRANSLATE_ALL`, `SUYU_AOT_FM_INLINE`.
- Smoke fixture command: `python tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl [--only-xmod]`; a `vcvars64` environment is required.

## Progress record

- 2026-10-07 cache U1-U4 and eleventh-session detail: [archived verbatim](OLD_HISTORY.md#2026-10-07-persistent-aot-cache-u1-u4-design-decisions-implemented-behavior-matrix-evidence).
- 2026-10-06 part 2 units 1-5: [archived verbatim](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4c-part-2-units-1-5-per-export-split-seam-multi-module-fixture-split-export-through-the-real-exporter-failure-injection).
- 2026-10-06 part 1: [archived verbatim](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4c-part-1-split-export-wiring-in-exporter-cmake-cache-and-package-v001-dev13).
