# Aevora Work Plan

Handoff prepared: 2026-10-20 (session 37 cleanup). See [session 37 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-37-cleanup), [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The current goal is lower host CPU usage per core so low-spec PCs run better at a 30 FPS cap; generation, compilation, or startup alone does not establish compatibility.

The long-term end goal and its milestones (N0-N10 runtime, G1-G4 tools and GUI) are defined in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md): a title-specific standalone native PLA application with no CPU JIT, Maxwell/emulated GPU processing, or general emulator runtime, plus the Aevora Launcher and Aevora Recompiler replacing all Suyu GUIs.

## Status

HEAD is `373bcc5` (documentation-only commits on top of `7609ab3`, which is README on top of `97cad8f`); host `GIT_REV` is `7609ab3a14992551ba578a14d4f5df05c8fcc3dd`, code-identical to HEAD because `7609ab3..373bcc5` changes only Markdown. The export at `7609ab3` is done; cache publication was not established here. B1 wait splitting is present but ineffective alone because waits are under 1 ms, with about 2600 timer events/s and HID pad update at 1 kHz the largest source. Commit `97cad8f` made the Npad default 4 ms; X-Y-X results show HostTiming about 60-64% versus 99% and lower process CPU with no measurable FPS change. Untested: AudioManager appearing only in the 1 ms AOT run, the remaining roughly 60% HostTiming cost, other event coalescing, exclusive-access and overlay/backend checks.

## Recent work (session 37)

- B1 shipped in `5ccacde`; `Event::WaitFor` now returns false on timeout in Windows fallbacks.
- `97cad8f` added default 4 ms Npad updates and `HostTimingEventStats` (`SUYU_HOST_TIMING_STATS=1`).
- `7609ab3` is a README commit; the host was rebuilt and its `GIT_REV` verified after export.
- JIT/AOT X-Y-X results are retained in the measurement table below.
- `ParseEnvIntInRange` is in `src/common/env_option.h` and is used for the 1-8 ms override.
- Smoke `run.py` exited 0 before the last two review fixes; `host_timing_wait_unit` and `env_option_unit` were re-run afterward.
- Code-review rounds found only P2/P3 findings; all were fixed.
- The user's `README.md` change was committed in `7609ab3`; the working tree was clean at the start of session 38.

## Immediate next action

1. Read `CLAUDE.md`, this plan, and `CODEBASE_INDEX.md`; check status/logs and confirm no `suyu.exe`, `ninja`, or `cl` holds the build tree. Ask before ending a user process.
2. B2: compare the AOT guest-core gap versus JIT at the same position with `SUYU_GUEST_TIME` and RIP sampling; make no code change first.
3. Optional: investigate AudioManager and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), or other periodic events can be coalesced.
4. Cheap: tracked `__pycache__`/`.bak` cleanup, `configure_audio` combo-index fix, `controller_assign.h` test, and optional `LoadLibraryExW` error-mode change with test.

## Open items

- Pre-existing Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` has 7 failures/1 error pre-existing; tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD checks, OpenGL/Null windows, FPS/overhead, and `tests/exclusive_access/run.py` remain untested.

## Measurement setup

Target: i5-12400F (6C/12T, P-cores), RTX 2060 SUPER, 16 GB; 30 FPS cap. Config: `frame_pacing_mode=0`, `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, `use_multi_core=true`. Metrics: process and busiest-thread CPU ms/game-frame (`core_pct`), with p99 frame interval; compare same-position JIT/AOT. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\` (baseline and `97cad8f` X-Y-X files); summarizer is `summarize_cpu_budget.py`.

| Backend / Npad period | FPS | HostTiming ms/frame (core %) | process CPU ms/frame | CPUCore_0/1/2 ms/frame | frame p50/p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| ec8e1a5 JIT baseline | 30.1 | 32.9 (98.6%) | 113.5 | 20.5 / 20.7 / 20.3 | 33.33 / 33.74 |
| ec8e1a5 AOT baseline | 25.3 | 39.3 (99.1%) | 152.1 | 30.4 / 32.1 / 31.6 | 39.1 / 50.8 |
| JIT 4 ms (X1) | 30 cap | 21.27 (63.8%) | 101.4 | 20.7 / 20.9 / 20.6 | 33.33 / 33.82 |
| JIT 1 ms (Y) | 30 cap | 32.95 (98.9%) | 113.9 | 20.6 / 21.6 / 21.3 | 33.33 / 33.79 |
| JIT 4 ms (X2) | 30 cap | 20.15 (60.5%) | 100.3 | 20.6 / 21.2 / 21.0 | 33.33 / 33.94 |
| AOT 4 ms (X1) | 25.6 | 23.54 (60.3%) | 132.3 | 29.9 / 31.5 / 31.2 | 37.5 / 56.4 |
| AOT 1 ms (Y) | 24.9 | 39.84 (99.2%) | 156.0 | 30.8 / 32.6 / 32.1 | 39.6 / 49.8 |
| AOT 4 ms (X2) | 25.4 | 25.16 (63.9%) | 135.4 | 30.0 / 32.1 / 31.5 | 38.4 / 51.0 |

The ec8e1a5 baseline used `SUYU_CPU_BUDGET=1`, 10-second windows, and user logs; AOT was below cap. The X-Y-X windows retained same-position runs (JIT 12/12/15; AOT 17/16/13; JIT C filtered scene/loading windows). In 4 ms runs, JIT `os_blocked` was about 3700-4800 per 42000-52000 waits/10 s and AOT wake-lateness p99 was at most 100 us; 1 ms had zero OS blocks. AudioManager appeared only in 1 ms AOT at 3.78 ms/frame (9.4% of a core).

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Held key/axis can remain stuck across capture; `controller_assign.h` is untested; Resolution Scale has a translated duplicate; maximize fallback needs one toggle; OpenGL/Null overlay is unavailable; PDH GPU accuracy is unverified.

## Contracts to preserve

Keep diagnostics execution-neutral when off, exact-0 default-on semantics, ABI/result/cache contracts, overlay action-queue/event-thread rules, and same-position CPU ms/frame. `SUYU_HOST_TIMING_BLOCK` is default-on; `SUYU_HOST_TIMING_STATS` is opt-in; `SUYU_HID_NPAD_UPDATE_MS` defaults to 4 ms and accepts 1-8. Preserve saved-value CAS, atomic monitor slots, 32-bit indexes, and two-line comments.

## Lessons

- New core files include `common/logging.h`; `run.py` needs `CPM_SOURCE_CACHE`.
- Copy measured logs before the next Qt start/export; use 10-second CPU-budget windows and do not build during FPS measurement.
- Mixed-ending files require byte-preserving edits. Use file edit tools and verify real LF counts; do not emit escaped newline text.
- A CMake source addition can trigger a long full rebuild; a running `suyu.exe` blocks relinking.
- Verify worker deliveries and Orca heartbeat-only returns before acting on a report.

## How to proceed

Follow the start checklist, verify evidence before changing code, use one worker at a time, and record only verified facts. Main agent implements; Code reviewer reviews; Code Explorer is read-only; md Implementer edits Markdown. Do not claim unrun exports or hand tests.

## How to work / procedures

- Build standalone tests after vcvars64 with `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- For CPU budget, set `SUYU_CPU_BUDGET=1` (optional `SUYU_CPU_BUDGET_WINDOW_SEC` 1-60, default 10), copy logs, and summarize with `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>` (about 300 at 30 FPS, 240 for AOT). Drop windows from other scenes (loading) by filtering before summarizing; keep diagnostics off for FPS-only runs.
- Opt-in stats: `SUYU_HOST_TIMING_STATS=1` logs `host-timing` wait and event lines every 10 s. Analysis helpers: `D:\HisuiWakeBuild\tmp_gt`, `D:\HisuiWakeBuild\diag\hotrva`.
- Replay overlay input with the real router and ImGui sources; follow the standing export procedure in CLAUDE.md and clean scratch output.
- Orca: one worker at a time, acknowledge deliveries, release workers, and check again after heartbeat-only messages.
