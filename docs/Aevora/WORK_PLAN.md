# Aevora Work Plan

Handoff prepared: 2026-10-12 (session 27). Earlier detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-12-from-work_planmd-session-27-cleanup).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD `8dd61a1` (user commit: record the AOT/JIT block comparison and the recomp_lookup change), on `884af21` (recomp lookup early miss). The uncommitted change set adds the 32-bit flat block index and launcher diagnostics. Host `D:\HisuiWakeBuild\msvc-release` was rebuilt after the change set and after the last integration run (suyu and suyu-cmd exit 0), but its `GIT_REV` remains `8dd61a1` because the tree was not reconfigured. The PLA export at `D:\HisuiWakeExports\_NEW\Pokémon Legends_ Arceus` was produced at `8dd61a1` and has the 64-bit index only; no PLA run or FPS measurement of the 32-bit index exists yet.


## What was done so far

- Dev19: FPXV.
- Dev20: sharded counters, two-line comment rule, and chain start.
- Dev21: guest-time ledger, lookup diagnostics, and XMOD1.
- Dev22 (9eda41f): `SUYU_RECOMP_EXCL_STATS` timing of the four guest exclusive bridge callbacks and `smoke_exclusive_stats_unit`.
- Dev23: a262080 enabled `SUYU_AOT_CROSS_CHAIN` by default (export-integration baseline pins it off; defaults checks both values); feb17ad added opt-in `SUYU_EXCL_LOCKFREE_MARK=1` and locked/lock-free `tests/exclusive_access`; 3b3e244 applied Orca-review fixes (x64 locked slot checks/clears use relaxed atomic_ref; the negative monitor test exits early with FAILED (9), exit 1). Verification: smoke SMOKE_EXIT=0, AOT cache matrix 279 passed/0 failed, `--only-defaults` 29 passed/0 failed (both on a262080), exclusive_access passes in both modes, host builds exit 0.
- AOT-vs-JIT short-block investigation compared production-configuration emitters with Dynarmic x64 JIT dumps; no repository change or production-DLL measurement was made. Block A was about 71 AOT instructions plus about 10 in `recomp_lookup`, versus about 39 JIT fastmem and 48 JIT without fastmem; Block B's taken path was about 54 AOT plus the run-slice return versus about 23 JIT.
- Commit `884af21` changed generated `recomp_lookup` to use the flat index for in-range aligned addresses and call `_lookup_search` only when index allocation failed. The new `smoke_lookup` fixture passed as part of `SMOKE_EXIT=0`; the full integration run had 212 passes and 2 unrelated `cryptography`-package failures. No speed effect has been measured.
- Dev24: Commit `884af21` changed generated `recomp_lookup` to use the flat index for in-range aligned addresses and call `_lookup_search` only when index allocation failed; the 32-bit flat block-index change adds the shared `recomp_image_index.h` contract, generated image tables and fallback reasons, host `RecompIndexView`, registry `index32_v1`, and launcher index diagnostics. It changes generated code and AOT cache identity; no speed effect has been established.

## Key observations

- AOT-vs-JIT (speed limit off) gap at session 24 was about 17.2 ms/frame (AOT 98.5 vs JIT 52.0/54.1 busy core-ms/frame); exact-FP bodies about 1.9 ms/frame, flat-index lookup about 5.5 ms/frame, dispatch about 2.1 ms/frame.
- AOT samples (session 24, 36,000 CPUCore samples): recompiled_image.dll 49.9%, ntdll 26.7% (mostly idle waits), launcher exe 17.8%, dynamic code 5.0% (one Dynarmic SpinLock stub page, 3 hot offsets); JIT dynamic code is spread over many pages (max about 1%). Exclusive-monitor host functions in the exe were about 4.4%.
- XMOD1 with all diagnostics on, same position: 20 FPS off, 23 FPS on; dispatches 31.5M -> 17.0M per 10 s; sdk<->main exits about 15.2M -> about 8M each way per 10 s (likely direct calls not chained).
- Exclusive callbacks per 10 s: about 20.5M loads, 20.1M stores, 0.5% failed stores. Ledger-lock: about 4.7 s thread time (load 110 ns, store 122 ns), about 20 core-ms/frame at 23 FPS. With lock-free mark: load 53 ns, store 98 ns, 3.3 s/window, about 13.6 core-ms/frame, and 23 -> 24 FPS; single integer readings may be noise. JIT cost is not measured.
- Remaining store cost is about 2.1 s/window (about 8.8 core-ms/frame): lock, CheckAndClear, guest-memory CAS under the lock, unlock.
- Lock-free trade-offs: pros are load about 2x faster, store about 20% faster, about 6 core-ms/frame less, untouched JIT inline paths, and saved-value CAS retained; cons are possible ABA, torn 128-bit saved value (CAS then fails), shared-env impact on JIT `ExclusiveRead`, no long PLA soak, plain slot accesses on arm64/loongarch64/riscv64, and FPS gain within noise.
- Short-block comparison: Block A (`ldr/add/str/ret`) was about 71 AOT instructions plus about 10 in non-inline `recomp_lookup` (about 13 branches; 424-byte lookup function), versus about 39 JIT fastmem (about 5 branches, 139-byte block code, shared RSB return excluded) and about 48 without fastmem (255 bytes). Block B's taken path was about 54 AOT plus the run-slice return versus about 23 JIT (186 bytes); its linked conditional branch was `cmp [halt],0; je target`.
- The roughly 41-instruction AOT Block A difference is approximately memory checks 28, block entry 16 (GG1 generation compare 4, halted 2, entry-point 5, frame 5), and RET 8. AOT memory access checks cost about 18-22 instructions each; JIT fastmem costs about 5-6 and omits the page-cross and watch-word checks. AOT CMP stores all four flags in the context (about 15 instructions); the earlier claim that it stored fewer flags was incorrect, while JIT packs them with `lahf/seto`.
- AOT conditional branches return to `recomp_image_run_slice` (lookup, budget decrement, indirect call); only unconditional B/BL chain. The earlier chaining experiment (2.00 versus 1.80 ms/frame, with unexplained JIT-transition and lookup-miss increases) is not a settled refutation of the current emitter.
- `recomp_lookup` now avoids the segment search on flat-index misses: the old path could perform about 10 probes over 661 PLA segments, while the new miss path is about 6 instructions versus roughly 90-190 by instruction count. Earlier session-24 lookup samples and the estimated 1.6 ms/frame were from a different configuration and do not measure this change; generated code changed, so reconfigure and a full export are required before an FPS comparison.
- First lookup-change reading: the same-position user-reported reading was 25.2 FPS (about 39.7 ms/frame) versus 24.6 FPS (about 40.7 ms/frame) for the baseline, +0.6 FPS (+2.4%) and about -1.0 ms/frame. This is one reading per build; earlier same-condition readings varied by about 1-1.5 FPS, so the difference is within observed spread and does not establish an effect. The old export folder may no longer be available for a repeat baseline reading; this is unverified.
- From `aot_manifest.json` (computed, not measured), PLA main text is 53,106,320 bytes (~13.3M instruction words; ~106 MB as 8-byte entries versus ~53 MB as 4-byte entries) and sdk text is 5,822,640 bytes (~11.6 MB versus ~5.8 MB), about 118 MB versus 59 MB together. Session-24's 578 of 1,521 samples immediately after flat-index load motivate the cache/TLB hypothesis, but do not measure this change.
- Full smoke exited 0; full integration after final fixes was 212 passed and 2 failed because Python lacks `cryptography`. The one-off split-failure integration result did not reproduce in two later full runs or the 25-pass split-only run; its cause is unknown. Reviewer should-fix findings and all listed negative controls were addressed.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, FP_SHARED/FPXV ABI/result/cache contracts, exactly-`0` default-on semantics, overlay and fallback wording, comment limit, and same-position averaged FPS with ms/frame comparisons. XMOD1 is on by default since dev23; `SUYU_AOT_CROSS_CHAIN=0` disables it. `SUYU_RECOMP_EXCL_STATS` and `SUYU_EXCL_LOCKFREE_MARK` are exact-`1` opt-ins and execution-neutral when off. Stores keep the saved-value compare-and-swap; every locked access to the monitor's reservation slots is an `atomic_ref`. The 32-bit index has one C/C++ definition in `recomp_image_index.h`; offset 0 means no block and is rejected for a real block, failed builds publish no partial table, and the legacy export answers "no table". Registry v1's optional `index32_v1` layout is accepted only when `entry_size` equals the current struct size.

## Open issues in priority order

1. After the user commits, run `reconf_build.cmd`, verify `GIT_REV`, and perform a full export; then take at least three same-position FPS readings per build for the 32-bit index A/B. The baseline is 24.6 FPS; the recomp_lookup change has one 25.2 FPS reading, within observed noise.
2. Evaluate the nested naturally-aligned page-cross check candidate; the `&&` form did not reduce instructions, and whole-helper inlining was worse on MK8D (image 100 to 222 MB, -14% FPS).
3. Older all-static launcher path and legacy DLL path do not report 32-bit index status; dispatch remains on `lookup` and is correct.
4. Integration environment note: the two portable unsealing checks need the missing Python `cryptography` package; the same failures appeared in earlier logs.
5. Re-experiment with conditional-branch chaining only after explaining the old regression and its increased JIT-transition/lookup-miss counts.
6. Design how to remove per-block GG1 generation, halted, entry-point, frame, and page-table-walk costs; fault handling is needed before removing the page-table walk.
7. Decide whether `SUYU_EXCL_LOCKFREE_MARK` becomes default-on (needs a long PLA soak; if so, `=0` disables per the default-on rule).
8. Test-and-test-and-set for the Dynarmic SpinLock stub (`spin_lock_x64.cpp` `EmitSpinLockLock`); gate it behind an opt-in and keep JIT unchanged unless measured.
9. Shorten the bridge path of exclusive ops (switch in `HostExclusive*`, out-of-line `GetPlainExclusivePointer`, `NoteExclusiveWrite`).
10. Remaining sdk<->main exits (about 8M each way per 10 s).
11. XMOD1 untested items: real ArmRecomp accounting, `RecordMiss`, `RecompGaps`, IC IVAU through the chain, 256-budget stop; optional positive table-publication log line.
12. Held: opcode host-size totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC `cl` real-title support, U2 only with explicit approval, two-lifetime seam, real loader/refusal paths, export_regression Qt-root lookup, old-manifest reuse, six tracked `.pyc` files, mutex+map `RecordMiss`/`RecordUnhandled`; carried FMI-off/FP_SHARED-off/default A/B exports, chain-start-off A/B, optional JIT built-in-cost attribution, and final sdk+0x346040 callee.

## Immediate next action

The user commits; reconfigure with `reconf_build.cmd`; verify `GIT_REV` equals HEAD; the user runs the full export; then take repeated same-position FPS readings. Ask which open issue to take next. Do not build or export without the user's go-ahead.

## How to proceed

Read `CLAUDE.md`, this file, and indexed source locations; run `git status`/`git log`, verify host `GIT_REV` against HEAD, then ask the user which target to take next. Agents never commit, tag, or push; the user makes commits.

## How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` from PowerShell (vcvars fails under Bash); keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include`; export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix`, via `cmd.exe /c` after vcvars64 and with a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd` with the three export variables cleared; never run concurrent builds or exports.
- Do not chain scripts that each call `vcvars64.bat` in one `cmd` session; start each stage as its own process and check that it is running.
- AOT-vs-JIT diagnostic tools are outside the repository in `D:\HisuiWakeBuild\diag\block_diff\`; lookup negative controls and comparison scripts are in `D:\HisuiWakeBuild\diag\lookup_work\`.
- The export integration test uses the host's compiled-in generator; rebuild the host before running integration after generator changes.
- Fixture-only smoke uses `python tests/recompiler_smoke/run.py --only-xmod --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include`; full suite takes about 10 to 15 minutes and may run in background.
- The `--only-xmod` run takes the same arguments as the smoke suite and finishes in a few minutes; run the full suite in the background when appropriate.
- FPXV: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes must stay unchanged.
- Exports are user-started; use a new empty output folder, same-position/orientation sampling, normal exit for logs, and a non-elevated shell. Never run CPU-heavy work during FPS measurement.
- Guest-time: set `$env:SUYU_GUEST_TIME="1"`, `$env:SUYU_RECOMP_LOOKUP_STATS="1"`, `$env:SUYU_RECOMP_LOOKUP_TARGETS="1"`; start JIT (`suyu.exe`, `SUYU_RECOMP_DIR` unset) or AOT launcher, stand still ~30 s, record averaged FPS, exit, read steady-state `guest-time core=N ...`; JIT speed-limit off is Ctrl+U.
- Lookup stats: set the same three variables, run the export launcher, then select `lookup-stats|lookup-targets` from `<export>\user\log\suyu_log.txt`; use windows from ~52 s.
- Chain sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"`; remove with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET`.
- Sampling: `D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>` (non-elevated; writes `<file>.modules` with every module base/size/path); copy launcher/`suyu.exe` and PDB (verify GUID/Age) before overwrite; `D:\HisuiWakeBuild\tmp_gt\sym_5bb3381` holds the `5bb3381` pair; symbolize with `llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines`.
- ARM64 bytes: enable Settings > Filesystem > Dump ExeFS, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress `.text`, check manifest `build_id`, then disable.
- Local analysis scripts outside the repo in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py`, `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py`, `locate_tables.py`, `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py`, and `arm_dis.py`; run Python with `-I -X utf8`. Orca long specs use `--spec "$(cat <file>)"`; parse multiple JSON objects from `check --wait --json`.
- After every export test run `D:\HisuiWakeBuild\rebuild-suyu.bat` via `cmd.exe /c`; after commits use `D:\HisuiWakeBuild\diag\reconf_build.cmd`, then verify `GIT_REV`; long builds may need background execution. Write C++ tests with the repository Write tool, not Python heredocs; restore weakened negative controls in finally.
- Helpers outside the repo are in `D:\HisuiWakeBuild\tmp_gt`; analysis scripts and dumped NSO details remain local.
- Lookup-stats run: set `SUYU_GUEST_TIME=1`, `SUYU_RECOMP_LOOKUP_STATS=1`, and `SUYU_RECOMP_LOOKUP_TARGETS=1` in a non-elevated shell, run the export launcher, stand still ~30 s, exit normally, then `Select-String -Path <export>\user\log\suyu_log.txt -Pattern "lookup-stats|lookup-targets"`; use windows from ~52 s.
- Local analysis scripts (outside the repository, may be recreated) are in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py` (ledger lines), `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py` and `locate_tables.py` (find the `{lo,hi,fn}` block tables in the DLL `.rdata`; the old fixed table RVAs in `diag\hotrva` are build-specific), `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py` (decompress an NSO), `arm_dis.py` (AArch64 via `llvm-objdump` COFF wrapper; there is no `llvm-mc`). Run Python with `-I -X utf8` (isolated mode ignores `PYTHONIOENCODING` and the script directory).
- Exclusive measurement: `$env:SUYU_GUEST_TIME="1"; $env:SUYU_RECOMP_EXCL_STATS="1"; $env:SUYU_RECOMP_LOOKUP_STATS="1"` (and `$env:SUYU_EXCL_LOCKFREE_MARK="1"` for lock-free) in a fresh non-elevated shell; stand still ~30 s, record averaged FPS, exit normally, read `excl-stats` from ~52 s. Keep only `suyu_log.txt` and `suyu_log.txt.old.txt`, so copy the log after EACH condition; remove `Env:SUYU_EXCL_LOCKFREE_MARK` before the other condition. LOOKUP_STATS adds a second lookup per exit and costs FPS; FPS-only runs have diagnostics off.
- Exclusive test: `python tests/exclusive_access/run.py --build-dir D:\HisuiWakeBuild\msvc-release --work <folder outside the repo>` after vcvars64 (via `cmd.exe /c`); it links host dynarmic.lib, so rebuild after dynarmic changes. Weakened ReadAndMarkLockFree storing INVALID_EXCLUSIVE_ADDRESS must exit 1; restore in finally.
- Tooling lessons: Python edit scripts putting `\n` escapes into C++ string literals produced real newlines twice (use Edit for string literals); Start-Process needs its log file created first; Orca `check --wait` replays until its `delivery_*` id is acked.

## Design notes

- The flat index replaces two binary searches over roughly 600k main-image entries; it is read-only after load, needs no locking or per-thread state, and costs 4 bytes per instruction word. A thread-local cache cost 34% in the race phase because the title's thread footprint is not free.
- Static `recomp_registration.c` entries use the v4 writer's renamed symbols and each generated definition has a prototype for `-Werror=missing-declarations`.
- Shared-image handles remain live for the process because published function pointers point into them; only a partially failed load frees its own handles.
- Guard v2 requires all modules to answer alike: agreement on 2 enables v2, and any other agreed answer means not negotiated everywhere.
- `Utf8ToWide` calls `MultiByteToWideChar` directly so malformed UTF-8 is an error without extra common sources.
- The compact dispatcher `OwnerEntry` table keeps the hot path's tens of millions of calls per second on contiguous entries instead of one cache line per `RecompImage`.
