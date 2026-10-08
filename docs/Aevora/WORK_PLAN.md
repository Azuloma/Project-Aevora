# Aevora Work Plan

Handoff prepared: 2026-10-08. HEAD is `0a9813a` (`v0.0.1-dev17`) on `main`; the Markdown cleanup of the fifteenth session is uncommitted. The user commits, tags, and pushes. Start at [Session handoff](#session-handoff-2026-10-08-fifteenth-session); historical detail is in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

| Area | Current status |
|---|---|
| Milestone 4a | Topology, registry, loader, split exporter, persistent cache, and U1-U4 synthetic work are complete for covered cases. |
| Step 6 PLA | Static AOT + FMI + main-DLL split runs; Kotobuki remains 17-21 FPS versus 30 FPS under JIT. |
| U1 and chaining | U1, opt-in direct B/BL chaining, and indirect RET/BR/BLR chaining are implemented, reviewed, and synthetic-fixture verified. |
| Verification | Smoke, cache matrix (194 passed/0 failed), negative controls, and host build passed; chain-run FPS is unreported. |

## Evidence so far (observations)

| Run | Core0/Core1/Core2 CPU % | User-reported FPS | Image size |
|---|---:|---:|---:|
| JIT | 76.8/78.7/77.2 | 30 | not measured |
| Static AOT (dev16 launcher) | 76.3/80.4/81.4 | 17-20 | Main DLL 1,857,218,560 bytes |
| + U1 | 77.5/82.5/81.3 | 17-21 | not measured |
| + direct chain | 77.0/82.1/83.6 | unreported | Main DLL 1,894,430,208 bytes |
| + direct and indirect chain | 75.8/80.8/81.0 | unreported | Main DLL 1,909,940,736 bytes; SizeOfImage 1,923,067,904 |

The last image is about 214 MiB below the 2 GiB LNK1248 limit. Block transfer (`run_slice` + lookup) is only about 3-4% of samples; generated main-DLL code is about 46-55%, with recurring hot RVAs `+0x20B0`, `+0x20D2`, `+0x211A`, and `+0x21AE`. Each core waits idle about 20% for an unexplained reason; the GPU thread waits 81% in its command queue, so the GPU is not the bottleneck. These are observations, not acceptance criteria.

## What the code now contains

- U1: lock-light exclusive-monitor host-pointer and reservation paths, page-aware fallback, conditional notification, and the redundant unlock fence removal; see `CHANGELOG.md` dev17 and [CODEBASE_INDEX.md](CODEBASE_INDEX.md).
- Direct chaining: `g_emit_direct_chain` emits opt-in same-module B/BL tail calls through `RECOMP_TAIL_CALL`; conditional branches remain unchained.
- Indirect chaining: `g_emit_indirect_chain` handles opt-in same-module RET/BR/BLR targets through `recomp_lookup`; misses return to `run_slice`.
- Both chain flags are off by default, preserve output when off, and affect manifest/bundle-id inputs; fixtures and cache matrix are verified.

## Milestones

Milestones 1-3 record environment, exporter/build issues, generated-code loading, and gap diagnostics. Milestone 4a is complete for covered synthetic cases; real loader/refusal paths and PLA acceptance remain open. Milestones 5-6 are planned rendering, timing, and release-evidence work; none is accepted.

## Remaining Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers.
- [ ] Compare FMI-off/on and record each image's `SizeOfImage`.
- [ ] Record link/hash/publish timings, loader inventory names, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS after each PLA run; chain-run FPS remains unknown.

## Persistent cache rule

The user-data cache producer is the git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration, while an uncommitted runtime change can reuse an old launcher. Never spoof producer metadata; clear the three export variables before the normal host rebuild.

## Held work and evidence boundary

Held items are the two-lifetime seam, real loader/refusal paths, `tests/export_regression` Qt-root lookup, MSVC `cl` route for the chain macro, old-manifest-without-chain-fields reuse, and six tracked `.pyc` files with no `.gitignore` entry. PLA results are user-run and scene-specific, not general compatibility or release evidence; the recompiled image has no PDB, so RVA attribution is the evidence boundary.

## Session handoff (2026-10-08, fifteenth session)

### State and completed work

HEAD is `0a9813a`/dev17 (code last changed in `cba2193`). Milestone 4a synthetic work, U1, direct chaining, indirect chaining, reviews, smoke, cache matrix, negative controls, and host build are complete. PLA observations show no measurable CPU change from U1 or chaining; generated code and unexplained idle waits remain the likely investigation targets.

### Open issues, in priority order

1. Disassemble and map hot main-DLL RVAs to generated functions; decide whether an attribution-only re-export is needed.
2. Stack-sample what the roughly 20% idle core wait is waiting on.
3. Finish the Step 6 checklist above.
4. Consider U2 only with explicit user approval and low priority; then address held work.

### Immediate next action

Disassemble and map the hot RVAs, then ask the user for FPS from the chain builds if it is still unknown.

## How to work / procedures

- Run `.cmd` helpers through PowerShell's `cmd.exe /c "D:\HisuiWakeBuild\diag\buildsuyu.cmd > D:\HisuiWakeBuild\diag\<log> 2>&1"`; read logs for ASCII `error C` and `BUILD_EXIT`. Run `runsmoke.cmd` once per finished fix set and require both smoke pass markers; a first-pass new test needs a deliberately weakened negative control.
- For cache integration, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, use a fresh short work directory, run `--only-aot-cache-matrix`, then rebuild. Keep logs/builds/exports outside the repository.
- For PLA, find the largest-working-set `Pokemon Legends_ Arceus` PID, run `sample_raw_kotobuki4.ps1` and `sample_stacks.ps1`, and confirm flags in `aot_manifest.json`. Use `SUYU_AOT_FASTMEM=1`, `SUYU_AOT_FM_INLINE=1`, `SUYU_AOT_SPLIT_MAIN=1`; add chain flags only for chain experiments.
- For Orca workers, write the specification with the Write tool, start the requested model/effort, submit an unobserved turn through its terminal handle, wait once for `worker_done`/`escalation`, verify the diff, release the worker, and acknowledge the delivery. See `CLAUDE.md` for role rules.

Repository hygiene: six `.pyc` files are tracked and `.gitignore` has no `pycache` entry; the user must decide whether to untrack them. The displaced plan text is archived under [the fifteenth-session cleanup](OLD_HISTORY.md#archived-on-2026-10-08-from-work_planmd-fifteenth-session-cleanup).
