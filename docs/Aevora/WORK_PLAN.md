# Aevora Work Plan

Handoff prepared: 2026-10-19 (session 36 end). See [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime; generation, compilation, or startup alone does not establish compatibility. Since 2026-10-19, optimize host CPU usage (per-core use) so low-spec PCs run better than the emulator, not to raise FPS.

## Status

HEAD is `7609ab3` (README on top of `97cad8f`); host `GIT_REV` was verified as `7609ab3a14992551ba578a14d4f5df05c8fcc3dd` after the export. An AOT export of `7609ab3` exists and replaces the `ec8e1a5` export as the latest; its cache publication was not established here. Untested: why AudioManager appeared only in the 1 ms AOT run, what the remaining HostTiming time consists of, whether other periodic events can be coalesced, `tests/exclusive_access/run.py`, and the listed overlay/backend windows.

## What was done so far

- Dev19-dev29 delivered the recompiler, diagnostics, index, chain/default-option work, TTAS, overlay, Android/iOS removal, style/font, HUD, F12 fixes, embedded font, and Bad Image suppression; `cf07cb2`/`941a465` set 150% embedded Roboto Bold ([CHANGELOG.md](../../CHANGELOG.md)).
- Session 36 changed the goal, removed B2-D11 (`1b29c7d`; old text at `d0e8d1d`), added `SUYU_CPU_BUDGET` (`ec8e1a5`, no CHANGELOG entry), measured JIT/AOT, found HostTiming's root cause, and decided B1.
- B1 wait split shipped but was ineffective alone: waits were overwhelmingly short, with no OS blocking in either mode; event stats showed about 2600 callbacks/s averaging 0-5 us while HostTiming was almost entirely waiting.
- By user decision (2026-10-20), the Npad default is 4 ms. Same-session JIT and AOT X-Y-X runs showed HostTiming at about 60-64% versus 99%, and process CPU lower by about 12-13 ms/frame (JIT) and 21-24 ms/frame (AOT), with no measurable FPS change; `SUYU_HID_NPAD_UPDATE_MS=1-8` remains an override.

## Immediate next action

1. Read `CLAUDE.md`, this plan, and `CODEBASE_INDEX.md`; check `git status`/`git log`; confirm no `suyu.exe`, `ninja`, or `cl` process holds the build tree. A running host `suyu.exe` locks `bin\suyu.exe` and causes `LNK1104`; ask the user before ending a process they may be using.
2. B2: compare the AOT guest-core gap versus JIT without a code change first, at the same position, using `SUYU_GUEST_TIME` and RIP sampling per the procedures section.
3. Optional follow-ups: investigate why AudioManager appeared only in the 1 ms AOT run, and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), and other periodic events can be coalesced.
4. Optional cheap items: tracked `__pycache__`/`.bak` cleanup, `configure_audio` combo-index fix, `controller_assign.h` unit test, and optional `recomp_bundle.cpp` `LoadLibraryExW` error-mode change with test.

## Open items

- Pre-existing Qt combo-index bug in `src/suyu/configuration/configure_audio.cpp:72`: index is cast to `AudioEngine`, but Libretro is index 4/value 5; value 4 is Null-sink fallback.
- `research/tests` has 7 failures/1 error pre-existing. Tracked artifacts: six `__pycache__` `.pyc` files (`tests/exclusive_access`, `tests/export_build_kit`, `tests/export_regression`, `tests/recompiler_smoke`, `tools/export_build_kit`) and `src/suyu/game_card.cpp.bak`; `tools/cpp-lint.sh:37-38` tab indentation is flagged by `git diff --check`.
- Bad Image dialog cause on this Windows build is undetermined.
- `recomp_bundle.cpp:305` `LoadLibraryExW` has no error-mode handling; optional runtime change not made.
- The 1 ms AOT run alone showed an AudioManager thread at 3.78 ms/frame (9.4% of a core); whether this is an event-rate effect or scene difference is unverified.
- The remaining roughly 60% of HostTiming is unresolved: umwait wait time versus OS wake cost has not been separated.
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

Same-session X-Y-X comparison: 4 ms / 1 ms / 4 ms, with `SUYU_CPU_BUDGET=1`. JIT windows had at least 300 frames; AOT windows had at least 240 frames and 100-170 process ms/frame. JIT C's first 13 windows were a different scene (title/loading plus a load stall), so only windows with 90-110 process ms/frame were retained (15 windows, filtered copy `jit_97cad8f_C_game.txt`). Filtered window counts were JIT 12/12/15 and AOT 17/16/13.

| Backend / Npad period | FPS | HostTiming ms/frame (core %) | process CPU ms/frame | CPUCore_0/1/2 ms/frame | frame p50/p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| JIT 4 ms (X1) | 30 cap | 21.27 (63.8%) | 101.4 | 20.7 / 20.9 / 20.6 | 33.33 / 33.82 |
| JIT 1 ms (Y) | 30 cap | 32.95 (98.9%) | 113.9 | 20.6 / 21.6 / 21.3 | 33.33 / 33.79 |
| JIT 4 ms (X2) | 30 cap | 20.15 (60.5%) | 100.3 | 20.6 / 21.2 / 21.0 | 33.33 / 33.94 |
| AOT 4 ms (X1) | 25.6 (24.1-26.1) | 23.54 (60.3%) | 132.3 | 29.9 / 31.5 / 31.2 | 37.5 / 56.4 |
| AOT 1 ms (Y) | 24.9 (24.6-25.2) | 39.84 (99.2%) | 156.0 | 30.8 / 32.6 / 32.1 | 39.6 / 49.8 |
| AOT 4 ms (X2) | 25.4 (25.1-25.7) | 25.16 (63.9%) | 135.4 | 30.0 / 32.1 / 31.5 | 38.4 / 51.0 |

JIT logs: `D:\HisuiWakeBuild\diag\cpu_budget\jit_97cad8f_A.txt`, `_B.txt`, `_C.txt`; filtered JIT C: `jit_97cad8f_C_game.txt`. AOT logs: `aot_97cad8f_A.txt`, `_B.txt`, `_C.txt`; filtered copies: `aot_97cad8f_A_game.txt`, `_B_game.txt`, `_C_game.txt` in the same folder. In the JIT 4 ms conditions, `os_blocked` was about 3700-4800 of roughly 42000-52000 waits per 10 s, with wake-lateness p99 at most 100 us in A and 500 us in C (maximum about 1-3 ms); AOT 4 ms wake-lateness p99 was at most 100 us (maximum about 0.7-1 ms). The 1 ms conditions had `os_blocked=0`. The AOT 4 ms X1 p99 includes one 166 ms window; AudioManager appeared only in the 1 ms AOT condition at 3.78 ms/frame (9.4% of a core).

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

B1 result: the wait split is shipped but ineffective on its own. The measured A/B and event breakdown are recorded above; waits were overwhelmingly short, with no OS blocking in either mode, while the 4 ms Npad default produced the indicative HostTiming reduction. Repeat the 1 ms/4 ms comparison in one session before treating the gain as final.

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
- A `&&` after `cl` under `cmd.exe` invoked from the PowerShell tool did not run the produced executable; run the executable separately.
- Orca `check --wait` returns on heartbeat-only deliveries; acknowledge the delivery and wait again.

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
