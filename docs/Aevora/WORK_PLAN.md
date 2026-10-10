# Aevora Work Plan

Handoff prepared: 2026-10-10. Current HEAD is `2805512` on `main`; detailed prior sessions are archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-10-from-work_planmd-twenty-second-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `2805512` (`v0.0.1-dev19: FPXV vector FADD/FSUB docs and AArch64 control exemption`) on `main`, on top of `544dad2` (FPXV code) and `a67035d` (dev18). FPXV had no measurable FPS effect in the user's usual PLA scene. User-observed A/B results show about 8.5 FPS with all listed optimizations off versus about 22 FPS with defaults, while one-variable direct/indirect chain-off results were 21/23.5 FPS; the chain-budget sweep was flat at 21-23 FPS. These observations are not agent measurements, and export identity, image sizes, cache fields, hardware, and exact environments were not provided.

## Evidence so far

| Run | Result | Evidence status |
|---|---|---|
| JIT | About 30 FPS in the usual scene | User observation |
| Fresh FPXV-on export (host `544dad2`) | Same FPS as before; no gain | User observation |
| All listed optimizations off | About 8.5 FPS | User observation; split-main setting unanswered |
| `SUYU_AOT_DIRECT_CHAIN=0` / `SUYU_AOT_INDIRECT_CHAIN=0` only | 21 / 23.5 FPS | User observation |
| `SUYU_RECOMP_CHAIN_BUDGET=32,64,128,256,512,1024` | 21, 22, 23, 22, 23, 22 FPS | User observation; value confirmed only by FPS |

The all-off result indicates generated-code speed matters; the earlier hypothesis that it was not the bottleneck is refuted. The flat budget sweep weakens the hypothesis that budget-bounded interrupt latency limits FPS. Chain-off results are within noise of the default and do not establish that chains hurt. The cause of the all-off drop and the remaining roughly 22-versus-30-FPS JIT gap are unresolved hypotheses.

Verified code facts: AOT CNTPCT uses `ArmRecomp::HostReadCntpct` and `CoreTiming().GetClockTicks()`, matching Dynarmic's source; the dispatcher checks `interrupted` only between slices; and `RecompThreadTimeLedger` records `RunThread` time, not waits or SVC service time, with no JIT counterpart.

## Milestones

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; PLA acceptance, remaining real refusal paths, and Milestones 5-6 remain open.

## Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers; obtain standard and FMI-off image sizes.
- [ ] Read cache outcome/publish/reused from the next export dialog; record timings, loader inventory, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS and hardware details after each PLA run.

## Persistent cache rule

The user-data cache producer is git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration; never spoof producer metadata. Before the next export, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, run the cleanup rebuild, reconfigure, and verify `GIT_REV`.

## Held items and evidence boundary

Held: per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 only with explicit approval. Also held are the two-lifetime seam, real loader/refusal paths, `tests/export_regression` Qt-root lookup, old-manifest reuse, and six tracked `.pyc` files. PLA results are user-run and scene-specific; the recompiled image has no PDB, so DLL samples are RVA attribution only.

## Work log

- Session 17: overlay, split-main, and default-on option work completed; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 18: thread-time ledger, FMI A/B, and sampler evidence recorded; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 19: FP_SHARED and review-fix batch completed; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 20: FPXV implementation and dev19 verification completed; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 21: FPXV export handoff recorded; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 22: A/B and chain-budget observations documented; this handoff is current.

## Session handoff (2026-10-10, twenty-second session)

### State

HEAD is `2805512` on `main`, on top of `544dad2` and `a67035d`. The host build tree `D:\HisuiWakeBuild\msvc-release` still has `GIT_REV 544dad26944b3c5913b12be0e47b159d6ea59c42`, so it is not configured for HEAD. User-observed PLA measurements in the same usual scene found no FPXV gain, about 8.5 FPS with all listed optimizations off, 21/23.5 FPS with direct/indirect chain disabled individually, and 21-23 FPS across chain budgets 32-1024; image sizes, cache fields, hardware, and exact export environments were not provided. It is unknown whether post-export cleanup was run.

Diagnostic-overhead change (uncommitted): `RecompShardedCounters` now supplies per-thread cache-line-aligned dispatch counters; `svc_calls`, `jit_to_static`, and the SVC histogram moved from the atomic-plus-mutex/map `RecompCounters` path, with SVC ids `>= 256` reported in one overflow line. Fallback JIT SVCs still count only toward `svc_calls`, `early_dispatches` stops at the 64-entry log limit, and `g_current_pcs` uses one cache line per core. `RecordMiss`/`RecordUnhandled` remain unchanged because they run only on static-to-JIT transitions; comments are cleaned up only when touched and are limited to two lines.

### Contracts to preserve

- Preserve FP_SHARED's exact per-module bodies and FPXV's option, manifest, cache-identity, ABI, and result contracts; do not extend FPXV without sampler/probe evidence.
- Preserve overlay architecture, honest `near`/fallback wording, and process/input ownership contracts.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error.
- `SUYU_RECOMP_THREAD_TIME` is opt-in and must not change execution; never fake cache producer identity.
- Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.
- Measure at the same position/orientation with an averaged FPS value for every A/B; report one number per condition.

### Open issues, in priority order

0. Reconfigure the host for `2805512`: clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, run `D:\HisuiWakeBuild\diag\reconf_build.cmd` via `cmd.exe /c` with a long timeout, then check `GIT_REV`; confirm cleanup state before exporting.
1. Decompose the all-off drop at the same position with `SUYU_AOT_FM_INLINE=0` only and `SUYU_AOT_FP_SHARED=0` only, plus a default re-measurement; collect image sizes and all three cache fields. `SUYU_AOT_FPX_VEC=0` already showed no change.
2. Explain the roughly 22-versus-30-FPS JIT gap with an opt-in, backend-agnostic per-guest-thread breakdown of run/SVC-service/wait time per frame, plus per-reason dispatcher-return counters (chain budget, outside-module indirect target, cross-module call, SVC, guard, conditional branch). Code must be byte-identical when off; require a synthetic test and negative control. Design only until approved.
3. Keep the chain-budget default at 256 unless new evidence; the user's 512 proposal is pending a decision.
4. Carry over per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 only with explicit approval.

### Immediate next action

Commit the change, reconfigure the host for the new hash, then run a same-position/orientation A/B with defaults to see whether FPS changes; carry forward the unchanged `RecordMiss`/`RecordUnhandled` behavior and comment-cleanup-when-touched rule. Then ask the user for same-position FMI/FP_SHARED one-variable exports and, in parallel, design (do not implement without approval) the issue-2 instrumentation.

## How to proceed

1. Read `CLAUDE.md` and this handoff; check `git status`/`git log`, clear export variables, reconfigure, and verify `GIT_REV` before exporting.
2. Ask for same-position/orientation averaged FPS, image sizes, the three cache fields, hardware, and exact export options for each condition.
3. Design the issue-2 instrumentation and synthetic negative control; do not implement it without approval.
4. Use the standard cycle: design, implement, required tests, Code reviewer review, fix findings, one full suite run, host rebuild, Markdown batch, then give the user the commit file list and message.
5. After export-running tests, run the cleanup rebuild. The user makes commits, tags, and pushes.

## How to work / procedures

- Run `.cmd` scripts from PowerShell with `cmd.exe /c script *> log`; Bash `cmd.exe` invocation is ineffective here. Do not feed quoted Python/Markdown through shell heredocs.
- Exports are user-started in the GUI; use a new empty output folder and clean up with `suyu.exe` closed.
- The game log is written only after normal game exit. Use a non-elevated shell and convert sampler `+0xN` offsets to real RVA by subtracting the module base.
- Never run concurrent builds; keep builds, exports, logs, caches, and game-derived outputs outside the repository. Use `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused`, not the lost log tail.
- Before exports or builds, remove the chain-budget variable with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET`; PowerShell `$env:` variables persist for the session.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"`; the value is read once at process start and is confirmed only by FPS. Sweep `32,64,128,256,512,1024`, one condition at a time.
- Run `D:\HisuiWakeBuild\diag\reconf_build.cmd` with `cmd.exe /c`; a hash change recompiles about 1,700 targets, so use a long timeout. Clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` first.
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`.
- FPXV smoke: `python tests/recompiler_smoke/run.py`; ABI5/ABI6 golden hashes must remain unchanged.
- Orca `check --wait --json` output can contain several JSON objects, with heartbeats first; parse each object.
