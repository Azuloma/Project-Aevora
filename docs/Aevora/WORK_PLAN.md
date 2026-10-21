# Aevora Work Plan

Handoff prepared: 2026-10-20 (session 38 cleanup). See [session 38 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-38-cleanup), [session 37 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-37-cleanup), [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The current goal is lower host CPU usage per core so low-spec PCs run better at a 30 FPS cap; generation, compilation, or startup alone does not establish compatibility.

The long-term end goal and its milestones (N0-N10 runtime, G1-G4 tools and GUI) are defined in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md): a title-specific standalone native PLA application with no CPU JIT, Maxwell/emulated GPU processing, or general emulator runtime, plus the Aevora Launcher and Aevora Recompiler replacing all Suyu GUIs. The supported gameplay path is confirmed and both GUIs use Tauri (2026-10-20).

## Status

HEAD is `2db4218` (`docs: compact WORK_PLAN for the session 39 handoff`) on `main`; the host was reconfigured and rebuilt in session 39 and `GIT_REV` in `msvc-release\src\common\scm_rev.cpp` equals `2db4218ebf260a8b050e29354790f3dae5359ccb`. The PLA export and the persistent AOT cache entry date from `7609ab3`; the new producer revision requires a full export before the next measurement. Optimization state: B1 wait splitting alone was ineffective (waits are under 1 ms; about 2600 timer events/s, HID pad update at 1 kHz the largest source); the 4 ms Npad default (`97cad8f`) cut HostTiming from about 99% to 60-64% of a core with no measurable FPS change (table below). Untested: AudioManager appearing only in the 1 ms AOT run, the remaining roughly 60% HostTiming cost, other event coalescing, and overlay/backend checks. macOS support is removed; Linux and FreeBSD builds were not run.

## Recent work (sessions 37-38)

- Session 37: B1 wait splitting (`5ccacde`), the 4 ms Npad default and `HostTimingEventStats` (`97cad8f`, `SUYU_HOST_TIMING_STATS=1`), and `ParseEnvIntInRange` in `src/common/env_option.h`; the X-Y-X results are in the measurement table below.
- Session 38 roadmap: [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md) defines the end goal (a title-specific standalone native PLA application with no CPU JIT, Maxwell/emulated GPU processing, or general emulator runtime) as milestones N0-N10 (runtime) and G1-G4 (tools and GUI). Decisions of 2026-10-20: the supported gameplay path is confirmed, and the Aevora Launcher and Recompiler GUI use Tauri.
- Session 38 code: macOS support and the Metal/MoltenVK backend were removed (`28011a2`, labelled `v0.0.1-devFINAL` by the user as a deliberate exception to the `devN` commit-count rule; the CHANGELOG entry has the evidence).
- Left on purpose after the removal: third-party macOS branches under `externals/`, `dist/languages`, `GPUVendor::Apple` (Asahi Linux), `.gitignore` Finder entries, and the archived `docs/Suyu` pages (clean up with the Qt GUI removal, G4).
- Not verified after the removal: Linux/FreeBSD configure and build, Windows gameplay, a new PLA export, and `research/tests`.

## N0 plan (session 39)

- The user chose N0 on 2026-10-20 and asked that it proceed stage by stage, with a report, a commit message (normal commit), and user approval at the end of every stage.
- Stages: 0 read-only hook survey (Code Explorer); 1 design note (one opt-in option, proposed `SUYU_BOUNDARY_INVENTORY=1`, execution-neutral when off; data model, output, thread safety); 2 header-only recorder plus standalone unit test with a negative control; 3a SVC and IPC hooks; 3b nvdrv, audio, HID, and filesystem hooks; 4 PLA collection on the JIT host `suyu.exe` along the supported path (AOT confirmation only after a user commit, reconfigure, and full export); 5 SDK-to-SVC/IPC mapping and NVN entry points (separate design); 6 structural-gate script with a shared deny list, synthetic test, and first report on the current launcher; 7 Markdown batch (check list, inventory summary, WORK_PLAN, CHANGELOG, CODEBASE_INDEX).
- Decisions (user, 2026-10-20): only summarized inventory goes into the repository; raw PLA logs stay local under `D:\HisuiWakeBuild\diag\n0\`. Stage 6 is not moved earlier.
- Stage 0 result (verified by the main agent against the current code; the survey report is local at `D:\HisuiWakeBuild\scratch\n0_stage0\report.md`):
  - SVC: JIT and AOT both reach `Svc::Call` from `PhysicalCore::RunThread` (`src/core/hle/kernel/physical_core.cpp:322-325`); `GuestTimeLedger` already reads the SVC number there (`:281-284`).
  - SVC caller: the AOT emitter writes `c->pc` (the resume PC, SVC address + 4) before `pending_svc` and `recomp_svc` (`src/core/recompiler/arm64_to_c.h:1511`), so the SVC site is resume PC - 4; x30 is the architectural LR. JIT exposes live LR/PC through `GetContext`.
  - IPC: `ServiceFrameworkBase::HandleSyncRequest`/`InvokeRequest`/`InvokeRequestTipc` (`src/core/hle/service/service.cpp:87-139`) have the service name and command ID; `ReportUnimplementedFunction` (`:67-85`) is the existing unimplemented-command report. Because nvdrv, audio, HID, and filesystem are HLE services, this one hook counts their command IDs; per-subsystem hooks are needed only for detail (ioctl numbers, paths, parameters).
  - nvdrv: `NVDRV::Open` and `Ioctl1/2/3` in `src/core/hle/service/nvdrv/nvdrv_interface.cpp:22,58-128`; the device name is known only at Open (`nvdrv.cpp` `Module::Open`), so the recorder must keep an fd-to-name map.
  - Audio: `audio_renderer_manager.cpp` `OpenAudioRenderer` (initial parameters) and `IAudioRenderer::RequestUpdate`/`RequestUpdateAuto` (`src/core/hle/service/audio/audio_renderer.cpp:77-94`).
  - Filesystem: `fsp_srv.cpp:466-490` (RomFS/data storage), `OpenSaveDataFileSystem`, `IFileSystem::OpenFile` (`fs_i_filesystem.cpp:91-100`, path and mode), `IFile::Read` (offset and size).
  - NVN: no `nvnBootstrapLoader` reference exists in the tree; the only guest symbol source found is each NSO's dynsym (runtime parser in `src/core/arm/recomp/arm_recomp.cpp`). Deferred to stage 5.
  - Build: all hook sites compile into `core`; a header-only recorder avoids a CMake source-list change, though including translation units recompile.
  - Output: follow the coverage pattern (`WriteRecompCoverageFile`, `arm_recomp.cpp:697-707`: env path override, else `LogDir`), plus periodic log reports because teardown may not run.

## N0 design (stage 1)

Design note for the boundary inventory, written in session 39 (stage 1). It describes intended code; nothing below is implemented yet.

- Option: `SUYU_BOUNDARY_INVENTORY=1` (`Common::EnvFlagDefaultOff`); any other value leaves it off. `SUYU_BOUNDARY_INVENTORY_PATH` overrides the report file, otherwise `LogDir/boundary_inventory.txt` (the `WriteRecompCoverageFile` pattern).
- Off path: the flag is read once into a process-wide `inline const bool`; every hook is a single `if` on it and does nothing else. No allocation, lock, clock read, or change to guest-visible results when off; hooks only read values the existing code has already parsed.
- Code: one header-only, OS-free recorder `src/core/boundary_inventory.h` (namespace `Core::BoundaryInventory`), so no CMake source-list change. Report formatting is a pure function of a snapshot, and the clock is passed in, so the unit test does not need `Core::System`.
- Data model (cumulative over the process; keys with count and first-seen time):
  - SVC: per physical core, a cache-line-aligned array of 256 counters indexed by SVC number, single writer per core (relaxed load and store); numbers above 0xFF go to one overflow counter. Hook: `PhysicalCore::RunThread` beside the existing `Svc::Call`.
  - IPC: (service name, command type, command ID) under one mutex, plus an `unimplemented` flag set from `ReportUnimplementedFunction`. Hook: `ServiceFrameworkBase::HandleSyncRequest`. Domain sub-interfaces are recorded under their own interface name.
  - nvdrv: `Open` records the device name and keeps an fd-to-name map (removed at `Close`); `Ioctl1/2/3` record (device name, variant, raw ioctl). Hooks: `NVDRV` in `nvdrv_interface.cpp`.
  - Audio: distinct `AudioRendererParameterInternal` tuples at `OpenAudioRenderer`; at `RequestUpdate` the count and min/max input, output, and performance buffer sizes.
  - HID and applets: HID command IDs come from the IPC record; library-applet creation records the applet ID (exact hook found in stage 3b).
  - Filesystem: opens by (kind: RomFS current process, data storage by ID, save data, SDMC, BIS, other; path; mode); reads by path with count, total bytes, and largest end offset. `IFile` keeps the opened path only when the option is on.
- Thread safety: SVC counters are single-writer per core; every other category has its own mutex. Contention while on is acceptable for a diagnostic; the inventory is not a timing measurement.
- Output: a deterministic, sorted, tab-separated cumulative report with sections `[svc]`, `[ipc]`, `[nvdrv]`, `[audio]`, `[applet]`, `[fs-open]`, `[fs-read]`. The whole file is rewritten at most every 10 seconds from a recording thread (rate limit checked with a try-lock) and once at shutdown, because teardown may not run; each write logs one `LOG_INFO` summary line with the distinct-key counts, not the full report.
- Caller attribution (SDK function per SVC/IPC) is stage 5; the data model leaves room for it. The AOT SVC site is the resume PC minus 4.
- Test: `tests/recompiler_smoke/boundary_inventory_unit.cpp`, registered as `smoke_boundary_inventory_unit`: per-core SVC counts and overflow, IPC keys and the unimplemented flag, nvdrv fd reuse after `Close`, filesystem aggregation, sorted output, and the 10-second write rate limit with an injected clock. Negative control: a build macro that weakens the model (`Close` does not remove the fd, so a reused fd keeps the old device name) must make the test fail fast.
- Repository output: the stage 7 summary aggregates filesystem paths to directory level with counts; raw reports stay in `D:\HisuiWakeBuild\diag\n0\`.
- Verification per stage: standalone build of the unit test and its negative control (stage 2); host build, Code reviewer review including the off-path check, and a short `suyu.exe` start with the option off and on (stages 3a and 3b).

## N0 design amendment (stage 4a: manual segments)

Written in session 39 after the first collection (run1, JIT host `suyu-cmd.exe`, option on). The cumulative report kept per-phase attribution hard, so recording becomes manual and segmented. The user approved the design on 2026-10-21 and chose to start paused.

- Run1 result (local files under `D:\HisuiWakeBuild\diag\n0\`): 32 distinct SVCs, 44 service interfaces with 121 commands, 34 nvdrv rows, 7 fs-open rows; `[audio]` and `[applet]` were empty (PLA uses `audout:u`/`hwopus`, not `audren:u`; no library applet was created on the played path). The cumulative snapshots copied at each step are valid and can be differenced into per-step deltas.
- Operation (suyu-cmd only): F9 marks (ends the open segment, writes its file, starts the next); F10 pauses and resumes; an Inventory window under Debug shows the state, the segment label, and Mark, Pause, Skip step, and Write now actions; a small on-screen line shows the state (for example `INV rec 3/8 battle 12.4 s`). Keys are provisional until checked against `fullscreen_toggle.h` and the game's key bindings.
- Labels: the supported-path steps in order (title, village, field, catch, battle, menu, pokedex, save); the overlay can also take a typed label.
- Start state: paused when a control host (suyu-cmd) is present; `SUYU_BOUNDARY_INVENTORY_START=1` starts recording at launch. Without a control host (Qt `suyu.exe`), recording is on from the start and the single open segment is auto-saved every 10 s, as in stage 3.
- Segments are independent: file `inventory_<run>_<NN>_<label>.txt`, header `# segment label=<label> elapsed_ms=<n>`, counts since the previous mark. The whole run is the sum of the segments (the local summarizer merges files). The open segment is auto-saved to `inventory_<run>_open.txt` every 10 s.
- Environment: `SUYU_BOUNDARY_INVENTORY_DIR` (output folder) and `SUYU_BOUNDARY_INVENTORY_RUN` (run label) replace `SUYU_BOUNDARY_INVENTORY_PATH`, which nothing outside the local helper used.
- Recorder semantics (stage 4a-1, implemented): `SetRecording` pauses the counters but the nvdrv fd-to-device map keeps following the guest; `EndSegment(label, elapsed_ns)` returns the segment text and resets it; SVC counts are the totals minus a baseline, each total read once, so concurrent marks lose and duplicate no call; tables are moved out under their own mutexes; the fd map is kept across marks.
- Stages: 4a-1 recorder segment API and unit tests (done: `smoke_boundary_inventory_unit` passes; ten header mutants fail it, including baseline not advanced, pause ignored, tables not reset, a mark that clears the fd map, and a pause that blocks the fd map); 4a-2 suyu-cmd host controls (done, see below); 4b the user re-collects the supported path.
- Stage 4a-2 (implemented, host build succeeded, not yet run by hand): `src/suyu_cmd/fullscreen_toggle.h` routes F9 (mark) and F10 (pause/resume) like F12, with `IsOverlayIndependentHotkey` so they work while an ImGui window holds the keyboard; they belong to the game when the inventory is off. `overlay/inventory_session.h` (pure: steps title, village, field, catch, battle, menu, pokedex, save then `extraN`, elapsed time that excludes pauses, a typed label that lasts one segment, `PendingSegmentFiles` retry queue, `FormatInventoryStateLine`) and `overlay/inventory_host.h` (event-thread glue: ends segments, writes `inventory_<run>_<NN>_<label>.txt`, `Finalize` at window teardown) are covered by `smoke_inventory_session_unit` (twelve header mutants across the session and hotkey tests fail it). The Inventory window and a state line (bottom right, amber when paused) are in `debug_overlay.cpp`; `emu_window_sdl2_vk.cpp` installs the host. Output: `SUYU_BOUNDARY_INVENTORY_DIR` (read as UTF-16 on Windows; default `<log dir>/boundary_inventory`), `SUYU_BOUNDARY_INVENTORY_RUN` (default `run`), `SUYU_BOUNDARY_INVENTORY_START=1` to start recording; the open segment is auto-saved to `inventory_<run>_open.txt`. Differences from the plan above: the command-bar entry is a top-level `Inventory` item beside `Debug` (Debug is itself a toggle item), and the typed-label field is implemented but its text input has not been tried by hand. Review fixes: a full retry queue retries before it drops anything and names the dropped files; `Finalize` makes a last write attempt at shutdown; the output folder no longer goes through the ANSI code page. Not run: suyu-cmd with the controls, a PLA session.

## N0 stage 4 result and stage 5 design

Written in session 39 after the manual collection (`boot1`: start to title with the option started recording; `test1`: eight segments title to save, started paused) and the stage 5 survey. Raw files stay local under `D:\HisuiWakeBuild\diag\n0\` (`boot1`, `test1`; `segments_report.py` and `summarize_inventory.py` read them); do not delete them, they are the only evidence.

- Stage 4 result (JIT host `suyu-cmd.exe`, one play of the supported path, so not exhaustive): 32 distinct SVCs; 44 service interfaces with 120 commands; six nvdrv devices (`nvmap`, `nvhost-as-gpu`, `nvhost-ctrl`, `nvhost-ctrl-gpu`, `nvhost-gpu`, `nvdisp_disp0`) with 34 open/ioctl rows; file system use is RomFS of the current process and save data (`/main`, `/main2`); no `audren` renderer is opened (audio runs through `audout:u` and `hwopus`); no library applet is created. After the village segment almost no new keys appear (field 2, catch 19 including save-data writes and time/random services, battle/menu/pokedex 0, save 1). The union of both collections matches run1 except one IPC command (probably `ISelfController` command 1, at exit) and the save-data `/backup` open, which are exit-time.
- Stage 5 survey (read-only, verified): SVC caller context is readable at the stopped CPU in `PhysicalCore::RunThread` for JIT and AOT (the AOT emitter and dynarmic both stop at SVC + 4); a frame-pointer backtrace walker exists (`src/core/arm/debug.cpp`); there is no module+RVA resolver (loader gives name and base only, `FindModules`/`GetModuleEnd` scan live memory); IPC handling runs while the client is blocked in SVC 0x21, so reading the client's context from the IPC hook needs scheduler locks; function names exist only where a module's dynsym has them; no NSO/ELF files exist on disk (only recompiled outputs); there is no `nvnBootstrapLoader` or NVN table anywhere in the tree.
- Approved design (2026-10-21): [callers] section with the SVC site (`pc - 4`, checked to be an `svc` instruction), LR, and two more return addresses up the x29 chain, printed as `name+0xRVA`; per SVC or IPC key the eight most frequent callers are kept, the least frequent is evicted into `other=`; module list taken once from `FindModules`/`GetModuleEnd` at the first SVC. IPC callers (5b): the SVC 0x21 hook stores the caller frames per thread id and `InvokeRequest` reads them by `ctx.GetThread()`, so no other thread's context is read. NVN (5c): at load, scan module memory for strings starting with `nvn` and for dynsym names containing `nvn` or `nn::`; the lists stay in local files. Repository rule: no guest addresses or game-derived name lists are committed, only aggregates.
- Stage 5a (done, host build succeeded, not run by hand): `src/core/boundary_inventory.h` has `ModuleRange`, `SetModules`/`Describe`, `ClaimModuleScan`, `RecordCaller`, `CollectCallerFrames`, `IsSvcInstruction` and the `[callers]` section; `PhysicalCore::RunThread` records the SVC caller (`RecordSvcCaller`, `RecordInventoryModules`). Covered by `smoke_boundary_inventory_unit` (eleven header mutants fail it). Review result: no runtime defect found; open point accepted by design: a failed or empty first module scan stays empty.
- Stages left: 5b IPC callers, 5c module scan and NVN strings, 5d collection and local analysis.

## Immediate next action

1. Read `CLAUDE.md`, this plan, `NATIVE_ROADMAP.md`, and `CODEBASE_INDEX.md`; check status and confirm no `suyu.exe`, `ninja`, or `cl` holds the build tree.
2. N0 stage 5b: IPC caller attribution through a per-thread stash written at SVC 0x21 and read by `InvokeRequest`; then 5c (module and NVN scan at load) and 5d (the user collects with `run_n0.bat`; analysis stays local) per `## N0 stage 4 result and stage 5 design`. Then stage 6 (structural-gate script) and stage 7 (Markdown batch).
3. Optional: investigate AudioManager and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), or other periodic events can be coalesced.
4. Cheap: tracked `__pycache__`/`.bak` cleanup, the `configure_audio` combo-index fix, a `controller_assign.h` test, an optional `LoadLibraryExW` error-mode change with test, the obsolete `*.plist` glob in `tools/translations/qt-source.sh`, and the now-dead `NOT MULTIARCH_BUILD` conditions in `src/dynarmic/CMakeModules/TargetArchitectureSpecificSources.cmake` and `src/dynarmic/tests/CMakeLists.txt`.

## Open items

- Pre-existing Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` has 7 failures/1 error pre-existing; tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD checks, OpenGL/Null windows, FPS/overhead, and Linux/FreeBSD configure and build (after the macOS removal) remain untested; `tests/exclusive_access/run.py` passed in session 38.
- Plain `suyu-cmd.exe <game>` does not auto-assign controllers (only exported packages do, `emu_window_sdl2.cpp:842`): assign Player 1 in the overlay Controller window. `suyu-cmd -g <path>` reads the path through the ANSI code page and garbles non-ASCII file names; the positional argument is read as UTF-16.

## Measurement setup

Target: i5-12400F (6C/12T, P-cores), RTX 2060 SUPER, 16 GB; 30 FPS cap. Config: `frame_pacing_mode=0`, `use_speed_limit=true`, `speed_limit=100`, `use_vsync=2`, `use_multi_core=true`. Metrics: process and busiest-thread CPU ms/game-frame (`core_pct`), with p99 frame interval; compare same-position JIT/AOT. Logs: `D:\HisuiWakeBuild\diag\cpu_budget\` (baseline and `97cad8f` X-Y-X files); summarizer is `summarize_cpu_budget.py`.

| Backend / Npad period | FPS | HostTiming ms/frame (core %) | process CPU ms/frame | CPUCore_0/1/2 ms/frame | frame p50/p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| ec8e1a5 JIT baseline | 30.1 | 32.9 (98.6%) | 113.5 | 20.5 / 20.7 / 20.3 | 33.33 / 33.74 |
| ec8e1a5 AOT baseline | 25.3 | 39.3 (99.1%) | 152.1 | 30.4 / 32.1 / 31.6 | 39.1 / 50.8 |
| JIT 4 ms (X1) | 30 cap | 21.27 (63.8%) | 101.4 | 20.7 / 20.9 / 20.6 | 33.33 / 33.82 |
| JIT 1 ms (Y) | 30 cap | 32.95 (98.9%) | 113.9 | 20.6 / 21.6 / 21.3 | 33.33 / 33.79 |
| JIT 4 ms (X2) | 30 cap | 20.15 (60.5%) | 100.3 | 20.6 / 21.2 / 21.0 | 33.33 / 33.94 |
| AOT 4 ms (X1) | 25.6 | 23.54 (60.3%) | 132.3 | 29.9 / 31.5 / 31.2 | 37.5 / 56.4 |
| AOT 1 ms (Y) | 24.9 | 39.84 (99.2%) | 156.0 | 30.8 / 32.6 / 32.1 | 39.6 / 49.8 |
| AOT 4 ms (X2) | 25.4 | 25.16 (63.9%) | 135.4 | 30.0 / 32.1 / 31.5 | 38.4 / 51.0 |

The ec8e1a5 baseline used `SUYU_CPU_BUDGET=1`, 10-second windows, and user logs; AOT was below cap. The X-Y-X windows retained same-position runs (JIT 12/12/15; AOT 17/16/13; JIT C filtered scene/loading windows). In 4 ms runs, JIT `os_blocked` was about 3700-4800 per 42000-52000 waits/10 s and AOT wake-lateness p99 was at most 100 us; 1 ms had zero OS blocks. AudioManager appeared only in 1 ms AOT at 3.78 ms/frame (9.4% of a core).

## Open overlay issues

See [IMGUI_OVERLAY.md](IMGUI_OVERLAY.md). Held key/axis can remain stuck across capture; `controller_assign.h` is untested; Resolution Scale has a translated duplicate; maximize fallback needs one toggle; OpenGL/Null overlay is unavailable; PDH GPU accuracy is unverified.

## Contracts to preserve

Keep diagnostics execution-neutral when off, exact-0 default-on semantics, ABI/result/cache contracts, overlay action-queue/event-thread rules, and same-position CPU ms/frame. `SUYU_HOST_TIMING_BLOCK` is default-on; `SUYU_HOST_TIMING_STATS` is opt-in; `SUYU_HID_NPAD_UPDATE_MS` defaults to 4 ms and accepts 1-8. Preserve saved-value CAS, atomic monitor slots, 32-bit indexes, and two-line comments.

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

Follow the start checklist, verify evidence before changing code, use one worker at a time, and record only verified facts. Main agent implements; Code reviewer reviews; Code Explorer is read-only; md Implementer edits Markdown. Do not claim unrun exports or hand tests.

## How to work / procedures

- Build standalone tests after vcvars64 with `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- For CPU budget, set `SUYU_CPU_BUDGET=1` (optional `SUYU_CPU_BUDGET_WINDOW_SEC` 1-60, default 10), copy logs, and summarize with `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>` (about 300 at 30 FPS, 240 for AOT). Drop windows from other scenes (loading) by filtering before summarizing; keep diagnostics off for FPS-only runs.
- Opt-in stats: `SUYU_HOST_TIMING_STATS=1` logs `host-timing` wait and event lines every 10 s. Analysis helpers: `D:\HisuiWakeBuild\tmp_gt`, `D:\HisuiWakeBuild\diag\hotrva`.
- Replay overlay input with the real router and ImGui sources; follow the standing export procedure in CLAUDE.md and clean scratch output.
- Orca: one worker at a time, acknowledge deliveries, release workers, and check again after heartbeat-only messages.
- Source-change equivalence check: `D:\HisuiWakeBuild\diag\pp_compare` holds `pp.py` (preprocess every translation unit of `compile_commands.json` with `/EP` into gzip files; run it from PowerShell with the repository untouched while it runs), `cmp.py` (accepts only `__LINE__` shifts derived from `git diff` and groups every other hunk by signature; `cmp.py --selftest` runs the negative controls), `after_only.py`/`before_only.py` (list added/removed lines that are not line shifts), `gen_variants.py` (build `smoke_export` from a source tree and write every smoke variant), `ed.py` (exact-match, line-ending-preserving edit helper) and `vcenv.bat`. Run them with `CPM_SOURCE_CACHE=D:\HisuiWakeBuild\cpm` where cmake is involved.
