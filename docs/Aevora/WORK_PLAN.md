# Aevora Work Plan

Handoff prepared: 2026-10-24. See [session 46 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-46-cleanup), [session 45 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-45-cleanup), [session 44 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-44-cleanup), [session 43 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-43-cleanup), and [older history](OLD_HISTORY.md).

## Objective
Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md).

## Status
HEAD is `ec1fdc2 v0.0.3-dev12` (runtime and tests as committed). At this handoff the working tree holds only uncommitted Markdown records (`WORK_PLAN.md`, `OLD_HISTORY.md`, `N2_DESIGN.md`); still run `git status`. Both host trees are built at `ec1fdc2`.
Commits: dev1 `8796a8c`, dev2 `65854f3`, dev3 `ce4bfd6`, dev4 `56db155`, slots rename `8133d92`, dev5 `ca825f8`, dev6 `65a1f15`, dev7 `d051efb`, dev8 `ddfb960`, dev9 `aebd786`, dev10 `4362604`, dev11 `eba359e`, dev12 `ec1fdc2`, docs-only records `5057b57` and `a0174a2`.

### N2 progress
| Stage | State |
|---|---|
| 0 | Done: inventory, design note, and test4 collection. |
| 1 | Done: standalone mapper, dev3, and mutants. |
| 2 | Done: shadow, dev4-dev6; accepted after a JIT PLA session on the dev5 build with 0 mismatch lines and `sync_failures=0`; details in [N2_DESIGN.md Stage 2 results](N2_DESIGN.md#stage-2-results). |
| 3 | Stage 3a, 3b-1, 3b-2, and 3b-3 through dev10 and Stage 3c (JIT and AOT) validated. |
| 4 | In progress: 4a-1 (dev11 `eba359e`) and 4a-2 (dev12 `ec1fdc2`) committed; 4a-2 passed the user's PLA JIT (pool on and `SUYU_NATIVE_MEM_POOL=0`) and no-JIT AOT validation (Session 46); 4b design agreed and split into 4b-1 (dev13), 4b-2 (dev14), 4b-3 (dev15), none started; 4c pending. |
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
- N2 Stages 0-3 (including 3c JIT and AOT) are complete. Stage 4a-1 (dev11) and 4a-2 (dev12) are committed and dev12 is PLA-validated on JIT and no-JIT AOT.

### What the work looks like now (read this first)
- N2 replaces the kernel's `KPageTable`, `KMemoryManager` and `KMemoryBlockManager` with the kernel-free native memory model under `src/core/native_memory/`, with adapters in `src/core/hle/kernel/k_native_memory_adapters.h`. The native model, the native physical pool (`SUYU_NATIVE_MEM_POOL`) and its fallbacks (`SUYU_NATIVE_MEM=0`, `SUYU_NATIVE_MEM_POOL=0`) are default-on. The goal of Stage 4 is the gate `structural_gate.py --require-absent N2` on a no-JIT launcher.
- Stage 4 is split: 4a (physical pool, done), 4b (replace `KPageTableBase` inside `KProcessPageTable`, three dev stages below), 4c (remove `KPageTable`, `KMemoryManager`, `KMemoryBlockManager` from the `SUYU_NO_JIT` build, the `KernelCore` memory-manager bypass, counted fail-fast stubs, rename of the `kernel.MemoryManager()` callers). Details and results per stage are in [N2_DESIGN.md](N2_DESIGN.md) (Stage 4 plan, 4a-1/4a-2 results, Stage 4b design).
- Validate on the no-JIT host (`D:\HisuiWakeBuild\msvc-release-nojit`) for AOT: it is the acceptance target (`suyu-static-nojit`, strict static only). The JIT tree gives the `SUYU_NATIVE_MEM=0` and pool-off comparison runs.

### Session 46 summary
- dev12 PLA validation passed, user-run on the `ec1fdc2` builds: JIT with the pool on (`native_phys_pool enabled: 5 regions, 4041 MiB`) and with `SUYU_NATIVE_MEM_POOL=0`, and a no-JIT AOT export (`suyu-static-nojit`, fallback off, `static -> JIT 0`, `JIT -> static 0`, about 273 s). All three ended with `native_mem final unsupported_ops=0 inconsistency=0`; only the known warnings and 8 unresolved GOT/PLT symbols appeared. Logs are in `D:\HisuiWakeBuild\diag\cpu_budget\` (`dev12_jit_pool_on.txt`, `dev12_jit_pool_off.txt`, `dev12_aot_nojit.txt`). Not covered: no log counts `MapPhysicalMemory` calls, no pool statistics at exit, and the export's `aot_cache_*` fields and FPS were not reported.
- Gate re-baseline on the no-JIT `suyu-cmd-static.exe` at `ec1fdc2`: `kernel_memory` 127 matches (1,002,184 symbols), `jit` 0; report `D:\HisuiWakeBuild\diag\gate_dev12_nojit.txt`. The earlier 122 was taken before dev12 and the "unchanged" claim was wrong (per-class split is not stored; the JSON keeps counts). Use 127 as the 4c starting value.
- 4b was designed with the user and split into 4b-1, 4b-2 and 4b-3; the design note is [N2_DESIGN.md Stage 4b design](N2_DESIGN.md#stage-4b-design-agreed-with-the-user-implementation-not-started). The `KernelCore` memory-initialization bypass moved from 4b to 4c, and the region math keeps one source (`NativeMemory::ComputeLayout`, already ported and tested in Stage 1, now also called by `KPageTableBase`).
- Older session summaries (44, 45) and the verbatim session 46 record are in the [session 46 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-46-cleanup) and the [session 45 archive](OLD_HISTORY.md#archived-on-2026-10-24-from-work_planmd-session-45-cleanup).
- This session changed only Markdown (`WORK_PLAN.md`, `OLD_HISTORY.md`, `N2_DESIGN.md`) and, locally, `CLAUDE.md`; there is no source change and no CHANGELOG entry. The edits were made by the main agent directly, not by the md Implementer worker; the user can ask for a diff check.

### Session 47: 4b-1 (dev13) implemented, uncommitted
- `KPageTableBase::InitializeForProcess` (`k_page_table_base.cpp`) now calls `NativeMemory::ComputeLayout` and assigns the `m_*` regions from the result. Item (a) settled: `m_address_space_start` is `{}` (0) in a fresh process table and `InitializeForKernel` is never used on it, so the early read was always 0. New unit checks `LayoutDrawsRandomsInKernelOrder` and `LayoutHonorsAslrBaseAndExtraAlias`; mutants `layout-aslr-base`, `layout-extra-alias`, `layout-kmap-draw` (CMake list and `run.py` list updated).
- Verified: standalone `native_memory_unit.cpp` passed and the 3 mutants failed under MSVC; JIT and no-JIT host trees built with 0 errors (the commit has not happened, so `GIT_REV` is unchanged). Code reviewer (Orca) reported two P2 items, both fixed: the shared `GetSpaceInfo` lacks the `HAS_NCE` 256 GiB Map39Bit limit (now `#error` under `HAS_NCE` in `k_page_table_base.cpp`; add the limit before any NCE build), and the draw-order test did not check the bounds passed to the generator (now checked; mutant `layout-slot-count`). Re-verified after the fixes: unit test and 4 mutants, both host trees 0 errors. Not done: second review of the fixes, full `run.py`, user's JIT PLA check (native on and `SUYU_NATIVE_MEM=0`; look for the same alias/heap/stack/kmap layout lines and `unsupported_ops=0`). CHANGELOG has the `v0.0.3-dev13` section. The unused `k_address_space_info.h` include in `k_page_table_base.cpp` was left in place.

## Immediate next action
1. Run `git status` and `git log -5`, and preserve user changes. The working tree should hold only the three Markdown files above; the user commits them as a docs-only record (no `devN`), suggested message: `Record dev12 PLA validation, gate re-baseline and the N2 Stage 4b design`. A docs-only commit changes `GIT_REV`; reconfigure and rebuild both host trees (one at a time) before any export or measurement, not necessarily before coding 4b-1.
2. Read `CLAUDE.md`, this file, `NATIVE_ROADMAP.md`, `CODEBASE_INDEX.md`, and `N2_DESIGN.md` (Stage 4 plan and Stage 4b design).
3. Do not write code until the user says go. Then run 4b-1 (dev13) as one unit: (a) settle whether `KPageTableBase::InitializeForProcess` reads `m_address_space_start` before it is assigned (`k_page_table_base.cpp:245` versus `:272`); (b) make it call `NativeMemory::ComputeLayout` with `KSystemControl::GenerateRandomRange`, keeping the draw order alias, heap, stack, kmap; (c) extend the layout unit tests and mutants; (d) Code reviewer, both host builds one at a time with `GIT_REV` checked; (e) report with the file list, a `v0.0.3-dev13` CHANGELOG section and a suggested message; (f) wait for the user's commit and the user's JIT PLA check (native on and `SUYU_NATIVE_MEM=0`).
4. Then 4b-2 (dev14, standalone predicates header, no PLA) and 4b-3 (dev15, connect `KNativeAddressSpace`, lazy `KPageTable`, log `native_mem: KPageTable not constructed`; JIT and no-JIT AOT PLA and a gate report against 127). Classify the forwarders without a native branch (`GetPhysicalAddress`, `Heap*`) before coding 4b-3. 4c follows with its own design review; Stage 5 needs the user's go-ahead.
5. Optional cleanups: the full `tests/recompiler_smoke/run.py` with `CPM_SOURCE_CACHE`, a real smoke CMake configure, `aot_cache_outcome` on the next export, unresolved GOT/PLT symbols such as `__tdata_align_rel`, the title confirmation, and the `native_physical_pool.h` unit-suffix nit (`offset`, `size` to `*_bytes`, only when a stage touches that header; update the mutant texts that mention them).
6. After any new commit, reconfigure and rebuild both host trees one at a time (see How to work / procedures) and check `GIT_REV`.

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
