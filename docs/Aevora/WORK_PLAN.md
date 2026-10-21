# Aevora Work Plan

Handoff prepared: 2026-10-21 (session 40). See [session 39 archive](OLD_HISTORY.md#archived-on-2026-10-21-from-work_planmd-session-39-cleanup), [session 38 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-38-cleanup), [session 37 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-37-cleanup), [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The current goal is lower host CPU usage per core so low-spec PCs run better at a 30 FPS cap; generation, compilation, or startup alone does not establish compatibility.

The long-term end goal and its milestones (N0-N10 runtime, G1-G4 tools and GUI) are defined in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md): a title-specific standalone native PLA application with no CPU JIT, Maxwell/emulated GPU processing, or general emulator runtime, plus the Aevora Launcher and Aevora Recompiler replacing all Suyu GUIs. The supported gameplay path is confirmed and both GUIs use Tauri (2026-10-20).

## Status

HEAD is `c338961` on `main` with the user's N0 stages 5b, 5c, and 6 commits included; stage 7 documentation is uncommitted. Host binaries were rebuilt after 5c (`suyu.exe` and `suyu-cmd.exe`, 8/8, 0 errors), but `GIT_REV` in `msvc-release\src\common\scm_rev.cpp` still reads an earlier hash until the next reconfigure. The PLA export/AOT cache entry still dates from `7609ab3`.

## N0 progress (session 40)

Every N0 stage was a normal commit declared by the user; this WORK_PLAN carries the record, so `CHANGELOG.md` is unchanged. N0 proceeds stage by stage; after each stage the main agent reports, gives the file list and a normal-commit message, and waits for the user's approval before the next stage (user instruction, 2026-10-20).

| Stage | What | State |
| --- | --- | --- |
| 0 | read-only hook survey | done (`b1f6610`) |
| 1 | design note | done (`437df06`) |
| 2 | header-only recorder and unit test | done (`511c147`) |
| 3a | SVC and IPC hooks | done (`1c5167e`) |
| 3b | nvdrv, audio, applet, filesystem hooks | done (`917cf94`) |
| 4 | collection on the JIT host; manual segments (4a-1 `0038de1`, 4a-2 `5cbafc6`); user collections `boot1` and `test1` (4b) | done |
| 5a | module table and SVC caller chains in `[callers]` | done (`810c1bf`) |
| 5b | IPC caller attribution through a per-thread stash written at SVC 0x21/0x22 | done (`b619bf9`) |
| 5c | module and NVN scan at load (local lists only) | done (`3c945e4`) |
| 5d | collection and local analysis of callers | done (test2 collection) |
| 6 | structural-gate script with a shared deny list, synthetic test, first report on the current launcher | done (`c338961`) |
| 7 | Markdown batch: supported-path check list, inventory summary (aggregates only), WORK_PLAN, CODEBASE_INDEX | done (uncommitted) |

### What the code does now

- Option `SUYU_BOUNDARY_INVENTORY=1` (exactly `1`; read once at process start); off, every hook is one flag check. Recorder: `src/core/boundary_inventory.h` (header-only, OS-free); file output: `src/core/boundary_inventory_report.h`.
- Hooks: `PhysicalCore::RunThread` (SVC number per core and caller chain, module table filled at the first SVC), `ServiceFrameworkBase::InvokeRequest`/`InvokeRequestTipc` (service, command type, command ID, unimplemented flag in one record), nvdrv `Module::Open/Close/Ioctl1-3`, audio `OpenAudioRenderer`/`RequestUpdateAuto`, `ILibraryAppletCreator`, `fsp_srv.cpp` opens, `IFileSystem::OpenFile`, `IFile::Read`, `IStorage::Read`; `System` shutdown writes the open segment after the cores stop. At SVC 0x21/0x22, `RecordSvcCaller` stashes four caller frames per guest thread id and `RecordBoundaryIpc` consumes the stash with `ctx.GetThread().GetThreadId()`; the stash is bounded to 1024 threads and clears when full. `ScanInventoryModules` runs on the first SVC after the module list is set, scanning loaded modules in 256 KiB chunks and using bounded dynsym parsing; modules larger than 512 MiB are cut.
- Segments: recording can be paused; `EndSegment` returns the segment and resets it; the nvdrv fd-to-device map survives pauses and marks. suyu-cmd controls: F9 mark, F10 pause/resume, Inventory window (command bar item beside Debug), state line at the bottom right. It starts paused when the controls exist (`SUYU_BOUNDARY_INVENTORY_START=1` starts recording); without them (Qt `suyu.exe`) it records from the start and auto-saves the open segment every 10 s.
- Output: folder `SUYU_BOUNDARY_INVENTORY_DIR` (default `<log dir>/boundary_inventory`), run label `SUYU_BOUNDARY_INVENTORY_RUN`; files `inventory_<run>_<NN>_<label>.txt` (segments), `inventory_<run>_open.txt`, and the local-only `inventory_<run>_scan.txt`. Format v1 is unchanged: `# boundary inventory v1`, `# segment label=<label> elapsed_ms=<n>`, sections `[svc] [ipc] [nvdrv] [audio] [applet] [fs-open] [fs-read] [callers]`. `SUYU_BOUNDARY_INVENTORY_SCAN=0` disables the scan.
- Tests: `smoke_boundary_inventory_unit`, `smoke_inventory_session_unit`, `smoke_fullscreen_toggle_unit` (F9/F10 routes); each new check has header mutants that must fail (see procedures).

### Results so far

- Union of boot1, test1, and test2 (18 reports): 32 distinct SVCs, 44 service interfaces with 121 commands, 34 nvdrv rows across six devices, no audio renderer open, no applet, 7 fs-open rows, and fs-read on two directories (RomFS of the current process and save data only).
- Test2: 202,179 IPC calls and 29 caller-recorded IPC keys with matching `[callers]` and `[ipc]` totals; SVC 0x21 was called 202,276 times. All four-frame IPC chains ended in the SDK module; SVC callers reached the main module for part of the calls. Test2 covered 24 distinct SVCs and 61 distinct keys; it had no boot segment.
- Test2 scan: 563 NVN strings and 15,965 matching dynsym names; the main module contained 524 NVN strings and a table of 524 NVN API function names. Scan time was not measured; collection completed. The bag step has no segment of its own in test1 or test2.
- Stage 6: 34 structural gate tests with an independent oracle pass, and all 56 tool mutants fail the suite. The first baseline report matched 1,007,632 public symbols and 37 imported DLLs; import-pattern matches were zero. `--require-absent N1` failed with `FAIL jit`, as expected for the JIT-capable launcher.

### Local data and tools (not in the repository)

`D:\HisuiWakeBuild\diag\n0\` holds `run_n0.bat <game> <label>`, `summarize_inventory.py`, `segments_report.py`, `union_summary.txt`, and local collections `boot1`, `test1`, and `test2` (do not delete). The test2 collection includes eight labeled segments, the open remainder, and the scan file. The structural gate and its tests are in the repository; its first report is local in the gate output folder. `D:\HisuiWakeBuild\scratch\n0_stage0\` holds the read-only survey reports and worker specs of session 39.

### Design decisions to keep

Raw guest addresses and game-derived name lists stay local, the repository gets aggregates only; the caller table keeps the eight most frequent callers per key and counts evicted ones in `other=`. The IPC caller is a per-thread stash written at SVC 0x21/0x22 and consumed once by the service thread, bounded to 1024 threads. The module scan writes only a local file and the repository receives aggregates only; the gate keeps one deny table for all milestones and refuses unverified evidence unless the user opts in.

### Not verified yet

Inventory under the AOT launcher; scan stall time; the asynchronous SVC 0x23 stash limit; deeper IPC attribution beyond the SDK; Linux for the gate; the typed-label input of the Inventory window.

## Immediate next action

1. Read `CLAUDE.md`, this plan, `NATIVE_ROADMAP.md`, `CODEBASE_INDEX.md`; run `git status`; confirm no `suyu`, `ninja`, or `cl` process holds the build tree; compare `GIT_REV` of the host with HEAD and rebuild with `D:\HisuiWakeBuild\rebuild-suyu.bat` through `Start-Process` if sources changed.
2. N0 is complete on the user's acceptance: the inventory files and gate report are recorded, and the supported-path check list is written in `N0_INVENTORY.md`.
3. After acceptance, candidates in roadmap order are N1 (CPU without JIT; the gate's N1 check is the done criterion) or N8a (NVN interception can start after N0), or parked CPU item B2 and the listed cheap cleanups. The choice is the user's.
4. Parked CPU work (B2): compare the AOT guest-core gap with JIT at the same position using `SUYU_GUEST_TIME` and RIP sampling, with no code change first; NATIVE_ROADMAP notes that tuning of the emulated scheduler and HostTiming does not carry over to the native runtime (N3). Optional: investigate AudioManager and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), or other periodic events can be coalesced.
5. Cheap: tracked `__pycache__`/`.bak` cleanup, the `configure_audio` combo-index fix, a `controller_assign.h` test, an optional `LoadLibraryExW` error-mode change with test, the obsolete `*.plist` glob in `tools/translations/qt-source.sh`, and the now-dead `NOT MULTIARCH_BUILD` conditions in `src/dynarmic/CMakeModules/TargetArchitectureSpecificSources.cmake` and `src/dynarmic/tests/CMakeLists.txt`.

## Open items

- Pre-existing Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` has 7 failures/1 error pre-existing; tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD checks, OpenGL/Null windows, FPS/overhead, and Linux/FreeBSD configure and build (after the macOS removal) remain untested; `tests/exclusive_access/run.py` passed in session 38.
- Plain `suyu-cmd.exe <game>` does not auto-assign controllers (only exported packages do, `emu_window_sdl2.cpp:842`): assign Player 1 in the overlay Controller window. `suyu-cmd -g <path>` reads the path through the ANSI code page and garbles non-ASCII file names; the positional argument is read as UTF-16.

## Measurement setup

Target: i5-12400F (6C/12T, P-cores), RTX 2060 SUPER, 16 GB; 30 FPS cap. Config: `frame_pacing_mode=0`, `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, `use_multi_core=true`. Metrics: process and busiest-thread CPU ms/game-frame (`core_pct`), with p99 frame interval; compare same-position JIT/AOT. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\` (baseline and `97cad8f` X-Y-X files); summarizer is `summarize_cpu_budget.py`. The X-Y-X table and its explanation are archived (session 39 archive).

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Held key/axis can remain stuck across capture; `controller_assign.h` is untested; Resolution Scale has a translated duplicate; maximize fallback needs one toggle; OpenGL/Null overlay is unavailable; PDH GPU accuracy is unverified.

## Contracts to preserve

Keep diagnostics execution-neutral when off, exact-0 default-on semantics, ABI/result/cache contracts, overlay action-queue/event-thread rules, and same-position CPU ms/frame. `SUYU_HOST_TIMING_BLOCK` is default-on; `SUYU_HOST_TIMING_STATS` is opt-in; `SUYU_HID_NPAD_UPDATE_MS` defaults to 4 ms and accepts 1-8. Preserve saved-value CAS, atomic monitor slots, 32-bit indexes, and two-line comments. N0 inventory: execution-neutral when off, report format v1 section names, one authoritative module list, no committed guest addresses.

## Lessons

- New core files include `common/logging.h`; `run.py` needs `CPM_SOURCE_CACHE`.
- Copy measured logs before the next Qt start/export; use 10-second CPU-budget windows and do not build during FPS measurement.
- Mixed-ending files require byte-preserving edits. Use file edit tools and verify real LF counts; do not emit escaped newline text.
- A CMake source addition can trigger a long full rebuild; a running `suyu.exe` blocks relinking.
- Verify worker deliveries and Orca heartbeat-only returns before acting on a report.
- `orca orchestration worker-release` rejects `--run`; pass only `--dispatch` (and `--json`).
- Bash heredocs, `printf`, and `sed` can turn backslash sequences into real newlines or drop backslashes; write Python edit scripts and `.bat` files with the Write tool and check `git diff --numstat` after every scripted edit (a `cut` helper once removed far more than intended because its end marker matched later text).
- `cl` prints in the console code page: run `tests/package_policy` without `-X utf8` (under vcvars64) or `test_policy_helpers` fails to decode the compiler output.
- `git rm` stages deletions; use `git reset -q` afterwards so every change stays unstaged for the user.
- `cleanup_scratch.ps1` removes the whole scratch folder even when a path is passed.
- Before a large deletion or refactor that must not change Windows behavior, take the `pp_compare` preprocess baseline before editing any source.
- Working copies of files that git has rewritten are CRLF (autocrlf) while their index blobs are LF, and `grep -c $'\r'` in Git Bash can report 0 for them: count bytes with Python and make scripted edits detect and preserve the endings (`git ls-files --eol`).

## How to proceed

Follow the start checklist, verify evidence before changing code, use one worker at a time, and record only verified facts. Main agent implements; Code reviewer reviews; Code Explorer is read-only; md Implementer edits Markdown. Do not claim unrun exports or hand tests. Stage workflow of N0: plan the stage, implement with a unit test and header mutants, Code reviewer review, host build, report in Japanese, file list and a normal-commit message ending with the Co-Authored-By line, then wait for the user's go-ahead.

## How to work / procedures

- Build standalone tests after vcvars64 with `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- For CPU budget, set `SUYU_CPU_BUDGET=1` (optional `SUYU_CPU_BUDGET_WINDOW_SEC` 1-60, default 10), copy logs, and summarize with `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>` (about 300 at 30 FPS, 240 for AOT). Drop windows from other scenes (loading) by filtering before summarizing; keep diagnostics off for FPS-only runs.
- Opt-in stats: `SUYU_HOST_TIMING_STATS=1` logs `host-timing` wait and event lines every 10 s. Analysis helpers: `D:\HisuiWakeBuild\tmp_gt`, `D:\HisuiWakeBuild\diag\hotrva`.
- Replay overlay input with the real router and ImGui sources; follow the standing export procedure in CLAUDE.md and clean scratch output.
- Orca: one worker at a time, acknowledge deliveries, release workers, and check again after heartbeat-only messages.
- Source-change equivalence check: `D:\HisuiWakeBuild\diag\pp_compare` holds `pp.py` (preprocess every translation unit of `compile_commands.json` with `/EP` into gzip files; run it from PowerShell with the repository untouched while it runs), `cmp.py` (accepts only `__LINE__` shifts derived from `git diff` and groups every other hunk by signature; `cmp.py --selftest` runs the negative controls), `after_only.py`/`before_only.py` (list added/removed lines that are not line shifts), `gen_variants.py` (build `smoke_export` from a source tree and write every smoke variant), `ed.py` (exact-match, line-ending-preserving edit helper) and `vcenv.bat`. Run them with `CPM_SOURCE_CACHE=D:\HisuiWakeBuild\cpm` where cmake is involved.
- Header-mutant check: copy the header under test with one deliberate change into a scratch folder, put that folder first on /I when building the unit test, and require a failing exit; write the scripts with the Write tool, build under cmd.exe /c with vcvars64, and delete the scratch folder afterwards.
- Collect with `D:\HisuiWakeBuild\diag\n0\run_n0.bat <game> <label>`; assign Player 1 in the overlay Controller window first; F10 starts recording, F9 ends a segment; quit normally.
- Run the structural gate with `python -B -I -X utf8 tools/structural_gate/structural_gate.py <image.exe> --pdb <image.pdb> [--require-absent N1] [--json-out F] [--text-out F]`.
- Read a local scan report as `inventory_<run>_scan.txt` in the inventory output folder; never commit it.
- Host rebuild: Start-Process cmd.exe /c D:\HisuiWakeBuild\rebuild-suyu.bat, then confirm cmake/ninja/cl are running and read D:\HisuiWakeBuild\rebuild-suyu.log; only suyu and suyu-cmd are rebuilt.
