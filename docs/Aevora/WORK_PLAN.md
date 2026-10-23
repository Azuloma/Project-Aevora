# Aevora Work Plan

Handoff prepared: 2026-10-22. See [session 44 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-44-cleanup), [session 43 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-43-cleanup), [session 42 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-42-cleanup), and [older history](OLD_HISTORY.md).

## Objective
Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md).

## Status
HEAD is the docs-only record commit `a0174a2` on top of `5057b57` and `4362604 v0.0.3-dev10` (runtime code equals dev10); working tree state is whatever `git status` shows, so run it.
Commits: dev1 `8796a8c`, dev2 `65854f3`, dev3 `ce4bfd6`, dev4 `56db155`, slots rename `8133d92`, dev5 `ca825f8`, dev6 `65a1f15`, dev7 `d051efb`, dev8 `ddfb960`, dev9 `aebd786`, dev10 `4362604`, dev11 `eba359e`, dev12 (uncommitted), docs-only record `5057b57`, docs-only record `a0174a2`.

### N2 progress
| Stage | State |
|---|---|
| 0 | Done: inventory, design note, and test4 collection. |
| 1 | Done: standalone mapper, dev3, and mutants. |
| 2 | Done: shadow, dev4-dev6; accepted after a JIT PLA session on the dev5 build: 0 mismatch lines, `sync_failures=0`, MapPhysicalMemory 2317 compared, SetMemoryAttribute 239, MapMemory 26, UnmapMemory 3, SetHeapSize 1, UnmapPhysicalMemory 1, SetMemoryPermission 0 not exercised; accepted with the recorded limits. |
| 3 | Stage 3a, 3b-1, 3b-2, and 3b-3 through dev10 and Stage 3c (JIT and AOT) validated. |
| 4 | In progress: 4a-1 (dev11) committed; 4a-2 (dev12) done pending the user's PLA validation; 4b and 4c pending. |
| 5 | Not started; needs the user's go-ahead. |

See [N2_DESIGN.md stage plan and verification](N2_DESIGN.md#stage-plan-and-verification).

### N2 limits to remember
- Stage 2 shadow limits still matter: the extra Locked/Uncached writer is unidentified, and relaxed physical-op comparisons are state-only.
- `QueryMemory` is not hooked and `Common::PageTable` entries are not compared.
- IPC map-alias (`SetupForIpc`), `MapCodeMemory`, and unsafe/insecure operations remain unsupported; other documented kernel divergences remain in [N2_DESIGN.md](N2_DESIGN.md).
- Full `tests/recompiler_smoke/run.py` and a real smoke CMake configure have never run.
- Stage 3c JIT and AOT validation passed: both JIT and exported static-launcher runs (native memory on and `SUYU_NATIVE_MEM=0`) completed without crashes; AOT ended with `unsupported_ops=0 inconsistency=0`, and both reports were fully static with `static -> JIT: 0` and zero unresolved import traps. CPU-budget A/B means were 107.72/108.08 ms/frame (p50 34.40/33.82 ms; p99 affected by one load stall), with separate launches and non-identical positions/operations; AOT stayed below JIT performance and no parity is claimed.
- The native model is default-on; `SUYU_NATIVE_MEM=0` is the comparison/fallback run, and non-39-bit spaces fall back per process.
- Unsupported operations and the kept divergences are listed in [N2_DESIGN.md Stage 3b-3 results](N2_DESIGN.md#stage-3b-3-results).
- If a later session reports an unsupported operation, implement it natively and review it as a new dev stage.

## Handoff
### Completed milestones
- N0 boundary inventory is done and accepted; see [N0_INVENTORY.md](N0_INVENTORY.md).
- N1 CPU-without-JIT is done and accepted with its recorded performance caveat.
- N2 Stages 0-3 (including 3c JIT and AOT) are complete as summarized above.

### Session 44 summary
- Both host trees were reconfigured and rebuilt at `5057b57`; Stage 3c passed on JIT and the exported AOT launcher with native memory on and `SUYU_NATIVE_MEM=0`.
- The stale JIT `svc_memory.cpp.obj` had an empty dependency entry and froze PLA; deleting the object and rebuilding fixed it without a source change.
- JIT and AOT both ended with `unsupported_ops=0 inconsistency=0`; AOT was fully static with `static -> JIT: 0`.
- AOT process CPU means were 107.72 versus 108.08 ms/frame; JIT steady windows were about 75.3 versus 77.2 ms/frame.
- Limits are separate launches and no bag-window split; no performance-parity claim is made.
- Full text is archived in the [session 44 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-44-cleanup).

## Immediate next action
1. Run `git status` and `git log -5`, and preserve user changes.
2. Read `CLAUDE.md`, this file, `NATIVE_ROADMAP.md`, `CODEBASE_INDEX.md`, and `N2_DESIGN.md`.
3. Run the N2 Stage 4a-2 PLA JIT and AOT sessions after backing up save data: compare with `SUYU_NATIVE_MEM_POOL=0`, check the `native_phys_pool enabled` log, boot without a crash, bag and quit, confirm native memory ends with `unsupported_ops=0`, and exercise a session with a large `MapPhysicalMemory`. 4b starts only after the 4a-2 commit and these sessions.
4. Optional cleanups are the full `tests/recompiler_smoke/run.py` with `CPM_SOURCE_CACHE`, a real smoke CMake configure, inspecting `aot_cache_outcome` on the next export, unresolved GOT/PLT symbols such as `__tdata_align_rel`, and the dev17 title confirmation.
5. After any new commit, reconfigure and rebuild both host trees one at a time (see [How to work / procedures](#how-to-work--procedures)) and check `GIT_REV`.

## Open items
- Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` failures and tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD, OpenGL/Null, FPS/overhead, and Linux/FreeBSD configure/build remain untested; controllers are not auto-assigned.
- Exported title is unconfirmed; other UI strings may still need the Aevora rename.
- No same-position JIT/AOT CPU ms/frame measurement; no-JIT export-build-kit was not built.
- Bare-PC fatal logs, GOT/PLT symbols, and the `UnpinHandle: Pin count imbalance` warning remain unexplained.
- Full `tests/recompiler_smoke/run.py` and `tests/exclusive_access` suites remain to be run; `StandaloneExclusiveMonitor` lacks ThreadSanitizer stress coverage.
- Old N0 follow-ups remain: AOT-launcher inventory, scan stall time, SVC 0x23 stash limit, SDK-external IPC attribution, Linux gate, and typed-label input.

## Measurement setup
Target: i5-12400F (6C/12T), RTX 2060 SUPER, 16 GB; 30 FPS cap. Measure process/busiest-thread CPU ms/game-frame, p99 frame interval, same-position JIT/AOT, and below-cap CPU ms/second. Logs are under `D:\HisuiWakeBuild\diag\cpu_budget\`; use `summarize_cpu_budget.py`.

## Open overlay issues
See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Held key/axis can remain stuck across capture; controller assignment is untested; Resolution Scale has a translated duplicate; maximize fallback needs one toggle; OpenGL/Null overlay is unavailable; PDH GPU accuracy is unverified.

## Contracts to preserve
Keep diagnostics execution-neutral when off, exact-0 default-on semantics, ABI/result/cache contracts, overlay action-queue/event-thread rules, and same-position CPU ms/frame. Preserve saved-value CAS, atomic monitor slots, 32-bit indexes, two-line comments, one authoritative module list, report format v1, and no committed guest addresses.

## Lessons
- New core files include `common/logging.h`; `run.py` needs `CPM_SOURCE_CACHE`.
- Copy measured logs before the next Qt start/export; use 10-second CPU-budget windows and do not build during FPS measurement.
- Mixed-ending files require byte-preserving edits; verify `git ls-files --eol` and byte counts.
- A CMake source addition can trigger a long full rebuild; a running `suyu.exe` blocks relinking.
- The `slots` identifier collides with the Qt keyword macro in widely included headers; use another name.
- From Git Bash, `cmd.exe /c <bat>` can drop into interactive cmd and hang; run vcvars scripts through the PowerShell tool.
- Header-mutant sweeps take about 15 s per mutant with cl; record a state-only/relaxed comparison as a weakened check.
- A freeze in a kernel header-only change whose environment switch is off may be a stale object with an empty ninja deps entry; run `ninja -t deps` and search `#deps 0,` before changing code.

## How to proceed
Read the start checklist, verify evidence, use one worker at a time, and record only verified facts. Per stage: plan, implement with a unit test and header mutants, Code reviewer, host build, md Implementer batch, Japanese report with file list and a commit message ending in `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`, then wait for the user's commit.

## How to work / procedures
- Standalone tests use vcvars64 and `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- CPU budget: set `SUYU_CPU_BUDGET=1`, copy logs, summarize with the documented helper, filter loading windows, and keep diagnostics off for FPS-only runs.
- Replay overlay input with real router/ImGui sources; follow the standing export procedure in `CLAUDE.md` and clean scratch output.
- Orca uses one worker at a time; acknowledge deliveries, release workers, and use one blocking `check --wait`.
- Host rebuild through `cmd.exe /c` with an absolute helper path; never build both trees concurrently.
- To run PLA with the host JIT build, start a new PowerShell, run `Get-ChildItem Env:SUYU_*`, change to `D:\HisuiWakeBuild\msvc-release\bin`, and run `.\suyu-cmd.exe "<game path>"` with the positional path and no output redirect; the log is `%APPDATA%\suyu\log\suyu_log.txt` and the previous run is `suyu_log.txt.old.txt`. An exported AOT launcher writes `<export folder>\user\log\suyu_log.txt`; the path may contain non-ASCII, so use `-LiteralPath`. Copy each log to `D:\HisuiWakeBuild\diag\cpu_budget\<name>` before the next start and summarize with `python -I -X utf8 summarize_cpu_budget.py <log> 100`; `native_mem final ...` is written only on a clean game quit.
- A freeze that stops at 0 frames while a core thread spins can be diagnosed by walking its stack with DbgHelp `StackWalk64` after checking the PDB GUID/Age, and by reading the process environment; do not trust a conclusion from a stale object.
- After a header-only kernel change, run `ninja -t deps` under `vcvars64` and search `#deps 0,` in `src/core`, `src/common`, `src/video_core`, and `src/hid_core`; delete that `.obj` and rebuild. The JIT tree once had `svc_memory.cpp.obj` with an empty deps entry, freezing PLA at the first `MapMemory` while native/shadow switches were off.
- Run host reconfigure and build as separate processes; chaining two `vcvars64` calls in one `.bat` stopped silently. After an export, run `rebuild-suyu.bat`.

## Working rules
- Per-commit `devN` CHANGELOG entries use `v0.0.3-devN` from dev1 unless the user declares a normal commit; the user makes all commits.
- The one-stage-at-a-time report/file-list/commit-message/wait rule applies until the user relaxes it.

## Context and token practice
Start a new session or `/compact` at stage boundaries/before breaks; read with offset/limit, print only extracted worker fields, and use one blocking `check --wait`.
