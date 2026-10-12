# Aevora Work Plan

Handoff prepared: 2026-10-12 (session 26). Earlier detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-12-from-work_planmd-session-26-cleanup).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

The user-committed baseline `d1981da` is v0.0.1-dev21 and includes the XMOD1 work. The real repository HEAD is `2740fee` (`docs: record XMOD1 PLA measurement (cross chain about +1 to 1.5 FPS, budget sweep no effect)`), a newer docs commit. The host `D:\HisuiWakeBuild\msvc-release` still has embedded `GIT_REV d1981da`; verify again after the next host reconfigure/build.

## What was done so far

- Dev19 FPXV: added the opt-in FPXV path; no user-observed FPS gain.
- Dev20: added sharded counters, the two-line comment rule, and opt-in chain start; no user-observed FPS gain.
- Dev21: added the guest-time ledger, lookup diagnostics, and the XMOD1 cross-module chain; diagnostics and fixture coverage were verified, while PLA confirmation remains optional.

## Key observations

- AOT versus JIT speed-limit-off gap is about 17.2 ms/frame; exact-FP helpers account for only about 1.9 ms/frame.
- Main<->sdk exits are about 15.2M each way per 10 s and are dominated by tiny `nn::os` leaf calls.
- Lookup out-of-range binary-search misses are estimated at about 1.6 ms/frame.
- XMOD1 measured about +1 to +1.5 FPS (1.7 to 2.7 ms/frame) from single user-readings; normal baseline was 23 FPS.
- Chain-budget sweep 256/1024/2048 showed no change; readings during a host build were invalid.
- Session 27 (dev22, `SUYU_RECOMP_EXCL_STATS`): guest exclusive callbacks cost about 4.5-4.7 s of thread time per 10 s window (about 20 core-ms/frame at 23 FPS, 110-122 ns per op incl. timer), about 20M loads and 20M stores per window, 0.5% stores failing; the JIT cost for the same work is not measured. Cross chain on (default since dev22): 23 FPS versus 20 FPS off, dispatches 31.5M -> 17.0M per 10 s; sdk<->main exits remain about 8M each way (likely direct calls, not chained).
- Next candidate: `SUYU_EXCL_LOCKFREE_MARK=1` (opt-in, lock-free reservation mark for loads) needs a commit, reconfigure, full export and an A/B with `excl-stats`; TTAS spin lock stub (separate) not implemented.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, FP_SHARED/FPXV ABI/result/cache contracts, exactly-`0` default-on semantics, overlay and fallback wording, comment limit, and same-position averaged FPS with ms/frame comparisons.

XMOD1 is feature bit `0x8`, on by default since dev23 (`SUYU_AOT_CROSS_CHAIN=0` disables it), and requires FM1, the indirect chain, and a split export. All images verify and share one immutable table; publication is cleared on refusal, and old/non-registry-v1 loaders refuse the feature. Cross-chain output is recorded in manifest/cache/bundle identity only when enabled; off output is preserved.

## Open issues in priority order

1. Confirm XMOD1 with exit counters using same-position averaged A/B measurements.
2. Evaluate the separate lookup early-miss change.
3. Add a positive XMOD1 table-publication log line if useful.
4. Exercise the untested XMOD1 items: real `ArmRecomp` accounting, `RecordMiss`, `RecompGaps`, IC IVAU through the chain, and the 256-budget stop.
5. Held: opcode host-size totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC `cl` real-title support, U2 only with explicit approval, two-lifetime seam, real loader/refusal paths, export_regression Qt-root lookup, old-manifest reuse, six tracked `.pyc` files, and mutex+map `RecordMiss`/`RecordUnhandled`. Carried: FMI-off/FP_SHARED-off/default A/B exports, chain-start-off A/B, optional JIT built-in-cost attribution, and the final sdk+0x346040 callee.

## Immediate next action

Ask the user to choose the next target: confirm XMOD1 with exit counters, evaluate the early-miss lookup, or investigate other AOT-JIT gap cost centers. Do not build or export without go-ahead.

## How to proceed

Read `CLAUDE.md`, this file, and the indexed source locations; run `git status`/`git log`, verify host `GIT_REV` against HEAD, then ask the user which target to take next. Agents never commit, tag, or push; the user makes commits.

## How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` from PowerShell (vcvars fails under Bash); keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke suite: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include` via `cmd.exe /c` after vcvars64. Export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix` with a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd` (reconfigure plus build; needed after a commit to refresh `GIT_REV`), with the three export variables cleared. Never run concurrent builds or exports.
- Fixture-only smoke: `python tests/recompiler_smoke/run.py --only-xmod --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include`.
- The `--only-xmod` run takes the same arguments as the smoke suite (`--source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include`) and finishes in a few minutes; the full suite takes about 10 to 15 minutes, so run it in the background.
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes in the smoke suite must stay unchanged.
- Exports are user-started; use a new empty output folder and same-position/orientation sampling. The game log is written only after normal game exit; start from a non-elevated shell.
- Never run a build, export, or other CPU-heavy work while the user measures FPS; same-position/orientation averaged readings are required.
- Guest-time measurement: set `$env:SUYU_GUEST_TIME="1"`, `$env:SUYU_RECOMP_LOOKUP_STATS="1"`, and `$env:SUYU_RECOMP_LOOKUP_TARGETS="1"` in the same non-elevated shell, start the game (JIT: `suyu.exe` with `SUYU_RECOMP_DIR` unset; AOT: the export launcher), stand still about 30 s, record the averaged FPS, exit normally, and read the `guest-time core=N ...` lines (steady state only; skip the loading windows). JIT speed limit off is Ctrl+U in `suyu.exe`.
- Lookup-stats run: set `SUYU_GUEST_TIME=1`, `SUYU_RECOMP_LOOKUP_STATS=1`, and `SUYU_RECOMP_LOOKUP_TARGETS=1` in a non-elevated shell, run the export launcher, stand still ~30 s, exit normally, then `Select-String -Path <export>\user\log\suyu_log.txt -Pattern "lookup-stats|lookup-targets"`; use windows from ~52 s.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"` (read once at process start, confirmed only by FPS); remove it with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET` before exports or builds.
- Sampling: `D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>` (non-elevated; writes `<file>.modules` with every module base/size/path). Before the next build or export overwrites them, copy the measured launcher/`suyu.exe` and its PDB (verify GUID/Age equality) to a safe folder; `D:\HisuiWakeBuild\tmp_gt\sym_5bb3381` holds the `5bb3381` pair. Symbolize with `llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines`.
- ARM64 source bytes: enable Settings > Filesystem > Dump ExeFS, start the game, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress the `.text` segment and check the NSO build id against the manifest module `build_id`; turn the setting off afterwards.
- Local analysis scripts (outside the repository, may be recreated) are in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py` (ledger lines), `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py` and `locate_tables.py` (find the `{lo,hi,fn}` block tables in the DLL `.rdata`; the old fixed table RVAs in `diag\hotrva` are build-specific), `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py` (decompress an NSO), `arm_dis.py` (AArch64 via `llvm-objdump` COFF wrapper; there is no `llvm-mc`). Run Python with `-I -X utf8` (isolated mode ignores `PYTHONIOENCODING` and the script directory).
- Orca: write long specs to a file and pass `--spec "$(cat <file>)"`; `check --wait --json` output can hold several JSON objects (heartbeats first); parse each one.
- After every export test run `D:\HisuiWakeBuild\rebuild-suyu.bat` via `cmd.exe /c` from PowerShell. After commits use `D:\HisuiWakeBuild\diag\reconf_build.cmd`, then verify `GIT_REV`. Long builds may need background execution.
- Write C++ tests with the repository Write tool (then CRLF conversion if needed), not Python heredocs; temporarily weaken headers for negative controls, restore in finally, and rerun the good build.
- Helpers outside the repo are in `D:\HisuiWakeBuild\tmp_gt`; analysis scripts and dumped NSO details remain local.
