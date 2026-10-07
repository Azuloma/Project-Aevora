# Aevora Work Plan

Handoff prepared: 2026-10-07. HEAD: `71ce879`; the user commits and pushes. Start with [Session handoff (2026-10-07, end of eleventh session)](#session-handoff-2026-10-07-end-of-eleventh-session), then read the persistent-cache section and part 2 acceptance list; older detail is in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Keep evidence limited to tested scenarios: successful generation, compilation, or startup is not gameplay compatibility.

## Handoff status

| Item | Evidence/status |
|---|---|
| Source repository | Root contains the Suyu-derived source, build files, dependency metadata, and archived upstream documentation. |
| Git baseline | Initial commit `9f932d5`; HEAD `71ce879`; the user commits and pushes. |
| Milestone 4a | Steps 1-3, 4(a)-(c) part 1, and 4(c) part 2 units 1-5 are implemented through `02c0298`; persistent AOT cache units U1-U4 are implemented through `71ce879` and verified on the synthetic fixture; items 6-7, 4(d), remaining tests, and PLA verification remain open. |
| Project/documentation | The project is Aevora (local folder remains HisuiWake); project docs are in `docs/Aevora/`, upstream docs in `docs/Suyu/`, and README/license policy is retained. |
| Host source build | Verified with Build Tools 2026 MSVC 14.51 + Ninja; `docs/Aevora/BUILD.md` is not written. |
| PLA export/run | JIT: 30 FPS. Hybrid (dev6-era, translate-all, budget 256): 15-20 FPS. v0.0.1-dev8 static AOT + Build (clang-cl): 25-30 FPS with no fallback in the observed overworld scene; conditions were not fully matched. Dev9 FMI export failed to link: image about 2.19 GB exceeds the 2 GB x64 limit (LNK1248). Steps 1-3, 4(a), 4(b), and 4(c) part 1 are committed through `d764d8e`; split export and linked v1 execution remain unverified. |
| Proposed path patch | Superseded. The observed non-ASCII failure was the launcher's window-title conversion (fixed in v0.0.1-dev5). Filename sanitization was not implemented; the package name still contains `ﾃｩ`. |

## Key facts from archived history

- **Host/tool baseline:** Windows 11, MSVC 14.51/Build Tools 2026, Ninja 1.13.2, CMake 4.4.3, clang-cl 22.1.8, Python 3.14.7, Vulkan SDK 1.4.363.0; use a short ASCII `CPM_SOURCE_CACHE`. ([details](OLD_HISTORY.md#2026-10-04-milestone-1-environment-and-first-configure-attempt))
- **Configure/build:** use the recorded CMake configure with explicit Git/glslang paths and `cmake --build <build-dir> --target suyu suyu-cmd -j 8`. ([details](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **Dependencies:** CPMUtil uses `cpmfile.json`; extraction uses `cmake -E tar xf` because `file(ARCHIVE_EXTRACT)` failed on non-ASCII entries. ([details](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **JIT/GPL:** PLA 1.1.1 ran at 30 FPS; the GPL rendering defect was isolated and GPL disabled by default. ([details](OLD_HISTORY.md#2026-10-04-first-pla-jit-run-on-the-source-build))
- **Export/build:** the export kit was produced; clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before packaging. ([details](OLD_HISTORY.md#2026-10-04-export-build-kit-produced))
- **Generated-code scaling:** job-pool sizing and unit splitting use 8 MiB/5,000-block defaults and compiler-aware budgets. ([details](OLD_HISTORY.md#2026-10-04-generated-code-build-parallelism-milestone-2))
- **Runtime baseline:** PLA JIT is 30 FPS; Hybrid is 15-20 FPS at budget 256 after SHL and chain-budget investigations. ([JIT](OLD_HISTORY.md#2026-10-04-first-pla-jit-run-on-the-source-build), [Hybrid](OLD_HISTORY.md#2026-10-05-long-slice-chain-budget-lowered-to-256-v001-dev6-unverified-in-export))
- **AOT conclusion:** Hybrid executed AOT-recompiled native application code in the observed run, while runtime services and GPU macro JIT/shader compilation remain; static AOT with fallback disabled is the stronger check. ([details](OLD_HISTORY.md#2026-10-05-is-the-hybrid-cpu-path-ahead-of-time-static-recompilation))
- **Dev6 static AOT:** The fallback-disabled export reached 16-20 FPS in the observed scene. ([details](OLD_HISTORY.md#2026-10-05-first-static-aot-export-v001-dev6-runs-with-no-fallback-16-20-fps))
- **Dev8 FZ-aware FPX1 gate:** The harness recorded 0 mismatches over 100.6 M cases. ([details](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-v001-dev8-unverified-in-pla))
- **Dev8 static AOT:** The fallback-disabled export reached 25-30 FPS with zero fallback in the observed scene. ([details](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-verified-in-pla-static-aot-25-30-fps))
- **AOT/FMI:** dev6 static AOT was 16-20 FPS; dev8 was 25-30 FPS with zero fallback; FZ was set in about 96% of measured saves; FMI targeted tests passed but the PLA launcher reached 2.19 GB and failed LNK1248. ([dev6](OLD_HISTORY.md#2026-10-05-first-static-aot-export-v001-dev6-runs-with-no-fallback-16-20-fps), [dev8](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-verified-in-pla-static-aot-25-30-fps), [FMI](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248))
- **Sampling/FMI limits:** dev8's hot block was 2-3.5% of busy-core samples with 35-49% waiting; same-scene JIT CPU was 275.7% versus 320.3% static, under unmatched conditions. ([sampling](OLD_HISTORY.md#2026-10-05-dev8-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld), [comparison](OLD_HISTORY.md#2026-10-05-same-scene-jit-sampling-remaining-gap-is-guest-cpu-cost))
- **Split image budget:** main `SizeOfImage` is 1,459.1 MiB without FMI and 1,783.7 MiB with FMI. ([measurement](OLD_HISTORY.md#2026-10-06-milestone-4a-step-3-main-module-dll-image-measured-without-and-with-fmi))

## Milestones 1-3: concise status

- **Milestone 1:** Host/tool/dependency inventory, source configure/build, export kit, and JIT baseline are recorded and verified where marked in [OLD_HISTORY.md](OLD_HISTORY.md); source lineage, module IDs, `BUILD.md`, and the complete JIT gameplay check remain open.
- **Milestone 2:** Exporter/build issues, deterministic unit splitting, and a successful modified export are recorded; separate Source inspection, path matrix, `recomp_registration.c.obj`, C1002, and compiler-route investigations remain open.
- **Milestone 3:** Generated code loading and gap/guard diagnostics are verified; the representative Hybrid gameplay checklist, matched measurements, and fallback-priority coverage remain open.

## Milestone 4: Evaluate fully static execution

Static AOT generation and strict no-fallback diagnostics are verified in the exercised scenario; gap fixes, representative gameplay coverage, matched JIT/Hybrid comparison, and unsupported-behavior documentation remain open. Acceptance is scenario-specific and does not establish whole-game compatibility; GPU shaders and runtime services remain separate concerns.

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

- [x] Synthetic multi-module fixture, split static and Hybrid exports through the real clang-cl exporter, linked v1 launcher execution without fallback, inventory/pre-relocation diagnostics, and the main-only all-DLL topology.
- [x] Source/analysis refusals reached by the available fixtures, including code-less module refusal; missing `main` and non-canonical NSO are stopped earlier by ExeFS validation.
- [x] Test-driven failure/retry injection for emit, compile, DLL link, launcher LNK1248, and tamper/missing DLL packaging; rejection reasons are reported without output.
- [x] All-static v4 export remains unchanged and runs the full synthetic chain.
- [x] Persistent AOT cache and its reuse/invalidation matrix; Build exports continue to delete `aot_cache` from packages.
- [ ] Items 6 and 7, and the not-covered cases: `launcher_link`, static-module compile/link injection, MSVC-route failures, real LNK1248 reproduction, two application lifetimes, and `export_regression` Qt-root lookup.

Evidence for the completed items is recorded in [OLD_HISTORY.md](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4c-part-2-units-1-5-per-export-split-seam-multi-module-fixture-split-export-through-the-real-exporter-failure-injection).

### Persistent AOT cache (DECIDED; option b)

The user chose option b: reintroduce and verify a persistent AOT cache. Benefits are faster re-exports, reproducibility checks, preservation of good builds after packaging failures, and making the existing reuse code live and tested; costs are about 3 GB per PLA entry, stale-artifact risk, and implementation effort. `RunAotPrecompile` already compares the manifest's full scan, backend, ABI/features, FMI, correctness revision, translate-all, source hash, coverage fingerprint, fallback policy, and split bundle/bindings/topology/DLL records, but reuse is unreachable because each export creates fresh staging; Build packaging also removes `aot_cache`.

The confirmed defaults are a user-data store, Build format only, and producer identity based on the suyu revision plus build id. The store is `aot_export_cache` under suyu user data, outside the repository and output folder. Each entry is `aot_export_cache/<title_id>_<platform>_<backend>_<split|all-static>/`; underscores are separators and key parts may contain dashes but never underscores. Layout is authoritative in `src/suyu/aot_export_cache.h`: `launcher/static_launcher.exe`, `aot_manifest.json`, `exefs/recomp_modules.cmake` for split entries, and `cache_entry.txt` with layout version, entry name, `producer.suyu_revision`, and `producer.build_id`. The platform name is `windows`.

**Implemented behavior:** U1 (store/key, atomic publish, lock, producer identity) is committed as `2043b92`; U2 (publish and failed-export preservation) as `edb6eb5`; U3 (persistent reuse and split reader) as `71ce879`; U4 (integration matrix and supporting status fields) is implemented but uncommitted at hand-off. Publishing occurs after successful AOT precompile and before packaging; valid artifacts remain available when later packaging fails. Reuse takes the per-entry lock without waiting, validates the record and artifacts, compares every recorded input and the manifest launcher hash/size, and copies only a full match. Mismatches name the item; lock contention skips both reuse and publish; entries replace in place without generations/LRU; source-format and non-Windows exports do not use the cache; packages contain no `aot_cache`.

The integration matrix covers identical reuse, source/backend/split/coverage/generator/producer invalidation, missing or tampered artifacts, failed export preservation, lock contention, and package exclusion. Test seams provide a cache root and producer revision/build id only for test-driven exports; status fields expose `reused`, named mismatch/reason, busy, publish, and not-applicable outcomes. Open: Hybrid publish/reuse on real PLA; PLA entry size, multi-GB launcher hash time, publish/copy time, real split export, and FMI; ACL-unreadable manifests, post-match copy failure, rename/restore failures, process-to-process unit lock contention, and dirty-tree-only producer changes; plus items 6/7 and the remaining acceptance cases listed above.

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

## Session handoff (2026-10-07, end of eleventh session)

### State at handoff

- **Current local state:** HEAD is `71ce879`; U1 `2043b92`, U2 `edb6eb5`, and U3 `71ce879` are committed; U4 is uncommitted. Uncommitted files are `src/suyu/game_export.cpp`, `src/suyu/game_export.h`, `src/suyu/main.cpp`, `tests/export_integration/run_export_integration.py`, and these Markdown files; the user commits. The host build tree was restored with the three export variables cleared; local work folders and logs are outside the repository.

- **Build/test:** Final `python tests/export_integration/run_export_integration.py --suyu-dir <build>/bin --work <new short folder> --only-aot-cache` passed 262/0; use `--only-aot-cache-matrix` for the matrix alone. Fixture entries are about 0.03 GiB. `rebuild-suyu.bat` rebuilt suyu and suyu-cmd afterwards and the three export variables were cleared. Never run two rebuilds or ninja invocations on one build tree concurrently.
- **Switches:** `SUYU_FPCR_STATS=1` is diagnostic-only and must not be used for FPS. `SUYU_RECOMP_CHAIN_BUDGET` is 1-8192, default 256. `SUYU_AOT_TRANSLATE_ALL=1` applies to Hybrid; static AOT forces translate-all and strict mode. `SUYU_AOT_FM_INLINE=1` remains off for PLA because the prior static export exceeded the 2 GB limit.

### What this session did

- Implemented persistent AOT cache U1-U4: U1 `2043b92`, U2 `edb6eb5`, U3 `71ce879`, and U4 uncommitted. The three confirmed defaults are a user-data store, Build format only, and producer revision plus build id.
- Verification counts: `--only-multimodule` 35 passed/0 failed; split Hybrid 16/0; injection 24/0; all-static 11/0; source refusals 3/3.
- Per-export seam run passed new split-option checks; two pre-existing failures need the Python `cryptography` package.
- Fixed the launcher LNK1248 blocking dialog in automated runs.
- The final synthetic-fixture cache run passed 262/0; status fields are the evidence because suyu's log tail is lost at exit.

### Verified context retained from history

JIT 30 FPS; Hybrid 15-20 FPS; dev6 static 16-20 FPS; dev8 static 25-30 FPS with zero fallback; FMI export 2.19 GB fails LNK1248; main DLL `SizeOfImage` is 1,459.1 MiB without FMI / 1,783.7 MiB with FMI. These measurements are historical evidence, not a matched comparison; details remain in [OLD_HISTORY.md](OLD_HISTORY.md).

### Open issues, in priority order

- Two-application-lifetime seam: bind `RecompProcess` twice, unregister on `SetRecompLookup(nullptr)`, and verify named `Core::Load` refusals with zero guest execution.
- Remaining Milestone 4a cases: `launcher_link` generic launcher-link failure (fallback to MSVC), static-module compile/link injection, MSVC-route failures, real LNK1248 (needs PLA), and real PLA split export.
- Older open investigations: review `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, the pre-existing GG1 `g_recomp_gg_word` link failure, and export/GPL/rtld/BUILD.md/version investigations.
- PLA static AOT repeats without/with FMI and the gameplay checklist remain open.
- Fix `tests/export_regression` Qt-root lookup for the CPM Qt package layout, then complete step 4(d).
- PLA repeats remain open: measure entry size, multi-GB launcher hash time, publish/copy time, real split export, and FMI. Hybrid cache behavior is verified only on the synthetic fixture.
- Remaining open cases include ACL-unreadable manifests, post-match copy failure, rename/restore failures, process lock contention unit coverage, dirty-tree-only producer changes, and items 6/7.

### Immediate next action

Implement the two-application-lifetime seam, then `export_regression` Qt-root lookup, step 4(d), and PLA repeats measuring entry size, launcher hash time, and publish/copy time.

### How to work

- Read `CLAUDE.md`, this handoff, the part 2 acceptance list, and the persistent-cache section; run `git status` and `git log` first.
- Unit loop: decide cause/approach, implement one behavior, verify, obtain Code reviewer review, fix findings, batch Markdown, report files and a suggested commit message; the user commits.
- Use Build Tools 2026 `vcvars64`; run `cmd /c <local>\rebuild-suyu.bat` from PowerShell (not Git Bash). After every export run, rebuild to clear the three export variables. Keep logs outside the repository and use Source format for generated C inspection.
- Orca: write worker specs to a file and start with `--spec "$(cat file)"`; recover `turn_start_unobserved` via `orca terminal list --json` and `orca terminal send --terminal <handle> --enter`; wait once with `orca orchestration check --wait --types worker_done,escalation --timeout-ms 3600000` in background; verify diff, `worker-release`, then `--ack`.
- Never use `SUYU_FPCR_STATS` during FPS runs; do not export PLA with `SUYU_AOT_FM_INLINE=1` until the image budget is solved; read generated files as UTF-8 in Python on Windows.

Keep raw logs, machine-specific paths, and game-derived artifacts local. After meaningful changes, record revision, evidence, files/reason, commands/settings, results/limits, and next action; link only to sanitized public evidence.

## Progress record

- 2026-10-06 part 2 units 1-5: [archived verbatim](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4c-part-2-units-1-5-per-export-split-seam-multi-module-fixture-split-export-through-the-real-exporter-failure-injection).
- 2026-10-06 part 1: [archived verbatim](OLD_HISTORY.md#2026-10-06-milestone-4a-step-4c-part-1-split-export-wiring-in-exporter-cmake-cache-and-package-v001-dev13).

Older design, measurement, and exporter entries remain in [OLD_HISTORY.md](OLD_HISTORY.md).
