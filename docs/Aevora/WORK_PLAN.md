# Aevora Work Plan

Handoff prepared: 2026-10-10. Detailed superseded material is in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-10-from-work_planmd-twenty-third-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

HEAD is `2b77bc2` (`v0.0.1-dev20`) on `main`. Host `D:\HisuiWakeBuild\msvc-release` is configured and built for `GIT_REV 2b77bc29c2e27b83323a018237c04fd571d32292`; the cleanup rebuild with export variables cleared also exited 0, and no suyu/ninja process was running. The working tree has only the user's unrelated `README.md` edit and asset changes.

## Evidence

| Run | Result | Evidence status |
|---|---|---|
| JIT | About 30 FPS in the usual scene | User observation |
| Fresh FPXV-on export | No FPS gain | User observation |
| All listed optimizations off | About 8.5 FPS | User observation; split-main setting unanswered |
| Direct/indirect chain off individually | 21 / 23.5 FPS | User observation |
| Chain-budget sweep 32..1024 | 21, 22, 23, 22, 23, 22 FPS | User observation |
| Chain-start-on PLA export | Export OK; main DLL 1,706.8 MiB; FPS unchanged | User observation; indirect chain was also on, so size/FPS effect is not attributable to chain start alone |

The all-off cause and roughly 22-versus-30-FPS JIT gap remain hypotheses. See [CHANGELOG.md](../../CHANGELOG.md) dev19/dev20 and [OLD_HISTORY.md](OLD_HISTORY.md) for evidence details.

## Milestones

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; PLA acceptance, remaining refusal paths, and Milestones 5-6 remain open.

## Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers; obtain standard and FMI-off image sizes.
- [ ] Read cache outcome/publish/reused from the next export; record timings, loader inventory, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS and hardware details after each PLA run.

## Persistent cache rule

The user-data cache producer is git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration; never spoof producer metadata. Before exporting, clear the three export variables, rebuild/reconfigure, and verify `GIT_REV`.

## Held items

Per-opcode-class host-size totals, SVC attribution, Stage F, automatic all-static re-plan, Step 6/held items, MSVC `cl` real-title support, U2 only with explicit approval, the two-lifetime seam, real loader/refusal paths, the `tests/export_regression` Qt-root lookup, old-manifest reuse, and six tracked `.pyc` files remain held. `RecordMiss`/`RecordUnhandled` remain mutex+map by design.

## Work log

- Sessions 17-18: overlay/split/default options, thread-time ledger, FMI A/B, and sampler evidence; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 19: FP_SHARED and review-fix batch; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 20: FPXV implementation and dev19 verification; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 21: FPXV export handoff; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 22: A/B and chain-budget observations; details in [OLD_HISTORY.md](OLD_HISTORY.md).
- Session 23: sharded counters, comment cleanup, opt-in chain start, export evidence, and cleanup rebuild; details in [CHANGELOG.md](../../CHANGELOG.md) dev20 and [OLD_HISTORY.md](OLD_HISTORY.md).

## Session handoff (twenty-third session)

### State

HEAD/host state is recorded above. Dev20 delivered per-thread sharded dispatch counters, the two-line comment rule and touched-comment cleanup, and opt-in chain-start entries with manifest/cache identity, budget refusal, and tests. The chain-start PLA export succeeded with a 1,706.8 MiB main DLL and no user-observed FPS change; indirect chain was also enabled.

### Contracts to preserve

- Preserve FP_SHARED and FPXV option, manifest, cache-identity, ABI, and result contracts; do not extend FPXV without sampler/probe evidence.
- Preserve overlay architecture, honest fallback wording, ownership contracts, and exactly-`0` default-on option semantics.
- Keep `SUYU_RECOMP_THREAD_TIME=1` opt-in and execution-neutral; never fake cache producer identity or treat zero Hybrid attempts as static proof.
- Measure same position/orientation with averaged FPS for every A/B and report one number per condition.
- Keep code comments at most two lines; chain start stays opt-in until a measured gain; sharded counters are single-writer per thread.

### Open issues, in priority order

1. Run same-position averaged-FPS A/B exports: FMI-off only, FP_SHARED-off only, and a default re-measurement; record image sizes and `aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`.
2. Design, but do not implement without approval, opt-in backend-agnostic per-guest-thread run/SVC-service/wait breakdown and per-reason dispatcher-return counters; require byte-identical-off behavior, synthetic coverage, and a negative control.
3. Decide the chain-budget default: 256 remains current; the 512 proposal is pending.
4. Optionally run same-position chain-start-off A/B and sampler attribution of hot blocks.
5. Carry the held items listed above; U2 requires explicit approval.

### Immediate next action

Ask the user which export condition to run next, starting with the same-position FMI-off, FP_SHARED-off, or default averaged-FPS A/B and its image/cache fields.

## How to proceed

For a fresh session, read `CLAUDE.md` and this file, run `git status`/`git log`, check host `GIT_REV` against HEAD, then ask the user which export condition to run next. The user makes commits, tags, and pushes.

## How to work / procedures

- Run `.cmd` scripts through `cmd.exe /c` after vcvars64; keep logs, builds, exports, caches, and game outputs outside the repository. Overwrite logs with `*>` if `Remove-Item` is blocked.
- Smoke suite: `python tests/recompiler_smoke/run.py --source <repo> --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include` via `cmd.exe /c` after vcvars64.
- Export matrix: `python tests/export_integration/run_export_integration.py --suyu-dir D:\HisuiWakeBuild\msvc-release\bin --work <new short folder> --only-aot-cache-matrix`; use a new empty work folder.
- Host rebuild: `D:\HisuiWakeBuild\diag\reconf_build.cmd`, or `cmake --build D:\HisuiWakeBuild\msvc-release --target suyu suyu-cmd -j 8`, after vcvars64 and with the three export variables cleared.
- Read-only recompiled-image scripts are in `D:\HisuiWakeBuild\diag\entry_study\`; record aggregate numbers only. Use cache status fields, not the lost log tail.
- Exports are user-started; use a new empty output folder and same-position/orientation sampling. Do not run concurrent builds or exports.
- Chain-budget sweep: `$env:SUYU_RECOMP_CHAIN_BUDGET=N; & "<export>\Pokémon Legends_ Arceus.exe"` (read once at process start, confirmed only by FPS; sweep 32..1024 one condition at a time); remove the variable with `Remove-Item Env:SUYU_RECOMP_CHAIN_BUDGET` before exports or builds.
- The game log is written only after normal game exit; sample from a non-elevated shell and convert `+0xN` offsets to RVA by subtracting the module base (the image has no PDB).
- FPXV harness: `python tests/recompiler_fpx/run.py --cases 3000 --adv 3000 --legs 123 --controls --hits`; ABI5/ABI6 golden hashes in the smoke suite must stay unchanged.
- Orca `check --wait --json` output can hold several JSON objects (heartbeats first); parse each one.
