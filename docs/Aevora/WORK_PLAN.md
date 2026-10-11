# Aevora Work Plan

Handoff prepared: 2026-10-11 (twenty-fourth session). Superseded material through the twenty-third session is in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-11-from-work_planmd-twenty-fourth-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `5bb3381` (`kernel: add opt-in SUYU_GUEST_TIME run/idle/host ledger for JIT/AOT comparison`) on `main`, on top of `2b77bc2` (`v0.0.1-dev20`). Host `D:\HisuiWakeBuild\msvc-release` was reconfigured and built at HEAD (`GIT_REV 5bb33816c483988e66087a96199ae7c194a90740`, exit 0). The PLA export in `D:\HisuiWakeExports\Pokémon Legends_ Arceus` and the user-data cache entry were produced at `5bb3381` (split topology: `main` as a DLL of 1,667,841,024 bytes; exported with default options, and the `chain_start` manifest value was not re-read). The Markdown files carry this session's uncommitted documentation batch.

## Key observations (user-reported FPS; one number per condition)

| Condition | FPS |
|---|---|
| JIT (`suyu.exe`, speed limit on) | 30 |
| JIT, speed limit off (Ctrl+U) | 38 |
| Static AOT export at `5bb3381` | 23 |
| All listed optimizations off (earlier) | about 8.5 |
| Direct / indirect chain off individually (earlier) | 21 / 23.5 |
| Chain-budget sweep 32..1024 (earlier) | 21, 22, 23, 22, 23, 22 |
| FPXV on; chain start on (earlier) | no FPS change |

## Current investigation: where JIT and AOT time goes (session 24)

Question: in the same scene (Kotobuki, same position and orientation), how do guest-code execution, host-side work, and waiting differ between JIT and AOT? Conditions A = AOT 23 FPS, B = JIT speed limit on 30 FPS, C = JIT speed limit off 38 FPS; `nvdec_emulation` was set to 1 for all (the AOT package value; the Qt config had 2).

### Method

- Opt-in `SUYU_GUEST_TIME=1` ledger (`src/core/hle/kernel/guest_time_ledger.h`, hooked in `PhysicalCore::RunThread`/`Idle`): per core and 10 s window it splits wall time into run (inside `ArmInterface::RunThread`), idle (inside `PhysicalCore::Idle`), and host (the gap between runs minus idle, charged to the preceding guest thread and SVC number). The three add up to about 100% (`unattributed_pct` 0). Backend-agnostic: identical for JIT and AOT.
- Host RIP sampling (`sample_raw_rip2.ps1`, 3 x 12,000 samples of `CPUCore_0..2`, all module bases recorded) symbolized with `llvm-symbolizer` and the matching PDB (launcher PDB GUID/Age verified equal to the packaged exe), the DLL block tables, and the dumped ARM64 `main` NSO.
- Per-frame cost = category share x 3 cores / FPS (ms of core time per frame). JIT cost per frame is stable under the speed limit (52.0 vs 54.1 ms), so the 30 FPS cap does not hide cost.

### Results

| Per core, steady state | run | idle | host |
|---|---:|---:|---:|
| JIT (ledger) | 51.6% | 42.2% | 5.0% |
| AOT (ledger) | 73.3% | 21.2% | 4.0% |

| ms of core time per frame (3 cores) | AOT (23) | JIT limit on (30) | JIT limit off (38) |
|---|---:|---:|---:|
| Wait | 32.0 | 48.0 | 24.8 |
| Busy (excluding wait) | 98.5 | 52.0 | 54.1 |
| JIT code region (generated code and its built-in handling) | - | 44.6 | 47.3 |
| AOT generated block functions (DLL) | 55.3 | - | - |
| AOT guest code in launcher (static modules) | 6.3 | - | - |
| AOT dispatch (`recomp_lookup_*`, slice, `RunThread`) | 6.3 | - | - |
| AOT DLL runtime helpers (FP and others) | 9.8 | - | - |
| AOT exclusive-access path (stub page + C++ layers) | 12.6 | about 0.1 (callbacks only) | about 0.1 |
| SVC/kernel, scheduler, other host | about 7.0 | about 6.5 | about 6.0 |

- SVC counts per frame are similar (about 1,116 JIT vs 1,185 AOT mutex SVCs per core), so the per-frame workload is comparable.
- The gap is guest-code execution plus its runtime services; SVC/kernel/scheduler/GPU-side host work is the same in both. The AOT-minus-JIT(limit off) busy difference is about 44.4 ms of core time per frame; divided by 3 it is about 14.8 ms, close to the 17.2 ms frame-time difference (43.5 vs 26.3 ms). This is a lead, not proof: the effect of any reduction also depends on inter-thread dependencies.
- How the gap splits between runtime services and code body is **undetermined**. The 28.7 ms AOT total for dispatch, helpers, exclusive path, and stubs is an AOT-side sum; the JIT 44.6 ms region contains its own equivalents (inline dispatch, exclusive handling, FP), so the true difference of each item is smaller. An earlier "about 65% runtime services" statement was retracted.
- Three main guest threads each run about 14 ms per frame (JIT) vs about 24 ms (AOT); the frame time minus main-thread run is about 19 ms in both.
- FPS estimates from any reduction are conditional on the even-split dependency assumption and must be stated as such.

### AOT profile facts (24,000-sample run, DLL tables)

- Profile is flat: top function 1.4% of samples, top 200 functions 19.5%, top 1,000 30.6%; 4,317 distinct sampled block functions. Sample-weighted host size is about 375 bytes per guest instruction word (table median about 120).
- First 24,000-sample run, of all samples: OS wait 25.7%, generated block functions 42.7%, DLL runtime helpers 7.4%, launcher plus dynamic page about 24%.
- Second 36,000-sample run (modules recorded, launcher symbolized with its PDB), of all samples: wait 24.5%, block functions 42.4%, DLL helpers 7.5%, launcher 17.8% (static-module guest code 4.9%, dispatch 4.8%, exclusive/lock 4.6%, SVC/kernel 1.7%, other host 1.0%, scheduler 0.4%, memory helpers 0.3%), SpinLock stub page 5.0%, other 2.6%.
- Block-function samples by ARM64 class (share of all samples): loads/stores 15.3%, branches 10.8%, integer 7.9%, FP 6.0%, NEON other 2.6% (functions without an entry table are spread uniformly; approximate).
- Representative top function (guest `0x2d1e290`, 88 ARM64 instructions to 6,133 x64): a plain 64-bit load expands to about 22 x64 instructions (bounds, page-cross, two-level table, slow-path call, write-back to the context); vector `fadd.4s` is four calls to an exact FP helper (about 57 x64), `fmul.4s` about 127-135 with eight calls; integer `lsl/add/mov` are 1-3. Guest registers live in the context (`[rsi+offset]`), not host registers. Vector FP helpers (`rva 0x2f90`, `0x47f0`) are heavy exact implementations observed running.
- Dispatch (`rva 0x1100`: slice check, `recomp_lookup`, indirect call) is 1.40% of samples; `recomp_lookup_sdk` 1.4% in the launcher.

### Exclusive-access path (investigated; designs only, not implemented)

- The dynamic page (5.04%) is the Dynarmic `SpinLock` stub (one 4 KB Xbyak page, `lock` then `unlock`), confirmed by source and by page-layout match; samples sit right after the `xchg` instructions (cache-line transfer between three cores), almost none in the `pause` loop. The `mfence` in unlock is already removed in source. JIT executes the same lock/unlock instructions inline, so this 6.6 ms is not AOT-specific.
- AOT-only extra: bridge `HostExclusive*` (1.09% of samples), monitor wrappers (1.78%, partly the shared `cmpxchg`), `Memory::GetPlainExclusivePointer` (0.54%), `NoteExclusiveWrite` fence and watch check (0.51%), `CheckAndClear` and others (0.42%), C++ `SpinLock` wrappers (0.37%): about 4.7% or at most about 6.1 ms per frame of core time, so even full removal is about 2 ms of frame time (about +1-2 FPS under the dependency assumption).
- Designs (approval needed): E1 direct exclusive helper per size using the existing inline page-table fast path and calling `ReadAndMark`/`DoExclusiveOperation` without virtual dispatch or out-of-line pointer resolution; E2 compiler barrier instead of `seq_cst` fence in `NoteExclusiveWrite` on x86-64 after a locked CAS. Both need targeted 2-thread tests with a negative control and a generator/ABI version check. First an opt-in exclusive-operation counter (sharded counters) is needed to get operations per frame.

## Contracts to preserve

- Preserve FP_SHARED and FPXV option, manifest, cache-identity, ABI, and result contracts; do not extend FPXV without sampler/probe evidence.
- Preserve overlay architecture, honest fallback wording, ownership contracts, and exactly-`0` default-on option semantics.
- Keep `SUYU_RECOMP_THREAD_TIME=1` and `SUYU_GUEST_TIME=1` opt-in (exactly `"1"`), execution-neutral when off, and single-writer per core/thread; never fake cache producer identity or treat zero Hybrid attempts as static proof.
- Measure same position/orientation with averaged FPS for every A/B and report one number per condition; compare backends in ms per frame, and before calling a cost AOT-specific state what the other backend pays for the same work.
- Keep code comments at most two lines; chain start stays opt-in until a measured gain.

## Open issues, in priority order

1. Code reviewer (Orca) review of commit `5bb3381` and one full `tests/recompiler_smoke/run.py` run: only the standalone `guest_time_unit` (with negative controls) and the host build were run.
2. User decision on the next target. Candidates by measured size: FP exact helpers (about 9.8 ms; measure call frequency and which FPX1/FPXV gate path is taken), dispatch/lookup (about 6.3 ms; `recomp_lookup_*`, slice checks), code-body difference (representative short load -> integer -> store blocks, not only the top function), exclusive path (low payoff, needs counters first). Re-measure with the same three conditions after any change.
3. Optional: attribute JIT's built-in costs (inline dispatch, FP, exclusive) so the AOT-minus-JIT difference per item can be stated; the JIT code region is currently one dynamic block.
4. Carried: same-position averaged-FPS A/B exports (FMI-off only, FP_SHARED-off only, default re-measurement) with `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`; chain-budget default (256 current, 512 proposed); chain-start-off A/B; per-reason dispatcher-return counters design (do not implement without approval).
5. Held items: per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, MSVC `cl` real-title support, U2 only with explicit approval, the two-lifetime seam, real loader/refusal paths, the `tests/export_regression` Qt-root lookup, old-manifest reuse, and six tracked `.pyc` files. `RecordMiss`/`RecordUnhandled` remain mutex+map by design.

## Immediate next action

Run `git status`/`git log`, check host `GIT_REV` against HEAD, then ask the user to choose the next target from open issue 2 (and to commit the Markdown batch). Do not start implementation, exports, or builds without the user's go-ahead.

## Milestones and cache rule

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; PLA acceptance, remaining refusal paths, and Milestones 5-6 remain open. The user-data cache producer is git revision plus branch and build id and the launcher is part of the entry; a committed producer change requires regeneration; never spoof producer metadata. A launcher carrying new runtime code needs commit plus full export: relink-only is not feasible because the generated registration sources and ABI headers are deleted after an export and the cache keeps only the launcher, DLL, manifest, and `recomp_modules.cmake`.

## How to proceed

For a fresh session: read `CLAUDE.md` and this file, run `git status`/`git log`, verify `GIT_REV`, then ask the user which target to take next. The user makes commits, tags, and pushes.

## How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` from PowerShell (vcvars fails under Bash); keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke suite: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include` via `cmd.exe /c` after vcvars64. Export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix` with a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd` (reconfigure plus build; needed after a commit to refresh `GIT_REV`), with the three export variables cleared. Never run concurrent builds or exports.
- Exports are user-started; use a new empty output folder and same-position/orientation sampling. The game log is written only after normal game exit; start from a non-elevated shell.
- Guest-time measurement: set `$env:SUYU_GUEST_TIME="1"` in the same non-elevated shell, start the game (JIT: `suyu.exe` with `SUYU_RECOMP_DIR` unset; AOT: the export launcher), stand still about 30 s, record the averaged FPS, exit normally, and read the `guest-time core=N ...` lines (steady state only; skip the loading windows). JIT speed limit off is Ctrl+U in `suyu.exe`.
- Sampling: `D:\HisuiWakeBuild\diag\hotrva\sample_raw_rip2.ps1 -ProcId <pid> -Out <file>` (non-elevated; writes `<file>.modules` with every module base/size/path). Before the next build or export overwrites them, copy the measured launcher/`suyu.exe` and its PDB (verify GUID/Age equality) to a safe folder; `D:\HisuiWakeBuild\tmp_gt\sym_5bb3381` holds the `5bb3381` pair. Symbolize with `llvm-symbolizer --obj=<exe> --relative-address --demangle --no-inlines`.
- Local analysis scripts (outside the repository, may be recreated) are in `D:\HisuiWakeBuild\tmp_gt`: `analyze.py` (ledger lines), `analyze_launcher.py`, `compare.py <aot_fps> <jit_cap_fps> <jit_unc_fps>`, `hot_analysis.py` and `locate_tables.py` (find the `{lo,hi,fn}` block tables in the DLL `.rdata`; the old fixed table RVAs in `diag\hotrva` are build-specific), `agg.py`, `per_insn.py`, `fn_samples.py`, `nso.py` (decompress an NSO), `arm_dis.py` (AArch64 via `llvm-objdump` COFF wrapper; there is no `llvm-mc`). Run Python with `-I -X utf8` (isolated mode ignores `PYTHONIOENCODING` and the script directory).
- ARM64 source bytes: enable Settings > Filesystem > Dump ExeFS, start the game, read `%APPDATA%\suyu\dump\<title id>\exefs\main`, decompress the `.text` segment and check the NSO build id against the manifest module `build_id`; turn the setting off afterwards.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"` (read once at process start, confirmed only by FPS); remove it with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET` before exports or builds.
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes in the smoke suite must stay unchanged.
- Orca: write long specs to a file and pass `--spec "$(cat <file>)"`; `check --wait --json` output can hold several JSON objects (heartbeats first); parse each one.
