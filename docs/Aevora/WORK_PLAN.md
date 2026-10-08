# Aevora Work Plan

Handoff prepared: 2026-10-08. HEAD is `cba2193` on `main`; the pending dev17 commit contains Markdown only, and the user commits, tags, and pushes. Start at [Session handoff](#session-handoff-2026-10-08-end-of-fourteenth-session); detail is archived in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop rendering and gameplay improvements on a verified runtime. Generation, compilation, or startup alone does not establish compatibility.

## Handoff status

| Item | Evidence/status |
|---|---|
| Git baseline | `main` at `cba2193`; the pending dev17 commit contains Markdown only. |
| Milestone 4a | Steps 1-3, 4(a)-(c) part 1, 4(d) U1-U4, and covered Step 5 cases are done on synthetic fixtures. |
| Persistent cache | Synthetic `--only-aot-cache` passed 272/0; build-kit package suite passed 14 tests. |
| Step 6 PLA | Static AOT + FMI + main-DLL split exported and ran; Kotobuki is 17-20 FPS versus 30 FPS JIT. Remaining measurements/items are open. |
| U1 exclusive access | Implemented and reviewed through three rounds; standalone test passed. PLA launcher observation completed; no material FPS change was reported. |
| Chaining | Direct B/BL and indirect RET/BR/BLR chaining are opt-in, independently flagged, reviewed, and synthetic-fixture verified. |

## Key facts

- Windows baseline: MSVC 14.51/Build Tools 2026, Ninja 1.13.2, CMake 4.4.3, clang-cl 22.1.8, Python 3.14.7, Vulkan SDK 1.4.363.0; use a short ASCII `CPM_SOURCE_CACHE`.
- The Step 6 exporting process had `SUYU_AOT_FASTMEM=1`, `SUYU_AOT_FM_INLINE=1`, and `SUYU_AOT_SPLIT_MAIN=1`; effective backend was `suyu-static`, fallback was false, image ABI/features were 6/7, and the bundle id was `a064231234e1da3dc7560138bf901fabef907c3182811f4422ef1cf6a716299e`.
- The launcher was 471,994,880 bytes (`SizeOfImage` 479,465,472); main DLL was 1,857,218,560 bytes (`SizeOfImage` 1,870,344,192; 1,783.7 MiB). The export linked and ran without LNK1248. Producer revision was `7db8f01` (dev16).
- Observed FPS: intro and early pier stable at 30; Kotobuki unstable at 17-20; matched JIT spot 30.0. GPU utilization was JIT 30-33% and AOT 18-25%.
- At Kotobuki, recompiled/JIT CPU samples were Core0 76.3/76.8%, Core1 80.4/78.7%, Core2 81.4/77.2%, HostTiming 98.5/98.8%, GPU 18.8/27.7%, VulkanWorker 9.2/14.1%. This is roughly 1.8x core time per frame at 17 versus 30 FPS; the JIT cap may make the true gap larger.
- RIP sampling found 46-55% in main-DLL generated code, 17-31% idle wait, about 7% Dynarmic SpinLock page, and about 3.4% per core in the guest-address lookup helper. Stack sampling identified idle core waits, not GPU/fence waits; the GPU thread waited 83% in its command queue. A GPU synchronization fault is not supported by this evidence.
- SpinLock breakdown was about 6.8-7.3% per core: pause 1.7-2.1%, acquire xchg 2.7-3.2%, release xchg 0.9-1.2%, redundant mfence 1.0-1.4%. The JIT's equivalent lock is inline and is not comparable as a separate page. Removing all exclusive-monitor cost is estimated at 1-2 FPS, not measured.
- Step 6 did not run FMI-off, per-image comparison, link/hash/publish timings, loader-name check, Hybrid publish/reuse, or cache-entry size measurement. Resolution-lowering FPS testing was suggested but not done.

- U1 launcher observation: 17-21 FPS average (user-reported); Core0/1/2 77.5/82.5/81.3%, HostTiming 98.8%, GPU 19.5%, VulkanWorker 9.9%; idle wait 27/19/22% of core samples; SpinLock about 3-4% per core, down from about 7%, while new exclusive helpers add about 2-3% per core.
- Direct-chain PLA observation: main DLL 1,894,430,208 bytes and SizeOfImage 1,907,552,256; Core0/1/2 77.0/82.1/83.6%, HostTiming 99.7%; run_slice about 2% and lookup about 1.2-1.5% per core. FPS was not reported.
- Direct-plus-indirect-chain PLA observation: main DLL 1,909,940,736 bytes and SizeOfImage 1,923,067,904 (about 1,834 MiB, about 214 MiB below LNK1248); launcher 487,511,552 bytes; Core0/1/2 75.8/80.8/81.0%, HostTiming 99.3%, GPU 19.1%; idle wait 19-21%, run_slice 1-1.4%, lookup 1-1.5%. FPS was not reported.

Interpretation: block-transfer overhead (run_slice plus lookup) is about 3-4% of samples and is not the main cost; about 46-55% is in generated main-DLL code, with recurring hot RVAs `+0x20B0`, `+0x20D2`, `+0x211A`, and `+0x21AE`. The roughly 20% per-core idle wait remains unexplained. These are observations, not acceptance criteria.

## Chain work

Direct chaining (`g_emit_direct_chain`, `SUYU_AOT_DIRECT_CHAIN=1`) tail-calls in-module unconditional B/BL targets through `RECOMP_TAIL_CALL`; conditional branches remain unchained because an earlier measurement was worse (2.00 versus 1.80 ms/frame). Indirect chaining (`g_emit_indirect_chain`, `SUYU_AOT_INDIRECT_CHAIN=1`) independently handles RET/BR/BLR through `recomp_lookup(c->pc-g_module_base)`, only for same-module blocks while budget remains; lookup misses return to `recomp_image_run_slice` without spending budget. Both are off by default, preserve byte-identical output when off, and add manifest and bundle-id inputs.

`recompiler_smoke` variants `abi6dc`, `ggdc`, `abi6ic`, and `ggdcic` pass. The `--bl` fixture covers all four settings at bases 0 and `0x40000`, including middle-block RET/BR, below-base targets, budget exhaustion, and BLR x30 ordering; structural checks and negative controls passed as intended. The AOT cache matrix passed 194 tests with 0 failures; the host build exited 0. Code review approved indirect chaining after two rounds; direct chaining had two rounds with findings fixed. MSVC `cl`, non-Windows compilers, PLA correctness beyond the observations above, and old manifests without chain fields were not tested.

## Milestones 1-3 and 4a

Milestones 1-3 record environment, exporter/build issues, generated-code loading, and gap diagnostics. Milestone 4a's topology, registry, loader, split exporter, cache, and synthetic U1-U4 work is complete for covered cases; real loader/refusal and PLA acceptance remain open. Accepted D1-D7 remain authoritative: one exporter-owned topology defines bindings, names, bundle, ABI, and features; generated code is never edited. See [design note](OLD_HISTORY.md#design-note-accepted-2026-10-06-draft-3).

## Milestones 5-6

Milestone 5 features are up to 4K, ultrawide, correctly timed 60 FPS, ambient occlusion, increased LoD/draw distance, and additional vegetation; none is accepted. Milestone 6 requires a clean-checkout build, release-input audit, GPL/source instructions, defined `devN`, and evidence-aligned README/changelog.

## U1 exclusive-monitor change

U1 adds a side-effect-free plain-page pointer, lock-light reservation/load or compare-and-swap wrappers, conditional write notification, page-aware slow-path fallback, and removes the redundant unlock `mfence`. It includes `tests/exclusive_access`; host builds passed after each revision and the standalone test passed exact u8/u16/u32/u64/u128 counters, reservation semantics, isolation, upper-half compare failure, attempted reporting, and a race-free negative control. Review round 3 approved with no findings. PLA, integration, recompiler smoke/fastmem/FPX, and Core::System notification coverage were not run.

U2 (lock-free reservation publish) needs explicit user approval and tolerates an ABA-class identical-value race; U3 would inline ldxr/stxr through FM1 with an ABI/version/handshake/hash change. Generated-code quality, lookup misses, and FMI comparison remain separate investigations.

## Remaining Step 6 checklist

- [x] Put U1 into a launcher and repeat the same Kotobuki position with FPS, per-thread CPU, RIP, and stack samplers.
- [ ] Compare FMI-off and FMI-on runs and record each image's `SizeOfImage`.
- [ ] Record link-time, hash-time, publish/copy-time, cache-entry size, and loader inventory names.
- [ ] Test Hybrid publish/reuse and confirm fallback transitions separately from strict static results.

## Persistent cache testing rule

The cache is under `%APPDATA%\\suyu\\aot_export_cache`; its producer is `Common::g_scm_rev` plus branch and its launcher is part of the entry. An uncommitted runtime change therefore cannot be observed through a reused launcher. A committed revision intentionally causes a producer mismatch and a full regeneration; do not edit or spoof producer metadata. The proposed relink-only experiment must use a copied package and backup, then the host export variables must be cleared before the normal host rebuild.

## Older open work

The two-application-lifetime seam still needs bind/unregister coverage and the smallest test-only `Core::System` dependencies. `tests/export_regression` still needs its CPM Qt-root lookup corrected. Older held work includes MSVC-route LNK1248 injection, real NSO/NSP/XCI and `ArmRecomp` refusal paths, stub identity-only failure, static-module injection, generic MSVC launcher fallback, extra cache failure cases, and the earlier export/GPL/rtld/BUILD.md/version investigations.

## Evidence boundary

PLA observations are user-run, scene-specific, and not acceptance of general compatibility or a release. Generated-code and ABI behavior are not changed by U1, but the host/runtime path and its launcher must be measured before drawing a performance conclusion.

## Session handoff (2026-10-08, end of fourteenth session)

### State

HEAD is `cba2193` on `main`; the pending dev17 commit contains Markdown only. U1 and both independently opt-in chain paths are implemented and verified; PLA evidence is user-run and specific to Kotobuki Village, and chain-run FPS was not reported.

### What was done

U1 launcher measurements and direct/direct-plus-indirect chain experiments were run at the same Kotobuki spot under strict static AOT + FMI + main-DLL split. The chain experiments increased image size but left run_slice/lookup at only about 3-4% of samples; recurring hot main-DLL RVAs and an unexplained roughly 20% idle wait remain. Synthetic smoke, BL execution, cache-matrix, negative controls, host build, and code reviews passed as recorded above.

### Open issues, in priority order

1. Identify hot RVAs `+0x20B0..+0x21AE` by disassembly and generated-function mapping, and measure generated-code cost (RVA attribution only).
2. Use a stack sampler to determine what the roughly 20% idle core wait is waiting on.
3. Finish Step 6: FMI-off/per-image comparison, link/hash/publish timings, loader inventory names, cache-entry size, and Hybrid publish/reuse.
4. Consider U2 only with explicit user approval and lower priority because U1 is not the bottleneck.
5. Held items: two-lifetime seam, real loader/refusal paths, `tests/export_regression` Qt-root lookup, MSVC `cl` route for the chain macro, old-manifest reuse, and six tracked `.pyc` files.

### Immediate next action

Disassemble the hot main-DLL RVAs and map them to generated functions. Keep the RVA attribution caveat because the recompiled image has no PDB.

### How to work

- Keep builds, exports, logs, samplers, and game-derived outputs outside the repository; never run concurrent rebuilds/ninja; user commits/tags/pushes.
- Use `D:\HisuiWakeBuild\diag\buildsuyu.cmd` through `cmd.exe /c` with an absolute path and clear the three export variables after export-running tests. The standing export environment is `SUYU_AOT_FASTMEM=1`, `SUYU_AOT_FM_INLINE=1`, `SUYU_AOT_SPLIT_MAIN=1`; add the direct/indirect chain variables only for those experiments.
- For PLA sampling, use the listed local samplers; a recompiled image has no PDB, so report DLL samples by RVA. The JIT exclusive lock is inline and cannot be compared with the SpinLock page.
- A standalone test must include a deliberately weakened, data-race-free negative control. Use cache status fields rather than the exporter log tail.

Repository hygiene: six `.pyc` files are tracked and `.gitignore` has no `pycache` entry; the user must decide whether to untrack them. Archived history: see [OLD_HISTORY.md](OLD_HISTORY.md#archived-on-2026-10-08-from-work_planmd-end-of-fourteenth-session).
