# Aevora Work Plan

Handoff prepared: 2026-10-09. Current HEAD is `a67035d` on `main`; the nineteenth-session detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-09-from-work_planmd-nineteenth-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

Milestone 4a topology, registry, loader, split exporter, persistent cache, and covered U1-U4 synthetic work are complete. PLA static AOT + FMI + split runs at 15-20 FPS versus about 30 FPS JIT, with the standard all-default image below the 2 GiB limit. The shared-FP change is recorded as `c821526`; the user observed considerably smaller generated files and about 1-2 FPS higher FPS after a fresh default-on export. Exact new sizes, FPS, hardware, and cache fields remain unrecorded.

## Evidence so far

| Run | CPU / FPS | Image size |
|---|---|---|
| JIT | 76.8/78.7/77.2 CPU; 30 FPS | not measured |
| Static AOT + FMI + split, earlier | 76.3/80.4/81.4 CPU; 17-20 FPS | Main DLL 1,857,218,560 bytes |
| 2bed3c6 all defaults | 71/76/76 busy; 15-20 FPS | Main SizeOfImage 1,923,067,904; launcher 495,382,528 |
| 2bed3c6 FMI-off | FPS change about 1 FPS, direction unrecorded | Main 1,588,723,712; launcher 397,508,608 |
| c821526 default-on | User observed about 1-2 FPS higher than 15-20; exact FPS not recorded | considerably smaller generated files; exact sizes not recorded |

Key observations: host profile is flat; roughly 34% of in-DLL samples are in functions at least 900 B per guest instruction; idle samples are no-runnable-guest-thread, not GPU/fence wait; no single guest thread is saturated; lock contention remains a hypothesis.

## Milestones

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; PLA acceptance, remaining real refusal paths, and Milestones 5-6 remain open.

## Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers; obtain standard and FMI-off image sizes.
- [ ] Read cache outcome/publish/reused from the next export dialog; record timings, loader inventory, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS and hardware details after each PLA run.

## Persistent cache rule

The user-data cache producer is git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration; never spoof producer metadata. Clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before the normal host rebuild.

## Held items and evidence boundary

Held: two-lifetime seam; real loader/refusal paths; `tests/export_regression` Qt-root lookup; MSVC `cl` chain route (synthetic smoke only); old-manifest-without-chain-fields reuse; six tracked `.pyc` files. Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 (explicit approval only) remain. PLA results are user-run and scene-specific; the recompiled image has no PDB, so DLL samples are RVA attribution only.

## Work log, sessions 17-19

- Session 17: overlay stages A-C were completed in `97e32dd` and `0ae08ea`.
- Session 17: overlay stages D-E and split-main work were completed in `575b796`.
- Session 17: default-on options and their review fixes were completed in `585c886`.
- Session 17: the seventeenth-session record contains the associated smoke, integration, and negative-control evidence.
- Session 18: `2bed3c6` added the per-guest-thread ledger.
- Session 18: FMI-off A/B image and FPS observations were recorded.
- Session 18: hot sampler offsets were attributed to real RVAs.
- Session 18: idle samples were attributed to no runnable guest thread, not GPU/fence wait.
- Session 19: cleanup rebuild exited 0 after export-running work.
- Session 19: `SUYU_AOT_FP_SHARED` shared exact FP bodies in `c821526`, with harness, smoke, and integration evidence.
- Session 19: three reviewer P2 findings were fixed and the documentation batch was completed.
- Session 19: the host was rebuilt with `GIT_REV c821526d36db0427e44be438c682afb8fa8cf905`.
- Session 19: the user observed smaller generated files and about 1-2 FPS higher FPS; exact values remain pending.

## Session handoff (2026-10-09, twentieth session)

### State

Current HEAD is `a67035d` (`v0.0.1-dev18: Optimization of the compilation process`). The shared-FP change and nineteenth-session user observation are recorded above; start by reconciling the current checkout and the recorded `c821526` host revision.

### Contracts to preserve

- Preserve FP_SHARED's exact per-module bodies, option/cache identity, and shared generator source of truth.
- Preserve overlay architecture, honest `near`/fallback wording, and process/input ownership contracts.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error.
- `SUYU_RECOMP_THREAD_TIME` is opt-in and must not change execution; never fake cache producer identity.
- Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.

### Open issues, in priority order

0. Obtain exact image sizes, `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`, FPS, hardware, and optional `SUYU_AOT_FP_SHARED=0` A/B from the user.
1. Investigate generator-side per-opcode-class host-size totals with a synthetic test and negative control; then consider SVC attribution by thread/address and one-variable chain exports.
2. Carry over Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 only with explicit approval.

### Immediate next action

Ask for the pending PLA data, then propose the per-opcode-class host-size investigation and let the user choose the next investigation.

## How to proceed

1. Read `CLAUDE.md` and this handoff; check `git status`/ `git log` and verify `GIT_REV` matches HEAD.
2. Ask for exact image sizes, the three cache fields, FPS and hardware, and optionally an `SUYU_AOT_FP_SHARED=0` A/B.
3. Propose per-opcode-class host-size totals with a synthetic test and negative control, then SVC attribution by thread/address, then one-variable chain exports; run one investigation at a time with the user choosing.
4. Use the standard cycle: design, implement, run the required FPX harness/smoke/integration tests, Code reviewer review, fix findings, one full suite run, host rebuild, Markdown batch, then give the user the commit file list and message.
5. After export-running tests, run the cleanup rebuild. After the user commits, reconfigure and verify `GIT_REV` before exporting.

## How to work / procedures

- Run `.cmd` scripts from PowerShell with `cmd.exe /c script *> log`; Bash `cmd.exe` invocation is ineffective here. Do not feed quoted Python/Markdown through shell heredocs.
- Exports are user-started in the GUI; environment experiments use a new empty output folder and clean up with `suyu.exe` closed.
- The game log is written only after normal game exit. Use a non-elevated shell and convert sampler `+0xN` offsets to real RVA by subtracting the module base.
- Never run concurrent builds; keep builds, exports, logs, caches, and game-derived outputs outside the repository. Use cache status fields rather than the lost log tail.
- FPX harness, smoke, and integration commands and verified logs are recorded in the archived nineteenth-session procedures.
