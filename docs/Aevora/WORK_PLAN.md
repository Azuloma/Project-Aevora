# Aevora Work Plan

Handoff prepared: 2026-10-12 (session 27). Earlier detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-12-from-work_planmd-session-27-cleanup).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD `3b3e244` (v0.0.1-dev23), working tree clean, host `D:\HisuiWakeBuild\msvc-release` reconfigured and built at HEAD (`GIT_REV 3b3e244c996f576043ab583d68bb2a680db7a2fd`, exit 0). The PLA export at `D:\HisuiWakeExports\_NEW\Pokémon Legends_ Arceus` was produced at `3b3e244` (default options, cross chain on; manifest `cross_chain` true was checked on the earlier a262080 export, not re-read for this one).

## What was done so far

- Dev19: FPXV.
- Dev20: sharded counters, two-line comment rule, and chain start.
- Dev21: guest-time ledger, lookup diagnostics, and XMOD1.
- Dev22 (9eda41f): `SUYU_RECOMP_EXCL_STATS` timing of the four guest exclusive bridge callbacks and `smoke_exclusive_stats_unit`.
- Dev23: a262080 enabled `SUYU_AOT_CROSS_CHAIN` by default (export-integration baseline pins it off; defaults checks both values); feb17ad added opt-in `SUYU_EXCL_LOCKFREE_MARK=1` and locked/lock-free `tests/exclusive_access`; 3b3e244 applied Orca-review fixes (x64 locked slot checks/clears use relaxed atomic_ref; the negative monitor test exits early with FAILED (9), exit 1). Verification: smoke SMOKE_EXIT=0, AOT cache matrix 279 passed/0 failed, `--only-defaults` 29 passed/0 failed (both on a262080), exclusive_access passes in both modes, host builds exit 0.

## Key observations

- AOT-vs-JIT (speed limit off) gap at session 24 was about 17.2 ms/frame (AOT 98.5 vs JIT 52.0/54.1 busy core-ms/frame); exact-FP bodies about 1.9 ms/frame, flat-index lookup about 5.5 ms/frame, dispatch about 2.1 ms/frame.
- AOT samples (session 24, 36,000 CPUCore samples): recompiled_image.dll 49.9%, ntdll 26.7% (mostly idle waits), launcher exe 17.8%, dynamic code 5.0% (one Dynarmic SpinLock stub page, 3 hot offsets); JIT dynamic code is spread over many pages (max about 1%). Exclusive-monitor host functions in the exe were about 4.4%.
- XMOD1 with all diagnostics on, same position: 20 FPS off, 23 FPS on; dispatches 31.5M -> 17.0M per 10 s; sdk<->main exits about 15.2M -> about 8M each way per 10 s (likely direct calls not chained).
- Exclusive callbacks per 10 s: about 20.5M loads, 20.1M stores, 0.5% failed stores. Ledger-lock: about 4.7 s thread time (load 110 ns, store 122 ns), about 20 core-ms/frame at 23 FPS. With lock-free mark: load 53 ns, store 98 ns, 3.3 s/window, about 13.6 core-ms/frame, and 23 -> 24 FPS; single integer readings may be noise. JIT cost is not measured.
- Remaining store cost is about 2.1 s/window (about 8.8 core-ms/frame): lock, CheckAndClear, guest-memory CAS under the lock, unlock.
- Lock-free trade-offs: pros are load about 2x faster, store about 20% faster, about 6 core-ms/frame less, untouched JIT inline paths, and saved-value CAS retained; cons are possible ABA, torn 128-bit saved value (CAS then fails), shared-env impact on JIT `ExclusiveRead`, no long PLA soak, plain slot accesses on arm64/loongarch64/riscv64, and FPS gain within noise.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, FP_SHARED/FPXV ABI/result/cache contracts, exactly-`0` default-on semantics, overlay and fallback wording, comment limit, and same-position averaged FPS with ms/frame comparisons. XMOD1 is on by default since dev23; `SUYU_AOT_CROSS_CHAIN=0` disables it. `SUYU_RECOMP_EXCL_STATS` and `SUYU_EXCL_LOCKFREE_MARK` are exact-`1` opt-ins and execution-neutral when off. Stores keep the saved-value compare-and-swap; every locked access to the monitor's reservation slots is an `atomic_ref`.

## Open issues in priority order

1. Decide whether `SUYU_EXCL_LOCKFREE_MARK` becomes default-on (needs a long PLA soak; if so, `=0` disables per the default-on rule).
2. Test-and-test-and-set for the Dynarmic SpinLock stub (`spin_lock_x64.cpp` `EmitSpinLockLock`); gate it behind an opt-in and keep JIT unchanged unless measured.
3. Shorten the bridge path of exclusive ops (switch in `HostExclusive*`, out-of-line `GetPlainExclusivePointer`, `NoteExclusiveWrite`).
4. Remaining sdk<->main exits (about 8M each way per 10 s).
5. Lookup early-miss evaluation (about 1.6 ms/frame estimate).
6. XMOD1 untested items: real ArmRecomp accounting, `RecordMiss`, `RecompGaps`, IC IVAU through the chain, 256-budget stop; optional positive table-publication log line.
7. Held: opcode host-size totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC `cl` real-title support, U2 only with explicit approval, two-lifetime seam, real loader/refusal paths, export_regression Qt-root lookup, old-manifest reuse, six tracked `.pyc` files, mutex+map `RecordMiss`/`RecordUnhandled`; carried FMI-off/FP_SHARED-off/default A/B exports, chain-start-off A/B, optional JIT built-in-cost attribution, and final sdk+0x346040 callee.

## Immediate next action

Ask the user which of open issues 1-4 to take. Do not build or export without go-ahead; the user commits, then reconfigure (`reconf_build.cmd`), verify `GIT_REV`, then the user starts the export.

## How to proceed

Read `CLAUDE.md`, this file, and indexed source locations; run `git status`/`git log`, verify host `GIT_REV` against HEAD, then ask the user which target to take next. Agents never commit, tag, or push; the user makes commits.

## How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` from PowerShell (vcvars fails under Bash); keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include`; export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix`, via `cmd.exe /c` after vcvars64 and with a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd` with the three export variables cleared; never run concurrent builds or exports.
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
