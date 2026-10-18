# Aevora Work Plan

Handoff prepared: 2026-10-18 (session 34 end). Older material: [session 29 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-29-cleanup), [session 30 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-30-cleanup), [session 31 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-31-cleanup), [session 32 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-32-cleanup), [session 33 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-33-cleanup), [session 34 cleanup](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-34-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

Current status: HEAD `4c711ee` plus uncommitted handoff docs (the user is expected to commit WORK_PLAN/CLAUDE/OLD_HISTORY). The host was reconfigured and rebuilt at `4c711ee`, with `GIT_REV` equal to HEAD at that time. The user reported a full export of `4c711ee` was OK: overlay Roboto with merged BIZ UDPGothic, 125% scale, `#46478B`, and rounded windows. No manifest/cache fields or FPS were reported; the dev27 FPS baseline was dropped, and optimization is paused. See [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-34-cleanup) for detail.

## What was done so far

- Dev19-dev27 delivered the recompiler, diagnostics, 32-bit index, chain/default-option work, TTAS and lock-free marks, fullscreen toggle, ImGui overlay, and Android/iOS removal; details are in [CHANGELOG.md](../../CHANGELOG.md).
- Dev28 and normal commit `4c711ee` delivered the overlay style/font work: Roboto with merged BIZ UDPGothic, 125% scale, `#46478B`, and rounded windows; see [CHANGELOG.md](../../CHANGELOG.md).
- The user-reported full export of `4c711ee` confirmed the overlay behavior described in Status.

## Immediate next action

1. Ask the user to choose the next step: B3 (exclusive bridge/lock path, behind an `=0` switch, then one export to compare on/off at the same position), non-measurement open items (configure_audio combo index, tracked `__pycache__`/`.bak` cleanup, `controller_assign.h` test), or open overlay issues.
2. After the user commits, reconfigure the host and verify `GIT_REV` before building or exporting.

## Open items

- Pre-existing Qt combo-index bug in `src/suyu/configuration/configure_audio.cpp:72,89`: combo index is cast to `AudioEngine` although Libretro is index 4 and value 5; value 4 remains the Null-sink fallback.
- `research/tests` has 7 failures and 1 error pre-existing on HEAD. Tracked repository artifacts that should not be in git are six `__pycache__` `.pyc` files (`tests/exclusive_access`, `tests/export_build_kit`, `tests/export_regression`, `tests/recompiler_smoke`, `tools/export_build_kit`) and `src/suyu/game_card.cpp.bak`; `tools/cpp-lint.sh:37-38` has tab indentation flagged by `git diff --check` (file style).
- `CHANGELOG.md` dev28 section still names BIZ UDPGothic as the only overlay font (now Roboto + merged BIZ UDPGothic); `4c711ee` has no CHANGELOG entry by the user's choice.

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

Read CLAUDE.md, README.md, this plan, and [CODEBASE_INDEX.md](CODEBASE_INDEX.md); inspect status and logs before work; ask the user when a choice is required; use one worker at a time; the main agent implements, Code reviewer reviews, Code Explorer surveys read-only, and md Implementer edits Markdown; keep changes focused; and record only verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run. If a Markdown batch shortens a section, compare git diff against the intended change; one batch dropped unrelated lines.

## How to work / procedures

- Build standalone tests after BuildTools vcvars64 through `cmd.exe /c` with `cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` when required.
- Every new standalone test needs a shared contract check with a deliberately weakened negative control that fails fast and cannot hang.
- For overlay input bugs, headlessly replay the real router/command-bar header with ImGui sources from `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` before changing code.
- Put scripts containing `\n` or quotes in files; use `D:\HisuiWakeBuild\scratch\<task>\`, then clean it with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1`.
- Build `suyu-cmd` after code changes; after the user's commit, reconfigure and verify `GIT_REV`; never build during FPS measurement. For a new PLA export, follow the standing export procedure in CLAUDE.md.
- For headless overlay replay, use the real router and ImGui sources above with press/release batching, focus events, and combo-popup coverage.
- Analysis helpers and sampling scripts are outside the repository in `D:\HisuiWakeBuild\tmp_gt` and `D:\HisuiWakeBuild\diag\hotrva`; keep diagnostics off during FPS-only runs.
- Orca: one worker at a time, acknowledge deliveries, release each worker, and remember that `check --wait` can replay earlier messages.

