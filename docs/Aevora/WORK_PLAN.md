# Aevora Work Plan

Handoff prepared: 2026-10-22. See [session 42 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-42-cleanup) and [older history](OLD_HISTORY.md).

## Objective
Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md).

## Status
HEAD is `d051efb v0.0.3-dev7` (Stage 3a, committed); working tree contains uncommitted `v0.0.3-dev8` (N2 Stage 3b-1 kernel-free headers only), pending the user's commit.
Commits: dev1 `8796a8c`, dev2 `65854f3`, dev3 `ce4bfd6`, dev4 `56db155`, slots rename `8133d92`, dev5 `ca825f8`, dev6 `65a1f15`.
The working tree contains only the uncommitted dev8 source/tests and its Markdown record.

### N2 progress
| Stage | State |
|---|---|
| 0 | Done: inventory, design note, and test4 collection. |
| 1 | Done: standalone mapper, dev3, and mutants. |
| 2 | Done: shadow, dev4-dev6; accepted after a JIT PLA session on the dev5 build: 0 mismatch lines, `sync_failures=0`, MapPhysicalMemory 2317 compared, SetMemoryAttribute 239, MapMemory 26, UnmapMemory 3, SetHeapSize 1, UnmapPhysicalMemory 1, SetMemoryPermission 0 not exercised; accepted with the recorded limits. |
| 3 | Stage 3a done; Stage 3b-1 done pending commit; Stage 3b-2, 3b-3, and 3c remain. |
| 4-5 | Not started. |

See [N2_DESIGN.md stage plan and verification](N2_DESIGN.md#stage-plan-and-verification).

### Stage 2 limits to remember
- The writer of extra Locked/Uncached bits on already-mapped pages during MapPhysicalMemory is unidentified.
- Physical-op comparison is state-only for the relaxed cases; zero mismatches does not prove permission/attribute preservation for those two ops.
- QueryMemory SVC is not hooked and `Common::PageTable` entries are not compared.
- Coverage was JIT-only; hunting diagnostics were removed in dev6 and must be re-added if hunting resumes.
- Full `run.py` and a real `tests/recompiler_smoke` CMake configure were never run for Stage 1/2.

### Stage 3b-1 limits to remember
- Headers are kernel-free and unconnected; `SUYU_NATIVE_MEM` does not exist in code before dev10.
- Dev9 adapters are not yet switched; no kernel adapter, host inclusion, JIT/no-JIT rebuild, or PLA session exists.
- IPC message copies are supported-path operations, but IPC map-alias (`SetupForIpc`) remains unobserved and unsupported.
- Dev10 must keep native and `KPageTable` per-process state exclusive; only layout/query helpers and `Common::PageTable` remain on `KPageTable`.

### Stage 3a limits to remember
- The full `tests/recompiler_smoke/run.py` suite was not run.
- A real `tests/recompiler_smoke` CMake configure was not run.
- The no-JIT tree was not rebuilt.
- No PLA session was run; bag and quit remain unsegmented.
- nvmap device-map locks and shared/transfer/vi/TLS callers are modeled but not connected; IPC map-alias/SetupForIpc, MapCodeMemory, and unsafe/insecure operations remain unsupported.
- The writer of extra Locked/Uncached bits remains unidentified.

## Handoff
### Completed milestones
- N0 boundary inventory is done and accepted; see [N0_INVENTORY.md](N0_INVENTORY.md).
- N1 CPU-without-JIT is done and accepted with its recorded performance caveat.
- N2 Stages 0-2 are complete as summarized above.

## Immediate next action
1. The user commits `v0.0.3-dev8` (Stage 3b-1 kernel-free headers and documentation).
2. Next implement Stage 3b-2: add adapters without switching `KProcessPageTable`, record exact replaced-kernel side effects in N2_DESIGN, and run JIT/no-JIT host builds.
3. Optional cleanups: run full `run.py`, a real `tests/recompiler_smoke` CMake configure, and a no-JIT/AOT shadow session.
4. Dev17 title confirmation remains pending.

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

## How to proceed
Read the start checklist, verify evidence, use one worker at a time, and record only verified facts. Per stage: plan, implement with a unit test and header mutants, Code reviewer, host build, md Implementer batch, Japanese report with file list and a commit message ending in `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`, then wait for the user's commit.

## How to work / procedures
- Standalone tests use vcvars64 and `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- CPU budget: set `SUYU_CPU_BUDGET=1`, copy logs, summarize with the documented helper, filter loading windows, and keep diagnostics off for FPS-only runs.
- Replay overlay input with real router/ImGui sources; follow the standing export procedure in `CLAUDE.md` and clean scratch output.
- Orca uses one worker at a time; acknowledge deliveries, release workers, and use one blocking `check --wait`.
- Host rebuild through `cmd.exe /c` with an absolute helper path; never build both trees concurrently.

## Working rules
- Per-commit `devN` CHANGELOG entries use `v0.0.3-devN` from dev1 unless the user declares a normal commit; the user makes all commits.
- The one-stage-at-a-time report/file-list/commit-message/wait rule applies until the user relaxes it.

## Context and token practice
Start a new session or `/compact` at stage boundaries/before breaks; read with offset/limit, print only extracted worker fields, and use one blocking `check --wait`.
