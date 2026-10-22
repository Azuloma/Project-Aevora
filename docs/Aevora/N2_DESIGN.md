# N2 Native Guest Memory Design

Status: Stage 0 design note, 2026-10-22 (test4). This document records verified observations and the staged plan for replacing the supported-path kernel memory manager with a native guest mapper.

## Purpose

N2 will provide a title-specific guest address space without the emulated kernel memory manager. The native mapper must preserve the fastmem layout expected by generated code while implementing only the memory operations observed on the confirmed N0 gameplay path.

## Scope and non-goals

The scope is the supported path's memory SVCs, guest code/heap/stack/TLS/shared-memory mappings, query and protection behavior, IPC map-alias buffers, and the consumers that must move with them. N2 does not replace threads, synchronization, services, storage, GPU command processing, or the general kernel; those belong to later milestones. GPU/rasterizer cache tracking remains on `Common::PageTable` and `Core::DeviceMemory`, not `KPageTable`.

## Observed

- The N0-observed memory SVCs—0x01 SetHeapSize (1), 0x03 SetMemoryAttribute (137), 0x04 MapMemory (32), 0x05 UnmapMemory (12), 0x06 QueryMemory (266), 0x13 MapSharedMemory (2), 0x15 CreateTransferMemory (119), 0x29 GetInfo (35), 0x2C MapPhysicalMemory (868), and 0x2D UnmapPhysicalMemory (1)—enter `src/core/hle/kernel/svc/svc_memory.cpp`, `svc_physical_memory.cpp`, `svc_query_memory.cpp`, `svc_shared_memory.cpp`, `svc_transfer_memory.cpp`, and `svc_info.cpp`, then reach `KProcessPageTable` (`src/core/hle/kernel/k_process_page_table.h`) and its `KPageTable`/`KPageTableBase` implementation (`src/core/hle/kernel/k_page_table_base.cpp`, about 5,800 lines).
- Other memory-manager users are `KThreadLocalPage` (`k_thread_local_page.cpp`), `KSharedMemory`, `KTransferMemory`, `KCodeMemory`, IPC buffer setup/cleanup in `k_server_session.cpp`, process creation in `k_process.cpp`, `KernelCore` initialization in `kernel.cpp`, nvdrv (`nvmap.cpp`, `core/container.cpp`), `vi/shared_buffer_manager.cpp`, `ro/ro_nro_utils.cpp`, `arm/debug.cpp`, `memory/cheat_engine.cpp`, `debugger/gdbstub.cpp`, and `service/jit/jit_code_memory.cpp`.
- The structural gate's `kernel_memory` deny list (`tools/structural_gate/structural_gate.py`, milestones N2 and N9) matches `KPageTable`, `KMemoryManager`, and `KMemoryBlockManager`; the JIT launcher baseline matched 120 symbols.
- No standalone test currently covers an SVC/KPageTable replacement for map, unmap, protection, or shared memory. Existing standalone memory tests are `src/tests/common/host_memory.cpp`, `tests/recompiler_fastmem`, and `tests/recompiler_smoke/gg_host.cpp`.
- Stage 0 added an opt-in `[memory]` recorder to `src/core/boundary_inventory.h` and the kernel hook sites listed in [CODEBASE_INDEX.md](CODEBASE_INDEX.md). With `SUYU_BOUNDARY_INVENTORY=1`, it records SVC shapes, IPC map-alias descriptors, TLS pages, shared-memory permissions and sizes, and process layout ranges; with the option off it is execution-neutral.

## Inferred / to be verified

- The `KProcessPageTable` to native-mapper object seam is high confidence because the SVC forwarding path is concentrated there; the Stage 1 mapper is a standalone object with no ownership seam yet.
- The `KProcess::Initialize`/`LoadFromMetadata` to address-space allocator seam is medium-high confidence; the exact layout must be fixed from the user's Stage 0 collection.
- The native mapper's core is a physical-memory-backed alias-region allocator plus stack remapping, TLS, shared memory, and transfer-memory locking (inference); the supported path does not reach `SetupForIpc`/`CleanupForIpc*`.
- Region bases are randomized per process creation (inference); Stage 2 must take bases from `KPageTable`'s result, while native-only mode needs one published, consistent base definition.
- The default `DeviceMemory` backing is 4 GiB (`memory_layout_mode` 4/6/8/10/12 GiB), so one 3.01 GiB `MapPhysicalMemory` call fits raw capacity; user-pool availability still needs runtime validation.
- `KernelCore::InitializeMemoryLayout` and slab heaps are medium confidence: kernel initialization is still needed by objects that remain until N3/N4, so bypass timing must be verified rather than assumed.
- The supported-path scope of each consumer listed above, and the behavior of physical addresses as `DeviceMemory` offsets, remains to be verified.

## Seams and FM1 contract

The current seam confidence is: SVC to `KProcessPageTable` high; `KProcessPageTable` to a native mapper object high; `Core::Memory` to `Common::PageTable`/`HostMemory` high; `KProcess::Initialize`/`LoadFromMetadata` to the address-space allocator medium-high; IPC session mapping medium; `KernelCore::InitializeMemoryLayout`/slab heaps medium; and GPU tracking only through `Common::PageTable` high.

A native mapper must keep the FM1 fastmem contract:

- `Common::PageTable` entries remain 32 bytes (`ptr`, `block`, `addr`, `recomp_watch`) with two attribute bits.
- Address-space width remains the NPDM-selected 39, 36, or 32 bits, with code bases `0x80000000`, `0x08000000`, or `0x00200000`.
- `fastmem_arena` is set in `Core::Memory::SetCurrentPageTable` (`src/core/memory.cpp:48-60`).
- Map, unmap, and protection continue through `Core::Memory::MapMemoryRegion`, `UnmapRegion`, `ProtectRegion`, and `HostMemory`.
- The GG1 page-table-swap notification remains intact, and the layout checks and handshake in `src/core/arm/recomp/arm_recomp.cpp` continue to pass.
- Region bases are process-specific observations, not a fixed contract: shadow mode must consume the `KPageTable` result, and native-only mode must publish bases from one consistent definition.

The address layout is exactly the same as `KPageTable`'s layout so that `SUYU_NATIVE_MEM_SHADOW=1` can compare the native mapper with the existing implementation.

## Stage plan and verification

| Stage | Work | Verification |
| --- | --- | --- |
| 0 | Record the design note and collect the opt-in detailed memory inventory. | User review/commit, then a real collection play; analyze `[memory]` and `[memory_paused]` rows. |
| 1 | Build an unconnected native mapper under `src/core/native_memory/`; implement the alias-region allocator and query first, then map, unmap, protect, shared memory, heap, and physical memory, each with a deliberately weakened negative control. Defer IPC map-alias to a fail-fast stub. | Standalone unit tests pass and each negative control fails fast. |
| 2 | Add shadow comparison behind `SUYU_NATIVE_MEM_SHADOW=1`. | Compare per-SVC results, `QueryMemory` output, and page entries; supported-path mismatch count is zero. |
| 3 | Make the mapper primary behind `SUYU_NATIVE_MEM` (`=0` disables), and rewire TLS, shared/transfer memory, IPC mapping, nvmap, and vi. | Fallbacks are counted/logged; supported-path play and CPU ms/frame are recorded; option name/default are confirmed. |
| 4 | Remove kernel memory-manager symbols from the no-JIT native build, add fail-fast stubs for unsupported callers, and bypass `KernelCore` memory initialization. | `--require-absent N1,N2` passes and unsupported calls name the operation and address. |
| 5 | Full export, supported-path play, gate, measurement, and documentation acceptance. | User-observed checklist, structural gate, and CPU ms/frame report. |

## Stage 0 inventory questions

Test4 answered the `GetInfo` types, region/state combinations, TLS/shared-memory counts and sizes, and process-layout observations. It found no IPC map-alias rows; bag and quit remain uncovered. The process-layout ranges still define what Stage 1 must compare against `KPageTable`.

Collection uses `D:\HisuiWakeBuild\diag\n0\run_n0.bat <game> <label>` on the JIT host frontend: assign Player 1 in the overlay Controller window, press F10 to start recording, F9 to end a labeled segment, and quit normally. The new `[memory]` section appears in each local `inventory_<run>_NN_<label>.txt`; it is not committed.

## Stage 0 results (test4, 2026-10-22)

Observed in the JIT-capable `suyu-cmd` collection (title, village, field, catch, battle, menu, pokedex, and save segments; bag and quit were not segmented):

- `[memory]` totals were 0x03 x202, 0x04 x13, 0x05 x13, 0x06 x241, 0x15 x102, and 0x2C x2117. `[memory_paused]` totals were `process_layout` x2, `shmem_create` x5, `tls_page` x4, 0x01 x1, 0x03 x175, 0x04 x26, 0x05 x3, 0x06 x292, 0x13 x8, 0x15 x13, 0x29 x35, 0x2C x2242, and 0x2D x1. The paused table is cumulative and repeats in every report; merge it by maximum count, not sum.
- No `ipc_map` row appeared. The hook is only in `k_server_session.cpp` map-alias processing, and the bag and quit were not segmented.
- The first 39-bit process layout had flags `0x47`, pool 0, system pages 4096, address space `[0x0,+0x8000000000)`, code `[0x800A4000,+0x60F7000)`, alias `[0x1106200000,+0x1000000000)`, heap `[0x2106200000,+0x200000000)`, stack `[0x1086200000,+0x80000000)`, and kernel map `[0x86200000,+0x1000000000)`. A second layout first appeared in battle: code `[0x80C61000,+0x60F7000)`, alias `[0x1106E00000,... )`, heap `[0x2106E00000,... )`, stack `[0x1086E00000,... )`, and kernel map `[0x86E00000,+0x1000000000)`. Sizes stayed constant; the bases changed. The user confirmed that the game was relaunched before the battle segment, so the change is ASLR between two launches of the same program.
- `MapPhysicalMemory` (0x2C) supplied 4,359 calls across both tables, about 85% of memory SVCs, from 4 KiB to 3.01 GiB; one call was 3,232,497,664 bytes and one matching 0x2D was 3,228,565,504 bytes. Most calls were 256 KiB, 512 KiB, or 1.25–2 MiB; size classes are power-of-two page bounds, not exact sizes. Targets were mostly alias pages in Free or Normal (`0x77EBD05`).
- `SetMemoryAttribute` (0x03) always used mask `0x8` and attr `0x0` or `0x8` on Normal alias pages, 4 KiB–300 MiB. `QueryMemory` (0x06) covered Normal, Free, Code, CodeData, Stack, ThreadLocal, and Inaccessible states; ThreadLocal was in kernel_map.
- Stack remapping used MapMemory (0x04) alias-to-stack for 39 calls (64 KiB, 128 KiB, 1 MiB, or 4 KiB), plus one 4 KiB code-to-stack map; UnmapMemory (0x05) was x16 at 64 KiB. SetHeapSize (0x01) was one 16 MiB call and the heap was otherwise unused.
- CreateTransferMemory (0x15) was x115 (24–32 KiB plus one 15,728,640-byte call). MapSharedMemory (0x13) was x8 with permission `0x1`; five shared-memory objects had owner permission `0x0`, user permission `0x1`, and sizes 4 KiB–17,825,792 bytes. Four TLS pages were created.
- GetInfo (0x29) used types 0 (`CoreMask`), 2 (`AliasRegionAddress`), 3 (`AliasRegionSize`), 11 (`RandomEntropy`, subtypes 0–3 with a handle), 12 (`AslrRegionAddress`), 13 (`AslrRegionSize`), 14 (`StackRegionAddress`), 15 (`StackRegionSize`), 16 (`SystemResourceSizeTotal`), 17 (`SystemResourceSizeUsed`), 21 (`TotalNonSystemMemorySize`), and 22 (`UsedNonSystemMemorySize`). Types 4–7 were not used.
- Observed low-byte memory states were Free `0x0`, Code `0x4DC7E03`, CodeData `0xFFEBD04`, Normal `0x77EBD05`, Stack `0x45C3C0B`, ThreadLocal `0x400000C`, and Inaccessible `0x10`.

Verification: `smoke_boundary_inventory_unit` passed with `cl` and `/Zc:preprocessor` (exit 0); the JIT host `rebuild-suyu.bat` completed 24/24 steps with 0 errors and linked `suyu-cmd.exe`. The full `tests/recompiler_smoke/run.py` suite was not run, and the no-JIT tree was not rebuilt. Merged evidence and scripts remain local at `D:\HisuiWakeBuild\diag\n0\test4\memory_merged.txt`, `D:\HisuiWakeBuild\diag\n0\merge_memory.py`, and `D:\HisuiWakeBuild\diag\n0\summarize_memory.py`.

## Stage 1 results

`src/core/native_memory/native_address_space.h` is a new header-only, unconnected mapper. `ComputeLayout` ports `KPageTableBase::InitializeForProcess` placement for 39-, 36-, and 32-bit spaces (including 32-bit without alias), accepts an injectable inclusive random range, and rejects invalid code ranges; `AddressSpace` implements the Stage 1 map/query/protection, heap, physical/shared-memory, TLS, and stack-alias operations, while `SetupForIpc` returns `NotSupported`.

The standalone unit verified the two observed test4 layouts, 800 randomized layouts (200 seeds across four address-space types) inside disjoint spaces, and the 3,232,497,664-byte map / 3,228,565,504-byte unmap against a 4 GiB capacity. Stage 1 findings also confirm that kernel `CheckMemoryState` requires uniform state and permission over a range, while the observed kernel-map, stack, alias, and heap regions were gapless and ordered because ASLR region randomization was effectively zero; only the code-derived base moved. The MSVC standalone build passed with exit 0 and `All synthetic native memory smoke checks passed.`; all 14 header mutants, built from the CMake function's output in `cmake -P` script mode, exited 1 with their intended failing check. Codex Code review found four P2 items: MapPhysicalMemory now counts any non-Free page as already mapped, UnmapPhysicalMemory accepts Normal pages with any permission attributes, UnmapMemory spans adjacent alias records whose page pairs were mapped together, and `HeapGrowthIsCappedByPhysicalCapacity` plus `heap-capacity` coverage was added; the block-boundary item remains for Stage 2 because the kernel preserves disable-merge attributes while the mapper only coalesces identical neighbours. The new `UnmapSpansAdjacentAliasRecords` test uses `alias-span`, and the physical-memory test has extra map/unmap cases. The full `tests/recompiler_smoke/run.py` suite was not run, and a real CMake configure was not run.

## Stage 2 results

Stage 2 adds an opt-in shadow comparison of the native mapper against `KPageTable` through `SUYU_NATIVE_MEM_SHADOW=1` (off unless exactly `1`, read when the process page table initializes). `Core::NativeMemory::Shadow` owns an `AddressSpace`, mutex, per-operation counters for `compared`, `result_mismatch`, `state_mismatch`, `boundary_only`, and `skipped`, synchronization-failure tracking, a bounded 32-entry pending-report list, and `Summary()`. The seven shadowed operations are `MapMemory`, `UnmapMemory`, `SetMemoryPermission`, `SetMemoryAttribute`, `SetHeapSize`, `MapPhysicalMemory`, and `UnmapPhysicalMemory`; `SetMaxHeapSize` is mirrored as part of heap handling.

The glue observes the complete real block table and `GetNormalMemorySize()` before the real operation, observes touched ranges afterward, then re-adopts the pre-operation table and runs the same operation under the shadow mutex. Overlapping hooked calls are skipped and counted. `AddressSpace::ReplaceBlocks` preserves observed boundaries when they tile the address space and derives the heap end from the leading Normal run; walks stop at the address-space end or a non-advancing block, and request ends saturate. Kernel result codes outside the modeled set map to `Result::Other`.

Kernel logs are `native_mem_shadow enabled`; warning lines `native_mem_shadow mismatch: <Op> addr=0x.. size=0x.. <detail>` (at most 32 pending reports per call); periodic summaries `native_mem_shadow compared/result/state/boundary/skipped: sync_failures=N <Op>=compared/result/state/boundary/skipped ...` every 64 compared operations; and `native_mem_shadow final ...` at `Finalize`. Result-class differences, state/permission/attribute differences, and non-tiling synchronization failures are mismatches. Differences only in block base/size, such as merge boundaries or disable-merge heads, are `boundary_only` and are not mismatches.

The 12-check MSVC `smoke_native_shadow_unit` passed with `cl /W4 /WX` (exit 0), all 14 shadow mutants failed at their intended `CHECK` lines, `smoke_native_memory_unit` still passes, and the `svc_memory.cpp.obj` glue compile check succeeded with no warnings. No full host or no-JIT rebuild, real test CMake configure, full `run.py`, or real enabled shadow session was verified; `Common::PageTable` entries are not compared, alias validity still uses alias records, unhooked mutators are absorbed by re-adoption, and a different real physical resource limit than the 4 GiB shadow constant may produce a result mismatch.

## Risks

- `SetupForIpc` map-alias semantics may differ from ordinary mappings.
- `KernelCore` initialization remains needed by kernel objects until N3/N4.
- GPU physical addresses must preserve their `DeviceMemory` offset meaning.
- The expected performance effect is none or neutral; the observed inventory is about 1,500 memory SVCs over a session, so performance claims require measurement.
- Stage 2 shadow comparison now reports block-boundary and merging differences as `boundary_only`: the kernel keeps disable-merge attributes (for example on each `MapMemory` head), while the mapper only coalesces identical neighbours, so `QueryMemory` base/size may differ without being a state mismatch.
- `MapMemory` source/destination validity currently relies on alias records rather than physical page groups; the HAS_NCE 256 GiB map limit is not modeled.
- IPC map-alias, transfer memory, device mapping, code memory, and `ProcessMemoryPermission` are not modeled; physical addresses and `DeviceMemory` backing are not modeled yet.

## Decisions

- Version is `v0.0.3`; per-commit entries count from `v0.0.3-dev1`.
- N2 proceeds stage by stage (Stage 0 through Stage 5), with a report, file list, suggested commit message, and wait for the user's commit after each stage.
- N2 uses the existing no-JIT tree `D:\HisuiWakeBuild\msvc-release-nojit`; a CMake option, working name `SUYU_NATIVE_MEMORY`, will be confirmed in Stage 3/4; there is no third build tree.
- The native address layout is exactly `KPageTable`'s layout, enabling shadow comparison.
- The repository moved to `https://github.com/project-aevora/project.aevora-main`; the old `Azuloma/project.aevora-main` URL redirects.

## Open questions

- Should `SUYU_NATIVE_MEM` default on when Stage 3 begins?
- Is the final option name `SUYU_NATIVE_MEM` or another confirmed name (the earlier working name is `SUYU_NATIVE_MEMORY`)?
- How will N3 TLS and stack setup consume the native mapper?
- What `KernelCore` memory initialization must remain until N3/N4, and when can it be bypassed?
- Does the bag screen or quit path exercise additional memory or IPC behavior? (They were not segmented in test4.)
- What region-base policy should native-only mode use, given per-process base randomization?
