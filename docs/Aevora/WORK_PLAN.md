# Aevora Work Plan

Handoff prepared: 2026-10-17 (session 32 end). Older material: [session 29 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-29-cleanup), [session 30 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-30-cleanup), [session 31 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-31-cleanup), [session 32 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-32-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `c456b32` (dev26). The host was reconfigured and rebuilt successfully; `GIT_REV` `c456b32fc117589543fe8f7c4a9ae6992ac92fef` matches HEAD. Logs: `D:\HisuiWakeBuild\logs\reconf_dev26.log` and `D:\HisuiWakeBuild\logs\build_dev26.log`. The last PLA export is `ac96270` in `D:\HisuiWakeExports\_NEW` and has no overlay work. Stage F0-F6 is complete and committed; see [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Optimization is paused. At the same position, the measured reference is AOT about 24.5-26 FPS (about 40.8-38.5 ms/frame) versus JIT 30 FPS (33.3 ms/frame); see [session 30 observations](OLD_HISTORY.md#session-30-record-2026-10-16) and [the archived measurements](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-30-cleanup).

## What was done so far

- Dev19-dev26 delivered the recompiler, diagnostics, 32-bit index, chain/default-option work, TTAS and lock-free marks, fullscreen toggle, and ImGui overlay; details are in [CHANGELOG.md](../../CHANGELOG.md).
- The overlay replaced the F12 panel through Stage F0-F6, including keyboard/text routing, actions, Graphics, Player 1 Controller, Mods/folders, Game, and fullscreen support; see [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md).
- The command-bar popup fix was confirmed by the user; F6 removed the Win32 F12 panel and branch.
- The host was reconfigured and rebuilt after dev26 with no compile/link errors; only pre-existing C4005 `LOG_*` warnings remained.

## Immediate next action

1. Ask the user to choose (a) a full dev26 PLA export with the overlay using the standing export procedure (this becomes the measurement baseline), or (b) resume the optimization backlog, recommended starting with B3, which itself needs the dev26 export first.
2. After an export, record launcher/DLL sizes, manifest options, and the user's same-position FPS and ms/frame reading.
3. For optimization, handle one item at a time through the Orca Code Implementer, then Code reviewer, and measure against the new baseline.

## Optimization backlog

| Item | One-line scope | Measured basis |
|---|---|---|
| B2 | Separate the monitor lock and per-core reservation slots by cache line. | Exclusive pool is about 14 core-ms/frame upper bound. |
| B3 | Measure exclusive timing and shorten the exclusive bridge/lock path. | Host exclusive functions are 5.57% of AOT samples versus 0.15% JIT; dispatch/lookup/slice is 3.18%. |
| B5 | Replace unlock `xchg` with a plain store after ordering analysis. | Same exclusive pool; no FPS attribution yet. |
| B6 | Reduce dispatch/lookup/slice and remaining sdk-to-main exits. | Dispatch/lookup/slice 3.18%; about 8M sdk exits each way per 10 seconds. |
| A7 | Refine FMI memory checks where alignment permits. | FMI checks are 3.9% of samples. |
| A8 | Revisit prologue/epilogue, entry checks, flag stores, and in-block transfers. | Prologue/epilogue 1.5%, entry checks 1.1%, flags/transfers about 0.3%. |
| C9 | Reduce rasterizer write-notification cost. | About 1.1 us/store and 1.6 core-ms/frame. |
| D10 | Complete the untested XMOD1 accounting, miss, gap, IVAU, budget, and publication paths. | No runtime measurement yet. |
| D11 | Revisit carried launcher, diagnostics, dependency, and held backlog items. | No single current measurement; see archived item list. |

B1 (TTAS) and B4 (lock-free marks default-on) are done in dev25; no speed effect was shown for TTAS. A7/A8 change generated code, so each requires a full export and golden-hash update.

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Still open: a held key/axis can remain stuck across capture; `controller_assign.h` is untested; the Qt Resolution Scale table has a second translated copy; a hand-undone maximize fallback needs one extra toggle; and the overlay is unavailable on OpenGL/Null, which the user plans to remove.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position averaged FPS with ms/frame. Cross chain, chain start, TTAS, lock-free marks, FM inline, direct/indirect chain, split main, and FPX are default-on (`=0` disables). Ledgers and lookup/exclusive/access diagnostics remain opt-in. Preserve saved-value CAS, atomic monitor slots, and the 32-bit index contract.

Overlay contracts are documented in [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md): the action queue is the only way the UI changes settings; settings apply on the event thread; Resolution Scale is save-only until next launch; the UI reads snapshots only; every capture/rebind begin has exactly one stop; and controller UI is Player 1 only.

## How to proceed

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; send code implementation to the Orca Code Implementer unless the user explicitly assigns it to the main agent; keep changes focused; and record only verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run.

## How to work / procedures

- Build standalone tests after BuildTools vcvars64 through `cmd.exe /c` with `cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` when required.
- Every new standalone test needs a shared contract check with a deliberately weakened negative control that fails fast and cannot hang.
- For overlay input bugs, headlessly replay the real router/command-bar header with ImGui sources from `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` before changing code.
- Put scripts containing `\n` or quotes in files; use `D:\HisuiWakeBuild\scratch\<task>\`, then clean it with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1`.
- Build `suyu-cmd` after code changes; after the user's commit, reconfigure and verify `GIT_REV`; never build during FPS measurement. For a new PLA export, follow the standing export procedure in CLAUDE.md.
- For headless overlay replay, use the real router and ImGui sources above with press/release batching, focus events, and combo-popup coverage.
- Analysis helpers and sampling scripts are outside the repository in `D:\HisuiWakeBuild\tmp_gt` and `D:\HisuiWakeBuild\diag\hotrva`; keep diagnostics off during FPS-only runs.
- Orca: one worker at a time, acknowledge deliveries, release each worker, and remember that `check --wait` can replay earlier messages.

