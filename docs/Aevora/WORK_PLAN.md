# Aevora Work Plan

Handoff prepared: 2026-10-09. HEAD is `585c886` on `main`; start at the seventeenth-session handoff below. Historical text moved from the prior handoff and procedures is archived in [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-09-from-work_planmd-sixteenth-session-cleanup).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Status

| Area | Current status |
|---|---|
| Milestone 4a | Topology, registry, loader, split exporter, persistent cache, and covered U1-U4 synthetic work are complete. |
| Step 6 PLA | Static AOT + FMI + main-DLL split runs; prior Kotobuki observation is 17-21 FPS versus 30 FPS JIT; all-default image size and chain FPS are unmeasured. |
| Verification | Host rebuild, clang-cl/MSVC smoke, negative controls, `--only-multimodule` 76/0, `--only-defaults` 24/0, and `--only-aot-cache` 308/0 passed. |

## Evidence so far (observations)

| Run | Core0/Core1/Core2 CPU % | User-reported FPS | Image size |
|---|---:|---:|---:|
| JIT | 76.8/78.7/77.2 | 30 | not measured |
| Static AOT (dev16 launcher) | 76.3/80.4/81.4 | 17-20 | Main DLL 1,857,218,560 bytes |
| + U1 | 77.5/82.5/81.3 | 17-21 | not measured |
| + direct chain | 77.0/82.1/83.6 | unreported | Main DLL 1,894,430,208 bytes |
| + direct and indirect chain | 75.8/80.8/81.0 | unreported | Main DLL 1,909,940,736 bytes; SizeOfImage 1,923,067,904 |

The last image is about 214 MiB below the 2 GiB LNK1248 limit. `run_slice` plus lookup is about 3-4% of samples; generated main-DLL code is about 46-55%, with hot RVAs `+0x20B0`, `+0x20D2`, `+0x211A`, `+0x21AE`. Each core waits idle about 20%; GPU command-queue wait is about 81%. These are observations, not acceptance criteria.

## What the code now contains

- Overlay: forced-bundled ImGui v1.92.9b, Vulkan extension point, present-pass recording under `scheduler.submit_mutex`, and a whole frame on the scheduler worker thread. Vulkan errors disable it; `SuyuCmd::AcquireDebugOverlay` is process-wide and never destroyed because the renderer owns raw-pointer shutdown.
- Input/UI: top-bar Settings and Cheats placeholders plus Debug toggle; bounded SDL-event-thread to UI-thread mouse queue, event-position ownership until release, overlay-only queueing, and Reset on focus loss/minimize. Keyboard forwarding is not implemented; fullscreen draws ImGui's pointer over the UI.
- Debug honesty: policy text only when recompiled code is active; fallback attempts are lookup misses plus unhandled/refused attempts, with none/attempted/refused verdicts. Guest PCs display `near <module>+offset`; PC 0 means no recompiled dispatch recorded. Build identity reads packaged manifest display fields; exporter-only cache fields are not shown. Native status supplies shader-build count.
- Options: `src/common/env_option.h` defines default-on flags (`SUYU_AOT_FM_INLINE`, `SUYU_AOT_DIRECT_CHAIN`, `SUYU_AOT_INDIRECT_CHAIN`, `SUYU_DEBUG_OVERLAY`; exactly `0` disables) and split default/0/1/error semantics (`SUYU_AOT_SPLIT_MAIN`), with unsupported default split quietly skipped.
- Tests: overlay logic, build identity, env-option units, default-option scenarios, multimodule scenarios, and AOT-cache matrix; negative controls and exporter-wiring weakening were caught and restored byte-identically.

## Milestones

Milestones 1-3 and covered Milestone 4a synthetic cases are complete; real loader/refusal paths, PLA acceptance, and Milestones 5-6 remain open.

## Step 6 checklist

- [x] Repeat the Kotobuki position with U1 and samplers.
- [ ] Compare FMI-off/on and record each image's `SizeOfImage`.
- [ ] Record link/hash/publish timings, loader inventory names, and cache-entry size.
- [ ] Test Hybrid publish/reuse and distinguish fallback transitions from strict static results.
- [ ] Ask the user for FPS after each PLA run; chain-run FPS remains unknown.

## Persistent cache rule

The user-data cache producer is git revision plus branch and build id; the launcher is part of the entry. A committed producer change requires regeneration; never spoof producer metadata. After export-running tests, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before the normal host rebuild.

## Held work and evidence boundary

Held items: two-lifetime seam; real loader/refusal paths; `tests/export_regression` Qt-root lookup; MSVC `cl` route for the chain macro; old-manifest-without-chain-fields reuse; six tracked `.pyc` files with no `.gitignore` entry. PLA results are user-run and scene-specific; the recompiled image has no PDB, so DLL samples remain RVA attribution only.

## Session handoff (2026-10-09, seventeenth session)

### State and completed work

Commits this session are `97e32dd` (overlay stages A-C), `0ae08ea` (A-C review fixes), `575b796` (stages D-E), and `585c886` (default-on options, review fixes, tests). The overlay is user-reported working “perfectly” on PLA after a fresh export made before `585c886`; no new-default PLA launcher exists yet. Final verification is host rebuild, clang-cl/MSVC synthetic smoke (`SMOKE_EXIT=0`), `--only-multimodule` 76/0, `--only-defaults` 24/0, `--only-aot-cache` 308/0, and the recorded negative controls.

### Contracts to preserve

- Keep the overlay in the Vulkan present pass and scheduler-worker frame path; preserve the process-wide lifetime seam and disable on Vulkan errors.
- Preserve event-thread ownership from the event's own last-drawn rectangles through release; queue overlay-owned events only; Reset on focus loss/minimize. Add keyboard/text with the same rule.
- Treat default-on options as exactly-`0` off; split is unset=>default where supported, `0` off, `1` explicit request, other values error. Do not claim unsupported default split occurred.
- Debug must say `near`, not `inside`; PC 0 means no recorded recompiled dispatch; attempts count lookup misses plus unhandled/refused paths; zero Hybrid attempts is not proof of static execution.

### Open issues, in priority order

1. Run a new FULL export from HEAD, regenerate the committed-revision cache, measure launcher and main-DLL `SizeOfImage` with all defaults on, and use LNK1248 advice (`FM_INLINE=0`, `DIRECT_CHAIN=0`, `INDIRECT_CHAIN=0`) if needed. Ask the user for FPS and hardware-check details. After export tests run `rebuild-suyu.bat`-style cleanup; never build twice on one tree.
2. Map hot main-DLL RVAs `+0x20B0`, `+0x20D2`, `+0x211A`, `+0x21AE` by disassembly and investigate the roughly 20% idle core wait by stack sampling; overlay guest PCs do not replace RVA mapping.
3. Stage F (not started): Settings -> Controller and Graphics ImGui windows, Graphics Resolution Scale, replacing the Win32 F12 controls; Cheats remains a placeholder. Add keyboard/text routing and test horizontal-wheel sign.
4. Consider automatic all-static re-plan because default-split Hybrid loses module-level JIT fallback; do not silently change the default without evidence.
5. Finish Step 6 and held items. MSVC `cl` chain support is synthetic-smoke-verified only; no real-title run.
6. U2 only with explicit user approval.

### Immediate next action

The user starts the new session by checking `git status`/`git log`, then chooses item 1 (recommended) or another item; ask what to do first before proceeding.

## How to work / procedures

- Do not feed Python or Markdown through shell heredocs when quotes/backslashes are present; write the script with the Write tool and run it. Edit/Write tools are fine; LF working copies are harmless when the repo expects CRLF.
- After `cl`/`clang-cl` produces an exe, wait for the file if it appears a few seconds late. After any export-running test, run the cleanup rebuild; never run two builds on one tree.
- `worker-start` can return unobserved `turnStart`: use `worker-show --dispatch`, confirm with `orca terminal read`, then submit with `orca terminal send --terminal <handle> --enter`; never send keys to a terminal you did not start. `check --wait` may replay the previous delivery; use `--ack`, read JSON as UTF-8, and ignore heartbeat lines.
- Long builds/tests over ten minutes move to the background; wait for the completion notification instead of polling. Throwaway negative-control runners and logs live under `D:\HisuiWakeBuild\ovl` and `D:\HisuiWakeBuild\diag`, outside the repo.
- Use `D:\HisuiWakeBuild\diag\buildsuyu.cmd` through `cmd.exe /c`; clear the three export variables after export runs. Keep builds, exports, logs, caches, and game-derived outputs outside the repository.
- For PLA, use the local samplers, confirm `aot_manifest.json`, and report user FPS. Use cache status fields (`aot_cache_outcome`, `aot_cache_publish`, `aot_cache_reused`), not the lost log tail, as cache evidence.
