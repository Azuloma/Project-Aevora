# Aevora Work Plan

Handoff prepared: 2026-10-07. HEAD: `8818bd3` (dev16); the user commits, tags, and pushes. Start at [Session handoff](#session-handoff-2026-10-07-end-of-twelfth-session); archived detail is in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Evidence remains scenario-specific: generation, compilation, or startup does not establish game compatibility.

## Handoff status

| Item | Evidence/status |
|---|---|
| Git baseline | `main` at `8818bd3` (tests: split-export failure before promotion, DLL-to-static cache binding, and build-kit refusal); preceding `7db8f01` is the v0.0.1-dev16 Markdown revision. Working tree is clean except untracked `__pycache__` folders. |
| Milestone 4a | Steps 1-3, 4(a)-(c) part 1, 4(d) U1-U4, and the covered Step 5 cases are done on the synthetic fixture. Remaining Step 5 cases and PLA verification are open. |
| Persistent cache | `--only-aot-cache` passed 272 / failed 0; build-kit package suite passed 14 tests. Build/Windows synthetic fixture only. |
| Host and PLA | Host tree rebuilt with both executables linked. No PLA, real split export, real `Core::Load`, or real `ArmRecomp::RunThread` ran this session. |

## Key facts

- Windows baseline: MSVC 14.51/Build Tools 2026, Ninja 1.13.2, CMake 4.4.3, clang-cl 22.1.8, Python 3.14.7, Vulkan SDK 1.4.363.0; use a short ASCII `CPM_SOURCE_CACHE`. CPM is active; extraction uses `cmake -E tar xf` for the non-ASCII archive issue. ([history](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- Historical runtime evidence is unmatched: JIT 30 FPS, Hybrid 15-20 FPS, dev6 static 16-20 FPS, dev8 static 25-30 FPS with zero fallback in the observed scene. FMI reached 2.19 GB and failed LNK1248; main DLL `SizeOfImage` was 1,459.1 MiB without FMI / 1,783.7 MiB with FMI. ([history](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248))
- Everything recorded for this session is synthetic-fixture evidence. The xmod bridge/dispatcher is a model, not `ArmRecomp`; FPX proves value/flag agreement only.

## Milestones 1-3

Milestone 1 recorded the host/tool/dependency inventory, source build, export kit, and JIT baseline. Milestone 2 recorded exporter/build issues and deterministic splitting. Milestone 3 verified generated-code loading and gap/guard diagnostics; representative gameplay, matched comparisons, and fallback coverage remain open.

## Milestone 4: Evaluate fully static execution

Static AOT generation and strict no-fallback diagnostics are verified in the exercised scenario. Gap fixes, representative gameplay, matched comparisons, and unsupported-behavior documentation remain open; GPU shaders and runtime services are separate concerns.

## Milestone 4a: Main-module DLL split

**Goal:** Keep the `main` DLL and static images below the MSVC x64 2 GB limit with fallback disabled.

| Step | Status |
|---|---|
| 1 survey; 2 design; 3 image measurement | Done; detail archived. |
| 4(a) topology/identity/prebuild; 4(b) registry/loader/inventory | Done. |
| 4(c) part 1 exporter/CMake/cache/package; part 2 execution evidence | Done, including U1-U4 cache work; detail archived. |
| 4(d) build-kit/runtime fixture | U1-U4 done on synthetic fixture. |
| 5 tests | Covered; remaining cases are listed under open issue 4. |
| 6 PLA verification | Not started; user-run and requires game files and human observation. |

### Accepted decisions D1-D7

D1 DLL-local runtime copy; D2 image/TLS state per image and shared state in host callbacks/context; D3 versioned registry v1/topology marker are authoritative and loading is transactional; D4 mixed in-tree/clang-cl modes use canonical names and `/MD`; D5 package, manifest, cache, and image checks derive from canonical topology; D6 kit stays static-only until split support is verified; D7 split is an explicit per-export option and all-static v4 is unchanged. One exporter-owned topology defines bindings, names, bundle, ABI, and features; generated code is never edited. ([full design](OLD_HISTORY.md#design-note-accepted-2026-10-06-draft-3))

## Milestone 5: Implement improvements one at a time

For each feature, identify the responsible layer, preserve a baseline/default mode, and record repeatable correctness and performance evidence. Planned features are up to 4K, ultrawide, correctly timed 60 FPS, ambient occlusion, increased LoD/draw distance, and additional vegetation; none is accepted yet.

## Milestone 6: Prepare a reproducible public release

Verify a clean-checkout build, inspect release contents for private/game inputs, preserve GPL notices/source instructions, define `devN`, and align README/compatibility/changelog with evidence. Do not label an unverified build stable.

## Session handoff (2026-10-07, end of twelfth session)

### State

HEAD is `8818bd3` on `main`; the tree is clean except untracked `__pycache__` folders. The user commits, tags, and pushes. All session evidence is synthetic-fixture only.

### What was done

Finished 4(d) U1-U4 and the covered Step 5 cases: xmod three-module static/DLL/static fixture, required bridge callback list and bundle-boundary lookup-miss model, loader refusals and path cases, split-export failure after DLL check and before promotion, DLL-to-static cache binding, and build-kit split refusal. Full smoke exited 0 with unchanged golden hashes; fastmem had 0 mismatches; reduced FPX had 1,786,000 and full FPX had 100,587,904 plus 30,320,000 cases, all with 0 mismatches. Cache integration passed 272/0, build-kit package tests passed 14, and the host tree rebuilt with both executables linked.

### Open issues, in priority order

1. Two-application-lifetime seam: bind `RecompProcess` twice, unregister on `SetRecompLookup(nullptr)`, and find the smallest test-only `Core::System` host dependencies. This is also where real `ArmRecomp` null-callback/strict lookup-miss refusals and NSO/NSP/XCI loader paths would be verified.
2. Fix `tests/export_regression` Qt-root lookup for the CPM Qt layout so `run_fixture.py` runs again.
3. Step 6 PLA verification and cache measurements: static AOT without/with FMI, per-image `SizeOfImage`, link time, zero JIT transitions, loader inventory names, FPS in the same overworld scene, sampling; entry size ~3 GB expected, multi-GB launcher hash time, publish/copy time, and Hybrid publish/reuse. User-run.
4. On hold: MSVC-route LNK1248 / `launcher_link` injection pairs (needs a production injection hook in `game_export.cpp`, currently clang-route only); NSO/NSP/XCI real-loader paths and real `ArmRecomp` refusals (need `Core::System`, issue 1); stub image missing only identity export; static-module compile/link injection; generic `launcher_link` fallback to MSVC.
5. Extra cache cases: ACL-unreadable manifest, post-match copy failure, rename/restore publish failure, two-process lock contention, dirty-tree-only producer difference.
6. Older investigations: `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, pre-existing GG1 `g_recomp_gg_word` link failure, and export/GPL/rtld/BUILD.md/version investigations. Two pre-existing per-export-seam checks need Python `cryptography`.

### Immediate next action

Ask the user whether to start issue 1 or Step 6; no U5 exists. If issue 1, design the lifetime/unregister contract and verify the smallest host seam, then obtain Code reviewer review; update Markdown only after the unit loop is complete.

### How to work

- Keep the existing rules: never run concurrent rebuilds/ninja; keep logs and game-derived outputs outside the repository; clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` after export-running tests; use status fields rather than the exporter log tail; user commits/tags/pushes.
- Smoke: `python tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl [--only-xmod]` (Build Tools 2026 `vcvars64`, `CMAKE_GENERATOR=Ninja`). Fastmem: `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`.
- FPX: `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls`; reduced: add `--cases 1000 --adv 2000 --legs 13`.
- Package tests: `python -m unittest tests.export_build_kit.test_package`. Cache integration: `python tests/export_integration/run_export_integration.py --suyu-dir <host build>/bin --work <new short folder> --only-aot-cache`.
- After any export-running test, rebuild the host tree with the local `rebuild-suyu.bat`-style script with all three export variables cleared.

Archived history: see [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-07-from-work_planmd-end-of-twelfth-session).
