# Aevora Work Plan

Handoff prepared: 2026-10-09. HEAD is `575b796` on `main`; overlay stages D and E are committed, and the user reported the Debug window working “perfectly” on PLA after a fresh full export. The user commits, tags, and pushes. Start at [Session handoff](#session-handoff-2026-10-08-sixteenth-session); historical detail is in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

| Area | Current status |
|---|---|
| Milestone 4a | Topology, registry, loader, split exporter, persistent cache, and U1-U4 synthetic work are complete for covered cases. |
| Step 6 PLA | Static AOT + FMI + main-DLL split runs; Kotobuki remains 17-21 FPS versus 30 FPS under JIT. The all-default PLA image size is not yet measured. |
| U1 and chaining | U1, default-on direct B/BL chaining, and default-on indirect RET/BR/BLR chaining are implemented, reviewed, and synthetic-fixture verified. |
| Verification | Smoke, cache matrix (308 passed/0 failed), negative controls, and host build passed; chain-run FPS is unreported. |

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
- Direct chaining: `g_emit_direct_chain` emits default-on same-module B/BL tail calls through `RECOMP_TAIL_CALL`; conditional branches remain unchained.
- Indirect chaining: `g_emit_indirect_chain` handles default-on same-module RET/BR/BLR targets through `recomp_lookup`; misses return to `run_slice`.
- FMI and both chain flags are on unless exactly `0`; each changes generated output or its manifest identity. The split default binds `main` as a DLL wherever supported and can be disabled with `SUYU_AOT_SPLIT_MAIN=0`; fixtures and cache matrix are verified.

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

## Session handoff (2026-10-08, sixteenth session)

### State and completed work

HEAD is `575b796`. Overlay stages D and E are committed; after a fresh full export, the user reported that the Debug window worked “perfectly” on PLA. This is a user observation with no itemized detail or performance numbers; the host build and smoke run pass, including the new overlay logic and build-identity tests.

The default-on environment change is implemented through `src/common/env_option.h`: unset/empty/other text keeps the flag options on, exactly `0` turns them off, and split uses a validated default/0/1 choice. The host rebuild passed with no AOT/overlay variables initially set; clang-cl and MSVC `cl` smoke passed, eight env-option negative controls were caught, exporter-wiring negative controls were caught and restored byte-identically, and the final integration runs passed 24/0 default scenarios and 308/0 AOT-cache scenarios. A real PLA export with the new defaults has not yet been run.

### Open issues, in priority order

1. The user must commit this change, then run a new full export to see the defaults in a PLA launcher and measure its `SizeOfImage` (including LNK1248 risk); ask the user for FPS and the detail of the hardware check if wanted.
2. Map hot main-DLL RVAs and investigate the idle-core wait from the previous handoff; these remain open and unchanged, and the overlay's guest PCs do not replace them.
3. Implement planned Settings -> Controller / Graphics ImGui windows and keyboard forwarding.
4. A Hybrid default-split export loses module-level JIT fallback until `SUYU_AOT_SPLIT_MAIN=0`; consider an automatic all-static re-plan.
5. Finish the Step 6 checklist above.
6. Consider U2 only with explicit user approval.

### Immediate next action

The user must commit this change before the persistent cache can produce a launcher with the new defaults; then run a new full PLA export and measure its image size.

## How to work / procedures

- Run `.cmd` helpers through PowerShell's `cmd.exe /c "D:\HisuiWakeBuild\diag\buildsuyu.cmd > D:\HisuiWakeBuild\diag\<log> 2>&1"`; read logs for ASCII `error C` and `BUILD_EXIT`. Run `runsmoke.cmd` once per finished fix set and require both smoke pass markers; a first-pass new test needs a deliberately weakened negative control.
- The overlay smoke tests need `nlohmann/json.hpp`; pass `--nlohmann-include`, set `NLOHMANN_INCLUDE_DIR`, or provide a `CPM_SOURCE_CACHE` containing `nlohmann_json/*/include`.
- The overlay is on by default; set `SUYU_DEBUG_OVERLAY=0` to disable it. Empty and other values keep it on.
- For cache integration, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, use a fresh short work directory, run `--only-aot-cache-matrix`, then rebuild. Keep logs/builds/exports outside the repository.
- For PLA, find the largest-working-set `Pokemon Legends_ Arceus` PID, run `sample_raw_kotobuki4.ps1` and `sample_stacks.ps1`, and confirm flags in `aot_manifest.json`. Defaults are on; set `SUYU_AOT_FM_INLINE=0`, `SUYU_AOT_DIRECT_CHAIN=0`, `SUYU_AOT_INDIRECT_CHAIN=0`, or `SUYU_AOT_SPLIT_MAIN=0` to disable an option. Chain experiments now switch a chain off to compare.
- For Orca workers, write the specification with the Write tool, start the requested model/effort, submit an unobserved turn through its terminal handle, wait once for `worker_done`/`escalation`, verify the diff, release the worker, and acknowledge the delivery. See `CLAUDE.md` for role rules.

Repository hygiene: six `.pyc` files are tracked and `.gitignore` has no `pycache` entry; the user must decide whether to untrack them. The displaced handoff is archived under [the sixteenth-session replacement](OLD_HISTORY.md#archived-on-2026-10-08-from-work_planmd-sixteenth-session-replacement).
