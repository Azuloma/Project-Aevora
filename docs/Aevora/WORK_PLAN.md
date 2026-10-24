# Aevora Work Plan

Handoff prepared: 2026-10-24. See [session 46 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-46-cleanup), [session 45 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-45-cleanup), [session 44 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-44-cleanup), [session 43 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-43-cleanup), and [older history](OLD_HISTORY.md).

## Objective
Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md).

## Status
HEAD is `9ca567a` (a merge of the remote CI workflow commit `99d2243` onto dev13 `49370cb v0.0.3-dev13`). At this handoff the working tree holds only uncommitted Markdown records (`WORK_PLAN.md`, `OLD_HISTORY.md`); still run `git status`. Both host trees are built at `9ca567a`.
Commits: dev1 `8796a8c`, dev2 `65854f3`, dev3 `ce4bfd6`, dev4 `56db155`, slots rename `8133d92`, dev5 `ca825f8`, dev6 `65a1f15`, dev7 `d051efb`, dev8 `ddfb960`, dev9 `aebd786`, dev10 `4362604`, dev11 `eba359e`, dev12 `ec1fdc2`, dev13 `49370cb`, docs-only records `5057b57` and `a0174a2`.

### N2 progress
| Stage | State |
|---|---|
| 0 | Done: inventory, design note, and test4 collection. |
| 1 | Done: standalone mapper, dev3, and mutants. |
| 2 | Done: shadow, dev4-dev6; accepted after a JIT PLA session on the dev5 build with 0 mismatch lines and `sync_failures=0`; details in [N2_DESIGN.md Stage 2 results](N2_DESIGN.md#stage-2-results). |
| 3 | Stage 3a, 3b-1, 3b-2, and 3b-3 through dev10 and Stage 3c (JIT and AOT) validated. |
| 4 | In progress: 4a-1 (dev11) and 4a-2 (dev12) committed and PLA-validated (Session 46); 4b-1 (dev13 `49370cb`) committed, reviewed and JIT-validated (Session 47); 4b-2 (dev14) is done in the working tree; 4b-3 (dev15) is implemented in the working tree awaiting PLA validation; 4c pending. |
| 5 | Not started; needs the user's go-ahead. |

See [N2_DESIGN.md stage plan and verification](N2_DESIGN.md#stage-plan-and-verification).

### N2 limits to remember
- Stage 2 shadow limits still matter: the extra Locked/Uncached writer is unidentified, and relaxed physical-op comparisons are state-only.
- `QueryMemory` is not hooked and `Common::PageTable` entries are not compared.
- IPC map-alias (`SetupForIpc`), `MapCodeMemory`, and unsafe/insecure operations remain unsupported; other documented kernel divergences remain in [N2_DESIGN.md](N2_DESIGN.md).
- Full `tests/recompiler_smoke/run.py` and a real smoke CMake configure have never run.
- Stage 3c JIT and AOT validation passed (native on and `SUYU_NATIVE_MEM=0`, `unsupported_ops=0 inconsistency=0`, AOT fully static); no performance parity is claimed. Details: [session 44 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-44-cleanup).
- The dev12 native physical pool (`SUYU_NATIVE_MEM_POOL`, default on) passed one JIT and one no-JIT AOT PLA session (Session 46) with `unsupported_ops=0`; a large `MapPhysicalMemory` was not confirmed (no counter) and `KMemoryManager` itself has no standalone test.
- The `PhysicalPoolSet` concurrent-allocation stress test does not detect removal of its allocation mutex (30 of 30 runs passed without it); the lock is covered by review only.
- The native model is default-on; `SUYU_NATIVE_MEM=0` is the comparison/fallback run, and non-39-bit spaces fall back per process.
- Unsupported operations and the kept divergences are listed in [N2_DESIGN.md Stage 3b-3 results](N2_DESIGN.md#stage-3b-3-results).
- If a later session reports an unsupported operation, implement it natively and review it as a new dev stage.

## Handoff
### Completed milestones
- N0 boundary inventory is done and accepted; see [N0_INVENTORY.md](N0_INVENTORY.md).
- N1 CPU-without-JIT is done and accepted with its recorded performance caveat.
- N2 Stages 0-3 (including 3c JIT and AOT) are complete. Stage 4a-1 (dev11) and 4a-2 (dev12) are committed and dev12 is PLA-validated on JIT and no-JIT AOT. Stage 4b-1 (dev13) is committed and JIT-validated.

### What the work looks like now (read this first)
- N2 replaces the kernel's `KPageTable`, `KMemoryManager` and `KMemoryBlockManager` with the kernel-free native memory model under `src/core/native_memory/`, with adapters in `src/core/hle/kernel/k_native_memory_adapters.h`. The native model, the native physical pool (`SUYU_NATIVE_MEM_POOL`) and its fallbacks (`SUYU_NATIVE_MEM=0`, `SUYU_NATIVE_MEM_POOL=0`) are default-on. The goal of Stage 4 is the gate `structural_gate.py --require-absent N2` on a no-JIT launcher.
- Stage 4 is split: 4a (physical pool, done), 4b (replace `KPageTableBase` inside `KProcessPageTable`: 4b-1 dev13 done, 4b-2 dev14 and 4b-3 dev15 open), 4c (remove `KPageTable`, `KMemoryManager`, `KMemoryBlockManager` from the `SUYU_NO_JIT` build, the `KernelCore` memory-manager bypass, counted fail-fast stubs, rename of the `kernel.MemoryManager()` callers). Details and results per stage are in [N2_DESIGN.md](N2_DESIGN.md) (Stage 4 plan, 4a-1/4a-2 results, Stage 4b design).
- Validate on the no-JIT host (`D:\HisuiWakeBuild\msvc-release-nojit`) for AOT: it is the acceptance target (`suyu-static-nojit`, strict static only). The JIT tree gives the `SUYU_NATIVE_MEM=0` and pool-off comparison runs.

### Sessions 46 and 47 summary
- Session 46: dev12 passed the user's PLA validation (JIT pool on and `SUYU_NATIVE_MEM_POOL=0`, no-JIT AOT `suyu-static-nojit`, all with `native_mem final unsupported_ops=0 inconsistency=0`). Gate baseline on the no-JIT `suyu-cmd-static.exe` at `ec1fdc2`: `kernel_memory` 127, `jit` 0 (`D:\HisuiWakeBuild\diag\gate_dev12_nojit.txt`); use 127 as the 4c starting value. 4b was designed with the user and split into 4b-1, 4b-2, 4b-3 ([N2_DESIGN.md Stage 4b design](N2_DESIGN.md#stage-4b-design-agreed-with-the-user-implementation-not-started)); the `KernelCore` bypass moved to 4c.
- Session 47: 4b-1 (dev13 `49370cb`) done. `KPageTableBase::InitializeForProcess` (`k_page_table_base.cpp`) now calls `NativeMemory::ComputeLayout` with `KSystemControl::GenerateRandomRange` (draw order alias, heap, stack, kmap) and assigns the `m_*` regions from the result; the early `m_address_space_start` read was always 0 (member `{}`), so it was dropped. Errors keep their behaviour (`ResultOutOfMemory` returned, others assert). New unit checks `LayoutDrawsRandomsInKernelOrder` (including the generator bounds) and `LayoutHonorsAslrBaseAndExtraAlias`, mutants `layout-aslr-base`, `layout-extra-alias`, `layout-kmap-draw`, `layout-slot-count`.
- Session 47 review: the Code reviewer (Orca, gpt-6.1-sol high) found two P2 items, both fixed: `NativeMemory::GetSpaceInfo` lacks the `HAS_NCE` 256 GiB Map39Bit limit (`k_page_table_base.cpp` now `#error`s under `HAS_NCE`; add the limit before any NCE build), and the draw-order test did not check the generator bounds. The fixes were not reviewed a second time.
- Session 47 validation: the unit test and 4 mutants passed or failed as intended under MSVC; both host trees built with 0 errors at `9ca567a` (`GIT_REV` checked). The user's JIT PLA check on that build: native on ran about 231 s and ended with `native_mem final unsupported_ops=0 inconsistency=0`; `SUYU_NATIVE_MEM=0` ran about 260 s with no `native_mem` lines (expected) and the pool still enabled. Neither log had an error or critical line beyond the known play-time file errors and STUBBED warnings. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\dev13_jit_native_on.txt` and `dev13_jit_native_off.txt`. Not covered: the logs do not print the layout (alias/heap/stack/kmap equality is not compared directly), and the exit state and FPS were not reported; no no-JIT AOT run was made (not required for 4b-1).
- Not done: the full `tests/recompiler_smoke/run.py`, and the unused `k_address_space_info.h` include in `k_page_table_base.cpp` was left in place.
- Session 48: 4b-2 (dev14) added the kernel-free `AddressRegions` holder, 24 `RegionState` static assertions, and nine-group unit coverage with 27 mutants. The unit passed under MSVC; all mutants failed at their intended checks, and JIT/no-JIT host trees rebuilt with 0 errors and linked both launchers. Full `run.py`, a real smoke CMake configure, PLA, and the structural gate were not run; the review fixes were not reviewed a second time.
- Session 48 (continued): 4b-3 (dev15) connected `KNativeAddressSpace` and lazy fallback `KPageTable` construction, redirected shared region/forwarder paths, and fixed mixed native/fallback instruction-cache and debug-IO ownership checks. JIT/no-JIT host trees rebuilt with 0 errors and linked both launchers; `ninja -t deps` found no new zero-dependency source entries. PLA, the 127-baseline structural gate, and full `run.py` remain not run; the fixes were not reviewed a second time.
- The verbatim Session 46 and 47 notes are in the [session 47 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-47-cleanup); older summaries are in the [session 46 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-46-cleanup) and the [session 45 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-45-cleanup).

## Immediate next action
1. Commit dev15 or run the PLA checks first: JIT native on must log `native_mem enabled` and `native_mem: KPageTable not constructed` and end with `native_mem final unsupported_ops=0 inconsistency=0`; JIT `SUYU_NATIVE_MEM=0` must work and print no native-memory lines; no-JIT AOT must pass. Then, after a commit, reconfigure and rebuild both host trees one at a time and check the new `suyu-cmd-static.exe` with a structural gate report against the 127 `kernel_memory` baseline.
2. After dev15 validation, start 4c with its own design review; Stage 5 needs the user's go-ahead.
3. Optional cleanups: the full `tests/recompiler_smoke/run.py` with `CPM_SOURCE_CACHE`, a real smoke CMake configure, `aot_cache_outcome` on the next export, unresolved GOT/PLT symbols such as `__tdata_align_rel`, the title confirmation, the unused `k_address_space_info.h` include in `k_page_table_base.cpp`, and the `native_physical_pool.h` unit-suffix nit (`offset`, `size` to `*_bytes`, only when a stage touches that header; update the mutant texts that mention them).
4. After any new commit, reconfigure and rebuild both host trees one at a time (see How to work / procedures) and check `GIT_REV`.

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
- A mutant must exit 1 through a failed check, never crash; guard pointers the mutated check protected, and run a mutant sweep from the definitions in `tests/recompiler_smoke/CMakeLists.txt` so the CMake text itself is what gets tested.
- Before judging a test result, confirm the executable was built; a compile failure looked like a test failure once. A probabilistic stress test is not a regression guard for a lock: measure it with the lock removed and record the result.
- In Git Bash `cmd.exe //c <bat>` with vcvars fails; use the PowerShell tool. `Start-Process -Wait` on a bat can hang after the work is done; start it without `-Wait` and watch the log or the process list.
- A Python edit run from a Bash heredoc turned a backslash-r-backslash-n in a string into real CR LF; write edit scripts with the Write tool.
- Check the existing `src/core/native_memory/` headers before designing a helper: `ComputeLayout` already ported the region math in Stage 1.
- Pick the host tree by the acceptance target: no-JIT (`msvc-release-nojit`, `suyu-static-nojit`) for N2 AOT validation and the structural gate, JIT for the comparison runs. The gate JSON stores counts and examples only, so save the text report when a per-class breakdown matters.
- A baseline number in this file can go stale after a runtime commit; re-take it on the commit it will be compared against.
- From the PowerShell tool run a scratch `.bat` by its absolute path (`cmd.exe /c D:\...\x.bat`); a relative `b.bat` was not found, and Git Bash `cmd.exe /c` with vcvars hung until the tool timed out. Compile standalone tests from a `.bat` written with the Write tool, because a Python heredoc turned `\P`/`\s` in paths into escapes. `rebuild-suyu.bat` finishes in about a minute for one `.cpp`, but the no-JIT tree needed 1674 steps (about 15 minutes) after a merge; start it without `-Wait` and poll the log.
- Orca: a stalled worker (no output for 10 minutes, terminal exited) cannot be retried while the Task is dispatched. Use `worker-stop --dispatch <id>`, then `task-update --id <task> --status failed`, then `worker-start --task <task> --retry-of <dispatch>` (no `--spec`). `check --wait` can return keepalive lines or replay an old delivery: parse the last JSON object with a `result`, check the sender and subject, and ack the delivery id on the next check. A worker's full report may be in the `worker_done` message body rather than the transcript.

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
