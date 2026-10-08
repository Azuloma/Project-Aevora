# Aevora Work Plan

Handoff prepared: 2026-10-08. HEAD remains `8818bd3` on `main`; uncommitted U1 runtime and Markdown work exists, and the user commits, tags, and pushes. Start at [Session handoff](#session-handoff-2026-10-08-end-of-thirteenth-session); detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Handoff status

| Item | Evidence/status |
|---|---|
| Git baseline | `main` at `8818bd3`; uncommitted U1 source/test and Markdown work exists. |
| Milestone 4a | Steps 1-3, 4(a)-(c) part 1, 4(d) U1-U4, and covered Step 5 cases are done on synthetic fixtures. |
| Persistent cache | Synthetic `--only-aot-cache` passed 272/0; build-kit package suite passed 14 tests. |
| Step 6 PLA | Static AOT + FMI + main-DLL split exported and ran; Kotobuki is 17-20 FPS versus 30 FPS JIT. Remaining measurements/items are open. |
| U1 exclusive access | Implemented and reviewed through three rounds; standalone test passed. PLA effect is not measured. |

## Key facts

- Windows baseline: MSVC 14.51/Build Tools 2026, Ninja 1.13.2, CMake 4.4.3, clang-cl 22.1.8, Python 3.14.7, Vulkan SDK 1.4.363.0; use a short ASCII `CPM_SOURCE_CACHE`.
- The Step 6 exporting process had `SUYU_AOT_FASTMEM=1`, `SUYU_AOT_FM_INLINE=1`, and `SUYU_AOT_SPLIT_MAIN=1`; effective backend was `suyu-static`, fallback was false, image ABI/features were 6/7, and the bundle id was `a064231234e1da3dc7560138bf901fabef907c3182811f4422ef1cf6a716299e`.
- The launcher was 471,994,880 bytes (`SizeOfImage` 479,465,472); main DLL was 1,857,218,560 bytes (`SizeOfImage` 1,870,344,192; 1,783.7 MiB). The export linked and ran without LNK1248. Producer revision was `7db8f01` (dev16).
- Observed FPS: intro and early pier stable at 30; Kotobuki unstable at 17-20; matched JIT spot 30.0. GPU utilization was JIT 30-33% and AOT 18-25%.
- At Kotobuki, recompiled/JIT CPU samples were Core0 76.3/76.8%, Core1 80.4/78.7%, Core2 81.4/77.2%, HostTiming 98.5/98.8%, GPU 18.8/27.7%, VulkanWorker 9.2/14.1%. This is roughly 1.8x core time per frame at 17 versus 30 FPS; the JIT cap may make the true gap larger.
- RIP sampling found 46-55% in main-DLL generated code, 17-31% idle wait, about 7% Dynarmic SpinLock page, and about 3.4% per core in the guest-address lookup helper. Stack sampling identified idle core waits, not GPU/fence waits; the GPU thread waited 83% in its command queue. A GPU synchronization fault is not supported by this evidence.
- SpinLock breakdown was about 6.8-7.3% per core: pause 1.7-2.1%, acquire xchg 2.7-3.2%, release xchg 0.9-1.2%, redundant mfence 1.0-1.4%. The JIT's equivalent lock is inline and is not comparable as a separate page. Removing all exclusive-monitor cost is estimated at 1-2 FPS, not measured.
- Step 6 did not run FMI-off, per-image comparison, link/hash/publish timings, loader-name check, Hybrid publish/reuse, or cache-entry size measurement. Resolution-lowering FPS testing was suggested but not done.

## Milestones 1-3 and 4a

Milestones 1-3 record environment, exporter/build issues, generated-code loading, and gap diagnostics. Milestone 4a's topology, registry, loader, split exporter, cache, and synthetic U1-U4 work is complete for covered cases; real loader/refusal and PLA acceptance remain open. Accepted D1-D7 remain authoritative: one exporter-owned topology defines bindings, names, bundle, ABI, and features; generated code is never edited. See [design note](OLD_HISTORY.md#design-note-accepted-2026-10-06-draft-3).

## Milestones 5-6

Milestone 5 features are up to 4K, ultrawide, correctly timed 60 FPS, ambient occlusion, increased LoD/draw distance, and additional vegetation; none is accepted. Milestone 6 requires a clean-checkout build, release-input audit, GPL/source instructions, defined `devN`, and evidence-aligned README/changelog.

## U1 exclusive-monitor change

U1 adds a side-effect-free plain-page pointer, lock-light reservation/load or compare-and-swap wrappers, conditional write notification, page-aware slow-path fallback, and removes the redundant unlock `mfence`. It includes `tests/exclusive_access`; host builds passed after each revision and the standalone test passed exact u8/u16/u32/u64/u128 counters, reservation semantics, isolation, upper-half compare failure, attempted reporting, and a race-free negative control. Review round 3 approved with no findings. PLA, integration, recompiler smoke/fastmem/FPX, and Core::System notification coverage were not run.

U2 (lock-free reservation publish) needs explicit user approval and tolerates an ABA-class identical-value race; U3 would inline ldxr/stxr through FM1 with an ABI/version/handshake/hash change. Generated-code quality, lookup misses, and FMI comparison remain separate investigations.

## Remaining Step 6 checklist

- [ ] Put U1 into a launcher using the user's selected relink-only or full-export route.
- [ ] Repeat the same Kotobuki position with FPS, per-thread CPU, RIP, and stack samplers.
- [ ] Compare FMI-off and FMI-on runs and record each image's `SizeOfImage`.
- [ ] Record link-time, hash-time, publish/copy-time, cache-entry size, and loader inventory names.
- [ ] Test Hybrid publish/reuse and confirm fallback transitions separately from strict static results.

## Persistent cache testing rule

The cache is under `%APPDATA%\\suyu\\aot_export_cache`; its producer is `Common::g_scm_rev` plus branch and its launcher is part of the entry. An uncommitted runtime change therefore cannot be observed through a reused launcher. A committed revision intentionally causes a producer mismatch and a full regeneration; do not edit or spoof producer metadata. The proposed relink-only experiment must use a copied package and backup, then the host export variables must be cleared before the normal host rebuild.

## Older open work

The two-application-lifetime seam still needs bind/unregister coverage and the smallest test-only `Core::System` dependencies. `tests/export_regression` still needs its CPM Qt-root lookup corrected. Older held work includes MSVC-route LNK1248 injection, real NSO/NSP/XCI and `ArmRecomp` refusal paths, stub identity-only failure, static-module injection, generic MSVC launcher fallback, extra cache failure cases, and the earlier export/GPL/rtld/BUILD.md/version investigations.

## Evidence boundary

PLA observations are user-run, scene-specific, and not acceptance of general compatibility or a release. Generated-code and ABI behavior are not changed by U1, but the host/runtime path and its launcher must be measured before drawing a performance conclusion.

## Session handoff (2026-10-08, end of thirteenth session)

### State

HEAD is still `8818bd3` on `main`; uncommitted U1 source/test and Markdown work exists. The user commits, tags, and pushes. Step 6 evidence is user-run and specific to Kotobuki Village; U1's PLA effect is not measured.

### What was done

The user ran static AOT + FMI + main-DLL split with strict static classification and zero JIT transitions. It linked and ran with the recorded manifest and image sizes, but Kotobuki was 17-20 FPS versus 30 FPS for JIT. RIP/stack sampling localized substantial generated-code and SpinLock cost and did not support a GPU synchronization fault. U1 was implemented, verified by the standalone test and host builds, and approved after three review rounds; the cache/runtime launcher constraint and unmeasured items are recorded.

### Open issues, in priority order

1. Ask how to put U1 into a launcher: relink-only against the existing persistent cache, or commit then full re-export.
2. Measure Kotobuki FPS and SpinLock sample share with U1 in the launcher; then decide U2 (explicit approval required) or generated-code investigation.
3. Finish Step 6: FMI-off/per-image comparison, timings, loader inventory names, Hybrid publish/reuse, and cache-entry size.
4. Two-application-lifetime seam, real loader/refusal paths, and `tests/export_regression` Qt-root lookup remain open; older issues 4-6 from the twelfth handoff remain on hold.

### Immediate next action

Ask the user to choose relink-only or commit plus full re-export. Do not fake the producer identity: an uncommitted runtime change reuses the old cached launcher, while a new commit invalidates the cache and requires a full rebuild/export.

### How to work

- Keep builds, exports, logs, samplers, and game-derived outputs outside the repository; never run concurrent rebuilds/ninja; user commits/tags/pushes.
- Use `D:\HisuiWakeBuild\diag\buildsuyu.cmd` through `cmd.exe /c` with an absolute path and clear the three export variables after export-running tests.
- For PLA sampling, use the listed local samplers; a recompiled image has no PDB, so report DLL samples by RVA. The JIT exclusive lock is inline and cannot be compared with the SpinLock page.
- A standalone test must include a deliberately weakened, data-race-free negative control. Use cache status fields rather than the exporter log tail.

Archived history: see [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-08-from-work_planmd-end-of-thirteenth-session).
