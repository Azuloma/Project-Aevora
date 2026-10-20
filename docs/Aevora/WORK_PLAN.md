# Aevora Work Plan

Handoff prepared: 2026-10-20 (session 38 cleanup). See [session 38 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-38-cleanup), [session 37 archive](OLD_HISTORY.md#archived-on-2026-10-20-from-work_planmd-session-37-cleanup), [session 36 archive](OLD_HISTORY.md#archived-on-2026-10-19-from-work_planmd-session-36-cleanup), [session 35 archive](OLD_HISTORY.md#archived-on-2026-10-18-from-work_planmd-session-35-cleanup), and [session 29 investigation record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. The current goal is lower host CPU usage per core so low-spec PCs run better at a 30 FPS cap; generation, compilation, or startup alone does not establish compatibility.

The long-term end goal and its milestones (N0-N10 runtime, G1-G4 tools and GUI) are defined in [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md): a title-specific standalone native PLA application with no CPU JIT, Maxwell/emulated GPU processing, or general emulator runtime, plus the Aevora Launcher and Aevora Recompiler replacing all Suyu GUIs. The supported gameplay path is confirmed and both GUIs use Tauri (2026-10-20).

## Status

HEAD is `28011a2` (`v0.0.1-devFINAL: remove macOS support and the Metal/MoltenVK backend`) on `main`; the working tree was clean after the commit. The host binaries in `D:\HisuiWakeBuild\msvc-release\bin` were built from the same sources, but `GIT_REV` in `msvc-release\src\common\scm_rev.cpp` still reads `19ed1919a56bd91044bba87e8cf3ceeec4ce0f5f` until the host is reconfigured and rebuilt. The PLA export and the persistent AOT cache entry date from `7609ab3`; the new producer revision requires a full export before the next measurement. Optimization state: B1 wait splitting alone was ineffective (waits are under 1 ms; about 2600 timer events/s, HID pad update at 1 kHz the largest source); the 4 ms Npad default (`97cad8f`) cut HostTiming from about 99% to 60-64% of a core with no measurable FPS change (table below). Untested: AudioManager appearing only in the 1 ms AOT run, the remaining roughly 60% HostTiming cost, other event coalescing, and overlay/backend checks. macOS support is removed; Linux and FreeBSD builds were not run.

## Recent work (sessions 37-38)

- Session 37: B1 wait splitting (`5ccacde`), the 4 ms Npad default and `HostTimingEventStats` (`97cad8f`, `SUYU_HOST_TIMING_STATS=1`), and `ParseEnvIntInRange` in `src/common/env_option.h`; the X-Y-X results are in the measurement table below.
- Session 38 roadmap: [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md) defines the end goal (a title-specific standalone native PLA application with no CPU JIT, Maxwell/emulated GPU processing, or general emulator runtime) as milestones N0-N10 (runtime) and G1-G4 (tools and GUI). Decisions of 2026-10-20: the supported gameplay path is confirmed, and the Aevora Launcher and Recompiler GUI use Tauri.
- Session 38 code: macOS support and the Metal/MoltenVK backend were removed (`28011a2`, labelled `v0.0.1-devFINAL` by the user as a deliberate exception to the `devN` commit-count rule; the CHANGELOG entry has the evidence).
- Left on purpose after the removal: third-party macOS branches under `externals/`, `dist/languages`, `GPUVendor::Apple` (Asahi Linux), `.gitignore` Finder entries, and the archived `docs/Suyu` pages (clean up with the Qt GUI removal, G4).
- Not verified after the removal: Linux/FreeBSD configure and build, Windows gameplay, a new PLA export, and `research/tests`.

## Immediate next action

1. Read `CLAUDE.md`, this plan, `NATIVE_ROADMAP.md`, and `CODEBASE_INDEX.md`; check status and confirm no `suyu.exe`, `ninja`, or `cl` holds the build tree. Ask before ending a user process.
2. Reconfigure and rebuild the host (`D:\HisuiWakeBuild\rebuild-suyu.bat` through `Start-Process`) and check that `GIT_REV` equals HEAD; a full export is required before any measurement.
3. Ask the user which work item comes next; it was undecided at the end of session 38.
   - N0 (boundary inventory, first runtime milestone of the roadmap): add opt-in, execution-neutral diagnostics that record, along the confirmed gameplay path, the SVC numbers, IPC services and command IDs, nvdrv ioctls, NVN entry points, audio renderer use, HID use, and filesystem access; write the manual check list for the supported path; add the structural-gate script and its first report. Start with a Code Explorer survey of where SVC and IPC dispatch live and which diagnostics already exist.
   - B2 (continue the CPU work): compare the AOT guest-core gap with JIT at the same position using `SUYU_GUEST_TIME` and RIP sampling; make no code change first. NATIVE_ROADMAP notes that tuning of the emulated scheduler and HostTiming does not carry over to the native runtime (N3).
4. Optional: investigate AudioManager and whether TouchUpdate/UpdateDefault (4 ms), KHardwareTimer (about 380/s), or other periodic events can be coalesced.
5. Cheap: tracked `__pycache__`/`.bak` cleanup, the `configure_audio` combo-index fix, a `controller_assign.h` test, an optional `LoadLibraryExW` error-mode change with test, the obsolete `*.plist` glob in `tools/translations/qt-source.sh`, and the now-dead `NOT MULTIARCH_BUILD` conditions in `src/dynarmic/CMakeModules/TargetArchitectureSpecificSources.cmake` and `src/dynarmic/tests/CMakeLists.txt`.

## Open items

- Pre-existing Qt combo-index bug: `src/suyu/configuration/configure_audio.cpp:72` maps Libretro index 4 to value 5; value 4 is Null-sink.
- `research/tests` has 7 failures/1 error pre-existing; tracked `__pycache__`/`.pyc`, `src/suyu/game_card.cpp.bak`, and the `cpp-lint.sh` tab issue remain.
- Bad Image dialog cause and `recomp_bundle.cpp:305` error-mode handling remain unresolved.
- HUD checks, OpenGL/Null windows, FPS/overhead, and Linux/FreeBSD configure and build (after the macOS removal) remain untested; `tests/exclusive_access/run.py` passed in session 38.

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
- Bash heredocs, `printf`, and `sed` can turn backslash sequences into real newlines or drop backslashes; write Python edit scripts and `.bat` files with the Write tool and check `git diff --numstat` after every scripted edit (a `cut` helper once removed far more than intended because its end marker matched later text).
- `cl` prints in the console code page: run `tests/package_policy` without `-X utf8` (under vcvars64) or `test_policy_helpers` fails to decode the compiler output.
- `git rm` stages deletions; use `git reset -q` afterwards so every change stays unstaged for the user.
- `cleanup_scratch.ps1` removes the whole scratch folder even when a path is passed.
- Before a large deletion or refactor that must not change Windows behavior, take the `pp_compare` preprocess baseline before editing any source.

## How to proceed

Follow the start checklist, verify evidence before changing code, use one worker at a time, and record only verified facts. Main agent implements; Code reviewer reviews; Code Explorer is read-only; md Implementer edits Markdown. Do not claim unrun exports or hand tests.

## How to work / procedures

- Build standalone tests after vcvars64 with `cmd.exe /c cl /std:c++20 /EHsc /W4 /I src <test>`; add `/Zc:preprocessor` and `/utf-8` as needed. Every new test needs a deterministic negative control.
- For CPU budget, set `SUYU_CPU_BUDGET=1` (optional `SUYU_CPU_BUDGET_WINDOW_SEC` 1-60, default 10), copy logs, and summarize with `python -I -X utf8 D:\HisuiWakeBuild\diag\cpu_budget\summarize_cpu_budget.py <log> <min_frames>` (about 300 at 30 FPS, 240 for AOT). Drop windows from other scenes (loading) by filtering before summarizing; keep diagnostics off for FPS-only runs.
- Opt-in stats: `SUYU_HOST_TIMING_STATS=1` logs `host-timing` wait and event lines every 10 s. Analysis helpers: `D:\HisuiWakeBuild\tmp_gt`, `D:\HisuiWakeBuild\diag\hotrva`.
- Replay overlay input with the real router and ImGui sources; follow the standing export procedure in CLAUDE.md and clean scratch output.
- Orca: one worker at a time, acknowledge deliveries, release workers, and check again after heartbeat-only messages.
- Source-change equivalence check: `D:\HisuiWakeBuild\diag\pp_compare` holds `pp.py` (preprocess every translation unit of `compile_commands.json` with `/EP` into gzip files; run it from PowerShell with the repository untouched while it runs), `cmp.py` (accepts only `__LINE__` shifts derived from `git diff` and groups every other hunk by signature; `cmp.py --selftest` runs the negative controls), `after_only.py`/`before_only.py` (list added/removed lines that are not line shifts), `gen_variants.py` (build `smoke_export` from a source tree and write every smoke variant), `ed.py` (exact-match, line-ending-preserving edit helper) and `vcenv.bat`. Run them with `CPM_SOURCE_CACHE=D:\HisuiWakeBuild\cpm` where cmake is involved.
