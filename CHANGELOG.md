# Changelog

All notable changes to Aevora are recorded here. Versions follow the scheme in [README.md](README.md#releases-and-versioning): release versions `vX.Y.Z`, development versions `vX.Y.Z-devN`. Development entries are not releases and are not recommended for general use.

## v0.0.3-dev16 - Unreleased

*Development entry 16 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added `KProcessPageTable::LogNativeFinalStatistics()`, which writes `native_mem final unsupported_ops=... inconsistency=...` once per native process.
- Added `docs/Aevora/RECOMPILE_DETAIL/CPU_RECOMPILER.md` (a consolidated reference for the ARM64-to-C CPU path) and the Stage 4c design section of `docs/Aevora/N2_DESIGN.md`.

### Changed

- `KernelCore::Impl::Shutdown` now logs the native final statistics before it closes the application process, because the process can outlive that `Close` and `KProcess::Finalize` was the only place that wrote the line. Closing the window with the X button, the only exit the AOT launcher offers, therefore should now write the line; this is not yet observed. `Finalize` calls the same function, so the line is written at most once.
- `WORK_PLAN.md` records the dev15 validation and the 4c-1 split (4c-1a statistics, 4c-1b the `KPhysicalMemory` rename).

### Recorded results (dev15, session 49)

- Both host trees were rebuilt at `0a974bc` with matching `GIT_REV`. JIT native on and no-JIT AOT logged `native_mem enabled` and `native_mem: KPageTable not constructed` and ran for about 270 s and 352 s without a crash or fatal; JIT with `SUYU_NATIVE_MEM=0` logged no `native_mem` lines. The structural gate (report mode) read `kernel_memory` 128 and `jit` 0 on the no-JIT `suyu-cmd-static.exe`. The user declared the dev15 test complete.

### Verification

- JIT host tree rebuilt (305 steps) and no-JIT host tree rebuilt (458 steps), one at a time, both with 0 errors; `suyu.exe` and `suyu-cmd.exe` linked. `ninja -t deps` found no `#deps 0,` entry under `src/core`, `src/common`, `src/video_core`, or `src/hid_core`. No unit test was added: the change sits in the kernel shutdown path and has no standalone form.

### Not run

- No PLA session for this change; the user deferred it. The `native_mem final` line is therefore unobserved on both the X-button exit and a guest exit, and `unsupported_ops=0 inconsistency=0` for dev15 stays unverified. The exported launcher loses the log tail at exit, so the line may still be missing for that reason. The change was not reviewed by the Code reviewer, and no full `run.py` was run. A commit makes the persistent AOT cache unusable, so the next measurement needs reconfigure and a full export.

## v0.0.3-dev15 - Unreleased

*Development entry 15 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added `KNativeAddressSpace`, which owns the shared process layout, `Common::PageTable`, native process-memory dependencies, allocation settings, and the native device-map lock.

### Changed

- Connected the native 39-bit process path to `KNativeAddressSpace` and construct `KPageTable` lazily only for the fallback path. Shared layout, region predicates, address conversion, heap forwarders, and process-table ownership now serve both modes; native processes no longer initialize the block manager or call `KPageTableBase::Finalize`.
- Made the instruction-cache scan and debug-IO paths safe for mixed native/fallback processes; debug-IO returns `ResultNotImplemented` for a native current process.

### Verification

- JIT and no-JIT host trees rebuilt one at a time after the fixes with 0 errors; `suyu.exe` and `suyu-cmd.exe` linked, with only the known `LOG_*` C4005 warnings. `ninja -t deps` found no new zero-dependency entries under the checked source trees. No new unit test was added; the predicates are covered by dev14's `native_address_regions_unit`.
- Forwarder classification was completed: `GetPhysicalAddress` uses the holder's `Common::PageTable`, while the three `Heap*` instance forwarders call static `KPageTableBase` functions; no counted fail-fast stubs were needed.

### Not run

- No PLA session (JIT native on, JIT `SUYU_NATIVE_MEM=0`, or no-JIT AOT), no structural gate against the 127 `kernel_memory` baseline, and no full `run.py`. The fixes were not reviewed a second time; the heap-allocated fallback `KPageTable` lifetime issue remains as the same slab-destructor limitation as the former embedded table.

## v0.0.3-dev14 - Unreleased

*Development entry 14 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the kernel-free `Core::NativeMemory::AddressRegions` holder and its region predicates, getters, and `RegionState` mirror, plus the `native_address_regions_unit` smoke test with 27 mutant negative controls.

### Changed

- Added `RegionState`/`Svc::MemoryState` static assertions to `k_native_memory_adapters.h`; the new holder remains unconnected to kernel logic.

### Verification

- The standalone MSVC unit passed with `All synthetic native address region smoke checks passed.`; all 27 mutants exited 1 at their intended checks. JIT and no-JIT host trees rebuilt one at a time with 0 errors and linked both launchers.

### Not run

- Full `tests/recompiler_smoke/run.py`, a real smoke CMake configure, any PLA session, and the structural gate were not run. The Code reviewer fixes were not reviewed a second time.

## v0.0.3-dev13 - Unreleased

*Development entry 13 toward v0.0.3. Not a release; no build artifacts are published.*

### Changed

- `KPageTableBase::InitializeForProcess` now takes its region placement from `NativeMemory::ComputeLayout` (draw order alias, heap, stack, kmap, via `KSystemControl::GenerateRandomRange`), so the kernel path and the native model share one implementation. The read of `m_address_space_start` before its assignment was always 0 (member default) and is dropped; layout failures keep the old behavior (`ResultOutOfMemory` returned, other failures assert).

### Added

- Layout unit checks for the random draw order and values, the ASLR base offset and the extra alias region, with four new mutants (`layout-aslr-base`, `layout-extra-alias`, `layout-kmap-draw`, `layout-slot-count`). The random-draw test also checks the inclusive bounds passed to the generator.
- `k_page_table_base.cpp` fails the build under `HAS_NCE`, because `NativeMemory::GetSpaceInfo` lacks the 256 GiB Map39Bit limit that `KAddressSpaceInfo` applies there.

### Verification

- `smoke_native_memory_unit` passed under MSVC standalone; the four new mutants failed at their intended checks. A Code reviewer pass (Orca, gpt-6.1-sol) found two P2 items (the dropped `HAS_NCE` limit, and the random-draw test ignoring the bounds); both were fixed and the host trees rebuilt after the fixes. JIT and no-JIT host trees rebuilt with 0 errors.

### Not run

- Full `run.py`, a second reviewer pass on the review fixes, and any PLA session (JIT with native on and `SUYU_NATIVE_MEM=0`) were not run.

## v0.0.3-dev12 - Unreleased

*Development entry 12 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the kernel-free PhysicalPoolSet and native KMemoryManager backend switch, with smoke coverage for 13 set groups and 16 physical-pool groups.

### Changed

- Native mode is default-on via SUYU_NATIVE_MEM_POOL; =0 retains the buddy managers while existing callers remain unchanged until 4c.

### Verification

- Both MSVC unit tests passed; all 11 set mutants and 20 pool mutants failed at their intended checks without hanging. JIT and no-JIT trees rebuilt with 0 errors and linked both launchers.

### Not run

- Full run.py, a real smoke CMake configure, any PLA session, standalone KMemoryManager coverage, and the new-launcher structural gate were not run.

## v0.0.3-dev11 - Unreleased

*Development entry 11 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the standalone `PhysicalPagePool` in `native_physical_pool.h`, a reference-counted single-region page pool with front/back extent allocation, aligned continuous allocation, reserve, open/close, free-run merging, and invalid-region rejection.
- Added `smoke_native_physical_pool_unit` with 15 check groups and 18 negative-control mutants, plus the CMake and `run.py` wiring.

### Changed

- Approved N2 Stage 4 is split into 4a-1 (this unconnected pool), 4a-2 (wiring KMemoryManager users), 4b (small page-table layout and `Common::PageTable` holder), and 4c (no-JIT source removal and fail-fast unsupported callers). Pool-to-KMemoryManager semantics remain for the wiring stage.

### Verification

- The MSVC `/W4 /Zc:preprocessor` unit build passed with no warnings and printed `All synthetic native physical pool smoke checks passed.` (exit 0). All 18 mutants exited 1 at their intended checks without hanging.
- The report-mode no-JIT gate baseline was 1,002,137 symbols and 122 `kernel_memory` matches (72 `KPageTableBase`, 22 `KMemoryManager`, 9 `KMemoryBlockManager`, and about 19 other matches); JIT was 0. Structural acceptance is `structural_gate.py --require-absent N2` on a no-JIT launcher.
- Review findings for region alignment/overflow, permanent reservation, back-position alignment coverage, and missing edge cases were fixed.

### Not run

- The full `tests/recompiler_smoke/run.py`, a real smoke CMake configure, host builds, reference-count overflow, and any PLA session were not run.

## v0.0.3-dev10 - Unreleased

*Development entry 10 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the native guest-memory primary page-table path in `KProcessPageTable`, selected by `SUYU_NATIVE_MEM` at initialization (default on; `=0` disables it), with per-process fallback to `KPageTable` for non-39-bit address spaces.
- Added native implementations for supported permissions, attributes, heap and physical memory, mappings, page groups, transfer/device locks, IPC copies, and memory queries; unsupported operations are counted and fail with `ResultNotImplemented` and an operation/address log.
- Added `KNativeProcessMemory::WithBoth`, stored-permission conversion, physical-page identity checks, and three conversion/accounting mutants.

### Changed

- Native mode is exclusive with the Stage 2 shadow; native wins with a warning when both options are enabled. `KPageTable` remains for layout/query helpers and its `Common::PageTable` implementation.
- Updated the nvmap container to query through `KProcessPageTable::QueryInfo`.

### Verification

- The two native-kernel-convert unit-test groups passed; the three new mutants exited 1 at their intended checks. JIT (304 steps) and no-JIT (298 steps) host trees rebuilt with 0 errors and linked `suyu.exe` and `suyu-cmd.exe`.
- Code-review findings for nvmap device locks, page-group identity, and static/IO mapping plus mixed-source IPC guards were fixed.

### Not run

- The full `run.py`, a real smoke CMake configure, and PLA sessions were not run. Back up save data before 3c because native memory is default-on.

## v0.0.3-dev9 - Unreleased

*Development entry 9 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added kernel-free pure conversions in `native_kernel_convert.h` and kernel adapters in `k_native_memory_adapters.h`: a physical page source over `KMemoryManager`, host mapping over `Core::Memory`, DeviceMemory access, page-group conversion with Open/Close, `KMemoryInfo` conversion, and per-process ownership with resource-limit accounting.
- Added compile-time checks of kernel result and encoding constants, plus the 12-mutant native-kernel-convert unit.

### Changed

- Added `MemoryInfo.device_count` and `KPageTableBase::GetHeapFillValue()`.
- Nothing is switched and no option exists yet; `SUYU_NATIVE_MEM` does not exist in code before dev10.

## v0.0.3-dev8 - Unreleased

*Development entry 8 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the kernel-free Stage 3b-1 native-memory headers: raw kernel-result mapping, state-rule validation, first-fit free-area search, anonymous/physical/linear mapping and copy operations, and generic owned-page finalization. IPC copy bodies are supported; IPC map-alias remains unobserved.

### Changed

- Recorded the Stage 3b-2 adapter plan and Stage 3b-3 switch plan. `SUYU_NATIVE_MEM` does not exist in code before dev10; dev10 will default it on, with `SUYU_NATIVE_MEM=0` disabling native memory.

### Verification

- `native_process_memory_unit` passed with 17 check groups; 46 header mutants exited 1 at their intended checks. `native_memory_unit` and `native_shadow_unit` still passed, and all 66 mutant anchors in `tests/recompiler_smoke/CMakeLists.txt` were unique.
- The reviewer's two P2 and one P3 findings were fixed with tests and mutants.

### Not run

- The full `tests/recompiler_smoke/run.py`, a real smoke CMake configure, JIT/no-JIT host rebuild, and any PLA session were not run; the headers are not included by the host yet.

## v0.0.3-dev7 - Unreleased

*Development entry 7 toward v0.0.3. Not a release; no build artifacts are published.*

### Changed

- Added the standalone native physical-backing and host-mapping layer `Core::NativeMemory::MappedAddressSpace`, backed by injectable physical-page allocation and host mapping interfaces; caller-owned mappings, aliases, transfer locks, device-map locks, physical queries, and finalization are modeled without wiring it into the kernel or host.
- Extended the native address-space shadow with IO memory, executable permissions, process-memory permission, transfer/device lock APIs, and mutation-free validation checks; added the native process-memory smoke unit and 28 mutant targets.
- Documented the N2 Stage 3a/3b/3c plan, Stage 3a results, `SUYU_NATIVE_MEM` option decision, caller inventory, and current limits.

### Verified

- The standalone `smoke_native_process_memory_unit` passed with MSVC 14.51, and all 28 new mutants exited 1 at their intended checks; existing native memory and native shadow units also passed after rebuilding.
- The JIT host tree rebuilt in 304 steps with 0 errors and linked `suyu.exe` and `suyu-cmd.exe`; no-JIT rebuild, full `run.py`, real smoke CMake configure, and PLA session were not run.

### Known limitations / held items

- Stage 3a is standalone and unconnected: nvmap device-map locks, shared/transfer/vi/TLS callers are modeled but not wired, and IPC map-alias/SetupForIpc, MapCodeMemory, and unsafe/insecure operations remain unsupported.
- Physical page allocation remains on `KMemoryManager` in Stage 3 behind `PhysicalPageSource`; primary and shadow modes are mutually exclusive, with primary mode winning with a warning. The writer of extra Locked/Uncached bits remains unidentified.
- Stage 3b must wire `KProcessPageTable` with counted fail-fast stubs, and Stage 3c must run JIT/AOT PLA sessions including bag and quit segments with CPU ms/frame comparison; save-data backup is required before Stage 3c.

## v0.0.3-dev6 - Unreleased

*Development entry 6 toward v0.0.3. Not a release; no build artifacts are published.*

### Changed

- Removed the temporary native-memory-shadow event ring, before/pre/after block dumps, and `std::source_location` event names used to hunt the unidentified writer. Mismatch lines, periodic/final summaries, atomic overlap detection, bystander guards, and the capacity override remain; `LogShadowReports` now runs inside the single shadow-mutex block.

### Verified

- A real JIT PLA session on the dev5 build (`ca825f8`) with `SUYU_NATIVE_MEM_SHADOW=1` recorded 0 `native_mem_shadow mismatch` lines and `sync_failures=0`; final compared/result/state/boundary/skipped counts were MapMemory 26/0/0/8/0, UnmapMemory 3/0/0/0/0, SetMemoryPermission 0/0/0/0/0, SetMemoryAttribute 239/0/0/0/0, SetHeapSize 1/0/0/0/0, MapPhysicalMemory 2317/0/0/71/23, and UnmapPhysicalMemory 1/0/0/0/0.
- JIT and no-JIT host trees built after dev6 with 0 errors; `suyu.exe` and `suyu-cmd.exe` linked. The unit test and mutants were unchanged and not re-run.

### Known limitations / held items

- The physical-op state-only relaxation remains, so zero mismatches does not prove permission/attribute preservation of already-mapped pages for `MapPhysicalMemory`/`UnmapPhysicalMemory`; the writer remains unidentified. The dev6 build has not had a real session, and no AOT/no-JIT session was run.
- A future mismatch reports only its mismatch line and summary; the removed event-ring and block-dump diagnostics must be re-added if writer hunting resumes. Stage 3 (`SUYU_NATIVE_MEM` primary mode) requires user approval.

## v0.0.3-dev5 - Unreleased

*Development entry 5 toward v0.0.3. Not a release; no build artifacts are published.*

### Changed

- Followed up the opt-in native memory shadow in `k_process_page_table.h`, `native_shadow.h`, `native_address_space.h`, `native_shadow_unit.cpp`, the smoke-test `CMakeLists.txt`, and `run.py`. Overlap detection uses one atomic `m_shadow_state` (high 32 bits are entries, low 32 bits are calls in flight); a call that is not alone or whose entry counter changed is counted as `skipped` and is not compared.
- Added 32 reviewed `ShadowBystander` RAII guards to unhooked wrappers that mutate the block table, including two extra source-table guards in `SetupForIpc` and `UnmapProcessMemory`. Guards were removed from 15 read-only wrappers (page-group open, debug read/write, device-address-space range open, cache operation, and memory-copy helpers). The shadow mutex covers only shadow state; no lock is held across kernel calls, and diagnostic dumps run after mutex release.
- Kept a 4 GiB shadow physical-capacity constant. For a real `LimitReached`, the capacity is set to the pre-call normal bytes for that operation and restored with `SetPhysicalCapacityBytes`, so that case agrees trivially. For `MapPhysicalMemory` and `UnmapPhysicalMemory`, only `state` is compared on pages mapped before the call and still non-Free afterward; newly mapped and freed pages receive a full comparison, split at pre-call block boundaries so merged blocks cannot hide a new page.
- Kept the 24-entry event ring and before/pre/after dumps for state mismatches; the `late` dump was removed because it took kernel locks under the shadow mutex. Renamed the Qt-colliding identifier `slots` to `slot_count` (committed in `8133d92`).
- Code review found 1 P1 and 4 P2 findings (mutex-held late dump, state-only comparison crossing a pre-call block boundary, over-guarded read-only wrappers, missing UnmapPhysicalMemory/freed-page coverage, and undocumented physical-op relaxation); all were fixed in this batch.

### Verified

- `native_shadow_unit` now has 17 check functions and passes. Its 20 header mutants (shadow-result, state-field, permission-field, attribute-field, physical-op, was-mapped, state-only, freed-pages, before-step, boundary, user-permission, range-clip, sync-skipped, report-cap, total, skipped, capacity, tiling, heap-end, and physical) all exited 1 under the scratch script runner at `D:\HisuiWakeBuild` (not via `run.py`).
- JIT and no-JIT host trees both built after the final code with 0 errors; `suyu.exe` and `suyu-cmd.exe` linked. A user JIT PLA session with the flag enabled, before the review fixes, recorded 0 mismatch lines and `sync_failures=0`: `MapMemory=26` compared (7 boundary-only), `UnmapMemory=3`, `SetMemoryAttribute=233`, `SetHeapSize=1`, `MapPhysicalMemory=2310` compared (64 boundary-only, 35 skipped), `UnmapPhysicalMemory=1`, and `SetMemoryPermission=0`.

### Not verified / Known limitations

- Full `run.py` and a real CMake configure of `tests/recompiler_smoke` were not run. The dev5 review fixes have not been run in a real session; Stage 3 (`SUYU_NATIVE_MEM` primary mode) must wait for user approval.
- Earlier runs before physical-op relaxation showed 1–4 `MapPhysicalMemory` state mismatches per run on ranges containing already-mapped pages (extra Locked with permission `0x48`, or Uncached on `DeviceShared`, visible only after the call). The writer was never identified: it was not a hooked or guarded call in the event ring, the pre-call snapshot equals the before snapshot, and the change appears by the after snapshot.
- Because of the relaxation, zero mismatches does not prove that native mapping preserves permission and attribute for already-mapped pages in `MapPhysicalMemory`/`UnmapPhysicalMemory`. `QueryMemory` is not hooked, `Common::PageTable` entries are not compared, alias validity still uses alias records, the real run covered JIT only (no AOT/no-JIT session), and diagnostics remain an open user decision.

## v0.0.3-dev4 - Unreleased

*Development entry 4 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the opt-in native memory shadow comparison (`SUYU_NATIVE_MEM_SHADOW=1`) between the native mapper and `KPageTable`. The option is off unless exactly `1`, is read when the process page table initializes, and creates no shadow object or behavior change when unset.
- Added `src/core/native_memory/native_shadow.h`, a kernel-type-free header-only `Core::NativeMemory::Shadow` with per-operation counters, synchronization failure tracking, bounded mismatch reports, and summaries for seven mapping, permission, attribute, heap, and physical-memory operations.
- Added `tests/recompiler_smoke/native_shadow_unit.cpp` and the `smoke_native_shadow_unit` target with 12 checks, plus 14 header-mutant targets covering shadow comparison and address-space adoption invariants.

### Changed

- Added `Result::Other`, `StoredBlock`, and `AddressSpace::ReplaceBlocks` to the native address-space mapper; observed block boundaries are adopted and the heap end is derived from the leading Normal run.
- Added header-only `KProcessPageTable` shadow glue. It observes the real block table and normal-memory size, mirrors each supported operation without holding the shadow mutex across kernel calls, skips overlapping calls, and logs periodic and final summaries. Page-table entries are not compared directly; touched-range block state, user permission, and attributes are compared instead.
- Result/state/permission/attribute differences and non-tiling synchronization failures are mismatches. Base/size differences alone are reported as `boundary_only`, and kernel result codes outside the modeled set map to `Result::Other`.

### Verified

- `smoke_native_shadow_unit` passed with MSVC `cl /W4 /WX` (exit 0); all 14 shadow mutants, rebuilt in scratch copies with the specified header replacements, exited 1 at their intended `CHECK` line. `smoke_native_memory_unit` still passes after the header change.
- The glue compile check for `svc_memory.cpp.obj` succeeded in the JIT release tree (exit 0, no warnings). The acceptance target is zero result/state mismatches and zero synchronization failures on a supported-path play; `boundary_only` need not be zero.

### Not verified

- No full host or no-JIT rebuild, real `tests/recompiler_smoke` CMake configure, full `run.py`, or real session with `SUYU_NATIVE_MEM_SHADOW=1` was completed. User collection with the option enabled is required before calling Stage 2 working.
- Alias validity still uses alias records; unhooked mutators are absorbed by the next re-adoption and are not checked. An unhooked mutator on another core can still cause a spurious state mismatch between observations, while overlap between hooked calls is skipped.
- The shadow uses a 4 GiB physical-capacity constant; a different real resource limit may appear as a `LimitReached` or `OutOfMemory` result mismatch. `Common::PageTable` entries and the QueryMemory SVC are not separately hooked.

## v0.0.3-dev3 - Unreleased

*Development entry 3 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the standalone header-only native address-space mapper `src/core/native_memory/native_address_space.h`, with process-layout computation and `AddressSpace` support for query, code mapping, stack remapping, permissions/attributes, heap sizing, physical/shared memory, TLS, and a fail-fast `SetupForIpc` stub.
- Added `tests/recompiler_smoke/native_memory_unit.cpp` and the `smoke_native_memory_unit` target, plus 14 exact-once header-mutant targets covering the Stage 1 mapper invariants; the 12 earlier mutants are joined by `heap-capacity` and `alias-span`, and the unit adds `UnmapSpansAdjacentAliasRecords` plus extra physical-memory map/unmap cases.

### Verified

- The standalone MSVC C++20 unit build passed (exit 0) and printed `All synthetic native memory smoke checks passed.`; all 14 mutants, built from the CMake function's output in `cmake -P` script mode, exited 1 with their intended failing check. Codex Code review found four P2 items: the mapper now counts any non-Free page as already mapped, unmapping accepts Normal pages with any permission attributes, unmapping spans adjacent alias records whose page pairs were mapped together, and `HeapGrowthIsCappedByPhysicalCapacity` plus `heap-capacity` coverage was added; the block-boundary item remains for Stage 2 because the kernel preserves disable-merge attributes while the mapper only coalesces identical neighbours.
- The unit verified both observed test4 process layouts, 800 randomized layouts across four address-space types, and the observed 3,232,497,664-byte map / 3,228,565,504-byte unmap against a 4 GiB capacity. Stage 1 is not in a CMake source list, so no host rebuild was needed.

### Known gaps

- The full `tests/recompiler_smoke/run.py` suite was not run; CMake wiring was exercised only through a `cmake -P` script-mode run, not a real configure. No host or no-JIT rebuild was done.
- Stage 2 shadow comparison remains unimplemented; block-boundary merging (including whether to model the kernel's disable-merge attributes), the HAS_NCE 256 GiB limit, IPC/transfer/device/code-memory paths, `ProcessMemoryPermission`, physical addresses, and `DeviceMemory` backing remain unmodeled.

## v0.0.3-dev2 - Unreleased

*Development entry 2 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Recorded the test4 memory-inventory collection: `[memory]` segment totals and cumulative `[memory_paused]` boot/pause totals, with the paused-table maximum-count merge rule.
- Corrected region classification by checking heap, alias, stack, and kernel map before the spanning 39-bit code region; recorded test4's randomized process-layout bases and stable sizes.

### Verified

- `smoke_boundary_inventory_unit` passed with MSVC `cl` and `/Zc:preprocessor` (exit 0). The JIT host `rebuild-suyu.bat` completed 24/24 steps with 0 errors and linked `suyu-cmd.exe`.
- Test4 observed 4,359 `MapPhysicalMemory` calls across `[memory]` and `[memory_paused]`, no `ipc_map` rows, five shared-memory objects, four TLS pages, and the listed memory SVC/state combinations; evidence remains local under `D:\HisuiWakeBuild\diag\n0\test4`.

### Not verified

- The full `tests/recompiler_smoke/run.py` suite was not run; the no-JIT tree was not rebuilt; and bag and quit were not segmented. The second `process_layout` is the user's relaunch of the game before the battle segment (ASLR changed the bases).
- Native-only region-base policy and the Stage 1 `Core::DeviceMemory` capacity check remain open; IPC map-alias behavior was not reached on the supported path.

## v0.0.3-dev1 - Unreleased

*Development entry 1 toward v0.0.3. Not a release; no build artifacts are published.*

### Added

- Added the opt-in `[memory]` boundary-inventory diagnostic for memory SVC shapes, IPC map-alias descriptors, TLS pages, shared-memory creation, and process layout ranges. Hook sites are `boundary_inventory.h`, `physical_core.cpp`, `k_server_session.cpp`, `k_thread_local_page.cpp`, `k_shared_memory.cpp`, and `k_process.cpp`.
- Added five `smoke_boundary_inventory_unit` tests, including deliberately weakened size-class and region-end negative controls.

### Verified

- The standalone unit built with MSVC `cl` and `/Zc:preprocessor` and printed `smoke_boundary_inventory_unit: OK` with exit code 0. The JIT host rebuild completed 27/27 steps with 0 errors and linked `suyu.exe` and `suyu-cmd.exe`.

### Not verified

- The new inventory hooks have not been exercised in a real session; the full `tests/recompiler_smoke/run.py` suite was not run, and the no-JIT tree was not rebuilt after Stage 0.

## v0.0.2-dev17 - Unreleased

*Development entry 17 toward v0.0.2. Not a release; no build artifacts are published.*

### Added

- Recorded N1 stage 5 acceptance at HEAD `a0fb2b3`: both trees were reconfigured with matching `GIT_REV`, the no-JIT tree rebuilt with 0 errors, the full PLA export used `suyu-static-nojit`, and the exported launcher passed the N1 structural gate with no `dynarmic` string.
- Recorded the no-JIT acceptance session: PLA ran from launch through exploring the Obsidian Fieldlands for about 110 seconds with zero static-to-JIT transitions, zero JIT-to-static transitions, zero unresolved import traps, 7,486,015,732 static blocks, and no crash or fatal log.

### Changed

- N1 is accepted by the user's request, with the performance caveat that static AOT has improved somewhat but still performs worse than JIT. N1 proves CPU execution without a JIT, not performance parity; closing the gap is later work measured with CPU ms per frame at the same position.
- At the user's request, the launcher title source now names the strict backend `Aevora Static AOT` and omits the ` | JIT transitions: N` suffix for no-JIT builds. This source change was not built or tested in stage 5.

### Not verified

- The exported launcher's title qualifier was not confirmed; the title change requires a commit, both-tree reconfiguration, a no-JIT rebuild, and a full re-export. The exporter dialog, game list, completion messages, and `main.cpp` still say `suyu static AOT (Experimental)`.
- No same-position JIT measurement was taken; the stage used one run and one scene sequence. The no-JIT export-build-kit was not built, the bare-PC fatal logs remain untouched, and the reason `suyu-cmd.exe --help` prints nothing remains unknown.
- The eight unresolved GOT/PLT symbol errors and one pin-count imbalance warning were not compared with a JIT session log.

## v0.0.2-dev16 - Unreleased

*Development entry 16 toward v0.0.2. Not a release; no build artifacts are published.*

### Added

- Recorded the N1 stage 4 exporter/backend selection and cache-identity fix.

### Changed

- The launcher tree now selects the export mode: the JIT frontend serves Hybrid/JIT, while the no-JIT frontend serves strict static AOT. Strict mode rejects modules it cannot recompile; Hybrid's recorded JIT retry remains intentional.
- Cache and manifest backend names distinguish JIT-capable static exports from no-JIT static exports (`suyu-static` versus `suyu-static-nojit`); the integration coverage uses a shared JIT static-backend constant.

### Not verified

- No export or session was run from the no-JIT frontend. The no-JIT export-build-kit was not packaged; it cannot provide a usable Hybrid mode and remains a release follow-up. Bare-PC fatal logs in the recompiler and generated-code guard paths remain module-unnamed.

## v0.0.2-dev15 - Unreleased

*Development entry 15 toward v0.0.2. Not a release; no build artifacts are published.*

### Added

- Recorded the N1 stage 3 no-JIT tree build and structural-gate results.

### Changed

- The no-JIT host frontends now build successfully without dynarmic entries; stage 4 is next for the exporter and build kit.

### Not verified

- The gate was run only on `suyu.exe` and `suyu-cmd.exe`; no exporter, strict launcher, or gameplay session was run on the no-JIT tree. The title bar, standalone monitor session, and `suyu-cmd.exe --help` behavior remain unverified.

## v0.0.2-dev14 - Unreleased

*Development entry 14 toward v0.0.2. Not a release; no build artifacts are published.*

### Added

- Added `NativeExclusiveState` and the `smoke_native_exclusive_unit` test for the dynarmic-free standalone exclusive monitor, including reservation, notification, callback-exclusion, lock-mode, race, and counter coverage.

### Changed

- `StandaloneExclusiveMonitor` now uses per-core reservation slots, a TTAS lock, optional lock-free marks, plain-page host load/CAS fast paths, and post-lock exclusive-write notification instead of one `std::mutex`.
- The stage 2 host rebuild succeeded with 0 errors; the unit passed under MSVC 14.51, and six header mutants failed fast. A host micro-benchmark showed the lock-free mark benefit for spin-wait loads but no demonstrated contended increment-loop gain.

### Not verified

- This path is compiled in every build but only selected by `SUYU_NO_JIT`; no no-JIT build or real session was run. The full smoke suite, `tests/exclusive_access`, ThreadSanitizer, and the standalone-monitor glue with a `Memory` instance were not tested. The no-JIT compile dependencies in `src/core/arm/recomp/arm_recomp.cpp` remain to be addressed in stage 3.

## v0.0.2-dev13 - Unreleased

*Development entry 13 toward v0.0.2. Not a release; no build artifacts are published.*

### Added

- Added `src/core/arm/recomp/recomp_pc_description.h`, which names the recompiler module and offset for a guest PC when a module snapshot is available.
- Added `tests/recompiler_smoke/pc_description_unit.cpp` and the registered `smoke_pc_description_unit`, covering the module-and-offset description contract and its bounds check.

### Changed

- Strict-mode lookup-miss, no-fallback, unimplemented-opcode warning, and unimplemented-opcode critical diagnostics now append the module and offset description.
- Recorded N0 acceptance and the N1 plan in `docs/Aevora/WORK_PLAN.md` and `docs/Aevora/NATIVE_ROADMAP.md`.

### Not verified

- The full `tests/recompiler_smoke/run.py` suite and a real strict session that hits a miss were not run. Unit-test edge cases for a bare PC of 0, `UINT64_MAX`, and 17 or more modules remain uncovered.

## v0.0.2-dev12 - Unreleased

*Development entry 12 toward v0.0.2. Not a release; no build artifacts are published.*

### Added

- Added `docs/Aevora/N0_INVENTORY.md`: the manual check list of the confirmed supported gameplay path (with the segment label of each step and what test1/test2 exercised), the aggregate inventory summary, and the first structural-gate report. It holds counts only; no game-derived names, paths, or addresses.

### Changed

- Closed the N0 record in `docs/Aevora/WORK_PLAN.md` (stages 5b, 5c, 5d, 6, and 7, the results, the local data and tools, the design decisions, and the not-verified list), `docs/Aevora/CODEBASE_INDEX.md` (the module-scan header, the structural gate and its test, the new document), and the N0 status in `docs/Aevora/NATIVE_ROADMAP.md`. N0 awaits the user's acceptance.
- From this entry on, every commit gets its own `devN` section again. The N0 stage commits before it carry no section of their own (see the next entry).

### Recorded results (from the N0 stages, committed before this entry)

- Stage 5b (`b619bf9`) attributes IPC callers through a per-thread stash written at SVC 0x21/0x22; stage 5c (`3c945e4`) writes a local module/NVN name scan; stage 6 (`c338961`) adds `tools/structural_gate/structural_gate.py` with `tests/structural_gate/test_structural_gate.py`.
- Test2 collection: 202,179 IPC calls with 100% caller coverage over 29 `ipc:` keys; all four-frame IPC chains end inside the SDK module. The union of boot1, test1, and test2 (18 reports) shows 32 SVCs, 44 service interfaces with 121 commands, 34 nvdrv rows over six devices, no audio renderer open, and no applet.
- The module scan found 563 NVN strings (524 in the main module) and 15,965 matching dynsym names in the local scan file.
- The structural gate has 34 tests with an independent oracle, and all 56 tool mutants fail the suite. Its first baseline on the current launcher read 1,007,632 public symbols (PDB GUID and age matched the image) and 37 imported DLLs; `--require-absent N1` fails with `FAIL jit`, as expected for the JIT-capable launcher.

### Not verified

- The inventory under the AOT launcher, the CPU stall of the module scan on the first SVC, the asynchronous SVC 0x23 stash limit, IPC attribution beyond the SDK, the gate on Linux, and the typed-label input of the Inventory window remain open. The gate and the scan were not run against a PLA export.

## v0.0.2-dev1 to v0.0.2-dev11 - Unreleased

*Development entries 1 to 11 toward v0.0.2 have no CHANGELOG sections of their own. Refer to the earlier commit log instead: the commits after `28011a2` (v0.0.1-devFINAL) up to `c338961`, run `git log 28011a2..c338961`.*

## v0.0.1-devFINAL - Unreleased

*Final development entry toward v0.0.1 (labelled devFINAL by the user's decision, not a counted devN). Not a release; no build artifacts are published.*

### Removed
- Removed macOS support and the Apple GPU backend: the Metal renderer (`src/video_core/renderer_metal`), the SDL Metal window (`emu_window_sdl2_mtl`), `externals/metal-cpp`, MoltenVK acquisition (`cpmfile.json`, `CPMUtil.cmake`, `DownloadExternals.cmake`, `externals/CMakeLists.txt`, `YUZU_USE_BUNDLED_MOLTENVK`) and the MoltenVK Vulkan workarounds (`Device::IsMoltenVK`, portability enumeration, Cocoa surface, the 16-binding limit, the primitive-restart override, the render-target type override and the texture-copy fallback), the dynarmic macOS exception handler and Rosetta check, the Mach/CoreFoundation/IOKit branches in `src/common`, the macOS bundle assets (`src/suyu/Info.plist`, `dist/suyu.icns`, `dist/Assets.car`, `dist/icns_generator.sh`) and `docs/Suyu/user/RunOnMacOS.md`.
- Removed the macOS export target (`GameExportDialog::TargetPlatform::MacOS`, `PackagePolicy::Platform::MacOS`), the `macos` and `libretro-macos` package-policy kinds with the framework-symlink allowance, and the MoltenVK dependency-source symlink exemption.
- Removed `WindowSystemType::Cocoa`, `GameSettings::OS::MacOS`, `Common::FS::GetBundleDirectory`, `GRenderWindow::InitializeMetal`, the `CXX_APPLE` and `CMAKE_OSX_ARCHITECTURES` multiarch handling, and the macOS-specific CMake conditions (`UNIX AND NOT APPLE` became `UNIX`).

### Changed
- Removed macOS wording from the generated CMake comments and from the generated runtime-header comment in `src/core/recompiler/arm64_to_c.h`; the three smoke golden hashes in `tests/recompiler_smoke/run.py` were recomputed. Only `CMakeLists.txt` (all 18 variants, both modules) and `recomp_runtime.h` (the 14 GG1 variants) moved; no block source moved.
- Shortened the comments this change touched to two lines.

### Kept
- Linux and FreeBSD support, third-party sources under `externals/` (glad, sse2neon, microprofile, libusb sources), the `dist/languages` translation data (removed with the Qt GUI later), `GPUVendor::Apple` (Asahi Linux reports an Apple GPU), the `.gitignore` Finder entries, and the archived upstream pages under `docs/Suyu`, which still describe macOS as upstream history.

### Compatibility
- No persisted setting or config key was removed; `RendererBackend` values are unchanged. The removed enumerators shift the numeric values of later `WindowSystemType`, `GameSettings::OS` and log-class entries; they are only used symbolically or in log lines. The CMake option `YUZU_USE_BUNDLED_MOLTENVK` no longer exists.

### Verified
- A Windows reconfigure left `build.ninja` and `compile_commands.json` byte-identical to the pre-change files (1726 records).
- All 1718 Windows translation units were preprocessed (`/EP`) before and after the source edits: 1594 identical, 6 differing only by `__LINE__` shifts derived from `git diff`, 116 differing only by the intended removals (every added and removed line was listed and checked); two moc autogen units could not be preprocessed in either run. Self-test negative controls (changed constant, string, hex value, wrong shift, removed line) were all rejected.
- The host rebuild (`rebuild-suyu.bat`) ran twice (before and after the review fixes); both logs had 0 compiler errors and linked `suyu.exe` and `suyu-cmd.exe`.
- Full `tests/recompiler_smoke/run.py` exited 0 with the recomputed golden hashes; the old generator reproduced the old hashes exactly, which validated the comparison harness. `tests/exclusive_access/run.py` exited 0 against the rebuilt host.
- `tests/package_policy`: 55 Python tests passed and the C++ `policy_unit` reported 193 checks, 0 failures. The new `test_zip_symlinks_are_rejected` was shown to fail when `Scanner.check_links` is disabled (reviewer probe).
- Code reviewer found no P1 or P2 issue and four P3 items (leftover `CMAKE_OSX_ARCHITECTURES` handling, the `CPPHTTPLIB_DISABLE_MACOSX_AUTOMATIC_ROOT_CERTIFICATES` define, the `macos-universal` helper text, the minicoro platform table, the broken `RunOnMacOS.md` link, the stale index entries, and touched comments over two lines); the code-side items were fixed and the documentation items are covered by this batch.

### Not verified
- Linux and FreeBSD configure, build and execution (not available on this machine); the changed Linux-only branches were checked by review only.
- Windows gameplay, rendering and the PLA export were not run. An export made before the next commit still carries the previous producer revision; commit, reconfigure and a full export are needed before the next measurement.
- `research/tests` was not run (7 failures and 1 error are recorded as pre-existing).

## v0.0.1-dev29 - Unreleased

*Development entry 29 toward v0.0.1. Not a release; no build artifacts are published.*

### Added

- Added the bottom-left performance HUD, drawn as ImGui foreground-list text with no window, input, or capture region. It is off at every launch and toggled by F12; it uses the default Roboto font, white text at alpha 0.70 (`kPerfHudTextAlpha`), no background or shadow, an 8 px scaled margin, and the line format `CPU: XXX.XX%, GPU: XXX.XX%, GMem: XXX.XX%, MMem: XXX.XX%, SysMem: XXX.XX%`; unavailable values print `N/A` (never 0%), and all values are N/A for the first second after switching on.
- F12 is claimed by `HotkeyKeyTracker` (`Route::ToggleHud`, `FullscreenKeys::perf_hud`, and `kNoScancode`) only for a window with a HUD (`EmuWindow_SDL2::HasPerfHud`/`ActiveHotkeys`), which is the Vulkan window with an overlay. With `SUYU_DEBUG_OVERLAY=0` or on OpenGL/Null windows, F12 still reaches the game. F12 works while an ImGui window holds the keyboard; the game never sees its press, repeat, or release, and HUD visibility does not use the UI action queue (`PerfHudVisibility` is a single-writer atomic).
- Added CPU, GPU, GMem, MMem, and SysMem sampling. CPU uses process `GetProcessTimes` deltas divided by wall time and logical cores; GPU uses the PDH `\GPU Engine(pid_<pid>_*)\Utilization Percentage` counters, summed per engine type and reported from the busiest type; GMem uses `VK_EXT_memory_budget` device-local heap usage/budget; MMem uses application `KProcess` physical-memory totals; and SysMem uses `GlobalMemoryStatusEx` total minus available over total. A failed CPU read gives `N/A` (`CpuPercentBetween`).
- Added `src/suyu_cmd/overlay/perf_hud.h`, `perf_hud_sampler.{h,cpp}`, `tests/recompiler_smoke/perf_hud_unit.cpp` (`smoke_perf_hud_unit`), and HUD-key ownership cases with a negative control in `fullscreen_toggle_unit.cpp`. Sampling runs on `PerfHudSampler`'s own thread once per second (`kPerfHudSampleIntervalMs`) only while visible, and the thread is joined in `DebugOverlay::Shutdown` before the Vulkan device goes away. CPU/GPU/SysMem are Windows-only; `suyu-cmd` links `pdh` on `WIN32`.

### Changed

- The overlay now uses embedded Roboto as the primary Latin font with BIZ UDPGothic Regular merged behind it for Japanese glyphs; this supersedes dev28's BIZ-only wording.
- Later normal commits `cf07cb2` and `941a465` changed the HUD to 150% text size in embedded Roboto Bold.
- `tests/recompiler_smoke/bundle_host.cpp` `wmain` now calls `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)` so the unloadable-DLL case (`dll-invalid`) fails through the loader diagnostic instead of the modal "Bad Image" dialog.
- Documentation commits `a1e9240` and `71879f9` are included in this milestone; the implementation commits are `4c711ee`, `ffdff12`, `16f899b`, and `10fad30`.

### Verified

- In a scratch folder, now removed, `perf_hud_unit` and `fullscreen_toggle_unit` built standalone with `cl /W4` and exited 0 before and after the review fixes.
- `rebuild-suyu.bat` reconfigured and built `suyu` + `suyu-cmd` (1595/1595, "Build succeeded") with the HUD; after the review fixes `buildsuyu.cmd` built 9/9 steps with `BUILD_EXIT=0`. No warnings were found in touched files; one pre-existing C4005 `WIN32_LEAN_AND_MEAN` redefinition at `emu_window_sdl2.cpp:148` was untouched.
- Code review by the Orca Code reviewer (`gpt-6.1-sol`, high) of `ffdff12` found 3 medium and 1 low findings, all confirmed and fixed in `16f899b`: F12 consumed when no HUD exists, F12 ignored while ImGui had the keyboard, failed `GetProcessTimes` reads producing 0%/100%, and a 3-line comment. The reviewer found no sampler/Shutdown race, no null resource-limit path, and one VulkanOverlayContext construction site.
- `python -B tests/recompiler_smoke/run.py --nlohmann-include D:\HisuiWakeBuild\cpm\nlohmann_json\v3.12.0\include` (MSVC, after vcvars64 through `cmd.exe`, `CPM_SOURCE_CACHE=D:\HisuiWakeBuild\cpm`) first failed at bundle-host `dll-invalid` with a 60 s `TimeoutExpired` because Windows showed a modal "Bad Image" dialog in the non-interactive session; the loader code and tests in that area had not changed, and whether the dialog came from Windows build 10.0.26300 was not determined. After the bundle-host fix, the full run exited 0 with 32 "smoke checks passed" lines, including PERF HUD, FULLSCREEN TOGGLE, and the 9 three-module split cases.
- User-reported hand test after the host rebuild and export (2026-10-18, after normal commits `cf07cb2` and `941a465`): the HUD was 150% size and readable in bold; F12 toggled it and values changed from `N/A` to numbers after switching it on; CPU/GPU/system memory roughly matched Task Manager; and F12 worked while an ImGui window was open.

### Not verified

- `SUYU_DEBUG_OVERLAY=0` F12 routing, OpenGL/Null windows, FPS/overhead effect, and `tests/exclusive_access/run.py` remain unverified (`run.py` was not run; the HUD does not touch that code). No numerical results were reported.

## v0.0.1-dev28 - Unreleased

*Development entry 28 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added BIZ UDPGothic Regular as the embedded ImGui overlay font. It is unmodified from `google/fonts` `ofl/bizudpgothic` at commit `6ce172f74aa355ea43eb964fa4a91570a4d3064d`; the TTF is 4,669,688 bytes with SHA-256 `258d7156c165f2ff774b6efee637c22c3b950de0d8a10e501137061bc8085d01`, and its name table carries the copyright and license. The font is licensed under SIL OFL 1.1 by Morisawa Inc. and Type Bank Co.; the copyright is `2022 The BIZ UDGothic Project Authors`.
- Added `CMakeModules/EmbedBinary.cmake`, which generates little-endian uint64 words and the TTF size in `dist/suyu_cmd/overlay_font_data.cpp` at build time. The `aevora_overlay_font` static library is linked by every `suyu-cmd` target, and the build kit freezes it with the other in-tree libraries using static reading only.
- Added `src/suyu_cmd/overlay/overlay_style.{h,cpp}` as the single source for overlay style, UI scale, theme colors, and font loading. The style uses a 1.25 scale, 13 px base font, a `#46478B` theme, 6 px window rounding at 100%, and a transparent main-menu-bar window background so only `MenuBarBg` is drawn; font-load failure logs an error and falls back to ImGui's default font.
- Added `tests/recompiler_smoke/overlay_style_unit.cpp`, registered as `smoke_overlay_style_unit`, checking theme colors and alpha rules, 125% scaling, and equality between embedded bytes and the TTF, with negative controls for opaque bars, fixed alpha, swapped channels, flipped bytes, and truncated bytes.

### Changed
- Fixed the menu-bar background alpha rule after review found `WindowBg` composited under `MenuBarBg` at about 0.996 effective alpha; the window background is now alpha 0. Buttons remain opaque, hovered/active buttons are lightened by 12%/25%, closed Resolution Scale and Device combo frames use the theme, and open lists and the frame-time graph keep default colors. Fixed window sizes and column widths now use `Scaled()`; the 20 px hover reveal strip remains intentionally unscaled.

### Verified
- `rebuild-suyu.bat` exited 0 after the review fixes; logs are `D:\HisuiWakeBuild\logs\build_dev28.log` and `D:\HisuiWakeBuild\logs\build_dev28b.log`.
- `overlay_style_unit` was built standalone with `cl` in a scratch folder and exited 0. The full `run.py` suite was not run, and the unit does not test ImGui style application or drawing.

### Not verified
- The overlay's font, 125% scale, colors, translucency, and rounding still need the user's hand test with host `suyu-cmd`. An exported build needs a commit, reconfigure, and full export first.

## v0.0.1-dev27 - Unreleased

*Development entry 27 toward v0.0.1. Not a release; no build artifacts are published.*

### Removed
- Removed Android and iOS support: `src/android` (Gradle app, JNI, resources), `src/common/android`, `research/ios` (iHorizon prototype), Oboe and iOS AudioQueue sinks, Android filesystem/input drivers, freedreno debug, libadrenotools/Oboe/Android VVL acquisition (`cpmfile.json`, `externals`, `.gitmodules`, `.patch/libadrenotools`), Android/iOS CMake options (`SUYU_ANDROID_LIBRETRO`, `YUZU_DOWNLOAD_ANDROID_VVL`, `SUYU_BUILD_IOS_CORE_PROBE`, `IHORIZON_GAMEPLAY`), `__ANDROID__`/iOS branches in `src`, `src/dynarmic` and vendored externals, Android-only tools and metadata (`.reuse/dep5`, `.codespellrc`, translation config, `package_policy` kinds/notices), and the orphaned `Common::HeapTracker`. `SUYU_NO_JIT` stays, default OFF.

### Kept
- Kept macOS, Linux, FreeBSD, non-Android libretro, Nintendo `Service::android`, `YUZU_LEGACY`, Turnip/Qualcomm detection, the shared `SigAction` workaround, and `SetAppDirectory`; license/attribution notices are preserved verbatim.

### Compatibility
- Removed `AudioEngine::Oboe` with Libretro kept at explicit value 5 because enum settings are persisted numerically (value 4 still falls back to the Null sink); removed `WindowSystemType::Android`, `Settings::Category::Android`, `GameSettings::OS` mobile values, `GraphicsContext::GetDriverLibrary`, `PlayerInput::use_system_vibrator`, FFmpeg MediaCodec fields, `PrintMessageToLogcat`, and `EmulatedController` output slot 4 (array 5 -> 4). No Windows config key was removed.

### Verified
- Windows preprocessed comparison of 1219 (batch 1), 1192 (batch 2), and 56 (review fixes) translation units with a token-aware normalizer (literals intact, source-location integers accepted only at LOG/ASSERT positions with diff-derived line shifts) found 0 unexplained differences. Negative controls rejected changed strings, raw strings, non-location integers, and wrong line shifts. Reconfigure left `build.ninja` and `compile_commands.json` (1717 records) byte-identical. `rebuild-suyu.bat` exited 0 with no new warning signatures versus dev26.
- Eight standalone smoke units passed; full recompiler smoke passed with the three golden hashes unchanged; package-policy 55 tests and 193 C++ checks passed; the restored shared shader test passed and failed under a scratch copy with the dedup branch removed. `research/tests`: 7 failures and 1 error reproduce identically on HEAD inputs (pre-existing: obsolete `correctness_revision` string, old generator fragment extraction, offline dependency expectations, MSVC D8016 `/RTC1` vs `/O2`).

### Review
- Code reviewer found no blockers; S1 (normalizer), S2 (microprofile and other attribution notices), S3 (shared shader test restored under `research/tests/fixtures`), and N1 (HeapTracker, unused includes) were fixed and re-reviewed.

## v0.0.1-dev26 - Unreleased

*Development entry 26 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added [IMGUI_OVERLAY.md](docs/Aevora/IMGUI_OVERLAY.md), the developer guide to the Vulkan overlay architecture, input routing, UI contracts, tests, and extension pattern.
- Added F1 ImGui overlay keyboard/text forwarding: owned key presses, bounded UTF-8 text events, focus reset, capture requests, and SDL text-input synchronization.
- Added the F2 bounded FIFO UI action/status channel with event-thread binding, reentrancy guard, detach refusal, loading-time pump, and overflow status.
- Added the F3 Graphics window with Resolution Scale, a shared 13-entry table, save-only next-launch application, and a Qt/table drift check.
- Added the F4 Player 1 Controller window with device selection, mapping, keyboard and Joy-Con actions, binding rows, and a non-blocking five-second rebind session with analog validation.
- Added F5 Mods and folders plus the Game section (title, title ID, version, applet): a background `ModsScanner` reads runtime path-table folders, lists flat `cheat_*` files, coalesces requests, caps entries at 512, distinguishes missing/unreadable folders, and reports up to eight errors.
- Added fullscreen toggle support for F11, Alt+Enter (including keypad Enter), and `View > Fullscreen`, with windowed geometry restore, maximize fallback, hotkey press ownership, and SDL enter/leave-fullscreen status updates.

### Changed
- F6 removed the Win32 F12 debug panel and its `OnKeyEvent` branch; F12 now follows ordinary keyboard routing. The Vulkan overlay remains available through the command bar, while OpenGL and Null retain no overlay fallback.
- Shared controller snapshots and Player 1 binding helpers keep UI reads mutex-protected and route mutations through the event thread; controller changes are reflected in the overlay.
- Overlay popup regions capture the whole display while open, and overlay reset clears ImGui keys and mouse state after focus loss.
- Fixed the command-bar popup bug: a combo popup was treated as a menu, summoning the hidden bar and letting `BeginMainMenuBar` steal focus; the rule now keeps only an already-visible bar alive. A headless replay used the real router, rule, and ImGui 1.92.9b sources, and `overlay_logic_unit` covers hidden-bar popup and hover cases.
- Commits: `22a1045`, `350af77`, `2d1a42e`, `2b219fa`, `468cd47`, `7ab529e`, `6498ed1`, `2fdf3f9`, `59d7338`, `1506b19`, `7138f29`, `7d73d31`, `06c8abb`; F6 is uncommitted.

### Verified
- Standalone tests pass after each stage with negative controls: `overlay_logic_unit`, `overlay_actions_unit`, `graphics_settings_unit`, and `controller_settings_unit`. Real-router mutations checked key-release ownership, reentrancy, detach refusal, same-value skip, and the analog `accept` predicate.
- `suyu-cmd` builds with exit 0 after every stage. The three new CMake registrations and `run.py` calls were added; the CMake-driven test build was not run, and tests were built directly with `cl`.
- One Code reviewer round per stage was completed; F1 six, F2 four, F3 three, and F4 seven findings were fixed.
- F5 standalone tests and the fullscreen unit pass with negative controls; `suyu-cmd` built with exit 0 after each change. The CMake-driven smoke build was not run; tests were built directly with `cl`.
- Fullscreen review fixes covered geometry restore, fallback reporting, modifier ownership, and SDL failure/event handling. The command-bar replay reproduced the old focus transition and verified selection/focus restoration after the fix.
- F6 verification: `buildsuyu.cmd` built `suyu` and `suyu-cmd` with `BUILD_EXIT=0` and no new warnings in touched files (`D:\HisuiWakeBuild\logs\f6_build.log`); `overlay_logic_unit` and `controller_settings_unit` built with `cl` and exited 0. The grep check leaves only unrelated F1-F12 ImGui key mapping, `bootmanager.cpp`, and `nintendo_account.cpp` matches. One Code reviewer round found a comment regression in Joy-Con combine/split descriptions; the Implementer restored the missing result, cleared-slot, and sideways-split details, and `git diff --check` passed.

### Not verified
- The combo/popup fix is confirmed by the user; the user approved F6 after the parity question. F6 itself has not been hand-tested yet. The F6 binaries contain the implemented F6 code, with the later review correction being comment-only.
- `BeginMapping` puts every input engine in configuration mode, which stops value updates; a key or axis held when a capture starts and released during it keeps its stored pressed value after `StopMapping`, so the game can see a stuck input until it is pressed and released again (`src/input_common/main.cpp`, `input_engine.cpp`). Needs an input-backend policy; not fixed.
- `controller_assign.h` has no unit test (it needs `ParamPackage`, which standalone test targets do not link), so the new `only_slot` filter and host wiring (`HandleControllerRequest`, `MakeRebindHooks`) are covered only by pure-logic seams and review.
- The Qt Resolution Scale table remains a second copy (gated by the drift test, not derived).
- Comments longer than two lines still exist elsewhere in touched files (reviewers listed examples in `emu_window_sdl2_vk.cpp`, `suyu_cmd/CMakeLists.txt`); trim them when touched.
- The host was reconfigured and rebuilt at `a9d6c62`; F6 binaries were built from the uncommitted tree. The last PLA export remains `ac96270` and contains none of the overlay work.

## v0.0.1-dev25 - Unreleased

*Development entry 25 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- `SUYU_SPINLOCK_TTAS`, `SUYU_EXCL_LOCKFREE_MARK` and `SUYU_AOT_CHAIN_START` are on by default; only the exact value `0` switches one off (any other text, including `1`, an empty value and unset, leaves it on). The stub and monitor read their variable once per process, so a switch needs a restart. TTAS and the lock-free marks affect the AOT launcher's host-side exclusive path only; the JIT's inline exclusive code is unchanged.
- Chain start (`g_emit_chain_start`) is skipped quietly when `SUYU_AOT_DIRECT_CHAIN=0` made it impossible; only an explicit `=1` warns. The per-image size refusal message now says to set `SUYU_AOT_CHAIN_START=0` to export without chain start entries.
- `tests/export_integration` pins `SUYU_AOT_CHAIN_START` off in its baseline like the other default-on options; the defaults scenarios check unset (on) and `0` (off), and the chain matrix adds `0` with the direct chain on, back to `1`, and the returning bundle id. `tests/exclusive_access/run.py` runs the test once per setting (unset, `0`, `1`, `2`).

### Added
- Added the `SUYU_SPINLOCK_TTAS` test-and-test-and-set variant of the x64 SpinLock stub: `EmitSpinLockLock` gained `bool ttas = false` (an `xchg` acquire, then `pause` plus a plain `cmp dword [ptr], 0` spin before retrying; no effect with waitpkg) and `SpinLockUsesTtas()` reports what the shared stub was emitted with. The default emission, the unlock and the JIT call site are unchanged.
- Added the TTAS checks to `tests/exclusive_access`: the default emission equals the legacy sequence byte for byte, the TTAS emission differs and re-reads the lock word while waiting, both locks exclude while held and keep every increment of three contending threads, and the environment selects the shared stub. Negative controls: a lock that never checks must let the second thread in, an expectation that disagrees with the environment fails, and a broken `cmp` immediate fails the byte check before any execution test that could spin forever.
- Added the opt-in `SUYU_RECOMP_ACCESS_STATS=1` (exact `1`) diagnostic: the host load/store callbacks that recompiled code reaches are classified by reason (tagged, out_of_range, unmapped, debug, rasterizer, page_cross, watched, other) from the page entry, counted per thread, timed with the TSC around the real callback, and reported every 10 s as an `access-stats` line with per-reason counts and `avg_ns_<reason>`. Off by default; when off the bridge callbacks are the unchanged ones and generated code, ABI and exports are untouched.
- Added the opt-in `SUYU_RECOMP_ACCESS_SITES=1` (needs ACCESS_STATS) diagnostic: three host return addresses per callback access are counted in per-thread tables and the busiest 16 stacks are reported as `access-sites` lines with `module+0xrva` (a stack walk per access, so timings are not representative). New files `recomp_access_stats.h`, `recomp_access_sites.h`, `recomp_host_frames.{h,cpp}`.
- Added `smoke_access_stats_unit` and `smoke_access_sites_unit`, registered in `tests/recompiler_smoke/CMakeLists.txt` and `run.py`; they carry negative controls (four weakened classifiers; a site key built from the first frame only).

### Verified
- The user ran a full PLA export at ac96270 into `D:\HisuiWakeExports\_NEW`; the launcher contains ac96270, `SUYU_SPINLOCK_TTAS`, and `SUYU_EXCL_LOCKFREE_MARK`, and the manifest records chain start, cross/direct/indirect chain, FM inline, FP shared and FPX vector as enabled. `recompiled_image.dll` is 1,805,439,488 bytes (1,721.8 MiB) and the launcher is 506,941,440 bytes (483.5 MiB), both below 2 GiB. The user reported one 26 FPS average (about 38.5 ms/frame), about +1.7 FPS over 24.3 FPS (about 41.2 ms/frame) at d0ae37b and near the 1-1.5 FPS same-position noise; no option-level A/B was run, so the result is unattributed.
- Both units pass standalone (`cl /W4`, no warnings); the weakened site-key variant fails as intended; the host (`suyu`, `suyu-cmd`) builds at a304400 and f943708 with exit 0 and no new warnings.
- Measured on PLA with the diagnostics (see WORK_PLAN and OLD_HISTORY, session 29): about 80,000-105,000 host callbacks per second, 98.6% on GPU-tracked pages, about 1.6 core-ms per frame in callback time.
- TTAS, measured at d0ae37b with a fully static export (`static -> JIT: 0`, same position, diagnostics off for FPS): 24.3 FPS off and 24.3 FPS on, both unsampled. Sampled (36,000 RIP samples each), the SpinLock stub page held 2085 samples (5.79%) off and 2115 (5.88%) on, and the host exclusive functions 5.28% off and 5.50% on (within the sampling error of about 0.12 points each). Inside the page the samples moved as designed: after the first `xchg` 917 off versus 559 on, the retry side (re-executed `xchg` off, `pause`/`cmp`/`jne` on) 428 versus 874, the unlock `xchg` 621 versus 613. The wait moved from `xchg` to a plain-load spin, but the lock time in total did not shrink: no speed effect is shown.
- `tests/exclusive_access/run.py` passes in all four settings (unset, `0`, `1`, `2`), each with locked and lock-free marks and exact u8..u128 counters; the host builds with exit 0.
- `--only-defaults` (31 cases) and `--only-aot-cache-matrix` (287 cases) pass with 0 failures. Negative controls failed as intended: chain start defaulting off, and `SUYU_AOT_CHAIN_START=0` ignored with the direct chain on.
- Two Orca Code reviewer rounds on the TTAS change (3 findings: a sleep-based start in the exclusion test, the early exit after the instruction checks, comment length) and on the default-on change (3 findings: the `=0` case needed the direct chain on, outdated opt-in wording in the docs, a three-line comment) were handled in code and tests; the docs findings are covered by the next Markdown batch.

### Not verified
- The full smoke run (`tests/recompiler_smoke/run.py`) and the other existing suites were not run for these commits; no Code reviewer round was taken.
- No speed effect is claimed; the diagnostics are for measurement only.
- A PLA run with the three options on by default now exists, including the 26 FPS reading, but it has no option-level attribution and no long lock-free-mark soak; the monitor's default (`kLockFreeMarks`) is not covered by a standalone test (it needs a `Core::System`; `EnvFlagDefaultOn` itself is covered by `smoke_env_option_unit`). The chain-start size is measured at 1,721.8 MiB for the current main DLL (the earlier indirect-chain-on export was 1,706.8 MiB), but its on/off A/B remains unmeasured.
- The smoke run and FPXV were not repeated for the default-on and TTAS changes; the generator output is unchanged.

## v0.0.1-dev24 - Unreleased

*Development entry 24 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Rewrote generated `recomp_lookup` to use the flat index for in-range, 4-byte-aligned addresses and added `_lookup_search` as the allocation-failure fallback; shortened the generated index comment and recomputed the ABI5, FM1, and FPX goldens.
- The 32-bit index measured 24.5 FPS (24.5/25.0/24.0) against a 24.6 FPS baseline, no speed effect established; index memory was about 65 MiB lower on PLA (computed).
- The legacy `recomp_image_index` export returns no table in new images; the registry entry grew and old registrations are refused by the `entry_size` check. Host dispatch prefers the 32-bit table, and goldens were recomputed.
- The generated-code comments touched by this change were shortened to two lines or fewer. The older all-static launcher and legacy DLL paths still omit index-status diagnostics; dispatch remains correct through `lookup`.

### Added
- Added the `smoke_lookup` synthetic test, `export.cpp --lookup` mode, and the CMake `LOOKUP_DIR` branch.
- Added the 32-bit block-index contract and `recomp_image_index32`, host `recomp_index_view.h`, registry `index32_v1`, launcher index reporting, and smoke units/fixtures for lookup, view, reporting, static registration, XMOD, and a DLL without the optional export.
- This changes generated code and AOT cache identity; no speed effect is established.

## v0.0.1-dev23 - Unreleased

*Development entry 23 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the opt-in `SUYU_EXCL_LOCKFREE_MARK=1` (exact `1`): `DynarmicExclusiveMonitor::ExclusiveRead` marks the reservation with `Dynarmic::ExclusiveMonitor::ReadAndMarkLockFree` (seq_cst store of the address, then the load) instead of taking the global monitor lock. Exclusive stores still take the lock and compare-and-swap against the saved value, so JIT inline paths stay consistent. Off by default; the locked path is unchanged. `tests/exclusive_access/exclusive_access_test.cpp` now runs every case with locked and lock-free marks.
### Fixed
- Review of the lock-free mark (Orca Code reviewer): the x64 monitor's locked `CheckAndClear`, `Clear` and `ClearProcessor` now access the reservation slots through relaxed `std::atomic_ref` loads/stores, so they no longer race with `ReadAndMarkLockFree` as plain accesses (the same machine code on x64). The exclusive-access test returns non-zero right after the deterministic reservation checks fail instead of looping in the counter cases.
### Changed
- `SUYU_AOT_CROSS_CHAIN` (XMOD1) is now on by default like the other AOT options (set `=0` to disable). Where FM1, the indirect chain, or a split export is missing it is skipped quietly; only an explicit `=1` that cannot be met warns. The export-integration baseline pins it off, and the defaults case checks `cross_chain` true when unset and false when `0`. Exports made with the default now carry feature bit `0x8` and a different bundle id than before.
### Verified
- Same-position PLA readings with `SUYU_GUEST_TIME`, `SUYU_RECOMP_EXCL_STATS` and `SUYU_RECOMP_LOOKUP_STATS` on: AOT without cross chain 20 FPS, with it 23 FPS (user averages). Exclusive callbacks took about 4.5 s (no cross chain) and 4.7 s (cross chain) of thread time per 10 s window (about 20 core-ms/frame at 23 FPS; 110/122 ns average per load/store, including about two TSC reads); about 20.5M loads and 20.1M stores per window, 0.5% of stores failing. Dispatches fell from 31.5M to 17.0M per 10 s with cross chain, while sdk<->main exits stay about 8M each way.
- Smoke suite exited 0 (`SMOKE_EXIT=0`). `exclusive_access_test` passed in both modes after the review fixes; the never-marking negative control now ends with `FAILED (9)` and exit 1 without hanging.
- `exclusive_access_test` passed in both modes; with a lock-free read that does not mark, the reservation cases failed (8 checks), so the lock-free cases do detect it. Host build exit 0.

## v0.0.1-dev22 - Unreleased

*Development entry 22 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the opt-in `SUYU_RECOMP_EXCL_STATS=1` diagnostic (`src/core/arm/recomp/recomp_exclusive_stats.h`, wired in `arm_recomp.cpp`): the four guest exclusive bridge callbacks are replaced by timed wrappers, and every 10 s a line `excl-stats window_ms loads stores stores_failed load_ms store_ms load_avg_ns store_avg_ns` is logged (TSC ticks, rate measured per window, lock waits included, summed over threads). Off unless the value is exactly `1`; when off the bridge keeps the untimed callbacks.
- Added `smoke_exclusive_stats_unit` (`tests/recompiler_smoke/exclusive_stats_unit.cpp`) registered in `CMakeLists.txt` and `run.py`.
### Verified
- `exclusive_stats_unit` passed (MSVC standalone) and failed with failed stores deliberately not counted; host `suyu` and `suyu-cmd` built with exit 0. The full smoke suite, an export, and a PLA measurement have not been run. Motivation: AOT samples show about 4.4% of core samples in exclusive-monitor host functions plus about 5% in the Dynarmic SpinLock stub page (WORK_PLAN to follow).

## v0.0.1-dev21 - Unreleased

*Development entry 21 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the opt-in `SUYU_GUEST_TIME=1` ledger (`src/core/hle/kernel/guest_time_ledger.h`, hooked in `PhysicalCore::RunThread` and `PhysicalCore::Idle`): every 10 s per core it logs `guest-time core=N window_ms=... run_pct idle_pct host_pct unattributed_pct slices threads | per-thread run/host | per-SVC host_pct calls`. Run is the time inside `ArmInterface::RunThread`, idle the `PhysicalCore::Idle` wait, and host the gap between runs minus idle, charged to the preceding thread and SVC number; the three sum to about 100%. It is backend-agnostic (JIT and AOT), single-writer per core, off unless the value is exactly `1`, and adds no clock reads when off.
- Added `smoke_guest_time_unit` (`tests/recompiler_smoke/guest_time_unit.cpp`) registered in `tests/recompiler_smoke/CMakeLists.txt` and `run.py`.
- Added opt-in lookup statistics, exit-pair classification, and exit-target reports through `SUYU_RECOMP_LOOKUP_STATS=1` and `SUYU_RECOMP_LOOKUP_TARGETS=1`, with `smoke_lookup_stats_unit` and `smoke_exit_targets_unit`.
- Added opt-in `SUYU_AOT_CROSS_CHAIN=1` (XMOD1, feature bit `0x8`): RET/BR/BLR targets that miss the module-local index are looked up in one immutable table the host publishes to every image (entries ascending by load base; owner is the last base at or below the PC; normal block entries, never `_s` entries). It requires FM1, the indirect chain, and a split export (`SUYU_AOT_SPLIT_MAIN`); otherwise it is ignored with a warning. Off output is byte-identical (ABI5/ABI6 goldens unchanged).
- Recorded `cross_chain` in the manifest, cache reuse check, bundle id (only when on), and `image_features`; registry v1 gains `xmod_v1` (NULL unless XMOD1) and the `SuyuRecompXmodTableV1` layout in `recomp_registry_v1.h`. Images export `recomp_image_xmod_v1` and refuse a table with another version/size/count, unsorted or duplicate bases, or a NULL lookup.
- Host `PublishRecompXmod` verifies all modules then hands every image the same table; any refusal clears all images. `UnpublishRecompXmod` is valid only while no guest thread runs; the table is never freed, and its pointer uses acquire/release. The table is not published while a force-miss or main-hole hook (`SUYU_RECOMP_FORCE_MISS_*`, `SUYU_RECOMP_MAIN_HOLE`) is set; `ActiveRecompMissHookName` identifies the active hook in the log. Loaders outside the registry v1 bundle loader refuse the bit (`kLegacyLoaderSupported`).
- Added `smoke_xmod_table_unit`; three-module fixture cases `cross`, `cross-off`, `cross-refuse`, and `cross-guard` cover a BR ring A->B->C->A, budgets 1/2/256, DLL start, misses, SVC/BRK targets, RET/BLR, FADD through the chain, and a guard in a chained module. Added AOT cache matrix cross-chain cases.
### Changed
- The ledger first charged SVC time by wall time around `Svc::Call`, which double-counted other fibers' runs (run+svc above 100%); it was replaced by non-overlapping run/idle/gap accounting before the commit.
- Guest-time windows now restart after a report following `BeginRun`, and report construction is outside the run interval while report logging occurs after `EndRun`.
- Cross-module chaining is implemented behind `SUYU_AOT_CROSS_CHAIN`; the host table is published only after bundle verification and is cleared if any image refuses it.
### Verified
- `guest_time_unit` passed (MSVC standalone), and failed under two deliberately weakened implementations (nested SVC time not subtracted in the first design; idle not subtracted from host in the final design); the host build of `suyu` and `suyu-cmd` exited 0 with only existing `LOG_*` macro-redefinition warnings; after the commit the host was reconfigured and `GIT_REV` matched HEAD `5bb3381`.
- Same-scene Kotobuki measurements (user FPS: AOT 23, JIT 30, JIT speed limit off 38): per core steady state JIT run/idle/host 51.6/42.2/5.0%, AOT 73.3/21.2/4.0%; busy core time per frame AOT 98.5 ms, JIT 52.0 ms (limit on) and 54.1 ms (limit off). Details and caveats are in WORK_PLAN.
- The lookup and exit-target units passed with deliberately weakened controls; the host build exited 0, and the full smoke suite passed with `SMOKE_EXIT=0`. Session-25 measurements and the cross-module-chain proposal are recorded in WORK_PLAN.
- The smoke suite exited 0 with goldens unchanged and the XMOD1 fixture passed 9 cases. Negative controls passed: removing XMOD lookup made `cross` fail, disabling the image-side version check made `cross-refuse` fail, and an unsorted table made the unit fail. Host builds of `suyu` and `suyu-cmd` exited 0; the export-integration AOT cache matrix passed 279 cases with 0 failures, including a real XMOD1 export whose launcher published the table and ran the guest chain through every module with no JIT fallback; the clean-up rebuild also exited 0. The Orca Code reviewer initially withheld approval for P1 synchronization, P2 force-miss hooks and test coverage, and P3 comment length; all four findings were fixed, with no re-review yet.
- At HEAD `d1981da`, the host was built and the cleanup rebuild exited 0 with `GIT_REV` `d1981da`. The user exported PLA with `SUYU_AOT_CROSS_CHAIN=1`: the manifest had `cross_chain=true` and `indirect_chain=true`/`direct_chain=true`/`chain_start=false`, `image_features=15`, and `fm_inline`/`fp_shared`/`fpx_vec=true`; the log showed all five modules ArmRecomp active, `static -> JIT: 0`, and no cross-module-table publication failure. User-read PLA FPS was 23 with no chain and with the existing chain (43.5 ms/frame), 24.5 with cross chain in the first run (40.8 ms/frame), and 24 at budget 256 in a later session (41.7 ms/frame). A cross-chain budget sweep read 24 FPS at 256, 1024, and 2048 (an earlier pass read 23.5 at 128 and 24 at 512); the first 1024 reading of 25 FPS was corrected by the user to no change, so the default 256 remains. Readings taken while a host rebuild was running were discarded because of CPU contention.
### Known limitations
- The ledger does not attribute time to guest functions; function-level attribution used host RIP sampling plus the DLL block tables and the dumped NSO.
- The exclusive-access path was analyzed (AOT-only extra about 6.1 ms of core time per frame at most; designs E1/E2 recorded in WORK_PLAN) but nothing was changed.
- Lookup diagnostics add a second host lookup per exit; the target table overflows and loses its tail; DLL-internal hit/miss counts are not measurable from the host. A PLA XMOD1 export exists, but its FPS difference is small and user-read; exit counters were not collected, and publication success is inferred only from the absence of publication-failure lines because there is no positive publication log line. Real `ArmRecomp` executed-block accounting, `RecordMiss` and `RecompGaps` with XMOD1, IC IVAU through the chain, and a 256-budget stop are untested; the out-of-range local lookup early miss remains a separate item.

## v0.0.1-dev20 - Unreleased

*Development entry 20 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added `RecompShardedCounters` with per-thread cache-line-aligned slots, single-writer load+store increments, aggregate reads whose leaked slots outlive their threads, and `smoke_sharded_counters_unit` coverage; its shared-slot negative control skips with a message on single-hardware-thread hosts.
- Added the opt-in `SUYU_AOT_CHAIN_START=1` experiment: `_s` entries serve direct-chain targets of at most eight instructions, sharing a guard and GG1 slot; it requires the direct chain and is off unless the value is exactly `1`.
- Added `Common::EnvFlagDefaultOff`, the `chain_start` manifest/cache/bundle-id field, `chain_start_budget.h`, per-image projected-size refusal, the `chain_start_budget` test stage and `smoke_chain_start_budget_unit`, extended BL fixtures and `abi6dcs`/`ggdcs` smoke variants, and export-integration chain-start cases.

### Changed
- Moved `svc_calls`, `jit_to_static`, and the SVC-number histogram from locked `RecompCounters` to sharded counters; SVC ids `>= 256` use one overflow line, fallback (JIT) SVCs count only toward `svc_calls`, and `early_dispatches` stops at the 64-entry log limit. `g_current_pcs` now uses one cache line per core, while `RecordMiss` and `RecordUnhandled` deliberately remain unchanged.
- Limited every code comment to at most two lines through the new `CLAUDE.md` rule; shortened all comments in `src/core/arm/recomp/arm_recomp.cpp` (comment-only, with non-comment lines verified identical), shortened the ChainTo comment, and moved the measurements and longer text to `OLD_HISTORY.md`.
- Chain-start refuses each image when its projected size exceeds 2 GiB minus a 128 MiB margin, records `chain_start` in manifest/cache/bundle identity, and remains opt-in; the work-plan handoff was updated for session 22.
- PLA calibration estimates are about 400,199 unique direct-chain target blocks (12.2% of 3,276,463); at the eight-instruction threshold, 291,542 blocks add about 134 MiB, or about 8.3% of the 1603 MiB main image. A microbenchmark measured about 1.9 versus 1.4 ns per tiny block; this is a synthetic upper bound, not a PLA measurement.

### Verified
- The standalone MSVC unit build passed, host `suyu` and `suyu-cmd` builds exited 0 with only existing `LOG_*` macro-redefinition warnings, and the full smoke suite exited 0, including `sharded_counters_unit`, `chain_start_budget_unit`, and the BL-start variants.
- `--only-aot-cache-matrix` passed 257 cases with 0 failures. Negative controls failed as intended for shared-slot counters and for chain-start entries without a guard, threshold 0, threshold 9, a separate GG1 slot, and the budget boundary.
- Orca Code reviewer findings on the counter change (3) and chain-start change (4) were fixed; the cleanup rebuild was completed with export variables cleared.

### Known limitations
- No FPS measurement was made for counter sharding. A fresh full PLA export with chain start enabled succeeded; the main DLL was 1,706.8 MiB with indirect chain also enabled, and the user observed no FPS change. The gain and per-option size contribution remain unmeasured; the 134 MiB and 8.3% values above are estimates.
- The 21 and 23.5 FPS observations are separate conditions (direct chain only off and indirect chain only off), not a direct-chain on/off pair. Whether these changes explain the 22-versus-30 FPS JIT gap or the 8.5 FPS all-off observation remains a hypothesis.
- Export/runtime verification remains incomplete; the work needs a commit and full export because the AOT cache producer is the commit hash.

## v0.0.1-dev19 - Unreleased

*Development entry 19 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added `SUYU_AOT_FPX_VEC` (`FPXV`), on unless exactly `0`, for the FPX1 all-lane vector FADD/FSUB fast path on 4S and 2S vectors. It is ignored without FPX1 and never active in the FPX shadow build.
- Added `fpx_vec` to the AOT cache manifest and split bundle identity, with old manifests matching only exports without FPXV. Added `fpxv`, `fpxvb`, and `fpxvz` FPX harness implementations and the `fpxv`/`abi6fv` smoke variants.

### Changed
- The FPXV helper tries one SSE2 add/sub across all lanes and keeps the result only when every lane passes the existing FPX1 keep rules; otherwise the unchanged per-lane path runs. On hosts without SSE2 it always declines.
- FPXV changes generated code and cache identity only; it does not change the image ABI, feature bit, or results. With FPXV off, emitted text remains byte-identical.

### Verified
- The FPX harness passed with 74,815,904 FPXV-versus-soft cases and 0 value or FPSR mismatches; the negative controls failed as designed. The MSVC smoke suite passed all variants, including `fpxv` and `abi6fv`, with unchanged ABI5/ABI6 golden hashes.
- Code review of `544dad2` found one AArch64 negative-control exemption issue, fixed afterwards in `tests/recompiler_fpx/run.py`; no other findings were reported. The host rebuild exited 0 with `GIT_REV` set to `544dad26944b3c5913b12be0e47b159d6ea59c42`.
- The user observed no FPS gain after a fresh FPXV-on export in the usual PLA scene; this was not measured by an agent.

### User-observed measurements recorded on 2026-10-10
- In the same usual scene, an export with `SUYU_AOT_FM_INLINE=0`, `SUYU_AOT_DIRECT_CHAIN=0`, `SUYU_AOT_INDIRECT_CHAIN=0`, `SUYU_AOT_FP_SHARED=0`, and `SUYU_AOT_FPX_VEC=0` measured about 8.5 FPS; the `SUYU_AOT_SPLIT_MAIN` setting was not confirmed. Individual direct-chain-off and indirect-chain-off exports measured 21 and 23.5 FPS.
- A `SUYU_RECOMP_CHAIN_BUDGET` sweep measured 21, 22, 23, 22, 23, and 22 FPS for 32, 64, 128, 256, 512, and 1024. The value was not logged; these are FPS-only observations, and the export configuration was not confirmed.
- Interpretation: the all-off result refutes the earlier hypothesis that generated-code speed is not the bottleneck; the flat budget sweep weakens the budget-bounded interrupt-latency hypothesis. The all-off cause and the remaining JIT gap are unresolved hypotheses.

### Known limitations / held items
- FMUL/FMLA, D-lane, and FADDP vector forms, PLA sampler attribution, `RECOMP_FPX_PROBE` keep-rate measurement, an AArch64/NEON path, and the export integration run remain undone. The user has not provided the export's `fpx_vec` manifest value, image sizes, cache status fields, FPS, hardware, or an `SUYU_AOT_FPX_VEC=0` A/B.
- The reason for the unchanged observed FPS is unknown. Possible explanations are hypotheses: vector FADD/FSUB is not the bottleneck, uncovered FP operations dominate, the all-lane keep rate is low, or prologue register saves offset the benefit.

## v0.0.1-dev18 - Unreleased

*Development entry 18 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added `SUYU_AOT_FP_SHARED`, which emits each FPX1 exact floating-point body once per generated module and calls it from the existing FPX1 fast paths. The option is on unless exactly `0`, is ignored without FPX1, and warns only when explicitly set to `1` without FPX1; it preserves the inline output when off.
- Added the Dear ImGui debug overlay for the Windows `suyu-cmd` launcher and the static launcher; it is created by default and can be disabled with `SUYU_DEBUG_OVERLAY=0`. It includes a command bar, input routing, Backend, Performance, CPU, Build identity, and GPU sections, plus standalone logic and build-identity smoke tests.
- Added the shared `src/common/env_option.h` rule for default-on flags and three-way choices, plus the `smoke_env_option_unit` coverage and default-option integration scenarios.
- Added ImGui v1.92.9b as the forced-bundled `Aevora::imgui` dependency, the Vulkan overlay extension point, recompiled-module and native-status diagnostics, and packaged AOT manifest display fields.
- Added the opt-in `SUYU_RECOMP_THREAD_TIME=1` per-guest-thread, per-core `RunThread` wall-time ledger and `smoke_thread_time_unit`; unset or other values leave it off.

### Changed
- Overlay-owned mouse input is routed through a bounded, mutex-protected queue with focus/minimize reset handling; keyboard input is not forwarded to ImGui yet. The overlay reports guest PCs and fallback activity as diagnostics and does not treat successful startup or zero hybrid fallback attempts as proof of static execution.
- `SUYU_AOT_FM_INLINE`, `SUYU_AOT_DIRECT_CHAIN`, and `SUYU_AOT_INDIRECT_CHAIN` are now on unless exactly `0`; `SUYU_AOT_SPLIT_MAIN` defaults to binding `main` as a DLL wherever supported, while unsupported default splits are skipped with a log line. `=0` disables each option; explicit `=1` split requests retain their prior refusal reasons, and other split values are errors.
- The chain defaults change generated code and the bundle id for everyone, making the earlier “opt-in until measured” wording obsolete. Split-only failures name `SUYU_AOT_SPLIT_MAIN=0`, and LNK1248 advice names the three feature opt-outs and `Hybrid with SUYU_AOT_SPLIT_MAIN=0`.
- The process-wide debug overlay is acquired by `SuyuCmd::AcquireDebugOverlay` and intentionally never destroyed because the renderer retains a raw pointer and shuts it down in its own destructor; this closes the overlay's early-exit lifetime issue without changing the pre-existing renderer/window-reference hazard.

### Verified
- Verified the shared FPX1 bodies with the MSVC FPX harness: `fpxs` had 0 mismatches against soft over 76,041,704 L1-L3 cases, while the `fpxb` weakened-body control had 61,560 mismatches as designed. The smoke suite, export integration defaults/cache matrices, and host rebuild passed.
- The user observed that a fresh default-on PLA export produced considerably smaller generated files and raised FPS by about 1-2 FPS in the usual scene, previously 15-20 FPS. Exact image sizes, `aot_cache_outcome`/`aot_cache_publish`/`aot_cache_reused` values, hardware details, and an `SUYU_AOT_FP_SHARED=0` A/B remain unrecorded.
- Commits `97e32dd`, `0ae08ea`, `575b796`, `585c886`, `2bed3c6`, and `c821526` are present on `main`; the host rebuild succeeded (`BUILD_EXIT=0`), the standalone thread-time unit passed, and its four deliberate weakening controls were caught. The full smoke run passed; the earlier `2bed3c6` review was not separately requested.
- The user reported that, after a fresh full export with stages D and E of the `SUYU_DEBUG_OVERLAY=1` ImGui overlay (including the router redesign and reviewed stage-E fixes), the export completed successfully and the Debug window worked “perfectly” on PLA. This is a user observation; no itemized sections or inputs exercised and no performance result were provided.
- The host rebuild was run with no AOT/overlay variables initially set; MSVC `cl` smoke included chain variants, and exporter-wiring weakening was caught and restored byte-identically.
- Integration passed `--only-multimodule` 76/0, `--only-defaults` 24/0, and final `--only-aot-cache` 308/0. Default scenarios covered unset default-on FMI/direct/indirect options, supported split binding, quiet unsupported-default skips, and `=0` all-off/all-static output.

### Known limitations / held items
- The standard 2bed3c6 export measured main/launcher `SizeOfImage` as 1,923,067,904/495,382,528 with no LNK1248 and 15-20 FPS in the usual scene; cache dialog status fields were not read. The FMI-off A/B measured 1,588,723,712/397,508,608 and about one FPS change with direction unrecorded. Per-thread measurements found no single saturated thread; lock contention as the cause of stalled time and idle cores is a hypothesis. The host-RIP profile was flat (4,242 functions; top 1.90%), and host bytes per guest instruction were mean 150 B, median 151 B, p90 313 B, p99 383 B; these are observations, not acceptance results.

## v0.0.1-dev17 - Unreleased

*Development entry 17 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added U1's lock-light exclusive monitor path and tests, opt-in direct B/BL chaining, and independent opt-in indirect RET/BR/BLR chaining. Chain paths are off by default, preserve byte-identical emitter output when off, use `RECOMP_TAIL_CALL` where enabled, and record `direct_chain`/`indirect_chain` in the manifest, cache reuse check, and bundle id.
- Added chain smoke variants `abi6dc`, `ggdc`, `abi6ic`, and `ggdcic`, the `--bl` execution fixture, structural checks, cache-matrix cases, environment-driven reuse/invalidation scenarios, and negative controls. The smoke suite passed under clang-cl; the AOT cache matrix passed 194 tests with 0 failures; the host build exited 0.
- Added the fourteenth-session handoff and archived the superseded thirteenth-session handoff.

### Changed
- Corrected comments and documentation for the 2 MiB fiber stack, `/O2` MSVC route, and historical chain-budget measurements; documented that conditional branches do not chain.
- Documented user-run Kotobuki observations: U1 reported 17-21 FPS, direct-chain and direct-plus-indirect-chain image sizes and CPU samples, recurring hot RVAs, and the unexplained idle wait. These are observations, not acceptance results; chain-run FPS was not reported. The standing procedure sets FASTMEM, FMI, and split-main environment variables, adding chain variables only for chain experiments.
- Retained the thirteenth-session handoff and the Kotobuki comparison line: static AOT was 17-20 FPS versus 30 FPS JIT before the U1 launcher observation.

### Verified
- Direct and indirect chain reviews completed (direct: two rounds with findings fixed; indirect: two rounds, final approved), and the BL fixture covered both bases, budget exhaustion, middle-block targets, below-base targets, and BLR x30 ordering. MSVC `cl`, non-Windows compilers, old manifests without chain fields, and broader PLA correctness remain untested.

## v0.0.1-dev16 - Unreleased

*Development entry 16 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the synthetic `xmod` three-module static/DLL/static fixture covering cross-image state, guards, FPX, and bridge behavior.
- Added the shared required host-bridge callback list and its generated-runtime drift check.
- Added 14 loader and bundle-boundary smoke cases covering feature/handshake refusals, missing pointers/exports, paths, and lookup misses.
- Added an AOT-cache integration case for reuse after a pre-promotion validation failure, preserving the earlier package and cache entry and leaving no staging or cache-root residue.
- Added the reverse module-binding matrix case, rebuilding and republishing when `main` changes from DLL to static.
- Added an export build-kit test that rejects split modules at CMake configure time without creating a build tree.

### Changed
- `ArmRecomp::CheckBridgeComplete` now uses the shared required callback list.
- U4 regression runs completed successfully: the full smoke suite exited 0 with unchanged ABI5/FM1/FPX golden hashes and all listed variants passing; fastmem reported 0 mismatches across 5 fmdiff and 4 GG1 legs; reduced FPX reported 1,786,000 cases with 0 mismatches, and full FPX reported 100,587,904 plus 30,320,000 cases with 0 mismatches.
- The AOT-cache integration run passed 272 tests with 0 failures; the export build-kit package suite passed 14 tests. The host tree was rebuilt successfully with both `suyu.exe` and `suyu-cmd.exe` linked.

## v0.0.1-dev15 - Unreleased

*Development commit 15 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added a persistent user-data AOT cache for Windows Build exports with authoritative entry layout, producer revision/build-id records, per-entry locking, atomic publish/replace, interrupted-publish recovery, and failed-package preservation.
- Added full-match reuse and named mismatch reasons for manifest inputs, split topology, artifacts, and launcher SHA-256/size records, plus test-only cache and producer seams and export status fields.
- Added the cache unit test and publish/reuse/invalidation integration matrix; the final synthetic-fixture run passed 262 tests with 0 failures.

### Changed
- Build packages continue to exclude `aot_cache`; Source-format, non-Windows, and lock-busy exports do not use or publish the persistent cache.

## v0.0.1-dev14 - Unreleased

*Development commit 14 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added a sparse multi-module fixture generator and integration scenarios for split static/Hybrid exports, linked launcher execution, and unchanged all-static output.
- Added a test-driven per-export split option and build-stage failure injection, including tampered or missing DLL packaging checks and rejection reasons in test-driven status.

### Fixed
- Launcher LNK1248 failures no longer block automated runs on a modal dialog; test-driven exports report the rejection while interactive users retain the dialog.

The persistent cache was subsequently implemented in v0.0.1-dev15; this entry records the pre-cache state.

## v0.0.1-dev13 - Unreleased

*Development commit 13 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added exporter, cache, manifest, package-policy, and launcher wiring for the opt-in `SUYU_AOT_SPLIT_MAIN` main-module DLL topology, including a versioned canonical bundle id and registry-v1 marker.
- Added shared PE `SizeOfImage` validation, canonical NSO module names, shared DLL-target verification, per-image manifest metadata, and package staging/verification for split exports.
- Added bound-inventory and pre-relocation INFO diagnostics, plus topology, PE, policy, and prebuild regression coverage; export-regression fixture stubs were updated but could not be run because their Qt-root lookup does not match the CPM Qt package layout.

### Changed
- The v1 launcher and in-tree/prebuilt CMake paths now build and collect split DLLs without importing them into the launcher; all-static v4 output remains unchanged.
- Split exports refuse missing or failed canonical modules, validate cache/package topology and DLL identities, and report image sizes against the 2 GiB Windows x64 limit.

Split export wiring is implemented, but exporter end-to-end execution, linked launcher guest execution, and the remaining Milestone 4a step 4(c) acceptance coverage are not yet verified (part 2).

## v0.0.1-dev12 - Unreleased

*Development commit 12 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the single-definition registry v1 layout and topology writers for mixed static/DLL recompiled bundles.
- Added production bundle loading with transactional DLL handling and identity, ABI, feature, and handshake validation.
- Added inventory-based binding before process publication, including canonical module matching and base application.
- Added host bridge completeness checks for exclusive, pair, clear-exclusive, and counter callbacks.
- Added the v1 launcher path behind `SUYU_RECOMP_REGISTRY_V1`.
- Added production bundle-loading, inventory-binding, and mixed-image execution smoke tests.

Split exports remain disabled until Milestone 4a step 4(c) connects the exporter, packaging, and linked launcher.

## v0.0.1-dev11 - Unreleased

*Development commit 11 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added a single authoritative module-topology header, an image-identity contract, and an opt-in identity export for shared images. The identity contract returns the canonical module name and raw 32-byte SHA-256 bundle digest.
- Added prebuild support for topology-bound DLL modules with build-time checks for topology version, bundle identity, target type, module membership, and effective DLL file name. Split exports are not usable end to end yet; registry v1, launcher loading, and the exporter option are later steps.
- Added fail-fast rejection of split exports in the in-tree launcher configuration without the registry marker and in the export build kit, plus topology, identity, and prebuild consumer smoke tests.

### Changed
- The exporter now writes `recomp_modules.cmake` and the module load order from the shared topology definition. All-static output remains byte-identical to the previous output.

## v0.0.1-dev10 - Unreleased

*Development commit 10 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- A static Build export whose clang-cl link fails with LNK1248 (image over the 2 GB Windows x64 limit) now stops with an explicit error instead of recompiling every module with MSVC, which links the same code into the same single image and was estimated at about 20 hours for PLA's main module. The existing Hybrid retry that routes the largest module to JIT when fallback is enabled and more than one module remains is unchanged; other link failures still fall back to MSVC.
- Renamed the project from HisuiWake to Aevora across the documentation; project documents moved from `docs/HisuiWake/` to `docs/Aevora/`, and `OLD_HISTORY_HW.md` became `OLD_HISTORY.md`.

### Added
- Failed compile and link output (clang-cl and MSVC paths) is appended to `export_build_failure.log` in suyu's log directory, because `suyu_log.txt` rotates on restart and the exporter deletes its work directory. Write and flush errors are checked; the file is truncated before appending once it exceeds 8 MiB. Only the captured tail of each process output (about 0.5-1 M characters) is kept.

### Documentation
- Added the main-module DLL split design plan (Milestone 4a) to the work plan and archived the sixth-session progress entries to `docs/Aevora/OLD_HISTORY.md`.
- Recorded the reviewed Milestone 4a main-module DLL split design note (D1-D7) in the work plan and closed the eighth session.
- Recorded the Milestone 4a step 3 measurement: the PLA main module built as a DLL is 1,459.1 MiB without FMI and 1,783.7 MiB with FMI (SizeOfImage), under the 2 GB image limit, exporting only the image ABI and depending only on CRT/OS DLLs.
- Added the "Modding and QoL readiness" rule to `CLAUDE.md`: recompiler changes must keep mods and QoL additions easy to add, resolve references from one authoritative source, and fail fast on mismatches so a small change cannot break the recompiler tool; the Code reviewer role now checks it.
- Disabled Claude Code auto memory for the project (`.claude/settings.json`) and moved the Orca worker operating notes that existed only in agent memory (spec preamble, wait timeout, worker release and acknowledgement, unsubmitted-spec recovery, re-checking `git status`) into `CLAUDE.md`.

## v0.0.1-dev9 - Unreleased

*Development commit 9 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Added the opt-in inline FM1 fast path (FMI), enabled by setting `SUYU_AOT_FM_INLINE=1` in the environment of the export. FMI requires FM1, needs a new export, leaves default output unchanged, records `"fm_inline"` in the manifest, and requires cache reuse to match that field; manifests without the field count as FMI off.

### Tests
- Added FMI fast-path and fallback legs to the fastmem differential tests, plus the GG1 watched-page differential executable.
- Added `abi6fmi` and `ggfmi` variants to the recompiler smoke tests; default output remains byte-identical.

### Known issues
- FMI on PLA made the static launcher image about 2.19 GB, over the 2 GB x64 image limit (LNK1248), so FMI cannot currently be used for a PLA static export. After the link failure, fallback-disabled export takes the existing MSVC per-module path (one `cl.exe` at a time on 16 GB hosts, about 20 h for main), also bound to fail; FPS and sampling remain unmeasured. Fixed in v0.0.1-dev10: the export now stops at the LNK1248 link failure.
- Whole-helper inlining was measured worse on MK8D (image size 100 to 222 MB and 14% lower FPS); FMI only inlines the FM1 fast path.
- Per-instruction entry labels that store and reload guest registers through `GuestContext` are not addressed by FMI.

## v0.0.1-dev8 - Unreleased

*Development commit 8 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Recompiled code now uses the native floating-point fast path (FPX1) when the guest runs with flush-to-zero (FPCR.FZ), as Pokemon Legends: Arceus does (FZ set in about 96% of sampled thread switches). Previously FZ closed the fast path, so every covered FP operation ran a software IEEE implementation. Under FZ a native result is kept only when no input is subnormal and the result is a normal number larger than the smallest normal (or, for add/subtract, exactly zero; for multiply/divide/FMA, the existing exact zero-operand cases), so results and FPSR stay bit-identical to the architecture. DN, AHP, FZ16, non-nearest rounding and trap enables still close the fast path. Requires a new export.

### Fixed
- The recompiler smoke suite's golden hashes were stale (they no longer matched after the v0.0.1-dev4 generated-CMake change) and the suite's exporter was built without /utf-8 on MSVC, so its output differed from real exports (which suyu builds with /utf-8) in UTF-8 comments and messages. The exporter test target now builds with /utf-8 and the goldens were recomputed; MSVC and clang-cl builds now give identical hashes.

### Known issues
- PLA static AOT overworld performance was observed at about 25-30 FPS, up from about 16-20 FPS with dev6 static AOT and versus 30 FPS with the JIT baseline, in one user-observed run with zero fallback. The FP differential harness compares against the software implementation on this host; the hardware-golden leg (L5) needs an AArch64 host and was not run.

### Documentation
- Split `WORK_PLAN.md`, archived older history verbatim in `docs/Aevora/OLD_HISTORY.md`, and added a key-facts summary plus archive rule.
- Updated README status to the dev8 evidence and corrected the stale SHL note in `CODEBASE_INDEX.md`.

## v0.0.1-dev7 - Unreleased

*Development commit 7 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- An opt-in FPCR diagnostic, enabled when `SUYU_FPCR_STATS` has any non-empty value other than `0`, tallies the guest FPCR of application threads at each thread switch and logs each newly seen value (decoded RMode/FZ/DN/AHP/FZ16/trap bits) plus a process-wide summary every 65,536 context saves, including how often FPSR.IXC is set. It works with both the JIT and recompiled CPU backends. The diagnostic is off by default; when enabled it takes a lock per context save, so FPS from such runs is not a performance measurement.

### Known issues
- Counts are cumulative per host process; restart suyu between comparisons.
- At most 16 distinct values are tracked; overflow is logged.
- The diagnostic samples context saves, not individual FP operations, so it does not give the per-operation FPX1 gate-open rate.

## v0.0.1-dev6 - Unreleased

*Development commit 6 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Recompiled code now returns to the dispatcher every 256 CPU blocks instead of 4096 on current export images (ABI 6 with `run_slice` enabled). The CPU core answers rescheduling requests sooner, reducing the time other cores spend waiting. In a Pokemon Legends: Arceus Hybrid export this reduced contention in the fiber scheduler and raised overworld FPS from about 14 to 15-20. The override `SUYU_RECOMP_CHAIN_BUDGET` (1-8192) still takes precedence.

### Known issues
- The reduction needs a new export to take effect; existing packages continue to use the 4096-block budget.
- Hybrid performance is still below the JIT baseline (30 FPS). Profiling points at generated-code quality (flag computation, out-of-line memory accesses, context-pointer reloads) as a possible cause; this is not yet confirmed.
- The new default was chosen from environment-variable runs on one game and one PC and has not yet been confirmed in a rebuilt export.

## v0.0.1-dev5 - Unreleased

*Development commit 5 toward v0.0.1. Not a release; no build artifacts are published.*

### Fixed
- Exported launchers whose file name contains characters outside the Windows ANSI code page (for example `é` in "Pokémon" on a Japanese system) exited immediately with "No mapping for the Unicode character exists in the target multi-byte code page". The window title and Discord title are now converted from paths as UTF-8.

### Known issues
- The fix needs a new export to take effect; existing packages cannot be relinked.
- When run from a source build, the exporter leaves export-specific CMake cache entries in the build tree; they must be cleared before packaging an export build kit.

## v0.0.1-dev4 - Unreleased

*Development commit 4 toward v0.0.1. Not a release; no build artifacts are published.*

### Changed
- Hybrid and static exports now compile generated game code in parallel according to available memory and the compiler. Previously the build allowed one compile per 8 GB of RAM, so a 16 GB PC compiled one file at a time (about 3.4 hours of compilation for Pokemon Legends: Arceus). With clang-cl the budget is 1.5 GB per compile with 4 GB reserved (7 parallel compiles on 16 GB); MSVC keeps 8 GB per compile.
- Generated code is split into smaller files (at most 5,000 blocks or 8 MiB each, previously 20,000 blocks or 32 MiB) so that more compiles fit in memory.
- New environment overrides for tuning: `SUYU_RECOMP_JOBS`, `SUYU_RECOMP_JOB_MEMORY_MB`, `SUYU_RECOMP_UNIT_BLOCKS`, `SUYU_RECOMP_UNIT_KB`.

### Known issues
- The new limits are not yet measured on a full export; export time, memory use, and in-game performance still need to be compared with the previous settings.
- The Linux/macOS Source-export build script still limits jobs to one per 8 GB of RAM.
- A Pokemon Legends: Arceus Hybrid export (clang-cl) now builds and links with no module left to JIT, but the resulting launcher (about 1.87 GB) does not start. Under investigation.
- Export reports record the compiler as "O2" instead of the compiler name.

## v0.0.1-dev3 - Unreleased

*Development commit 3 toward v0.0.1. Not a release; no build artifacts are published.*

### Fixed
- Graphics pipeline libraries (Vulkan GPL) are now disabled by default. With GPL enabled, some objects in Pokemon Legends: Arceus rendered with wrong or missing textures, and the affected objects changed after clearing the pipeline cache. The same defect occurs in Suyu v0.0.12.
- The Qt frontend now shows the renderer extension settings (graphics pipeline libraries, Extended Dynamic State, Vertex Input Dynamic State, Sample Shading) in a new GraphicsExtensions tab and in per-game configuration. These settings were previously only editable in the configuration file.

### Known issues
- The underlying GPL defect is not fixed; enabling GPL can still cause rendering errors.

## v0.0.1-dev2 - Unreleased

*Development commit 2 toward v0.0.1. Not a release; no build artifacts are published.*

### Fixed
- `suyu.exe` (Qt frontend) now links with the default bundled static Qt 6.11.1 on Windows x64. The VulkanMemoryAllocator implementation (`video_core/vulkan_common/vma.cpp`) is no longer compiled into `video_core`; each executable compiles it, and the Qt frontend uses Qt's built-in copy when Qt is static (except on Apple). Approach follows Eden PR #2994.

### Known issues
- With static Qt, the Qt frontend uses Qt's VMA 3.2.1 (built with `VMA_DEDICATED_ALLOCATION 0`) instead of the bundled VMA 3.3.0; runtime behavior has not been tested.
- Application startup, game loading, export build kit, and libretro builds have not been tested.
- Export build kits generated before this change must be regenerated.

## v0.0.1-dev1 - 2026-10-04

*Development commit 1 toward v0.0.1. Not a release; no build artifacts are published.*

### Added
- Agent guide (`CLAUDE.md`) and project subagent definitions (`.claude/agents/`) for AI-assisted development.
- Work plan with milestones, acceptance criteria, and a dated progress record ([docs/Aevora/WORK_PLAN.md](docs/Aevora/WORK_PLAN.md)).
- Read-only codebase survey ([docs/Aevora/CODEBASE_INDEX.md](docs/Aevora/CODEBASE_INDEX.md)).

### Fixed
- Dependency extraction on Windows systems whose ANSI code page is not UTF-8 (e.g. Japanese, code page 932): `CMakeModules/CPMUtil.cmake` now extracts downloaded archives with `cmake -E tar` instead of `file(ARCHIVE_EXTRACT)`, which failed on non-ASCII entry names in the Boost 1.90.0 archive (`Invalid empty pathname`).

### Known issues
- `suyu.exe` (Qt frontend) does not link with the default bundled Qt 6.11.1 on Windows x64: the static Qt build and `video_core` both define VulkanMemoryAllocator symbols (`LNK2005`/`LNK1169`). The resolution is pending.
- `suyu-cmd.exe` builds and links with MSVC 14.51 (Release), but application startup, PLA loading, and game export have not been tested.
- Windows builds require Git for Windows (for `patch`) and the Vulkan SDK (for `glslangValidator`) in addition to the compiler toolchain.
- The build's embedded version string still comes from the inherited `CMakeModules/GenerateSCMRev.cmake` and does not yet reflect Aevora versioning.

### Notes
- Verified host build setup and detailed evidence are recorded in the work plan's progress record.
