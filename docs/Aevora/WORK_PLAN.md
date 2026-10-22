# Aevora Work Plan

Handoff prepared: 2026-10-22 (test4). See [session 41 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-41-cleanup), [session 40 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-40-cleanup), [session 39 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-39-cleanup), [session 38 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-38-cleanup), [session 37 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-37-cleanup), [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md); the supported gameplay path and Tauri GUI direction are confirmed.

## Status

HEAD is `ca825f8 v0.0.3-dev5: harden the native memory shadow comparison (N2 stage 2)`. Dev6 is the current uncommitted follow-up: it removes the temporary event ring and before/pre/after block dumps used to hunt the unidentified writer while retaining mismatch lines, summaries, overlap detection, bystander guards, and the capacity override. A real JIT PLA session on dev5 with `SUYU_NATIVE_MEM_SHADOW=1` recorded 0 mismatch lines and `sync_failures=0`; final counts were MapMemory 26/0/0/8/0, UnmapMemory 3/0/0/0/0, SetMemoryPermission 0/0/0/0/0, SetMemoryAttribute 239/0/0/0/0, SetHeapSize 1/0/0/0/0, MapPhysicalMemory 2317/0/0/71/23, and UnmapPhysicalMemory 1/0/0/0/0. JIT and no-JIT host trees built after dev6 with 0 errors and linked both frontends; the unit test and mutants were unchanged and not re-run. The repository origin moved to `https://github.com/project-aevora/project.aevora-main` (the old `Azuloma/project.aevora-main` URL redirects).

## Handoff

### Completed milestones

- N0 boundary inventory was accepted 2026-10-21. Commits `b619bf9`, `3c945e4`, `c338961`, and `29be34a` delivered SVC/IPC/nvdrv/audio/applet/file hooks, caller/module scans, local aggregates, and the structural gate; see [N0_INVENTORY.md](N0_INVENTORY.md) and the [session 41 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-41-cleanup).
- N1 “CPU without a JIT” was accepted by the user's request on 2026-10-21 with the performance caveat. Stage 1: `58573ba` dev13 adds module names to strict-mode miss diagnostics via `recomp_pc_description.h`. Stage 2: `c7880f5` dev14 adds `NativeExclusiveState` and `StandaloneExclusiveMonitor` with dynarmic-equivalent fast paths. Stage 3: `d344f3e` dev15 adds the `SUYU_NO_JIT` `msvc-release-nojit` tree, which builds both frontends and passes the structural gate. Stage 4: `a0fb2b3` dev16 makes `BackendName` separate `suyu-static`, `suyu-static-nojit`, and `suyu-hybrid` in the AOT cache key and manifest. Stage 5: dev17 acceptance exported from the no-JIT frontend with manifest `effective_backend suyu-static-nojit`, gate PASS on `suyu-cmd-static.exe`, and PLA played to the Obsidian Fieldlands for about 110 s fully static (static->JIT 0, JIT->static 0); CPU budget windows were 84-152 ms/frame, with p50 33 ms through 42 s then about 50 ms.

The JIT tree is `D:\HisuiWakeBuild\msvc-release`; the `SUYU_NO_JIT=ON` tree is `D:\HisuiWakeBuild\msvc-release-nojit`. Helpers are `configure-msvc-release-nojit.bat`, `rebuild-suyu.bat`, and `rebuild-suyu-nojit.bat`; never build both trees at once, and reconfigure both after each commit so `GIT_REV` matches HEAD. The exporter uses the tree of its own frontend, so a no-JIT frontend can only export strict static AOT. The no-JIT output example is `D:\HisuiWakeExports\Pokémon Legends_ Arceus` (local, game-derived, never committed). `native_status.h` supplies the `(JIT-capable)` / `(no-JIT build)` qualifier logic.

### Known limitation

- Static AOT has improved somewhat but still performs worse than the JIT (user observation; no same-position JIT measurement exists). N1 proves CPU execution without a JIT, not performance parity.

## Immediate next action

1. The user commits dev6 with the message `v0.0.3-dev6: remove temporary native memory shadow hunt diagnostics`.
2. If writer hunting is needed again, re-add the removed event-ring and before/pre/after block-dump diagnostics; a future mismatch currently reports only its mismatch line and summary.
3. Begin Stage 3 (`SUYU_NATIVE_MEM` primary mode) only after the user approves.
4. The dev17 title confirmation remains pending.

## Open items

- Pre-existing Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` has 7 failures/1 error pre-existing; tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD checks, OpenGL/Null windows, FPS/overhead, and Linux/FreeBSD configure/build after macOS removal remain untested; `tests/exclusive_access/run.py` passed in session 38.
- Plain `suyu-cmd.exe <game>` does not auto-assign controllers (`emu_window_sdl2.cpp:842`); assign Player 1 in the overlay Controller window. `suyu-cmd -g <path>` reads through the ANSI code page and garbles non-ASCII names; the positional argument is UTF-16.
- Exported launcher title (qualifier and new “Aevora Static AOT”) is unconfirmed until both-tree reconfigure after `fee3120`, no-JIT rebuild, and full re-export. Other UI strings still say “suyu static AOT (Experimental)” (exporter dialog, game list, completion messages, `main.cpp`); the user may want them renamed to Aevora.
- No same-position JIT/static-AOT CPU ms/frame measurement; no-JIT export-build-kit was not built.
- Bare-PC fatal logs remain (`arm_recomp.cpp` about 2397/2480; `arm64_to_c.h` about 7999/8044/8054). The reason `suyu-cmd.exe --help` prints nothing is unknown.
- Eight unresolved GOT/PLT symbols (`__EX_start`, `__EX_end`, `__got_start`, `__got_end`, `__tbss_align_abs/rel`, `__tdata_align_abs/rel`) appeared at startup and one “UnpinHandle: Pin count imbalance” warning appeared; neither was compared against a JIT log.
- Full `tests/recompiler_smoke/run.py` and `tests/exclusive_access` suites were not run for N1. `StandaloneExclusiveMonitor` was exercised by the real session but not stress-tested (no ThreadSanitizer).
- Old N0 follow-ups: inventory under the AOT launcher, scan stall time, SVC 0x23 stash limit, IPC attribution beyond the SDK, gate on Linux, and typed-label input.
- The full `tests/recompiler_smoke/run.py` suite was not run, and the no-JIT tree was not rebuilt after Stage 0; bag and quit were not segmented in test4.
- The full `tests/recompiler_smoke/run.py` suite was not run for N2 Stage 1; CMake wiring was verified only through a `cmake -P` script-mode run, not a real configure. Stage 2 must decide whether to model the kernel's disable-merge block attributes, which can make `QueryMemory` base/size smaller after adjacent `MapMemory` calls than in the mapper that only coalesces identical neighbours.
- N2 Stage 2 has not had a full `run.py` run, a real `tests/recompiler_smoke` CMake configure, or a real supported-path session with the dev6 code; the verified real session was JIT-only on dev5. AOT/no-JIT session coverage remains absent. The unit test and mutants were unchanged and not re-run after dev6.
- The writer of the extra Locked/Uncached bits on already-mapped pages during `MapPhysicalMemory` was not identified. Because physical-op comparison relaxes already-mapped, still-non-Free pages to `state` only, zero mismatches does not prove permission/attribute preservation for `MapPhysicalMemory`/`UnmapPhysicalMemory`; the removed event ring and block dumps must be re-added if hunting resumes.
- The shadow does not compare `Common::PageTable` entries or hook the `QueryMemory` SVC; alias validity still uses alias records, and unhooked mutators are absorbed by the next re-adoption rather than checked. The 4 GiB shadow capacity is made trivially agreeing for a real `LimitReached` on that operation by temporarily using pre-call normal bytes, then restoring it.

## Measurement setup

Target: i5-12400F (6C/12T), RTX 2060 SUPER, 16 GB; 30 FPS cap. Metrics are process/busiest-thread CPU ms/game-frame (`core_pct`), p99 frame interval, and same-position JIT/AOT; below-cap runs also compare CPU ms/second. Logs are under `D:\HisuiWakeBuild\diag\cpu_budget\`; use `summarize_cpu_budget.py`.

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Held key/axis can remain stuck across capture; `controller_assign.h` is untested; Resolution Scale has a translated duplicate; maximize fallback needs one toggle; OpenGL/Null overlay is unavailable; PDH GPU accuracy is unverified.

## Contracts to preserve

Keep diagnostics execution-neutral when off, exact-0 default-on semantics, ABI/result/cache contracts, overlay action-queue/event-thread rules, and same-position CPU ms/frame. Preserve saved-value CAS, atomic monitor slots, 32-bit indexes, two-line comments, one authoritative module list, report format v1, and no committed guest addresses.

## Lessons

- New core files include `common/logging.h`; `run.py` needs `CPM_SOURCE_CACHE`.
- Copy measured logs before the next Qt start/export; use 10-second CPU-budget windows and do not build during FPS measurement.
- Mixed-ending files require byte-preserving edits; verify `git ls-files --eol` and byte counts.
- A CMake source addition can trigger a long full rebuild; a running `suyu.exe` blocks relinking.
- PowerShell `Start-Sleep` chained with other commands can be blocked: use Bash until-loops or the Monitor tool.
- Remove `D:\fx_*` and scratch folders only with `D:\HisuiWakeBuild\diag\cleanup_scratch.ps1 -Path <folder>`.
- Python that reads a file written by Bash must use Windows paths (`D:\...`), not `/d/...`.
- Verify worker deliveries and Orca heartbeat-only returns before acting on a report; `worker-release` takes `--dispatch`, not `--run`.

## How to proceed

Read the start checklist, verify evidence, use one worker at a time, and record only verified facts. For N1 and later: plan, implement with a unit test and header mutants, Code reviewer review, host build, md Implementer batch, Japanese report with file list and a commit message ending in `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`, then wait. Detailed N1 stage notes are in the [session 41 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-41-cleanup).

## How to work / procedures

- Standalone tests use vcvars64 and `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- CPU budget: set `SUYU_CPU_BUDGET=1`, copy logs, summarize with `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>`; filter loading windows and keep diagnostics off for FPS-only runs.
- Replay overlay input with real router/ImGui sources; follow the standing export procedure in `CLAUDE.md`; clean scratch output.
- Orca: one worker at a time, acknowledge deliveries, and release workers; heartbeat notices need no action; wait with the single `check --wait --types worker_done,escalation`.
- Gate: `python -B -I -X utf8 tools/structural_gate/structural_gate.py <image.exe> --pdb <image.pdb> [--require-absent N1] [--json-out F] [--text-out F]`.
- Collect with `D:\HisuiWakeBuild\diag\n0\run_n0.bat <game> <label>`; assign Player 1 first; F10 starts recording and F9 ends a segment; quit normally. Read local scans as `inventory_<run>_scan.txt`; never commit them.
- Host rebuild through `cmd.exe /c` with an absolute helper path; confirm cmake/ninja/cl are running and read logs. Never build both trees concurrently.
- Source-equivalence and header-mutant procedures remain in `CLAUDE.md`; use `D:\HisuiWakeBuild\diag\pp_compare`, `CPM_SOURCE_CACHE=D:\HisuiWakeBuild\cpm`, and scratch cleanup as applicable.

## Working rules

- Per-commit `devN` CHANGELOG entries are required from `v0.0.3-dev1` onward unless the user declares a normal commit; the user makes all commits.
- The one-stage-at-a-time report/file-list/commit-message/wait rule applies to milestone work until the user relaxes it.

## Context and token practice

Start a new session or `/compact` at stage boundaries/before breaks; read with offset/limit or grep, print only extracted worker fields, and use one blocking `check --wait --types worker_done,escalation`.
