# Aevora Work Plan

Handoff prepared: 2026-10-13 (session 28). Current cleanup is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-13-from-work_planmd-session-28-cleanup); the 2026-10-12 archive remains [here](OLD_HISTORY.md#archived-on-2026-10-12-from-work_planmd-session-27-cleanup).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is aaaedd5, on 8dd61a1 and 884af21; uncommitted changes are Markdown only (CHANGELOG.md, this plan, and this cleanup). Host D:\HisuiWakeBuild\msvc-release was reconfigured and rebuilt at HEAD with GIT_REV aaaedd5696f61ed6822e06b6d58343cd95f4800c. The PLA export at that revision has cross/direct/indirect chain and FM inline enabled, chain start disabled; its 32-bit index totals 67,068 KiB (about 65.5 MiB).

## What was done so far

- Dev19: FPXV.
- Dev20: sharded counters, two-line comment rule, and opt-in chain start.
- Dev21: guest-time ledger, lookup diagnostics, and XMOD1.
- Dev22: SUYU_RECOMP_EXCL_STATS and smoke_exclusive_stats_unit.
- Dev23: cross chain default-on, opt-in lock-free marking, atomic monitor slots, and review fixes.
- Dev24: 32-bit block index, index-miss segment-search removal, launcher index report, tests, negative controls, and full verification.
- Session 28:
  - Compared production clang-cl /O2 AOT blocks with x64 Dynarmic JIT dumps from the same ARM64 sequences.
  - Hand counts found memory checks, block entry, RET, and flag materialization as the main short-block differences.
  - Confirmed the current host was rebuilt at HEAD and recorded its GIT_REV.
  - Produced the PLA export at aaaedd5 with cross/direct/indirect chain and FM inline enabled.
  - Confirmed the 32-bit index report totals 67,068 KiB across the five modules.
  - Compared baseline, lookup-change, and current FPS readings under the same conditions.
  - Found no measurable FPS effect from lookup-miss removal or the 32-bit index; older exports were overwritten.
  - Removed old build/test leftovers and retained the documented scratch cleanup rule and folders.

## Key observations

- At session 24, AOT versus JIT speed-limit-off differed by about 17.2 ms/frame busy-core time (98.5 versus 52.0/54.1); this is a lead, not a proof of per-component causality.
- In 36,000 samples, recompiled_image.dll was 49.9%; low-RVA helpers, lookup, and dispatch were about 7.5%. Generated block bodies are the remaining high-value area; profile shares do not equal AOT-minus-JIT costs.
- Block A (ldr/add/str/ret) was about 71 AOT instructions plus about 10 in lookup versus about 39 JIT fastmem; memory checks account for about 28 of the roughly 41-instruction difference, with entry about 16 and RET about 8. JIT omits page-cross/watch-word checks; AOT flag stores materialize four flags. These are hand counts for one short block, not whole-program measurements.
- Baseline 3b3e244: 24.6 FPS (~40.7 ms/frame); 8dd61a1: one 25.2 FPS reading; aaaedd5: 24.5/25.0/24.0 (mean 24.5, ~40.8 ms/frame). Same-condition readings varied by about 1–1.5 FPS, so lookup and 32-bit index changes show no measurable FPS effect; older exports were overwritten.
- No measured effect: lookup miss-path change and 32-bit index; chain experiment (2.00 versus 1.80 ms/frame) remains unexplained. Earlier helper/inlining and lock-free single readings are not settled performance evidence.

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position averaged FPS with ms/frame. Cross chain is default-on (=0 disables); chain start, ledger, lookup/exclusive diagnostics, and lock-free marking remain opt-in. Preserve saved-value CAS, atomic monitor-slot accesses, and the one-definition 32-bit index contract: offset 0 means no block, failed builds publish no partial table, legacy export means no table, and registry index32_v1 requires the current entry size.

## Open issues in priority order

### Group A — remaining AOT/JIT gap (about 17 ms/frame busy-core at session 24)

1. Per-access checks: measure the nested naturally-aligned page-cross candidate; page-table walk and GG1 watch-word removal needs fault handling or a design change.
2. Per-block entry costs: GG1 generation, halted, entry-point, and frame checks; SUYU_AOT_CHAIN_START covers entry testing with a 2 GiB budget.
3. Flag materialization: four stores per compare versus JIT's packed word.
4. RET and conditional-branch transfers: re-experiment with chaining only after explaining the earlier regression.

Evidence for all four is instruction-count analysis, not a measured FPS gain; keep changes small and negatively controlled.

### Group B — carried work and environment limits

5. Decide whether SUYU_EXCL_LOCKFREE_MARK becomes default-on; needs a long PLA soak.
6. Test-and-test-and-set for the Dynarmic SpinLock stub, opt-in and JIT-neutral until measured.
7. Shorten the exclusive bridge path.
8. Remaining sdk<->main exits (about 8M each way per 10 s).
9. XMOD1 untested items: real ArmRecomp accounting, RecordMiss, RecompGaps, IC IVAU through the chain, 256-budget stop, and optional positive publication log.
10. Older all-static/legacy DLL launcher diagnostics, Python cryptography dependency, and held items (opcode totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC cl title support, U2, two-lifetime seam, real loader/refusal paths, export-regression Qt lookup, old-manifest reuse, tracked .pyc files, mutex+map miss records, carried A/B exports, chain-start-off A/B, optional JIT attribution, and final sdk+0x346040 callee).

## Immediate next action

Ask the user to choose an issue; do not build or export without approval.

## How to proceed

Before more code, use D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 on the current AOT export by RVA (only when the user is not measuring) to attribute the gap to block bodies. Judge instruction counts first, then at least three same-position FPS readings per build; require a gain beyond the 1–1.5 FPS noise spread. Keep each change small, add a negative-controlled test, and obtain one Code reviewer round before a full export.

## How to work / procedures

- Clean every build/test/export/probe/experiment: use D:\HisuiWakeBuild\scratch\<task>\, then D:\HisuiWakeBuild\diag\cleanup_scratch.ps1; check targets and leave no D:\fx_*, __pycache__, or .bak.
- Keep msvc-release, cpm, diag, tmp_gt, ovl, imgui_dl, backup, logs, and root .bat helpers.
- Run .cmd/vcvars stages via cmd.exe /c from PowerShell; start each vcvars64 stage as its own process and check it is running within a minute. Never run concurrent builds/exports.
- Smoke: python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include; fixture-only: add --only-xmod.
- Export matrix: python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix, after vcvars and with an empty work folder.
- Host rebuild: D:\HisuiWakeBuild\diag\reconf_build.cmd with export variables cleared; after export tests run D:\HisuiWakeBuild\rebuild-suyu.bat via cmd.exe /c.
- Rebuild the host before export integration after generator changes; the compiled-in generator otherwise remains stale. A warm full run took about 11 minutes and a first core rebuild after configuration changes may take about 30 minutes.
- Exports are user-started; use a new empty output folder, same-position/orientation sampling, normal exit for logs, and a non-elevated shell. Never run CPU-heavy work during FPS measurement.
- Guest-time: set `$env:SUYU_GUEST_TIME="1"`, `$env:SUYU_RECOMP_LOOKUP_STATS="1"`, `$env:SUYU_RECOMP_LOOKUP_TARGETS="1"`; start JIT (`suyu.exe`, `SUYU_RECOMP_DIR` unset) or AOT launcher, stand still ~30 s, record averaged FPS, exit, read steady-state `guest-time core=N ...`; JIT speed-limit off is Ctrl+U.
- Chain sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"`; remove with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET`.
- Lookup stats: set SUYU_GUEST_TIME=1, SUYU_RECOMP_LOOKUP_STATS=1, SUYU_RECOMP_LOOKUP_TARGETS=1, run the launcher, stand still ~30 s, exit normally, then select lookup-stats|lookup-targets from the log using windows from ~52 s.
- Sampling: D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>; copy launcher/suyu.exe and matching PDB before overwrite; symbolize with llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines.
- ARM64 bytes: enable Settings > Filesystem > Dump ExeFS, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress `.text`, check manifest `build_id`, then disable.
- Local scripts are in D:\HisuiWakeBuild\tmp_gt: analyze.py, analyze_launcher.py, compare.py, hot_analysis.py, locate_tables.py, agg.py, per_insn.py, fn_samples.py, nso.py, arm_dis.py; use Python -I -X utf8. Keep FPS diagnostics off during FPS-only runs.
- FPXV: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes must stay unchanged.
- After commits reconfigure/build and verify GIT_REV; after every export copy logs and rebuild as required. Do not weaken negative controls; C++ tests use repository editing tools.
- Exclusive measurement: `$env:SUYU_GUEST_TIME="1"; $env:SUYU_RECOMP_EXCL_STATS="1"; $env:SUYU_RECOMP_LOOKUP_STATS="1"` (and `$env:SUYU_EXCL_LOCKFREE_MARK="1"` for lock-free) in a fresh non-elevated shell; stand still ~30 s, record averaged FPS, exit normally, read `excl-stats` from ~52 s. Keep only `suyu_log.txt` and `suyu_log.txt.old.txt`, so copy the log after EACH condition; remove `Env:SUYU_EXCL_LOCKFREE_MARK` before the other condition. LOOKUP_STATS adds a second lookup per exit and costs FPS; FPS-only runs have diagnostics off.
- Exclusive test: `python tests/exclusive_access/run.py --build-dir D:\HisuiWakeBuild\msvc-release --work <folder outside the repo>` after vcvars64 (via `cmd.exe /c`); it links host dynarmic.lib, so rebuild after dynarmic changes. Weakened ReadAndMarkLockFree storing INVALID_EXCLUSIVE_ADDRESS must exit 1; restore in finally.
- Tooling lessons: Python edit scripts putting `\n` escapes into C++ string literals produced real newlines twice (use Edit for string literals); Start-Process needs its log file created first; Orca `check --wait` replays until its `delivery_*` id is acked.

## Design notes

The flat index replaces two binary searches with a read-only 4-byte-per-instruction-word table; its measured memory reduction is real, but its cache/TLB performance effect was not established. Generated registration uses current ABI names and prototypes; shared image handles remain live while published pointers exist; guard negotiation requires agreement across modules.
