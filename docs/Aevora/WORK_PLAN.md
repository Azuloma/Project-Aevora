# Aevora Work Plan

Handoff prepared: 2026-10-24. See [session 48 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-48-cleanup), [session 47 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-47-cleanup), [session 46 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-46-cleanup), [session 45 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-45-cleanup), [session 44 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-44-cleanup), and [older history](OLD_HISTORY.md).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md).

## Status

HEAD is `0a974bc` (v0.0.3-dev15, N2 stage 4b-3). Both host trees were reconfigured and rebuilt at `0a974bc` in session 49 (`GIT_REV` checked, 0 errors); documentation edits (CPU_RECOMPILER.md, the Stage 4c design) are uncommitted. Recent commits: dev13 `49370cb`, merge `9ca567a`, dev14 `78c7036` (4b-2), dev15 `0a974bc` (4b-3). Earlier commits and docs-only records remain listed in the archived history and prior status records.
Commits: dev1 `8796a8c`, dev2 `65854f3`, dev3 `ce4bfd6`, dev4 `56db155`, slots rename `8133d92`, dev5 `ca825f8`, dev6 `65a1f15`, dev7 `d051efb`, dev8 `ddfb960`, dev9 `aebd786`, dev10 `4362604`, dev11 `eba359e`, dev12 `ec1fdc2`, dev13 `49370cb`, merge `9ca567a`, dev14 `78c7036`, dev15 `0a974bc`, docs-only records `5057b57` and `a0174a2`.

### N2 progress

| Stage | State |
|---|---|
| 0 | Done: inventory, design note, and test4 collection. |
| 1 | Done: standalone mapper, dev3, and mutants. |
| 2 | Done: shadow, dev4-dev6; accepted after the recorded JIT PLA session. |
| 3 | Done: Stage 3a, 3b-1, 3b-2, 3b-3, and 3c; JIT/AOT validation complete. |
| 4 | 4a-1 (dev11) and 4a-2 (dev12) committed and PLA-validated; 4b-1 (dev13) committed and JIT-validated; 4b-2 dev14 `78c7036` committed; 4b-3 dev15 `0a974bc` committed; the user declared its test complete in session 49 (see [dev15 validation](#dev15-validation-session-49)); 4c is designed ([Stage 4c design](N2_DESIGN.md#stage-4c-design-agreed-with-the-user-implementation-not-started)) and awaits the user's go. |
| 5 | Not started; needs the user's go-ahead. |

See [N2_DESIGN.md stage plan and verification](N2_DESIGN.md#stage-plan-and-verification).

### N2 limits to remember

- Stage 2 shadow limits remain: the extra Locked/Uncached writer is unidentified, and relaxed physical-op comparisons are state-only ([Stage 2 results](N2_DESIGN.md#stage-2-results)).
- Unsupported IPC map-alias, `MapCodeMemory`, unsafe/insecure operations, and other documented kernel divergences remain ([Stage 3b-3 results](N2_DESIGN.md#stage-3b-3-results)).
- Full `tests/recompiler_smoke/run.py` and a real smoke CMake configure have never run; no performance parity is claimed ([Stage 3b-3 results](N2_DESIGN.md#stage-3b-3-results)).
- Native memory is default-on; `SUYU_NATIVE_MEM=0` is the fallback/comparison run, and non-39-bit spaces fall back per process ([Stage 4b design](N2_DESIGN.md#stage-4b-design-agreed-with-the-user-implementation-not-started)).
- The physical pool's concurrent-allocation lock is covered by review only; its stress test passed even with the mutex removed ([Stage 3b-3 results](N2_DESIGN.md#stage-3b-3-results)).
- `GetSpaceInfo` lacks the `HAS_NCE` 256 GiB Map39Bit limit and must be fixed before any NCE build ([Stage 4b-1 results](N2_DESIGN.md#stage-4b-1-results)).
- The `native_mem final` statistics line is not written when the window is closed with the X button; 4c-1 fixes this ([dev15 validation](#dev15-validation-session-49)).
- The fallback heap-allocated `KPageTable` is not freed because the KProcess slab runs no destructor; resetting it in `Finalize` risks use-after-finalize ([Stage 4b-3 results](N2_DESIGN.md#stage-4b-3-results)).

## Handoff

### Where we are

N2 Stages 0-3 are complete and validated. Stage 4a and 4b are implemented through dev15: dev14 and dev15 are done and dev15's test is declared complete. Stage 4c is designed and awaits the user's explicit go-ahead; Stage 5 follows.

### Completed milestones

- dev11 `eba359e`: added the kernel-free physical page pool and unit/mutant coverage.
- dev12 `ec1fdc2`: added `PhysicalPoolSet`, the default-on native pool backend, and PLA validation.
- dev13 `49370cb`: shared `ComputeLayout` with `KPageTableBase`, plus unit/mutant coverage and JIT validation.
- dev14 `78c7036`: added kernel-free `AddressRegions`/`RegionState`, assertions, and unit/mutant coverage.
- dev15 `0a974bc`: added `KNativeAddressSpace`, lazy fallback `KPageTable`, shared region paths, and native ownership checks.

### What the work looks like now

N2 replaces the kernel memory model in the native 39-bit path while retaining explicit fallback behavior. The 4b-2 and 4b-3 code is committed; dev15 was checked in session 49 (start-up lines on JIT and AOT, no `native_mem` lines with `SUYU_NATIVE_MEM=0`, gate 128) and declared complete by the user. The no-JIT host is the AOT acceptance target; use the JIT host for comparison runs. See the [session 48 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-48-cleanup) and [session 47 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-47-cleanup).

## Immediate next action

1. Commit the documentation edits (the user commits); then check `git status` and `git log -5`. If a commit lands, reconfigure and rebuild both host trees one at a time and check `GIT_REV`.
2. 4c-1a (dev16, shutdown statistics) is implemented and built on both host trees, uncommitted; the user deferred its PLA check. After the user commits it, reconfigure and rebuild both trees one at a time and check `GIT_REV`; the first later export should check for the `native_mem final` line on the X-button exit ([Stage 4c design](N2_DESIGN.md#stage-4c-design-agreed-with-the-user-implementation-not-started)).
3. 4c-1b (dev17, the `KPhysicalMemory` rename) starts with the `pp_compare` `/EP` baseline taken before any source edit and a fresh no-JIT gate baseline: session 49 read `kernel_memory` 128 on the dev15 launcher, against 127 and 122 in earlier notes. Write no 4c-1b code before the user says go.
4. Optional cleanups: full `tests/recompiler_smoke/run.py` with `CPM_SOURCE_CACHE`, a real smoke CMake configure, `aot_cache_outcome` on the next export, unresolved GOT/PLT symbols, title confirmation, the unused `k_address_space_info.h` include, the `native_physical_pool.h` unit-suffix nit, decide fallback `KPageTable` ownership/reset, and re-review the dev14/dev15 fixes.
5. After any commit, reconfigure and rebuild both host trees one at a time and check `GIT_REV`.

### dev15 validation (session 49)

Host trees: both reconfigured and rebuilt at `0a974bc` (`GIT_REV` checked, 0 errors, `suyu.exe` and `suyu-cmd.exe` linked). Logs were copied by the user (`suyu_log_JITon.txt`, `suyu_log_JIToff.txt`, and the export's `user\log\suyu_log_AOTon.txt`).

- JIT native on: `native_mem enabled` and `native_mem: KPageTable not constructed` at 30.36 s; about 270 s of play, no crash or fatal.
- JIT `SUYU_NATIVE_MEM=0`: no `native_mem` lines; about 250 s of play, no crash. The log does not show the environment variable.
- No-JIT AOT (`suyu-static-nojit`): the same two lines at 0.52 s; about 352 s of play, no crash or fatal.
- Not observed: `native_mem final unsupported_ops=... inconsistency=...` in any of the three logs, because each session ended with the window X button (`ForceStop`, then `BufferQueue has been abandoned`). My reading of the code is that `KProcess::Finalize` does not run on that exit; this was not traced through `kernel.Shutdown`. The `unsupported_ops=0` and `inconsistency=0` values for dev15 are therefore unverified, as is optimized-memory use by PLA.
- Gate (report mode, no-JIT `suyu-cmd-static.exe` with PDB, 1,002,169 symbols): `jit` 0; `kernel_memory` 128 (earlier notes: 127 and 122; the cause of the difference is not investigated); scheduler 170, service_framework 4400, filesystem 1839, hid 2722, audio 1241, gpu_maxwell 6015, emulator_system 88. Report saved as `D:\HisuiWakeBuild\diag\gate_n2_dev15_nojit.txt`.
- The user declared the dev15 test complete without the clean-quit statistics line and without the gate against 127.

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
- Preserve file bytes and line endings in scripted edits; verify with `git ls-files --eol` and byte counts.
- Run vcvars through PowerShell using `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat`; use absolute `cmd.exe /c` helper paths and do not build both trees concurrently.
- Copy measured logs before the next Qt start/export, and verify an executable was built before judging a test result.
- A mutant must fail fast through an intended check; run sweeps from the CMake mutant list and clean scratch output afterward.
- A replayed old Orca delivery can answer a new `check --wait`; inspect its subject, acknowledge it, and wait again. Orca "You have 1 orchestration message" notices are heartbeats.

## How to proceed
Read the start checklist, verify evidence, use one worker at a time, and record only verified facts. Per stage: plan, implement with a unit test and header mutants, Code reviewer, host build, md Implementer batch, Japanese report with file list and a commit message ending in `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`, then wait for the user's commit.
A stage that changes runtime behaviour also needs the user's PLA session before the next stage starts.

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
