# Aevora Work Plan

Handoff prepared: 2026-10-21 (session 41). See [session 40 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-40-cleanup), [session 39 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-39-cleanup), [session 38 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-38-cleanup), [session 37 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-37-cleanup), [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Goal: lower host CPU usage per core at 30 FPS; generation, compilation, or startup alone does not establish compatibility. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md); the supported gameplay path and Tauri GUI direction are confirmed.

## Status

HEAD still `29be34a` (`v0.0.2-dev12: record the N0 inventory, check list, and gate report (N0 stage 7)`) on `main`. Uncommitted: stage 1 files (`recomp_pc_description.h`, `arm_recomp.cpp`, and `pc_description_unit.cpp`) plus this handoff batch. Host binaries (`suyu.exe`, `suyu-cmd.exe`) were rebuilt after stage 1; `GIT_REV` in `D:\HisuiWakeBuild\msvc-release\src\common\scm_rev.cpp` is older than HEAD until the next reconfigure. The PLA export and persistent AOT cache entry still date from `7609ab3`; a full export is required before any AOT measurement.

## N0 summary

N0 boundary-inventory stages 0-7 are done: recorder `src/core/boundary_inventory.h` (+ `_report.h`, `_scan.h`), SVC/IPC/nvdrv/audio/applet/file hooks, manual segments (F9 mark, F10 pause, Inventory window), SVC and IPC caller chains, local module/NVN scan, structural gate `tools/structural_gate/structural_gate.py`, and [N0_INVENTORY.md](N0_INVENTORY.md) (check list, aggregates, gate report). Commits: 5b `b619bf9`, 5c `3c945e4`, 6 `c338961`, 7 `29be34a`; earlier stages are in the archive. Results: 32 SVCs, 44 service interfaces / 121 commands, 34 nvdrv rows over six devices, no audio renderer open, no applet; the local scan found 563 NVN strings and 15,965 NVN/SDK dynsym names; IPC callers stop inside the SDK within four frames; the first gate baseline on the current launcher fails N1 (JIT present) as expected. N0 was accepted by the user on 2026-10-21. The scratch folder `D:\HisuiWakeBuild\scratch\n0_stage0` was deleted afterwards.

Local data: `D:\HisuiWakeBuild\diag\n0\` (collections `boot1`, `test1`, `test2`—do not delete; helpers `run_n0.bat`, `summarize_inventory.py`, `segments_report.py`) and `D:\HisuiWakeBuild\diag\n0\gate\` (baseline).

## Design decisions to keep (N0)

Raw guest addresses and game-derived names stay local; the repository gets aggregates only. The IPC caller is a per-thread stash written at SVC 0x21/0x22 and consumed once by the service thread, bounded to 1024 threads. The module scan writes only a local file. The gate keeps one deny table for all milestones and refuses unverified evidence unless the user opts in.

## Not verified yet

Inventory under the AOT launcher; scan stall time on the first SVC; asynchronous SVC 0x23 stash limit; IPC attribution beyond the SDK (optional: 8 frames or first non-SDK frame); the gate on Linux and with a real symbols-only launcher; typed-label input of the Inventory window.

## Immediate next action

1. N1 is chosen; stage 1 is complete and awaits the user's commit. The next stage is stage 2, the dynarmic-free native exclusive monitor and its comparison against the mutex implementation.
2. After the user commits, reconfigure and rebuild the host before exporting; the host embeds the git hash at CMake configure time.

## N1 notes

- `SUYU_NO_JIT` is defined at `CMakeLists.txt:229`; it removes dynarmic sources and links in the core, video, and Suyu CMake files. `MakeExclusiveMonitor` selects `StandaloneExclusiveMonitor` under this option, while `suyu-cmd-static` links the shared `core` library.
- The title qualifier is `(JIT-capable)` versus `(no-JIT build)` in `src/suyu_cmd/native_status.h`; the structural-gate N1 deny list rejects dynarmic symbols and imports.
- The planned no-JIT tree is `D:\HisuiWakeBuild\msvc-release-nojit`; the existing `D:\HisuiWakeBuild\msvc-release` tree remains the JIT tree for Qt, Hybrid, and comparison.
- Residual fatal logs that still print a bare PC are the legacy IC IVAU refusals in `src/core/arm/recomp/arm_recomp.cpp` and generated-code guards in `src/core/recompiler/arm64_to_c.h`; the latter's changed or unmapped-code guard aborts immediately in strict mode.

## Open items

- Pre-existing Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` has 7 failures/1 error pre-existing; tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD checks, OpenGL/Null windows, FPS/overhead, and Linux/FreeBSD configure/build after macOS removal remain untested; `tests/exclusive_access/run.py` passed in session 38.
- Plain `suyu-cmd.exe <game>` does not auto-assign controllers (`emu_window_sdl2.cpp:842`); assign Player 1 in the overlay Controller window. `suyu-cmd -g <path>` reads through the ANSI code page and garbles non-ASCII names; the positional argument is UTF-16.

## Measurement setup

Target: i5-12400F (6C/12T), RTX 2060 SUPER, 16 GB; 30 FPS cap. Config: `frame_pacing_mode=0`, `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, `use_multi_core=true`. Metrics: process/busiest-thread CPU ms/game-frame (`core_pct`), p99 frame interval, same-position JIT/AOT; below-cap runs also compare CPU ms/second. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\` (baseline and `97cad8f` X-Y-X); summarizer `summarize_cpu_budget.py`. X-Y-X details are in the session 39 archive.

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Held key/axis can remain stuck across capture; `controller_assign.h` is untested; Resolution Scale has a translated duplicate; maximize fallback needs one toggle; OpenGL/Null overlay is unavailable; PDH GPU accuracy is unverified.

## Contracts to preserve

Keep diagnostics execution-neutral when off, exact-0 default-on semantics, ABI/result/cache contracts, overlay action-queue/event-thread rules, and same-position CPU ms/frame. `SUYU_HOST_TIMING_BLOCK` is default-on; `SUYU_HOST_TIMING_STATS` is opt-in; `SUYU_HID_NPAD_UPDATE_MS` defaults to 4 ms and accepts 1-8. Preserve saved-value CAS, atomic monitor slots, 32-bit indexes, and two-line comments. N0 preserves execution-neutral-off behavior, report format v1 section names, one authoritative module list, and no committed guest addresses.

## Lessons

- New core files include `common/logging.h`; `run.py` needs `CPM_SOURCE_CACHE`.
- Copy measured logs before the next Qt start/export; use 10-second CPU-budget windows and do not build during FPS measurement.
- Mixed-ending files require byte-preserving edits: use file edit tools, verify real LF counts, and do not emit escaped newline text.
- A CMake source addition can trigger a long full rebuild; a running `suyu.exe` blocks relinking.
- Verify worker deliveries and Orca heartbeat-only returns before acting on a report.
- `orca orchestration worker-release` rejects `--run`; pass only `--dispatch` (and `--json`).
- Bash heredocs, `printf`, and `sed` can alter backslash sequences; write Python edit scripts and `.bat` files with the Write tool and check `git diff --numstat` after every scripted edit.
- `cl` uses the console code page: run `tests/package_policy` without `-X utf8` under vcvars64 or `test_policy_helpers` fails to decode compiler output.
- `git rm` stages deletions; use `git reset -q` afterwards so all changes stay unstaged for the user.
- `cleanup_scratch.ps1` removes the whole scratch folder even with a path.
- Before a large deletion/refactor that must not change Windows behavior, take the `pp_compare` preprocess baseline first.
- Git may rewrite working copies to CRLF while index blobs stay LF; count bytes with Python, preserve endings, and check `git ls-files --eol`.

## How to proceed

Follow the start checklist, verify evidence before changing code, use one worker at a time, and record only verified facts. Main agent implements; Code reviewer reviews; Code Explorer is read-only; md Implementer edits Markdown. Do not claim unrun exports or hand tests. N0 stage workflow: plan, implement with a unit test and header mutants, Code reviewer review, host build, Japanese report with file list and normal-commit message ending in the Co-Authored-By line, then wait for user go-ahead.

## How to work / procedures

- Build standalone tests after vcvars64 with `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- CPU budget: set `SUYU_CPU_BUDGET=1` (optional `SUYU_CPU_BUDGET_WINDOW_SEC` 1-60, default 10), copy logs, summarize with `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>` (about 300 at 30 FPS, 240 AOT); filter loading windows and keep diagnostics off for FPS-only runs.
- Opt-in stats: `SUYU_HOST_TIMING_STATS=1`; logs every 10 s. Helpers: `D:\HisuiWakeBuild\tmp_gt`, `D:\HisuiWakeBuild\diag\hotrva`.
- Replay overlay input with real router/ImGui sources; follow the standing export procedure in `CLAUDE.md`; clean scratch output.
- Orca: one worker at a time, acknowledge deliveries, and release workers. Heartbeat notices need no action (no `check`, no second waiter); wait with the single `check --wait --types worker_done,escalation`.
- Source-change equivalence check: `D:\HisuiWakeBuild\diag\pp_compare` holds `pp.py` (preprocess every translation unit of `compile_commands.json` with `/EP` into gzip files; run it from PowerShell with the repository untouched while it runs), `cmp.py` (accepts only `__LINE__` shifts derived from `git diff` and groups every other hunk by signature; `cmp.py --selftest` runs the negative controls), `after_only.py`/`before_only.py` (list added/removed lines that are not line shifts), `gen_variants.py` (build `smoke_export` from a source tree and write every smoke variant), `ed.py` (exact-match, line-ending-preserving edit helper) and `vcenv.bat`. Run them with `CPM_SOURCE_CACHE=D:\HisuiWakeBuild\cpm` where cmake is involved.
- Header-mutant check: copy the header to scratch, deliberately change it, put scratch first on `/I`, require a failing unit-test exit, use the Write tool for scripts, build under `cmd.exe /c` with vcvars64, and delete scratch afterwards.
- Collect with `D:\HisuiWakeBuild\diag\n0\run_n0.bat <game> <label>`; assign Player 1 first; F10 starts recording and F9 ends a segment; quit normally.
- Gate: `python -B -I -X utf8 tools/structural_gate/structural_gate.py <image.exe> --pdb <image.pdb> [--require-absent N1] [--json-out F] [--text-out F]`.
- Read a local scan as `inventory_<run>_scan.txt` in the inventory output folder; never commit it.
- Host rebuild: `Start-Process cmd.exe /c D:\HisuiWakeBuild\rebuild-suyu.bat`; confirm cmake/ninja/cl are running and read `D:\HisuiWakeBuild\rebuild-suyu.log`; only suyu and suyu-cmd are rebuilt.

## Working rules since session 40

- Per-commit `devN` CHANGELOG entries are the rule again from `v0.0.2-dev12` (dev1-dev11 have no sections; see `git log 28011a2..c338961`) unless the user declares a normal commit; the user makes all commits.
- The one-stage-at-a-time report/file-list/commit-message/wait rule applies to milestone work until the user relaxes it.

## Context and token practice

A token audit found context grew about 40k→361k over 145 calls (cache reads dominate), including one 340k-token rebuild after a 2 h 45 min idle gap; 30/95 Bash calls were Orca round-trips. Start a new session or `/compact` at stage boundaries/before breaks; read with offset/limit or grep; print only extracted worker fields; treat “You have 1 orchestration message” as a heartbeat (no `check`, no second waiter) and rely on the single blocking `check --wait --types worker_done,escalation`.
