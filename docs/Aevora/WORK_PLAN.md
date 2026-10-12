# Aevora Work Plan

Handoff prepared: 2026-10-11 (session 25). Detailed session-24 material is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-11-from-work_planmd-session-25-cleanup).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `d1981da` on `main`, with the stage 3-4 XMOD1 work committed by the user. Host `D:\HisuiWakeBuild\msvc-release` was built and cleaned up successfully; its embedded `GIT_REV` is `d1981da`. The user exported PLA at HEAD with `SUYU_AOT_CROSS_CHAIN=1` and measured the FPS difference.

## Key observations

- Session-24 Kotobuki table remains: JIT 30 FPS (limit on), 38 (off), AOT 23; busy core ms/frame AOT 98.5, JIT 52.0/54.1. AOT minus JIT (limit off) is about 44.4 ms core time/frame, about 14.8 ms/core and 17.2 ms frame-time gap. The retracted “about 65% runtime services” claim remains retracted; compare in ms/frame.
- AOT DLL low-RVA helpers are 7.53% (9.8 ms/frame): flat-index lookup 4.22% (5.5 ms), dispatch 1.59% (2.1 ms), inferred exact-FP bodies 1.45% (1.9 ms), other 0.3% (0.4 ms). Exact-FP helpers are therefore about 1.9 ms, not 9.8 ms.
- In `recomp_lookup`, 578/1,521 samples were after the flat-index load (about 2.1 ms/frame estimate); about 440 were out-of-range binary-search misses over 661 segments (about 1.6 ms/frame estimate). The generated fallback is in `arm64_to_c.h`.
- With lookup diagnostics, steady 10-second windows from about 52 s show about 31.5M dispatches, 30.4M covered exits (96%), 0.70M SVC, 0.55M budget exhausted, 0 halted/uncovered per window. Main↔sdk pairs dominate (about 15.2M each way); sdk leaf targets dominate the top target list, but the table overflowed, so shares are lower bounds.
- Interpretation only: tiny sdk leaf calls may cost two host dispatcher round trips (~70 ns each). Cross-module chaining could reduce this, conditional on dependency assumptions; it is not proof.
- The user-read PLA measurement was 23 FPS with no chain and with the existing chain, and 24.5 FPS in the first cross-chain run or 24 FPS at budget 256 later (about 1 to 1.5 FPS, or 1.7 to 2.7 ms/frame); the budget sweep found no change at 256, 1024, or 2048, so the default 256 remains. These are single readings, not same-position averaged A/B measurements; readings during a host rebuild were discarded because of CPU contention.

## Current investigation

The new lookup diagnostics are implemented behind exact-`1` `SUYU_RECOMP_LOOKUP_STATS` and `SUYU_RECOMP_LOOKUP_TARGETS` (targets requires stats). They provide per-thread return counters, top module pairs, and target reports; target collection adds a second host lookup per exit. New smoke targets are `smoke_lookup_stats_unit` and `smoke_exit_targets_unit`.

Cross-module chaining is approved by the user and implemented behind `SUYU_AOT_CROSS_CHAIN=1` as XMOD1 (feature bit `0x8`). It requires FM1, the indirect chain, and a split export; it records `cross_chain` in manifest/cache reuse/bundle identity only when enabled and preserves off output. One immutable host-published table uses ascending load bases, last-base ownership, normal block entries, and no `_s` entries; all images verify the same version/size/count and lookup, and publication is cleared on any refusal. Force-miss and main-hole hooks suppress publication and are identified by `ActiveRecompMissHookName`; the registry v1 loader is the only loader supporting XMOD1. Coverage includes `smoke_xmod_table_unit`, the three-module `cross`/`cross-off`/`cross-refuse`/`cross-guard` fixture cases with budgets 1/2/256, and AOT cache matrix cases.

Commits since the prior handoff, all user-made: `5bb3381` guest-time ledger; `99493e8` guest-time window restart/log placement; `d8a2f74` opt-in lookup stats; `d5fb221` per-module pair exits; `36a1a40` exit target reports. Reviews found and fixes were verified for window baselines/ticks, SVC and halt classification, module-list changes, UINT64_MAX overflow, concurrent publication, and rebase.

## Design under review

Cross-module indirect chaining is approved and implemented as feature bit XMOD1 (`0x8`) with all-or-none bundle verification; old hosts refuse the unsupported bit with a diagnostic. `SUYU_AOT_CROSS_CHAIN` is opt-in, recorded in manifest/cache identity and split-bundle identity only when enabled; off output stays byte-identical (ABI5/ABI6 goldens).

The shared lookup returns normal absolute-index entries, preserves GG1 and mid-block handling, and never returns direct-chain `_s` entries. One immutable table is published only after all modules verify, bind, set base, and expose indexes; it is cleared on refusal and is never freed while guest threads can run. Force-miss hooks publish nothing with a reason. The fixture covers budgets 1/2/256, A→B→C→A, SVC/BRK, FADD, RET/BLR, misses, guard behavior, and unchanged off goldens; real `ArmRecomp` accounting, `RecordMiss`, `RecompGaps`, IC IVAU through the chain, and a 256-budget stop remain untested. Evaluate the separate out-of-range lookup early miss later with its own A/B and golden update.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, FP_SHARED/FPXV ABI/result/cache contracts, exactly-`0` default-on semantics, overlay and fallback wording, comment limit, and same-position averaged FPS with ms/frame comparisons. E1/E2 exclusive designs remain low-payoff proposals; obtain an opt-in exclusive-operation counter first. Do not extend FPXV without sampler/probe evidence.

## Open issues, in priority order

1. The user-committed stage 3-4 work is built and exported at HEAD `d1981da`; the cross-chain PLA measurement is recorded above. Remaining optional confirmation is a same-position averaged A/B with the existing chain and exit counters to confirm and attribute the 1 to 2.7 ms/frame difference.
2. Evaluate the separate lookup early-miss change and add the untested XMOD1 items above; a positive XMOD1 table-publication log line is also an optional follow-up.
4. Held: opcode host-size totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC `cl` real-title support, U2 only with explicit approval, two-lifetime seam, real loader/refusal paths, export_regression Qt-root lookup, old-manifest reuse, six tracked `.pyc` files; `RecordMiss`/ `RecordUnhandled` remain mutex+map. Carried: FMI-off/FP_SHARED-off/default A/B exports with cache fields, chain budget 256/512, chain-start-off A/B. Optional: JIT built-in-cost attribution and final sdk+0x346040 callee.

## Immediate next action

Ask the user to choose the next target: confirm the cross-chain result with exit counters, evaluate the separate lookup early miss, or investigate other cost centers from the AOT-JIT gap.

## Milestones and cache rule

Milestones 1–3 and covered 4a synthetic cases are complete; PLA acceptance, refusal paths, and Milestones 5–6 remain open. Cache producer identity is revision plus branch/build id; never spoof it. A new launcher runtime requires a commit and full export.

## How to proceed

Read `CLAUDE.md`, this file, and the indexed source locations; confirm status/revision; obtain user approval before implementation. Agents never commit, tag, or push; the user makes commits.

## How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` from PowerShell (vcvars fails under Bash); keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke suite: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include` via `cmd.exe /c` after vcvars64. Export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix` with a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd` (reconfigure plus build; needed after a commit to refresh `GIT_REV`), with the three export variables cleared. Never run concurrent builds or exports.
- Exports are user-started; use a new empty output folder and same-position/orientation sampling. The game log is written only after normal game exit; start from a non-elevated shell.
- Guest-time measurement: set `$env:SUYU_GUEST_TIME="1"`, `$env:SUYU_RECOMP_LOOKUP_STATS="1"`, and `$env:SUYU_RECOMP_LOOKUP_TARGETS="1"` in the same non-elevated shell, start the game (JIT: `suyu.exe` with `SUYU_RECOMP_DIR` unset; AOT: the export launcher), stand still about 30 s, record the averaged FPS, exit normally, and read the `guest-time core=N ...` lines (steady state only; skip the loading windows). JIT speed limit off is Ctrl+U in `suyu.exe`.
- Sampling: `D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>` (non-elevated; writes `<file>.modules` with every module base/size/path). Before the next build or export overwrites them, copy the measured launcher/`suyu.exe` and its PDB (verify GUID/Age equality) to a safe folder; `D:\HisuiWakeBuild\tmp_gt\sym_5bb3381` holds the `5bb3381` pair. Symbolize with `llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines`.
- Local analysis scripts (outside the repository, may be recreated) are in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py` (ledger lines), `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py` and `locate_tables.py` (find the `{lo,hi,fn}` block tables in the DLL `.rdata`; the old fixed table RVAs in `diag\hotrva` are build-specific), `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py` (decompress an NSO), `arm_dis.py` (AArch64 via `llvm-objdump` COFF wrapper; there is no `llvm-mc`). Run Python with `-I -X utf8` (isolated mode ignores `PYTHONIOENCODING` and the script directory).
- ARM64 source bytes: enable Settings > Filesystem > Dump ExeFS, start the game, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress the `.text` segment and check the NSO build id against the manifest module `build_id`; turn the setting off afterwards.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"` (read once at process start, confirmed only by FPS); remove it with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET` before exports or builds.
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes in the smoke suite must stay unchanged.
- Orca: write long specs to a file and pass `--spec "$(cat <file>)"`; `check --wait --json` output can hold several JSON objects (heartbeats first); parse each one.
- Lookup-stats run: set `SUYU_GUEST_TIME=1`, `SUYU_RECOMP_LOOKUP_STATS=1`, and `SUYU_RECOMP_LOOKUP_TARGETS=1` in a non-elevated shell, run the export launcher, stand still ~30 s, exit normally, then `Select-String -Path <export>\user\log\suyu_log.txt -Pattern "lookup-stats|lookup-targets"`; use windows from ~52 s.
- After every export test run `D:\HisuiWakeBuild\rebuild-suyu.bat` via `cmd.exe /c` from PowerShell. After commits use `D:\HisuiWakeBuild\diag\reconf_build.cmd`, then verify `GIT_REV`. Long builds may need background execution.
- Write C++ tests with the repository Write tool (then CRLF conversion if needed), not Python heredocs; temporarily weaken headers for negative controls, restore in finally, and rerun the good build.
- Helpers outside the repo are in `D:\HisuiWakeBuild\tmp_gt`; analysis scripts and dumped NSO details remain local.
