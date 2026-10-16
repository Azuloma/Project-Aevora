# Aevora Work Plan

Handoff prepared: 2026-10-16 (session 30). Older material: [session 29 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-29-cleanup), [session 30 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-30-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD ac96270 = v0.0.1-dev25. The host was reconfigured and rebuilt at ac96270; re-check `GIT_REV ac96270` at session start. The last PLA export is `D:\HisuiWakeExports\_NEW` at ac96270, fully static, with TTAS, lock-free marks, and chain start default-on; launcher and PDB are in `D:\HisuiWakeBuild\backup\ac96270_aot`. Older backups are `d0ae37b_aot` and `f943708_aot`/`_jit`; evidence logs and samples are in `D:\HisuiWakeBuild\logs`, analysis scripts in `D:\HisuiWakeBuild\tmp_gt`. Optimization is paused; ImGui Stage F is the focus. These Markdown changes and previous batches are uncommitted until the user commits.

## What was done so far

- Dev19: FPXV; see CHANGELOG.md.
- Dev20: sharded counters, two-line comment rule, opt-in chain start; see CHANGELOG.md.
- Dev21: guest-time ledger and lookup diagnostics; see CHANGELOG.md.
- Dev22: `SUYU_RECOMP_EXCL_STATS`; see CHANGELOG.md.
- Dev23: cross chain default-on, lock-free-mark option, atomic monitor slots; see CHANGELOG.md.
- Dev24: 32-bit block index, index-miss shortcut, launcher index report; see CHANGELOG.md.
- Dev25: access diagnostics, SpinLock TTAS, and default-on TTAS/lock-free marks/chain start; see CHANGELOG.md.
- Session 30: measured TTAS and ac96270 PLA export; main DLL 1,721.8 MiB, launcher 483.5 MiB, 26 FPS versus 24.3 FPS unattributed; see CHANGELOG.md.

## Immediate next action

Run F0, the read-only Code Explorer survey, then implement F1; see the Stage F plan.

## Stage F plan (ImGui Settings, current focus)

Goal: Settings -> Graphics and Controller (Player 1) windows plus the Mods/folders and status content of the F12 panel in the ImGui overlay, then retire the F12 panel. Each stage has one behavior, tests with a negative control, one Code reviewer round, and a user commit; settings changes use the action queue and shared definitions.

- F0 survey: SDL event pump and queue drain; settings/HID callers; SDL text input; resolution table; mods discovery; every F12 reference. Read-only, output path:line references.
- F1 keyboard/text forwarding: owned key presses, text events, focus reset, bounded queue, atomic text-input request; add router tests and a weakened-router negative control.
- F2 UI action queue: bounded ordered event-thread queue and status channel; test ordering, overflow, cross-thread drain, and wrong-side execution.
- F3 Graphics: Resolution Scale only, next-launch note, one shared 13-entry table, and apply-action tests.
- F4 Controller: Player 1 device/binding/Joy-Con actions, non-blocking rebind state machine, and standalone tests.
- F5 Mods/folders/status: parity with the F12 panel.
- F6 retire F12: remove panel/key branch and update all references after user confirms PLA parity.

## Open issues in priority order

### Group E - ImGui Stage F

F0 survey, F1 keyboard/text, F2 action queue, F3 Graphics, F4 Player 1 Controller, F5 Mods/folders/status, and F6 F12 retirement; follow the Stage F plan above.

### Paused optimization backlog

- B2: separate monitor lock and reservation slots with `alignas(64)`; evaluate after TTAS.
- B3: exclusive timing/bridge shortening; baseline was deferred by the user.
- B5: plain-store unlock needs STLXR/LDAR ordering analysis.
- B6: dispatch/lookup/slice was 3.18% of samples; sdk/main exits about 8M each way per 10 s.
- A7: FMI checks were 3.9% of samples; refine only with fault handling preserved.
- A8: prologue/entry checks were 1.5%/1.1%; chain-start on/off A/B remains outstanding.
- C9: rasterizer write notification is about 1.1 us/store, about 1.6 core-ms/frame.
- D10: XMOD1 accounting, miss/gap recording, IVAU, budget stop, and publication-log checks remain.
- D11: older diagnostics, attribution, loader/refusal, export-regression, and carried A/B items remain.

Details and the full Group A/B/C/D lists are archived in OLD_HISTORY.md. The exclusive-access upper bound was about 14 core-ms/frame; SpinLock baseline was 6.0%/5.57%, while TTAS was 5.79% vs 5.88% page and 5.28% vs 5.50% host functions. Chain-start A/B and lock-free soak remain outstanding; the 26 FPS attribution is open.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position averaged FPS with ms/frame. Cross chain, chain start, TTAS, lock-free marks, FM inline, direct/indirect chain, split main, and FPX options are default-on (`=0` disables); ledgers and lookup/exclusive/access diagnostics remain opt-in. Preserve saved-value CAS, atomic monitor slots, the 32-bit index contract, and overlay decisions: F12 retires after ImGui parity, Resolution Scale applies next launch, and controller UI is Player 1 only.

## How to proceed

Use the Stage F rhythm: one behavior per stage, tests with a negative control, one Code reviewer round, user commits, and md Implementer batches at milestones. When resuming optimization, first use the user's on/off measurements; the user currently declined attribution measurements.

## How to work / procedures

- Clean every build/test/export/probe/experiment: use D:\HisuiWakeBuild\scratch\<task>\, then D:\HisuiWakeBuild\diag\cleanup_scratch.ps1; check targets and leave no D:\fx_*, __pycache__, or .bak.
- Keep msvc-release, cpm, diag, tmp_gt, ovl, imgui_dl, backup, logs, and root .bat helpers.
- Run .cmd/vcvars stages via cmd.exe /c from PowerShell; start each vcvars64 stage as its own process and check it is running within a minute. Never run concurrent builds/exports.
- Smoke: python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include; fixture-only: add --only-xmod.
- Export matrix: python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix, after vcvars and with an empty work folder.
- Host rebuild: D:\HisuiWakeBuild\diag\reconf_build.cmd with export variables cleared; after export tests run D:\HisuiWakeBuild\rebuild-suyu.bat via cmd.exe /c.
- Rebuild the host before export integration after generator changes; the compiled-in generator otherwise remains stale. A warm full run took about 11 minutes and a first core rebuild after configuration changes may take about 30 minutes.
- Exports are user-started; use a new empty output folder, same-position/orientation sampling, normal exit for logs, and a non-elevated shell. Never run CPU-heavy work during FPS measurement.
- FPXV: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes must stay unchanged.
- After commits reconfigure/build and verify GIT_REV; after every export copy logs and rebuild as required. Do not weaken negative controls; C++ tests use repository editing tools.
- Tooling lessons: Python edit scripts putting `\n` escapes into C++ string literals produced real newlines twice (use Edit for string literals); Start-Process needs its log file created first; Orca `check --wait` replays until its `delivery_*` id is acked.
- Exclusive measurement: lock-free marking and TTAS are on by default; use `$env:SUYU_EXCL_LOCKFREE_MARK="0"` for the locked condition and `$env:SUYU_SPINLOCK_TTAS="0"` for the old stub, with `$env:SUYU_GUEST_TIME="1"; $env:SUYU_RECOMP_EXCL_STATS="1"; $env:SUYU_RECOMP_LOOKUP_STATS="1"` for `excl-stats` (a separate run from FPS-only runs); stand still ~30 s, record averaged FPS, exit normally, read `excl-stats` from ~52 s, and copy the log after EACH condition (only the current and one previous log are kept).
- Exclusive test: `python tests/exclusive_access/run.py --build-dir D:\HisuiWakeBuild\msvc-release --work <folder outside the repo>` after vcvars64 (via `cmd.exe /c`) runs unset, `0`, `1` and `2` of `SUYU_SPINLOCK_TTAS`, each with locked and lock-free marks; it links host dynarmic.lib, so rebuild after dynarmic changes. A weakened ReadAndMarkLockFree must exit 1; restore in finally.
- Sampling (RIP samples of CPUCore_0..2): `D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>` from a separate non-elevated PowerShell while standing still; analysis with `D:\HisuiWakeBuild\tmp_gt\analyze_launcher.py <samples> <launcher.exe next to its pdb> <label> "Pokémon Legends_ Arceus.exe" <top>` and `tmp_gt\page_cmp.py` (SpinLock page offsets and exclusive-function shares). The remaining sampling, guest-time, lookup/access-stats, ARM64-dump and JIT-comparison procedures are archived verbatim in OLD_HISTORY.md (session 30 cleanup, `How to work / procedures`).
- Session 30 lessons: restoring a negative-control source with Copy-Item preserves old mtime, so rewrite/touch before rebuilding and rerun the check; `Remove-Item D:\fx_*` is blocked, use `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1 -Path D:\fx_<name>`; start Orca workers from Bash with `--spec "$(cat <file>)"`; parse every JSON object from `orca orchestration check --wait`, select the one with `messages`, and pass `--ack` while checking dispatchId; run `vcvars64.bat` through PowerShell `cmd.exe /c "call vcvars64.bat >nul 2>&1 && ..."`; use `(std::numeric_limits<T>::max)()` when xbyak.h exposes windows.h's `max`; restore weakened negative controls and rerun the matrix, stopping after the expected FAIL only once worker processes are gone; warm export integration takes 13-15 minutes and `--only-defaults` about 3 minutes; F12's modal Win32 loop runs on the SDL event thread, so its settings changes already run there.

## Design notes

The arena fastmem design record is archived in OLD_HISTORY.md and was not implemented. The TTAS record and measurements are archived there; TTAS produced no measured gain and remains default-on by user decision.
