# Aevora Work Plan

Handoff prepared: 2026-10-16 (session 29). Older material: [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-16-from-work_planmd-session-29-cleanup) (this cleanup, with the session 29 investigation record right after it: [record](OLD_HISTORY.md#session-29-investigation-record-2026-10-14-to-2026-10-16)); earlier cleanups: [2026-10-13](OLD_HISTORY.md#archived-on-2026-10-13-from-work_planmd-session-28-cleanup), [2026-10-12](OLD_HISTORY.md#archived-on-2026-10-12-from-work_planmd-session-27-cleanup).

## Objective

Determine whether PLA can run through Suyu-derived static CPU recompilation, then improve the verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is 9ba03c6 (README only), on f943708 and a304400, the two session 29 code commits; both add opt-in diagnostics and change nothing when off. Host D:\HisuiWakeBuild\msvc-release was reconfigured and rebuilt at f943708 with GIT_REV f943708b2e06985f3869fc1a78cfea8836245e0c; the README-only commit does not change code (re-check `git status`, `git log` and GIT_REV at the start of the next session). The last PLA export is D:\HisuiWakeExports\_NEW at f943708 (cross/direct/indirect chain and FM inline on, chain start off, fully static: `static -> JIT: 0`); its launcher and PDB are in D:\HisuiWakeBuild\backup\f943708_aot, and the JIT `suyu.exe` and PDB used for comparison are in backup\f943708_jit. Evidence logs and samples of session 29 are in D:\HisuiWakeBuild\logs (`*_f943708*` files); analysis scripts are in D:\HisuiWakeBuild\tmp_gt.

## What was done so far

- Dev19-dev24: FPXV, sharded counters, guest-time ledger, lookup diagnostics, exclusive stats, cross chain default-on, opt-in lock-free marking, 32-bit block index; see CHANGELOG.md and OLD_HISTORY.md.
- Session 29 (dev25): sampled the AOT export and ranked where block time goes; investigated an arena (host base + guest address) fastmem design and probed the fault-recovery mechanism; added the opt-in host-callback diagnostics (access stats with callback timing, call-site stacks); measured block-internal overhead shares; compared AOT with JIT per frame; planned an opt-in test-and-test-and-set SpinLock stub. Details, tables and the design record are in OLD_HISTORY.md (session 29 investigation record).
- Not started: any implementation of the arena fastmem, register promotion, or the TTAS stub (the TTAS plan is written below and awaits user approval).

## Key observations

Measured at f943708 at the same structure-heavy PLA position unless noted; RIP samples are 36,000 per run over CPUCore_0..2 and sampling perturbs FPS, so FPS comes from unsampled runs.

- FPS, speed limit off: AOT 24.5 FPS (40.8 ms/frame), JIT 30 FPS (33.3 ms/frame); wall gap 7.5 ms/frame. Earlier AOT readings: 3b3e244 24.6 FPS; aaaedd5 24.5/25.0/24.0; same-position noise about 1-1.5 FPS.
- Core time per frame (3 cores x frame time, samples as shares): AOT busy 89.4 core-ms vs JIT busy 64.2 (gap 25.2, which does not map 1:1 to wall time). Generated code: AOT 67.1 (DLL 49.5% + `blk_*` in the launcher 5.3% of samples) vs JIT 57.9 (all its dynamic code) = +9.2. Host-side code outside generated blocks: AOT 21.9 vs JIT 5.9 = +16.0 (about 63% of the gap).
- Exclusive-access overhead is the largest measured pool: host exclusive functions (`ExclusiveWrite128/32`, `HostExclusiveLoad`, `GetPlainExclusivePointer`, `NoteExclusiveWrite`, `CheckAndClear`, `ExclusiveRead*`, `SpinLock`) are 5.57% of AOT samples (0.15% in JIT), and one 4 KiB dynamic page with 6.0% of samples (about 7.3 core-ms/frame) is inferred to be the Dynarmic x64 SpinLock stub (hot offsets 0x0e, 0x07, 0x24 hold 43%, 20% and 30% of the page: after the lock xchg, after pause, after the unlock xchg; all three cores equally). Together about 11.6% (about 14 core-ms/frame). The JIT's inline exclusive cost sits inside its generated-code time, so 14 is an upper bound for the gap. The lock is one global `SpinLock` in `Dynarmic::ExclusiveMonitor`, stored next to the reservation slots; the JIT's inline code calls the same `EmitSpinLockLock` emitter.
- Dispatch/lookup/slice 3.18% of samples; SVC/kernel 2.05% (JIT 3.21%).
- Inside generated blocks (30.6% of all samples in the analysed functions; heuristic instruction-pattern shares, with sample skid, not removable costs): FMI memory checks 3.91% (lower bound, 5.4% of busy), guest GPR stores 5.5% of which 3.55% are stores right after guest data loads (load latency, also paid by JIT; GPR loads 0.09%, overwritten stores 0.25%), prologue/epilogue 1.46%, entry checks 1.1% (GG1 0.41, entry pc 0.48, halted 0.14, chain budget 0.10), flag stores 0.10%, in-block transfers 0.16%, vector register file 1.34%.
- Host memory callbacks (accesses that leave the inline path): 80,000-105,000 per second, 98.6% on GPU-tracked (rasterizer) pages, 1.3% page-crossing, essentially none unmapped, debug, watched, tagged or out of range. Callback time about 395 ms per 10 s (loads about 90 ns, stores about 1.1 us), about 1.6 core-ms/frame; the store cost is the rasterizer write notification, not the access method. About 294 distinct call stacks; the top 16 are 80%; three `sdk` blocks (`blk_sdk_53ad64`, `53ae20`, `53ae94`) are about 37%.
- `ctx.pc` is updated only at block exit, so it is stale while blocks chain; attribute host callbacks by stack frames, not by `ctx.pc`.
- Conclusions: the arena fastmem would remove at most the inline-check share (about 4-6% of samples) and rasterizer pages would fault about 100,000 times per second without per-site demotion, so it is not being pursued; promoting guest registers to block locals has little to gain; no single remaining Group A item exceeds about 4% of samples. No FPS effect has been shown for the lookup miss path, the 32-bit index or the chain experiment (2.00 vs 1.80 ms/frame, unexplained).

## Contracts to preserve

Keep opt-in diagnostics execution-neutral when off, exact-0 default-on semantics, FP_SHARED/FPXV ABI/result/cache contracts, overlay/fallback wording, two-line comments, and same-position averaged FPS with ms/frame. Cross chain is default-on (=0 disables); chain start, ledger, lookup/exclusive diagnostics, and lock-free marking remain opt-in. Preserve saved-value CAS, atomic monitor-slot accesses, and the one-definition 32-bit index contract: offset 0 means no block, failed builds publish no partial table, legacy export means no table, and registry index32_v1 requires the current entry size. The opt-in access-stats and access-sites diagnostics (SUYU_RECOMP_ACCESS_STATS=1, SUYU_RECOMP_ACCESS_SITES=1) stay off unless the value is exactly 1 and must remain execution-neutral when off.

## Open issues in priority order

### Group B - exclusive access (largest measured pool, about 14 core-ms/frame upper bound)

1. SpinLock stub test-and-test-and-set, opt-in `SUYU_SPINLOCK_TTAS=1` (plan in Design notes; awaiting user approval to implement).
2. Cache-line separation of the monitor lock and the per-core reservation slots (`alignas(64)`); evaluate after item 1.
3. Baseline exclusive timing with `SUYU_RECOMP_EXCL_STATS=1` (existing diagnostic; the user postponed this measurement, so it is taken when item 1 is evaluated) and shortening the exclusive bridge path / lock acquisitions per access.
4. Decide whether `SUYU_EXCL_LOCKFREE_MARK` becomes default-on (needs a long PLA soak).
5. Replace the unlock `xchg` by a plain store: needs an ordering analysis (STLXR followed by LDAR); outside item 1.
6. Dispatch/lookup/slice (3.18% of samples) and the remaining sdk<->main exits (about 8M each way per 10 s).

### Group A - generated-block overhead (each small; instruction-pattern shares only)

7. Memory checks (FMI): 3.9% of samples; small refinements such as omitting the page-cross check for naturally aligned accesses (page-table walk and watch-word removal need fault handling).
8. Prologue/epilogue 1.5%; block-entry checks 1.1% (`SUYU_AOT_CHAIN_START` covers entry testing under a 2 GiB budget); flag stores and in-block transfers about 0.3%. Re-experiment with RET/conditional-branch chaining only after the earlier regression is explained.

### Group C - GPU-tracked page callbacks

9. Rasterizer write notification costs about 1.1 us per store (about 1.6 core-ms/frame in total); low priority.

### Group D - carried work and environment limits

10. XMOD1 untested items: real ArmRecomp accounting, RecordMiss, RecompGaps, IC IVAU through the chain, 256-budget stop, and optional positive publication log.
11. Older all-static/legacy DLL launcher diagnostics, Python cryptography dependency, and held items (opcode totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC cl title support, U2, two-lifetime seam, real loader/refusal paths, export-regression Qt lookup, old-manifest reuse, tracked .pyc files, mutex+map miss records, carried A/B exports, chain-start-off A/B, optional JIT attribution, and final sdk+0x346040 callee).

## Immediate next action

Ask the user to approve implementing item 1 (the TTAS plan below) or to choose another issue; do not build or export without approval.

## How to proceed

Implement one behavior at a time (the opt-in TTAS stub), add the unit/negative-control tests, get one Code reviewer round, then ask the user to commit, reconfigure (check GIT_REV) and run one full export. Evaluate with one export and the environment switch: same-position sampler share of the SpinLock page and of the host exclusive functions (baselines 6.0% and 5.57%), `excl-stats` averages (separate run), and averaged FPS with ms/frame; only a gain beyond the 1-1.5 FPS noise counts for FPS. Keep each change small and negatively controlled.

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
- Access stats: set `SUYU_RECOMP_ACCESS_STATS=1` (exactly `1`) for per-window `access-stats` lines (loads/stores by reason: tagged, out_of_range, unmapped, debug, rasterizer, page_cross, watched, other; callback `load_ms`/`store_ms` and `avg_ns_<reason>`); add `SUYU_RECOMP_ACCESS_SITES=1` for `access-sites` lines (top 16 host call stacks as `module+0xrva`; a stack walk per access, so its timings are not representative). Stand still about 30 s, exit normally, read from about 52 s; copy the log after each condition. Symbolize launcher RVAs with `python -I -X utf8` and llvm-symbolizer fed through a Python subprocess (not a PowerShell pipe) against the exported launcher and its PDB (check GUID/Age equality first); map DLL RVAs to guest ranges with the `rvamap` helpers in D:\HisuiWakeBuild\diag\hotrva.
- Sampling analysis: after `sample_raw_rip2.ps1`, `tmp_gt\analyze_launcher.py` gives module and exe-symbol shares; `tmp_gt\check_share.py` (FMI check windows), `check_cats.py` (block overhead categories) and `check_regs.py` (guest register file traffic) disassemble the hot functions with llvm-objdump through a COFF wrapper and weight instructions by samples (heuristic patterns, sample skid; quote the limits); `tmp_gt\dump_fn.py` prints one function. Compare AOT and JIT in core-ms/frame (3 cores x frame time x sample share), taking FPS from unsampled runs.
- JIT comparison run: start `suyu.exe` with `SUYU_RECOMP_DIR` unset, speed limit off (Ctrl+U), same position; back up `suyu.exe` and its PDB before sampling.

## Design notes

Planned SpinLock TTAS (opt-in, JIT-neutral): add `bool ttas = false` to `EmitSpinLockLock` in `src/dynarmic/src/dynarmic/common/spin_lock_x64.cpp`; the JIT call site (`emit_x64_memory.h`, waitpkg flag unchanged) keeps the default, so JIT code stays byte-identical. The stub (`SpinLockImpl::Initialize`, `waitpkg=false`) reads `SUYU_SPINLOCK_TTAS` once in `GlobalInitialize` (`getenv`, exactly `1`) and emits `start: mov eax,1; xchg [ptr],eax; test eax,eax; jz done; spin: pause; cmp dword [ptr],0; jne spin; jmp start; done: ret`. The unlock is unchanged. Verification: a unit test that the default emission is byte-identical to today's; the `tests/exclusive_access/run.py` legs (locked and lock-free) repeated with TTAS and exact u8..u128 counters; a negative control that must fail by wrong totals without a data race in the test and without an endless spin (for example a check-then-set lock that is not atomic); incremental host build, smoke and FPXV runs, one Code reviewer round. Expected effect: at most the failed-xchg/pause share of the 7.3 core-ms/frame page (about 1.5-5 core-ms/frame), because the cross-core transfer of the lock line is not removed; judge by sampler and excl-stats, not FPS.
Arena fastmem design record (not implemented): direct access `[arena + va]` with hardware faults for special pages would reuse the JIT arena and page protections, but generated C has no recoverable fault sites; the probe in the record shows `asm goto` with an image-relative exception table and a vectored handler works under clang-cl 22.1.8, and the blockers are the rasterizer-page fault rate and the missing per-site demotion. See OLD_HISTORY.md (session 29 investigation record).
