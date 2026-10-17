# Aevora Work Plan

Handoff prepared: 2026-10-17 (session 31 end). Older material: [session 29 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-29-cleanup), [session 30 cleanup](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-30-cleanup), [session 31 cleanup](OLD_HISTORY.md#archived-on-2026-10-17-from-work_planmd-session-31-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `06c8abb`. The last PLA export is unchanged at `ac96270` in `D:\HisuiWakeExports\_NEW` and contains none of the overlay work. The host was last rebuilt successfully with `GIT_REV` `7d73d31`; the binaries contain the later command-bar fix but retain that embedded revision until the next reconfigure. Optimization is paused. The user is hand-testing the overlay; no results beyond the combo/popup fix have been recorded. These Markdown changes remain uncommitted until the user commits.

## What was done so far

- Dev19-dev25 delivered FPXV, diagnostics, index, chain, TTAS, and default-option work; see [CHANGELOG.md](../../CHANGELOG.md).
- Session 30 measured TTAS and recorded the `ac96270` PLA export; see [CHANGELOG.md](../../CHANGELOG.md).
- Stage F0-F5 delivered the overlay survey, keyboard/text forwarding, action queue, Graphics, Player 1 Controller, Mods and folders, background scanning, cheat-file discovery, errors, and Game section; commits are listed below and in [CHANGELOG.md](../../CHANGELOG.md).
- Fullscreen toggle adds F11, Alt+Enter, View menu, geometry restore, maximize fallback, hotkey ownership, and SDL enter/leave handling (`7138f29`, `7d73d31`).
- The command-bar popup bug was fixed in `06c8abb`; headless replay of the real router/rule and ImGui 1.92.9b sources reproduced the old focus theft and verified the fix.

## Immediate next action

For the next session, in order:

1. Ask the user for hand-test results and record only what they report.
2. If a build matching HEAD is wanted, reconfigure and rebuild `suyu` and `suyu-cmd` through `cmd.exe /c` with BuildTools vcvars64: `cmake -S <repo> -B D:\HisuiWakeBuild\msvc-release`; verify `GIT_REV` afterward. This takes about 20 minutes; do not measure FPS meanwhile.
3. Fix hand-test findings one at a time; staggered first-use window positions are a known candidate.
4. After parity is confirmed, F6 retires F12: remove its panel/key branch, controller-assignment wording, and docs/index/CLAUDE references.
5. After F6, ask whether to resume the paused optimization backlog or export PLA with the overlay. A full export needs a commit, reconfigure, and the standing export procedure.

## Stage F plan

F0 survey: done. F1 keyboard/text forwarding: done (`22a1045`, `350af77`). F2 action queue: done (`2d1a42e`, `2b219fa`). F3 Graphics: done (`468cd47`, `7ab529e`). F4 Controller: done (`6498ed1`, `2fdf3f9`). F5 Mods/folders/status and Game section: done (`59d7338`, `1506b19`). Fullscreen toggle: done (`7138f29`, `7d73d31`). Command-bar popup fix: done (`06c8abb`). F6 remains: retire F12 only after the user confirms parity by hand.

## Open issues in priority order

### Group E - overlay hand test

Confirm by hand: Resolution Scale pick/save/next-launch effect; Controller Map, keyboard, rebinding, timeout, Clear, Combine/Split; Mods and folders Open x3, list, Rescan, and cheat files; Game section; fullscreen F11/Alt+Enter/menu, Exclusive/Borderless, restore size, and multi-monitor; no click leakage while menus/combos are open; and F12 parity. The combo/popup issue is fixed in `06c8abb`, but the user has not yet confirmed it. All overlay windows currently use `(16, 48)` and overlap; stagger them if the user wants the UX fix.

Other open Group E items: a held key/axis can remain stuck when capture starts and release occurs during capture (`src/input_common/main.cpp`, `input_engine.cpp`); `controller_assign.h` has no unit test because it needs `ParamPackage`; and the Qt Resolution Scale table is a translated second copy guarded by `smoke_graphics_settings_unit`. A maximize fallback that the user undoes by hand needs one extra toggle. Longer comments in F12 code may be reported by review and are removed with F6.

### Paused optimization backlog

B2, B3, B5, B6, A7, A8, C9, D10, and D11 remain unchanged. Full details and the older Group A/B/C/D lists are in [OLD_HISTORY.md](OLD_HISTORY.md). No attribution is made for the 26 FPS reading.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position averaged FPS with ms/frame. Cross chain, chain start, TTAS, lock-free marks, FM inline, direct/indirect chain, split main, and FPX are default-on (`=0` disables). Ledgers and lookup/exclusive/access diagnostics remain opt-in. Preserve saved-value CAS, atomic monitor slots, the 32-bit index contract, and F12 parity decisions.

Overlay contracts: the action queue is the only way the UI changes settings; settings apply on the event thread; Resolution Scale is save-only until next launch; the UI reads snapshots only; every capture/rebind begin has exactly one stop; and controller UI is Player 1 only.

## How to proceed

Read CLAUDE.md, README.md, this plan, and the index; inspect status/log before work; ask the user when a choice is required; use one worker at a time; keep changes focused; and record verified facts in the next Markdown batch. Do not claim a hand-test or export result that was not reported or run.

## How to work / procedures

- Build standalone tests after BuildTools vcvars64 through `cmd.exe /c` with `cl /std:c++20 /EHsc /W4 /I src <test>`. Add `/Zc:preprocessor` and `/utf-8` for `settings_enums.h` or UTF-8 literals.
- Every new standalone test needs a shared contract check that a deliberately weakened copy fails; negative controls must fail fast and must not hang.
- For overlay input bugs, write a headless replay linking the real router/command-bar header with ImGui sources from `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`) before changing code.
- Put scripts containing `\n` or quotes in files; shell heredocs broke twice. Scratch lives in `D:\HisuiWakeBuild\scratch\<task>\` and is removed after each task.
- Build `suyu-cmd` after code changes. Reconfigure after the user's commit to refresh `GIT_REV`; never run builds during FPS measurement. Use the standing export procedure for a new PLA export.
- Orca: one worker at a time, acknowledge the previous delivery id, release each worker, and note that `check --wait` can replay earlier messages.

## Design notes

`AcquireDebugOverlay` is never destroyed because the renderer retains a raw pointer and owns shutdown. ImGui state belongs to the scheduler worker; capture regions lag one frame. F3 is save-only because the running renderer reads rescaling information without synchronization. F1 Reset uses `ClearInputKeys`/`ClearInputMouse` because Reset+FocusGained merge in one ImGui batch. A popup keeps an already-visible command bar up but must not summon a hidden bar: combo popups are not menus, and the old rule let `BeginMainMenuBar` steal focus. Hotkey press ownership lasts until release; focus loss resets it.

