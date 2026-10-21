# N2 Native Guest Memory Design

Status: Stage 0 design note, 2026-10-21 (session 43). This document records verified observations and the staged plan for replacing the supported-path kernel memory manager with a native guest mapper.

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

- The `KProcessPageTable` to native-mapper object seam is high confidence because the SVC forwarding path is concentrated there; the exact object lifetime and ownership are to be verified in Stage 1.
- The `KProcess::Initialize`/`LoadFromMetadata` to address-space allocator seam is medium-high confidence; the exact layout must be fixed from the user's Stage 0 collection.
- IPC session mapping is medium confidence until map-alias kinds and sizes are collected and `SetupForIpc` semantics are tested.
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

The address layout is exactly the same as `KPageTable`'s layout so that `SUYU_NATIVE_MEM_SHADOW=1` can compare the native mapper with the existing implementation.

## Stage plan and verification

| Stage | Work | Verification |
| --- | --- | --- |
| 0 | Record the design note and collect the opt-in detailed memory inventory. | User review/commit, then a real collection play; analyze `[memory]` rows. |
| 1 | Build an unconnected native mapper under `src/core/native_memory/`; implement map, unmap, protect, shared memory, query, heap, and physical memory, each with a deliberately weakened negative control. | Standalone unit tests pass and each negative control fails fast. |
| 2 | Add shadow comparison behind `SUYU_NATIVE_MEM_SHADOW=1`. | Compare per-SVC results, `QueryMemory` output, and page entries; supported-path mismatch count is zero. |
| 3 | Make the mapper primary behind `SUYU_NATIVE_MEM` (`=0` disables), and rewire TLS, shared/transfer memory, IPC mapping, nvmap, and vi. | Fallbacks are counted/logged; supported-path play and CPU ms/frame are recorded; option name/default are confirmed. |
| 4 | Remove kernel memory-manager symbols from the no-JIT native build, add fail-fast stubs for unsupported callers, and bypass `KernelCore` memory initialization. | `--require-absent N1,N2` passes and unsupported calls name the operation and address. |
| 5 | Full export, supported-path play, gate, measurement, and documentation acceptance. | User-observed checklist, structural gate, and CPU ms/frame report. |

## Stage 0 inventory questions

The user's collection should answer which `GetInfo` types and subtypes occur; which region/state combinations occur; which IPC map-alias kinds and sizes occur; how many TLS pages and what shared-memory sizes occur on the supported path; and the process-layout line. The process-layout ranges are needed to fix the exact native address layout and contract against `KPageTable`.

Collection uses `D:\HisuiWakeBuild\diag\n0\run_n0.bat <game> <label>` on the JIT host frontend: assign Player 1 in the overlay Controller window, press F10 to start recording, F9 to end a labeled segment, and quit normally. The new `[memory]` section appears in each local `inventory_<run>_NN_<label>.txt`; it is not committed.

## Risks

- `SetupForIpc` map-alias semantics may differ from ordinary mappings.
- `KernelCore` initialization remains needed by kernel objects until N3/N4.
- GPU physical addresses must preserve their `DeviceMemory` offset meaning.
- The expected performance effect is none or neutral; the observed inventory is about 1,500 memory SVCs over a session, so performance claims require measurement.

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
