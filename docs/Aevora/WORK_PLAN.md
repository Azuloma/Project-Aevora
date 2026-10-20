# Aevora Work Plan

Handoff prepared: 2026-10-19 (session 36 end). See [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime; generation, compilation, or startup alone does not establish compatibility. Since 2026-10-19, optimize host CPU usage (per-core use) so low-spec PCs run better than the emulator, not to raise FPS.

## Status

HEAD is `bd7521c` (session 36 handoff docs; code last changed in `ec8e1a5`, `SUYU_CPU_BUDGET`). B1 is implemented and built in the working tree but uncommitted and unmeasured; host `GIT_REV` is still `ec8e1a5f150d05b08102ad09c8da7e974b5b1191`. An AOT export of `ec8e1a5` exists, its cache is published, and it supplied the AOT baseline. Untested: the B1 JIT A/B and acceptance numbers, full export/AOT A/B, `run.py` after the thread timeout fix, `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py`.

## What was done so far

- Dev19-dev29 delivered the recompiler, diagnostics, index, chain/default-option work, TTAS, overlay, Android/iOS removal, style/font, HUD, F12 fixes, embedded font, and Bad Image suppression; `cf07cb2`/`941a465` set 150% embedded Roboto Bold ([CHANGELOG.md](../../CHANGELOG.md)).
- Session 36 changed the goal, removed B2-D11 (`1b29c7d`; old text at `d0e8d1d`), added `SUYU_CPU_BUDGET` (`ec8e1a5`, no CHANGELOG entry), measured JIT/AOT, found HostTiming's root cause, and decided B1.
- B1 implemented the OS-blocked HostTiming wait split, single-writer stats, timeout contract fix, comments, and smoke unit; the host was rebuilt successfully, but user A/B measurements are pending.

## Immediate next action

1. Read `CLAUDE.md`, this plan, and `CODEBASE_INDEX.md`; check `git status`/`git log`; confirm no `suyu.exe`, `ninja`, or `cl` process holds the build tree. A running host `suyu.exe` locks `bin\suyu.exe` and causes `LNK1104`; ask the user before ending a process they may be using.
2. User commits the B1 change in a normal commit (no CHANGELOG/devN; files: `src/common/thread.cpp`, `src/core/{CMakeLists.txt,core_timing.h,core_timing.cpp,host_timing_wait.h}`, `tests/recompiler_smoke/{CMakeLists.txt,run.py,host_timing_wait_unit.cpp}`, and the two docs), then runs the JIT A/B at the same position with `SUYU_HOST_TIMING_BLOCK=0` versus default, both with `SUYU_CPU_BUDGET=1` and `SUYU_HOST_TIMING_STATS=1`, and reports the host-timing lines, CPU budget, and audio/feel check.
3. After the user's commit: run `rebuild-suyu.bat`, verify `GIT_REV`, user JIT A/B, full export, AOT A/B; record results.
4. Then B2.
5. Optional cheap items: tracked `__pycache__`/`.bak` cleanup, `configure_audio` combo-index fix, `controller_assign.h` unit test, and optional `recomp_bundle.cpp` `LoadLibraryExW` error-mode change with test.

## Open items

- Pre-existing Qt combo-index bug in `src/suyu/configuration/configure_audio.cpp:72`: index is cast to `AudioEngine`, but Libretro is index 4/value 5; value 4 is Null-sink fallback.
- `research/tests` has 7 failures/1 error pre-existing. Tracked artifacts: six `__pycache__` `.pyc` files (`tests/exclusive_access`, `tests/export_build_kit`, `tests/export_regression`, `tests/recompiler_smoke`, `tools/export_build_kit`) and `src/suyu/game_card.cpp.bak`; `tools/cpp-lint.sh:37-38` tab indentation is flagged by `git diff --check`.
- Bad Image dialog cause on this Windows build is undetermined.
- `recomp_bundle.cpp:305` `LoadLibraryExW` has no error-mode handling; optional runtime change not made.
- The HUD checks listed as not reported in [CHANGELOG.md](../../CHANGELOG.md) remain untested: `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py`.

## Measurement setup and baseline

Target: i5-12400F (6C/12T, all P-cores), RTX 2060 SUPER, 16 GB; 30 FPS cap. Config: `frame_pacing_mode=0` (Auto), `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, `use_multi_core=true`. Primary metrics: whole-process CPU ms/game-frame and busiest-thread ms/frame (`core_pct`); secondary: p99 frame interval. Compare AOT/JIT at the same standing position.

| Metric | JIT | AOT |
| --- | ---: | ---: |
| FPS | 30.1 | 25.3 |
| frame p50 / p99 ms | 33.33 / 33.74 | 39.1 / 50.8 |
| process CPU ms/frame | 113.5 | 152.1 |
| process CPU ms/s | 3416 | 3851 |
| HostTiming ms/frame (core %) | 32.9 (98.6%) | 39.3 (99.1%) |
| guest cores total ms/frame | 61.6 | 94.1 |
| CPUCore_0/1/2 ms/frame | 20.5 / 20.7 / 20.3 | 30.4 / 32.1 / 31.6 |
| GPU ms/frame | 9.9 | 8.8 |
| VulkanWorker ms/frame | 4.4 | 4.7 |

Baseline: `ec8e1a5`, `SUYU_CPU_BUDGET=1`, 10-second windows, user logs; JIT 13 steady windows (≥300 frames), AOT 12 (≥245). AOT is below cap, so its per-frame values include slower frames. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\jit_ec8e1a5_suyu_log.txt`, `D:\HisuiWakeBuild\diag\cpu_budget\aot_ec8e1a5_suyu_log.txt`; summarizer: `D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py`.

| Item | Scope | Measured basis |
| --- | --- | --- |
| B1 | Block HostTiming in the OS for long waits; umwait only the last margin. | HostTiming 32.9 (JIT) / 39.3 (AOT) ms/frame at about 99% of a core. |
| B2 | Break down the AOT guest-core gap with `SUYU_GUEST_TIME` and RIP sampling at the same position, AOT vs JIT; no code change. | Guest cores 94.1 (AOT) vs 61.6 (JIT) ms/frame, +32.5. |

## B1 plan (decided 2026-10-19)

Behaviour: while the remaining wait exceeds a margin, sleep in the OS with the existing `Event::WaitUntil` (condition variable), waking margin early; wait the remainder with the existing `Event::WaitFor`; `Set()` still wakes either wait at once.

Root cause: CoreTiming's HostTiming thread (`core_timing.cpp:73`) is the only caller of `Common::Event::WaitFor`, which on WAITPKG CPUs waits with `umonitor`/`umwait` (`src/common/thread.cpp:211-219`) and keeps the thread running from the OS's view.

Files: new `src/core/host_timing_wait.h` (OS-free: wait split from `wait_ns` and `timer_resolution_ns`, margin = 2 x timer resolution (1 ms at 0.5 ms); single-writer stats: wait-length histogram, wake lateness p50/p99/max, OS-block count); `src/core/core_timing.{h,cpp}` (wait loop, stats written only by the HostTiming thread); `src/core/CMakeLists.txt`; new `tests/recompiler_smoke/host_timing_wait_unit.cpp` with negative controls (no margin subtracted; OS block for short waits) registered in `tests/recompiler_smoke/CMakeLists.txt` and `run.py`.

Switches (user decisions): `SUYU_HOST_TIMING_BLOCK` default-on from the start (`=0` restores umwait-only waiting); `SUYU_HOST_TIMING_STATS=1` opt-in, a `host-timing` line every 10 s, no work when off; stats ship together with B1.

Verification: unit tests with `/W4 /WX`; `run.py`; host build (CMake list change, expect a full rebuild of about 25 minutes); Code reviewer and fixes; user JIT A/B at the same position (`SUYU_HOST_TIMING_BLOCK=0` vs default, both with `SUYU_CPU_BUDGET=1` and `SUYU_HOST_TIMING_STATS=1`) including an audio/feel check; commit, full export, AOT A/B.

Acceptance: HostTiming `core_pct` falls substantially (target 20% or less from about 99%); FPS not lower and frame p99 not worse beyond noise; wake-lateness p99 rises by no more than the timer resolution (0.5 ms); no reported audio or behaviour regression.

Risks: late timer events may disturb audio, VSync, or guest waits (watch AOT, already below 30 FPS); the 1 ms margin is still waited with umwait, so short event intervals limit the saving; if stats show waits are mostly under the margin, switch to a high-resolution waitable timer design.

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Still open: a held key/axis can remain stuck across capture; `controller_assign.h` is untested; the Qt Resolution Scale table has a second translated copy; a hand-undone maximize fallback needs one extra toggle; the overlay is unavailable on OpenGL/Null, which the user plans to remove; and PDH GPU accuracy versus Task Manager is unverified.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position CPU ms per game frame at the 30 FPS cap (`SUYU_CPU_BUDGET`), with FPS from unsampled runs. Cross chain, chain start, TTAS, lock-free marks, FM inline, direct/indirect chain, split main, and FPX are default-on (`=0` disables). Ledgers, lookup/exclusive/access diagnostics, and `SUYU_CPU_BUDGET` remain opt-in. Preserve saved-value CAS, atomic monitor slots, and the 32-bit index contract.

Overlay contracts are documented in [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md): the action queue is the only way the UI changes settings; settings apply on the event thread; Resolution Scale is save-only until next launch; the UI reads snapshots only; every capture/rebind begin has exactly one stop; and controller UI is Player 1 only.

## Lessons (session 36)

- Qt `suyu.exe` rotates logs on restart: a measured log becomes `%APPDATA%\suyu\log\suyu_log.txt.old.txt` when `suyu.exe` starts again (for example for an export); copy it immediately. AOT writes `<export folder>\user\log\suyu_log.txt`; `log_filter` is `*:Info`, so Core Info cpu-budget lines are logged.
- `common/logging/log.h` redefines `LOG_*` after `common/logging.h` and causes C4005; new core files include `common/logging.h`.
- Adding a source to a CMake list made reconfigure rebuild the whole host (1751 steps, about 20 minutes); plan before measurement.
- `run.py` needs `CPM_SOURCE_CACHE` (`D:\HisuiWakeBuild\cpm`) for `nlohmann/json`.
- Orca `check --wait` can replay an earlier worker's `worker_done`; verify the diff, ack, and wait again.
- Shell-written Markdown turned newlines into backslash-n and PowerShell backtick escapes destroyed text; use the file edit tool, and check real LF counts after each batch.
- From session 35: mixed LF/CRLF in `tests/recompiler_smoke/CMakeLists.txt`/`run.py`; `worker-start --worktree current` rejects `--name`; bad-DLL tests call `SetErrorMode`; `rebuild-suyu.bat` relinks both executables and a running `suyu.exe` blocks it.
- Starting `rebuild-suyu.bat` from a Bash-quoted `powershell -Command` silently did not start; start it with `Start-Process` from the PowerShell tool and confirm `cl`/`ninja` are running.

## How to proceed

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; the main agent implements, Code reviewer reviews, Code Explorer surveys read-only, and md Implementer edits Markdown; keep changes focused; and record only verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run. If a Markdown batch shortens a section, compare git diff against the intended change; one batch dropped unrelated lines.

## How to work / procedures

- Build standalone tests after BuildTools vcvars64 via `cmd.exe /c`: `cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as required.
- Every new standalone test needs a shared contract check and deliberately weakened negative control that fails fast and cannot hang.
- For overlay input bugs, headlessly replay the real router/command-bar header with ImGui at `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` first.
- Put scripts containing `\n` or quotes in files; use `D:\HisuiWakeBuild\scratch\<task>\`, then clean it with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1`.
- Build `suyu-cmd` after code changes; after the user's commit reconfigure and verify `GIT_REV`; never build during FPS measurement. Follow `CLAUDE.md` for new PLA exports.
- CPU budget: set `$env:SUYU_CPU_BUDGET="1"`; optionally `$env:SUYU_CPU_BUDGET_WINDOW_SEC` 1-60 (default 10); one `cpu-budget` line/window. Summarize: `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>` (about 300 at 30 FPS, lower below cap). `GetThreadTimes` granularity is about 15.6 ms; missed baselines/exited threads go to untracked; `suyu-cmd` `exit(0)` does not stop the sampler.
- `python -B tests/recompiler_smoke/run.py` needs `CPM_SOURCE_CACHE` set to the CPM cache (for example `D:\HisuiWakeBuild\cpm`) for `nlohmann/json.hpp`.
- For headless overlay replay, use the real router and ImGui sources with press/release batching, focus events, and combo-popup coverage.
- Analysis helpers/scripts are outside the repository in `D:\HisuiWakeBuild\tmp_gt` and `D:\HisuiWakeBuild\diag\hotrva`; keep diagnostics off during FPS-only runs.
- Orca: one worker at a time, acknowledge deliveries, release each worker, and remember that `check --wait` can replay earlier messages.
