# Aevora Work Plan

Handoff prepared: 2026-10-09. HEAD is `c821526` on `main`; the last code commit is `c821526`. Superseded text is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-09-from-work_planmd-eighteenth-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

Milestone 4a topology, registry, loader, split exporter, persistent cache, and covered U1-U4 synthetic work are complete. Static AOT + FMI + main-DLL split runs in the usual scene at 15-20 FPS versus about 30 FPS JIT; the standard all-default 2bed3c6 export is verified below the 2 GiB image limit, but cache dialog fields were not read. The shared-FP change is committed as c821526, the host was reconfigured and rebuilt with GIT_REV c821526d36db0427e44be438c682afb8fa8cf905, and the user observed considerably smaller generated files and about 1-2 FPS higher FPS after a fresh default-on PLA export.

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
- `SUYU_AOT_FP_SHARED` shares the exact FPX1 add/sub/div/fma-mul/recps/rsqrts/sqrt bodies once per generated module through `recomp_runtime.c`/`.h`; its option, manifest, bundle-id, cache-reuse, changed-items, and build-identity paths are covered by the recorded tests.
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

## Session handoff (2026-10-09, nineteenth session)

### State and completed work

HEAD is `c821526` on `main`; the last code commit is `c821526`. The committed change adds `SUYU_AOT_FP_SHARED`: with FPX1 enabled and the option not exactly `0`, each module emits one exact body for add, sub, div, fma/mul, recps, rsqrts, and sqrt, while the existing FPX1 sites make one-line calls; without FPX1 the option is ignored, and an explicit `1` produces a warning. The inline and out-of-line bodies come from the same generator functions, and disabling the option preserves byte-identical emitted text for the verified units and variants.

The MSVC FPX harness passed: `fpxs` had 0 mismatches versus soft over 76,041,704 L1-L3 cases; `fpxb` had 61,560 mismatches as the weakened negative control; the kept fast-path count check also catches a closed shared path. Smoke passed with goldens unchanged, export integration passed `--only-defaults` 26/0 and `--only-aot-cache-matrix` 213/0, and the host rebuild exited 0. The code reviewer reported three P2 findings, all fixed; shared plus FPX shadow was not exercised. Cleanup after the FMI-off export also exited 0 with the four environment variables unset and no `suyu` process running.

### Contracts to preserve

- Preserve overlay architecture and honesty: Vulkan present pass, scheduler-worker frame, process-wide lifetime seam, error disable; say `near`, not `inside`; PC 0 means no recorded recompiled dispatch; attempts include lookup misses and unhandled/refused paths.
- Preserve input ownership through release, queue only overlay-owned events, reset on focus loss/minimize, and apply the same rule to keyboard/text when added.
- Default-on options are exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error. Do not claim unsupported default split occurred.
- `SUYU_AOT_FP_SHARED` is on unless exactly `0`, is ignored without FPX1, and warns only for an explicit `1` without FPX1. With FPX1 and sharing on, preserve one exact per-module body for each of add/sub/div/fma-mul/recps/rsqrts/sqrt, the one-line calls inside the existing do-while fast paths, and the shared generator source of truth. The option is part of the bundle identity, manifest, cache reuse comparison, changed-items list, and build-identity display; a missing manifest field means inline and a non-boolean field mismatches.
- `SUYU_RECOMP_THREAD_TIME` is an opt-in measurement switch, off by default, and must not change execution. Never fake cache producer identity. Attempts are not handoffs, and zero Hybrid attempts is not proof of static execution.

### Open issues, in priority order

0. Obtain from the user the exact image sizes, `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`, FPS and hardware details for the fresh default-on PLA export; optionally obtain an `SUYU_AOT_FP_SHARED=0` A/B. The user observation so far is considerably smaller generated files and about 1-2 FPS higher FPS than the previously reported 15-20 FPS.
1. Recommended next investigation: generator-side host-size totals per guest opcode class, because the FP share of the PLA image remains unmeasured; keep this ahead of SVC attribution or chain A/B work.
2. Carry over Stage F, the automatic all-static re-plan, Step 6 and held items, MSVC `cl` real-title support, and U2 only with explicit approval.

### Immediate next action

Ask the user for the exact image sizes, the three cache status fields, FPS and hardware details from the fresh default-on PLA export, and optionally an `SUYU_AOT_FP_SHARED=0` A/B; then proceed with the generator-side per-opcode-class size investigation.

## How to work / procedures

- Reconfigure after every user commit before building/exporting: the host embeds the git hash at CMake configure time. Check `GIT_REV` in `msvc-release\src\common\scm_rev.cpp`; `buildsuyu.cmd` alone may report no work and retain the old hash.
- Run `.cmd` scripts from PowerShell with `cmd.exe /c script *> log`; Bash `cmd.exe` invocation is ineffective here. Do not feed quoted Python/Markdown through shell heredocs.
- Exports are user-started in the GUI; environment experiments start `suyu.exe` from a shell, use a new empty output folder, copy `suyu-cmd-static.pdb` immediately after export, then clean up with `suyu.exe` closed.
- The game log is written only after normal game exit. The user must use a non-elevated shell and stand in the measured scene. Convert sampler `+0xN` offsets to real RVA by subtracting the module base.
- Never run concurrent builds; keep builds, exports, logs, caches, and game-derived outputs outside the repository. Use cache status fields rather than the lost log tail. The user commits, tags, and pushes; the Code reviewer must review code changes.
- FPX harness: `python tests/recompiler_fpx/run.py --cases 300 --controls` with MSVC; smoke: `python tests/recompiler_smoke/run.py --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include --cc cl --cxx cl`; integration: `python tests/export_integration/run_export_integration.py --suyu-dir <build>/bin --work <new short folder> --only-defaults` and `--only-aot-cache-matrix`. The verified logs are outside the repository under `D:\HisuiWakeBuild\diag`.
