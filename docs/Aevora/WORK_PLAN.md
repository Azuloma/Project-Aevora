# Aevora Work Plan

Handoff prepared: 2026-10-22. See [session 43 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-43-cleanup), [session 42 archive](OLD_HISTORY.md#archived-on-2026-10-22-from-work_planmd-session-42-cleanup), and [older history](OLD_HISTORY.md).

## Objective
Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The N0-N10/G1-G4 end goal is in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md).

## Status
HEAD is `4362604 v0.0.3-dev10` (N2 Stage 3b-3, committed); the working tree holds only uncommitted Markdown updates (`WORK_PLAN.md`, `OLD_HISTORY.md`).
Commits: dev1 `8796a8c`, dev2 `65854f3`, dev3 `ce4bfd6`, dev4 `56db155`, slots rename `8133d92`, dev5 `ca825f8`, dev6 `65a1f15`, dev7 `d051efb`, dev8 `ddfb960`, dev9 `aebd786`, dev10 `4362604`.

### N2 progress
| Stage | State |
|---|---|
| 0 | Done: inventory, design note, and test4 collection. |
| 1 | Done: standalone mapper, dev3, and mutants. |
| 2 | Done: shadow, dev4-dev6; accepted after a JIT PLA session on the dev5 build: 0 mismatch lines, `sync_failures=0`, MapPhysicalMemory 2317 compared, SetMemoryAttribute 239, MapMemory 26, UnmapMemory 3, SetHeapSize 1, UnmapPhysicalMemory 1, SetMemoryPermission 0 not exercised; accepted with the recorded limits. |
| 3 | Stage 3a, 3b-1, 3b-2, and 3b-3 implemented through dev10; Stage 3c JIT validation passed and AOT validation remains. |
| 4-5 | Not started. |

See [N2_DESIGN.md stage plan and verification](N2_DESIGN.md#stage-plan-and-verification).

### N2 limits to remember
- Stage 2 shadow limits still matter: the extra Locked/Uncached writer is unidentified, and relaxed physical-op comparisons are state-only.
- `QueryMemory` is not hooked and `Common::PageTable` entries are not compared.
- IPC map-alias (`SetupForIpc`), `MapCodeMemory`, and unsafe/insecure operations remain unsupported; other documented kernel divergences remain in [N2_DESIGN.md](N2_DESIGN.md).
- Full `tests/recompiler_smoke/run.py` and a real smoke CMake configure have never run.
- Stage 3c JIT validation passed: native memory default-on and `SUYU_NATIVE_MEM=0` both ran without crashes, with `unsupported_ops=0 inconsistency=0` for the native run; steady 301-frame windows averaged 75.3 and 77.2 ms/frame, with frame p99 about 34 ms in both and the 30 FPS cap held. Separate launches did not guarantee identical position/operation, the native-on bag time was not recorded, so no bag-segment A/B or performance-parity claim is made; AOT 3c has not run.
- The native model is default-on; `SUYU_NATIVE_MEM=0` is the comparison/fallback run, and non-39-bit spaces fall back per process.
- Unsupported operations and the kept divergences are listed in [N2_DESIGN.md Stage 3b-3 results](N2_DESIGN.md#stage-3b-3-results).
- If Stage 3c reports an unsupported operation, implement it natively and review it as a new dev stage before proceeding.

## Handoff
### Completed milestones
- N0 boundary inventory is done and accepted; see [N0_INVENTORY.md](N0_INVENTORY.md).
- N1 CPU-without-JIT is done and accepted with its recorded performance caveat.
- N2 Stages 0-3b-3 are complete as summarized above; Stage 3c is the next user validation.

### Session 43 summary
- Dev8 (Stage 3b-1) added kernel-free anonymous mapping, free-area search, and IPC copy rules to the native headers; nothing was connected.
- Dev9 (Stage 3b-2) added the unconnected kernel adapters (page source, resource-limit accounting, host map, KNativeProcessMemory) and the replaced-kernel side-effect table.
- Dev10 (Stage 3b-3) made `KProcessPageTable` use the native guest-memory model by default, with per-process fallback for non-39-bit spaces.
- Unsupported native operations now return `ResultNotImplemented`, count and log their operation/address, and report final counts at process finalize.
- Native mode wins over the shadow option with a warning; initialization logs `native_mem enabled`.
- Reviewer P1: grep every caller of a replaced API, including service code such as nvmap; reviewer P2: guard every method that reads block state.

### Session 44 summary
- Stage 3c JIT validation passed with native memory default-on and with `SUYU_NATIVE_MEM=0`; the native run logged `native_mem enabled` and final `unsupported_ops=0 inconsistency=0`, with no unsupported lines or crash. Logs are `D:\HisuiWakeBuild\diag\cpu_budget\jit_4362604_native_on_log.txt` and `jit_4362604_native_off_log.txt`.
- In steady 301-frame windows from t=61..111 s, process CPU averaged 75.3 versus 77.2 ms/frame and frame p99 was about 34 ms in both; separate launches, unrecorded native-on bag time, and non-identical position/operation prevent a bag-segment A/B or performance-parity claim. Initial load windows through t=51 s stalled in both runs; AOT 3c has not run.
- The first two launches froze in `Shadow::CompareBlocks` under `KProcessPageTable::RunShadowed` from `Svc::MapMemory` because the JIT `svc_memory.cpp.obj` was stale and ninja held an empty header dependency entry; deleting the object and rebuilding fixed the run, with no source change.
- After `vcvars64`, `ninja -t deps` shows no unexpected `#deps 0,` core objects in `src/core`, `src/common`, `src/video_core`, or `src/hid_core`; only the documented video-core `nsight_aftermath_tracker.cpp.obj` and non-core generated/data objects are legitimate. The no-JIT tree has no such core object; its `svc_memory.cpp.obj` has 247 dependencies, is older than the header, and must be timestamp-checked after the next build.
- Diagnostics used DbgHelp `StackWalk64` with a PDB GUID/Age check; scratch files were removed with `cleanup_scratch.ps1`.

## Immediate next action
1. The user commits the Markdown updates as a normal commit, with no devN entry.
2. Reconfigure both host trees with the standing procedure and check `GIT_REV`, then confirm the no-JIT `svc_memory.cpp.obj` timestamp updated after its next build.
3. Run the AOT Stage 3c session: commit, full export, bag and quit, check `native_mem enabled` and `unsupported_ops=0`, and compare with `SUYU_NATIVE_MEM=0`; if anything freezes in `RunShadowed`, perform the same stale-object check.
4. If `unsupported_ops>0`, follow the existing rule and implement it natively as a new dev stage.
5. Keep the optional cleanups and the dev17 title item; do not start Stages 4-5.

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

## Working rules
- Per-commit `devN` CHANGELOG entries use `v0.0.3-devN` from dev1 unless the user declares a normal commit; the user makes all commits.
- The one-stage-at-a-time report/file-list/commit-message/wait rule applies until the user relaxes it.

## Context and token practice
Start a new session or `/compact` at stage boundaries/before breaks; read with offset/limit, print only extracted worker fields, and use one blocking `check --wait`.
