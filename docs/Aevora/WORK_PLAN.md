# Aevora Work Plan

Handoff prepared: 2026-10-09. HEAD is `709dbd6` on `main`; the last code commit is `2bed3c6`. Superseded text is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-09-from-work_planmd-eighteenth-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

Milestone 4a topology, registry, loader, split exporter, persistent cache, and covered U1-U4 synthetic work are complete. Static AOT + FMI + main-DLL split runs in the usual scene at 15-20 FPS versus about 30 FPS JIT; the standard all-default 2bed3c6 export is verified below the 2 GiB image limit, but cache dialog fields were not read. Host rebuild and the standalone thread-time unit passed; the full smoke run and required Code reviewer review of 2bed3c6 remain open.

## Evidence so far (observations)

| Run | Core0/Core1/Core2 CPU % | FPS | Image size |
|---|---:|---:|---:|
| JIT | 76.8/78.7/77.2 | 30 | not measured |
| Static AOT + FMI + split, earlier | 76.3/80.4/81.4 | 17-20 | Main DLL 1,857,218,560 bytes |
| 2bed3c6 standard, all defaults | 71/76/76 busy | 15-20 | Main SizeOfImage 1,923,067,904; launcher 495,382,528 |
| 2bed3c6 `SUYU_AOT_FM_INLINE=0` | not recorded | about 1 FPS change, direction unrecorded | Main 1,588,723,712; launcher 397,508,608 |

The standard main image is about 214 MiB below LNK1248. Host-RIP samples: 46.9% recompiled functions, 49.4% outside the DLL; 4,242 functions sampled, top 1/10/50/200/1000 = 1.90%/7.2%/13.2%/20.4%/31.9%, so the profile is flat. Host bytes per guest instruction: mean 150 B, median 151 B, p90 313 B, p99 383 B; sample-weighted mean 636 B and about 34% of in-DLL samples in functions at least 900 B per guest instruction. Lookup/run-slice dispatch is about 5% of samples. These are observations, not acceptance criteria; function grouping can overstate the size ratio.

Per-thread ledger, steady state from log time 62 s: core 0 busy 71% (thread 77 57%, 88 11%); core 1 busy 76% (80 54%, 89 9%, 100 7%, 93 2%); core 2 busy 76% (81 53%, 90 9%, 100 8-9%). Threads 77/80/81 and 88/89/90 resume at SDK SVC stubs; thread 100 enters main code. Coverage in the roughly three-minute run: 10,589,833 SVCs, including GetCurrentProcessorNumber 5,117,171, ArbitrateUnlock 2,063,210, ArbitrateLock 1,961,915, WaitProcessWideKeyAtomic 343,495, SignalToAddress 322,484, SignalProcessWideKey 316,923, WaitForAddress 307,213, and SendSyncRequest 133,884; static-to-JIT transitions 0. The lock-contention explanation for stalled time and idle cores is a hypothesis, not verified.

## What the code now contains

- Overlay/UI diagnostics, honest `near`/fallback wording, ownership-based mouse routing, default-on option parsing, split topology, cache publish/reuse, and their synthetic/integration tests remain as previously documented.
- `src/core/arm/recomp/recomp_thread_time.h` defines `RecompThreadTimeLedger`; `arm_recomp.cpp` enables `ThreadTimeScope` only when `SUYU_RECOMP_THREAD_TIME=1`, one ledger per implementation, and logs a ten-second top-six report. It measures `RunThread` wall time (guest plus in-call host callbacks), excludes idle waits and post-return SVC service, and must not alter execution when unset or any other value.
- `tests/recompiler_smoke/thread_time_unit.cpp` is registered and invoked by the smoke runner. Its standalone test passed; deliberate weakening controls caught sorting, clock clamp, reset, and wait regressions.

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

Held: two-lifetime seam; real loader/refusal paths; `tests/export_regression` Qt-root lookup; MSVC `cl` chain route (synthetic smoke only); old-manifest-without-chain-fields reuse; six tracked `.pyc` files. Stage F (Controller/Graphics windows, keyboard/text routing, horizontal wheel sign), automatic all-static re-plan, and U2 (explicit approval only) remain. PLA results are user-run and scene-specific; the recompiled image has no PDB, so DLL samples are RVA attribution only.

## Session handoff (2026-10-09, eighteenth session)

### State and completed work

The user commit `709dbd6` updates README only; `2bed3c6` adds the opt-in per-guest-thread ledger. Hot sampler offsets `+0x20B0/+0x20D2/+0x211A/+0x21AE` are real RVAs `0x32C0/0x32E2/0x332A/0x33BE`, all in the guest-to-host lookup helper. Same-build PDB stack attribution identifies 21-33% idle samples as `PhysicalCore::Idle -> Cnd_wait` because no guest thread is runnable, not GPU/fence wait.

The fresh all-default export succeeded without LNK1248: main SizeOfImage 1,923,067,904 and launcher 495,382,528; FPS stayed 15-20. The `FM_INLINE=0` A/B reduced main/launcher images by about 17%/20% but changed FPS by about one FPS with direction unrecorded, so code size alone is not the limiting explanation. The host-RIP profile was flat as recorded above. The ledger measurements and coverage show no single saturated thread; lock contention remains only a hypothesis. Local diagnostic scripts and full result files remain outside the repository under `D:\HisuiWakeBuild\diag\hotrva`.

### Contracts to preserve

- Preserve overlay architecture and honesty: Vulkan present pass, scheduler-worker frame, process-wide lifetime seam, error disable; say `near`, not `inside`; PC 0 means no recorded recompiled dispatch; attempts include lookup misses and unhandled/refused paths.
- Preserve input ownership through release, queue only overlay-owned events, reset on focus loss/minimize, and apply the same rule to keyboard/text when added.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error. Do not claim unsupported default split occurred.
- `SUYU_RECOMP_THREAD_TIME` is an opt-in measurement switch, off by default, and must not change execution. Never fake cache producer identity. Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.

### Open issues, in priority order

0. Cleanup rebuild after the FMI-off export: user closes game and `suyu.exe`; verify `SUYU_AOT_FM_INLINE`, `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` are unset; run `buildsuyu.cmd` through PowerShell.
1. Obtain the Orca Code reviewer review of `2bed3c6`, then run `tests/recompiler_smoke/run.py` including `smoke_thread_time_unit`.
2. Recommended next investigation: generator-side host-size totals per guest opcode class, with a synthetic test, to identify the apparent condition/flag expansion. Alternatives are SVC attribution by thread/address, then one-variable direct/indirect chain exports only after (a) or (b).
3. Read `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused` from the next export dialog, never the log tail.
4. Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, and U2 approval remain as above.

### Immediate next action

The new session first reads `CLAUDE.md` and this handoff, checks `git status`/`git log`, asks the user to close the game and `suyu.exe` for item 0, then proposes investigation 2(a) and asks which investigation to run.

## How to work / procedures

- Reconfigure after every user commit before building/exporting: the host embeds the git hash at CMake configure time. Check `GIT_REV` in `msvc-release\src\common\scm_rev.cpp`; `buildsuyu.cmd` alone may report no work and retain the old hash.
- Run `.cmd` scripts from PowerShell with `cmd.exe /c script *> log`; Bash `cmd.exe` invocation is ineffective here. Do not feed quoted Python/Markdown through shell heredocs.
- Exports are user-started in the GUI; environment experiments start `suyu.exe` from a shell, use a new empty output folder, copy `suyu-cmd-static.pdb` immediately after export, then clean up with `suyu.exe` closed.
- The game log is written only after normal game exit. The user must use a non-elevated shell and stand in the measured scene. Convert sampler `+0xN` offsets to real RVA by subtracting the module base.
- Never run concurrent builds; keep builds, exports, logs, caches, and game-derived outputs outside the repository. Use cache status fields rather than the lost log tail. The user commits, tags, and pushes; the Code reviewer must review code changes.
