# Aevora Work Plan

Handoff prepared: 2026-10-18 (session 35 end). All earlier material is archived in [OLD_HISTORY.md](OLD_HISTORY.md) under dated `Archived on <date> from WORK_PLAN.md (session N cleanup)` sections, including the [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup) and the [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `1b29c7d` (docs-only after `941a465` runtime); the host tree was rebuilt from the uncommitted `SUYU_CPU_BUDGET` change (`GIT_REV` is still `941a465...` because the build was not reconfigured after a commit). `python -B tests/recompiler_smoke/run.py` passed with 33 checks. Still untested are `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py`.

## What was done so far

- Dev19-dev27 delivered the recompiler, diagnostics, index, chain/default-option work, TTAS, overlay, and Android/iOS removal ([CHANGELOG.md](../../CHANGELOG.md)).
- Dev28 delivered the overlay style/font work; normal `4c711ee` merged Roboto with BIZ UDPGothic ([CHANGELOG.md](../../CHANGELOG.md)).
- Dev29 delivered the performance HUD, F12 ownership fixes, embedded font update, and bundle-host Bad Image suppression ([CHANGELOG.md](../../CHANGELOG.md)).
- Normal `cf07cb2` and `941a465` set the HUD to 150% embedded Roboto Bold ([CHANGELOG.md](../../CHANGELOG.md)).

## Immediate next action

1. Read `CLAUDE.md`, this plan, and `CODEBASE_INDEX.md`; check `git status`/`git log`; confirm no `suyu.exe`, `ninja`, or `cl` process holds the build tree. A running host `suyu.exe` locks `bin\suyu.exe` and causes `LNK1104`; ask the user before ending a process they may be using.
2. The user commits the `SUYU_CPU_BUDGET` change (a dev entry or normal commit is the user's choice; ask), then reconfigures, rebuilds, and checks `GIT_REV`.
3. Baseline measurement: JIT with `suyu.exe` (`SUYU_RECOMP_DIR` unset) is possible immediately after the host rebuild; AOT needs a full export after the commit because the launcher carries new core code. Use the same position, stand still, and the configured 30 FPS cap; read steady-state `cpu-budget` lines while skipping loading windows, with no builds or exports during measurement.
4. Build the new optimization backlog from the breakdown, ranked by CPU ms per frame saved, with an `=0` switch for every item.
5. Optional cheap non-measurement items remain available: tracked `__pycache__`/`.bak` cleanup, the `configure_audio` combo-index fix, a `controller_assign.h` unit test, and optionally the `recomp_bundle.cpp` `LoadLibraryExW` error-mode change with a test.

## Open items

- Pre-existing Qt combo-index bug in `src/suyu/configuration/configure_audio.cpp:72`: combo index is cast to `AudioEngine` although Libretro is index 4 and value 5; value 4 remains the Null-sink fallback.
- `research/tests` has 7 failures and 1 error pre-existing on HEAD. Tracked repository artifacts that should not be in git are six `__pycache__` `.pyc` files (`tests/exclusive_access`, `tests/export_build_kit`, `tests/export_regression`, `tests/recompiler_smoke`, `tools/export_build_kit`) and `src/suyu/game_card.cpp.bak`; `tools/cpp-lint.sh:37-38` has tab indentation flagged by `git diff --check` (file style).
- The cause of the Bad Image dialog on this Windows build is undetermined.
- Product loader `recomp_bundle.cpp:305` `LoadLibraryExW` still has no error-mode handling; an optional runtime change was offered to the user and was not made.
- The HUD checks listed as not reported in [CHANGELOG.md](../../CHANGELOG.md) remain untested: `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py`.

## Optimization direction (restarted)

On 2026-10-19, the user changed the goal to reducing per-core CPU usage for low-spec machines. The previous backlog (B2-D11) was removed, and optimization restarts from scratch. The target is the user's i5-12400F (6 cores/12 threads, all P-cores), RTX 2060 SUPER, and 16 GB PC at a 30 FPS cap, using `frame_pacing_mode=0`, `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, and `use_multi_core=true`; the user never changed pacing. Primary metrics are whole-process CPU ms per game frame and busiest-thread CPU ms per frame (`core_pct`), with p99 game-frame interval as secondary; compare AOT and JIT at the same position and optionally use affinity-limited runs to emulate fewer cores. Before this change there were no per-frame process/thread CPU or p99 metrics; the HUD is a 1 s `GetProcessTimes` percentage, guest ledgers cover guest-core wall time, and frame history has only mean/max. Surveyed spin candidates include the 30/60/90/120-only `vk_scheduler.h` frame-pacing yield (not taken in Auto), Dynarmic `SpinLock`, and semaphore spins in `common/atomic_helpers.h`. The new opt-in `SUYU_CPU_BUDGET=1` diagnostic (default off; one relaxed atomic load per game frame when off) samples Windows process/thread CPU, reports top 12 threads and frame percentiles every 1-60 s (default 10), and is available to both Qt JIT and AOT; use windows of 10 s or more because `GetThreadTimes` has roughly 15.6 ms clock-tick granularity. Thread baselines match id plus creation time; missed baselines, exited threads, and dropped intervals are reported or excluded as documented by the diagnostic. The guest-requested `suyu-cmd` `exit(0)` callback does not stop/join the sampler; normal window close does. Verified standalone unit build and 20 runs, host rebuild of `suyu`/`suyu-cmd`, and 33-check smoke run all passed; no PLA measurement has run yet. Keep the backlog ranked by CPU ms per frame saved, with an `=0` switch on each item, and retain the old cheap non-measurement items as a separate optional item. The removed backlog remains in git history (commit `d0e8d1d`).

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Still open: a held key/axis can remain stuck across capture; `controller_assign.h` is untested; the Qt Resolution Scale table has a second translated copy; a hand-undone maximize fallback needs one extra toggle; the overlay is unavailable on OpenGL/Null, which the user plans to remove; and PDH GPU accuracy versus Task Manager is unverified.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position CPU ms per game frame at the 30 FPS cap (`SUYU_CPU_BUDGET`), with FPS from unsampled runs. Cross chain, chain start, TTAS, lock-free marks, FM inline, direct/indirect chain, split main, and FPX are default-on (`=0` disables). Ledgers, lookup/exclusive/access diagnostics, and `SUYU_CPU_BUDGET` remain opt-in. Preserve saved-value CAS, atomic monitor slots, and the 32-bit index contract.

Overlay contracts are documented in [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md): the action queue is the only way the UI changes settings; settings apply on the event thread; Resolution Scale is save-only until next launch; the UI reads snapshots only; every capture/rebind begin has exactly one stop; and controller UI is Player 1 only.

## Session 35 lessons

- `tests/recompiler_smoke/CMakeLists.txt` and `run.py` have mixed LF/CRLF line endings; scripted replaces must read with `newline=''` and check exact bytes (`repr`) or use the Edit tool. Git's "LF will be replaced by CRLF" warnings are harmless.
- `orca orchestration worker-start --worktree current` rejects `--name`; the first `check --wait` often returns a status message or replays the previous worker's result. Acknowledge the previous delivery id and wait again for `worker_done`.
- A Windows "Bad Image" modal dialog (loading a garbage DLL) blocks a non-interactive test run; tests that load bad DLLs call `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)`.
- `ImGui::GetFontSize()` already includes the 1.25 UI scale, so HUD text size is `GetFontSize() x kPerfHudTextScale` (1.5); stb_truetype ignores variable-font axes, so bold needs a static Bold TTF embedded as a separate (non-merged) font via `OverlayBoldFont()`.
- A build with `rebuild-suyu.bat` reconfigures and relinks both `suyu.exe` and `suyu-cmd.exe`; if `suyu.exe` is running, only `cmake --build ... --target suyu-cmd` can link.
- An unmodified third-party font must be recorded in `.reuse/dep5` with its origin and SHA-256.

## How to proceed

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; the main agent implements, Code reviewer reviews, Code Explorer surveys read-only, and md Implementer edits Markdown; keep changes focused; and record only verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run. If a Markdown batch shortens a section, compare git diff against the intended change; one batch dropped unrelated lines.

## How to work / procedures

- Build standalone tests after BuildTools vcvars64 through `cmd.exe /c` with `cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` when required.
- Every new standalone test needs a shared contract check with a deliberately weakened negative control that fails fast and cannot hang.
- For overlay input bugs, headlessly replay the real router/command-bar header with ImGui sources from `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` before changing code.
- Put scripts containing `\n` or quotes in files; use `D:\HisuiWakeBuild\scratch\<task>\`, then clean it with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1`.
- Build `suyu-cmd` after code changes; after the user's commit, reconfigure and verify `GIT_REV`; never build during FPS measurement. For a new PLA export, follow the standing export procedure in CLAUDE.md.
- For CPU budget measurement, set `$env:SUYU_CPU_BUDGET="1"` and optionally `$env:SUYU_CPU_BUDGET_WINDOW_SEC` to 1-60; read the `cpu-budget` line from the normal `suyu` log, skip loading windows, and use windows of 10 s or more. Thread baselines, dropped intervals, and the `suyu-cmd` `exit(0)` sampler limitation are documented above.
- `python -B tests/recompiler_smoke/run.py` needs `CPM_SOURCE_CACHE` set to the CPM cache (for example `D:\HisuiWakeBuild\cpm`) for `nlohmann/json.hpp`.
- For headless overlay replay, use the real router and ImGui sources above with press/release batching, focus events, and combo-popup coverage.
- Analysis helpers and sampling scripts are outside the repository in `D:\HisuiWakeBuild\tmp_gt` and `D:\HisuiWakeBuild\diag\hotrva`; keep diagnostics off during FPS-only runs.
- Orca: one worker at a time, acknowledge deliveries, release each worker, and remember that `check --wait` can replay earlier messages.

