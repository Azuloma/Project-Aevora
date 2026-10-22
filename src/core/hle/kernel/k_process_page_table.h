// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

#include "common/env_option.h"
#include "common/logging.h"
#include "core/hle/kernel/k_native_memory_adapters.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_scoped_lock.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/kernel/svc_types.h"
#include "core/native_memory/native_shadow.h"

namespace Kernel {

namespace NativeMemory = Core::NativeMemory;

class KProcessPageTable {
private:
    // Matches the default DeviceMemory backing; the real limit is the process resource limit.
    static constexpr u64 ShadowPhysicalCapacityBytes = 4ULL << 30;
    static constexpr u64 ShadowSummaryInterval = 64;

    KPageTable m_page_table;
    std::unique_ptr<NativeMemory::Shadow> m_shadow;
    // High 32 bits count entries, low 32 bits count calls in flight; one atomic keeps them consistent.
    static constexpr u64 ShadowEntryUnit = (1ULL << 32) | 1;
    std::atomic<u64> m_shadow_state{0};

    // Unhooked mutators mark an overlap so a concurrent comparison is skipped, not misjudged.
    class ShadowBystander {
    public:
        explicit ShadowBystander(KProcessPageTable& table)
            : m_table(table), m_counted(table.m_shadow != nullptr) {
            if (m_counted) {
                m_table.m_shadow_state.fetch_add(ShadowEntryUnit);
            }
        }
        ~ShadowBystander() {
            if (m_counted) {
                m_table.m_shadow_state.fetch_sub(1);
            }
        }
        ShadowBystander(const ShadowBystander&) = delete;
        ShadowBystander& operator=(const ShadowBystander&) = delete;

    private:
        KProcessPageTable& m_table;
        bool m_counted;
    };

    static bool IsShadowRequested() {
        const char* value = std::getenv("SUYU_NATIVE_MEM_SHADOW");
        return Common::EnvFlagDefaultOff(value ? value : "");
    }

    static NativeMemory::Result ClassifyResult(Result result) {
        if (result.IsSuccess()) {
            return NativeMemory::Result::Success;
        }
        if (result == ResultInvalidAddress) {
            return NativeMemory::Result::InvalidAddress;
        }
        if (result == ResultInvalidSize) {
            return NativeMemory::Result::InvalidSize;
        }
        if (result == ResultInvalidMemoryRegion) {
            return NativeMemory::Result::InvalidMemoryRegion;
        }
        if (result == ResultInvalidCurrentMemory) {
            return NativeMemory::Result::InvalidCurrentMemory;
        }
        if (result == ResultInvalidNewMemoryPermission) {
            return NativeMemory::Result::InvalidNewMemoryPermission;
        }
        if (result == ResultInvalidCombination) {
            return NativeMemory::Result::InvalidCombination;
        }
        if (result == ResultOutOfMemory) {
            return NativeMemory::Result::OutOfMemory;
        }
        if (result == ResultLimitReached) {
            return NativeMemory::Result::LimitReached;
        }
        return NativeMemory::Result::Other;
    }

    NativeMemory::Layout ObserveLayout() const {
        NativeMemory::Layout layout;
        layout.width = m_page_table.GetAddressSpaceWidth();
        layout.address_space = {GetInteger(m_page_table.GetAddressSpaceStart()),
                                m_page_table.GetAddressSpaceSize()};
        layout.code = {GetInteger(m_page_table.GetCodeRegionStart()),
                       m_page_table.GetCodeRegionSize()};
        layout.alias_code = {GetInteger(m_page_table.GetAliasCodeRegionStart()),
                             m_page_table.GetAliasCodeRegionSize()};
        layout.alias = {GetInteger(m_page_table.GetAliasRegionStart()),
                        m_page_table.GetAliasRegionSize()};
        layout.heap = {GetInteger(m_page_table.GetHeapRegionStart()),
                       m_page_table.GetHeapRegionSize()};
        layout.stack = {GetInteger(m_page_table.GetStackRegionStart()),
                        m_page_table.GetStackRegionSize()};
        layout.kernel_map = {GetInteger(m_page_table.GetKernelMapRegionStart()),
                             m_page_table.GetKernelMapRegionSize()};
        return layout;
    }

    std::vector<NativeMemory::StoredBlock> ObserveBlocks(u64 start, u64 end) const {
        std::vector<NativeMemory::StoredBlock> blocks;
        const u64 space_end =
            GetInteger(m_page_table.GetAddressSpaceStart()) + m_page_table.GetAddressSpaceSize();
        for (u64 cursor = start; cursor < end;) {
            KMemoryInfo info;
            Svc::PageInfo page_info;
            if (m_page_table.QueryInfo(std::addressof(info), std::addressof(page_info), cursor)
                    .IsError() ||
                info.GetAddress() >= space_end || info.GetEndAddress() <= cursor) {
                break;
            }
            blocks.push_back({info.GetAddress(), info.GetSize(),
                              static_cast<NativeMemory::MemoryState>(info.GetState()),
                              static_cast<u8>(info.GetPermission()),
                              static_cast<u8>(info.GetAttribute())});
            cursor = info.GetEndAddress();
        }
        return blocks;
    }

    // Saturates instead of wrapping so an out-of-space request cannot restart the walk.
    static u64 SaturatedEnd(u64 address, u64 size) {
        return address + size < address ? ~0ULL : address + size;
    }

    void LogShadowReports() {
        for (const std::string& report : m_shadow->TakeReports()) {
            LOG_WARNING(Kernel, "native_mem_shadow mismatch: {}", report);
        }
        u64 compared = 0;
        for (u32 op = 0; op < static_cast<u32>(NativeMemory::ShadowOp::Count); ++op) {
            compared += m_shadow->Counts(static_cast<NativeMemory::ShadowOp>(op)).compared;
        }
        if (compared % ShadowSummaryInterval == 0) {
            LOG_INFO(Kernel, "native_mem_shadow compared/result/state/boundary/skipped: {}",
                     m_shadow->Summary());
        }
    }

    // Kernel locks are taken only outside the shadow mutex; overlapping calls are skipped, not compared.
    template <typename RealFn, typename ShadowFn>
    Result RunShadowed(NativeMemory::ShadowOp op, u64 address, u64 size, u64 second_address,
                       RealFn&& real_fn, ShadowFn&& shadow_fn) {
        if (!m_shadow) {
            R_RETURN(real_fn());
        }
        const u64 entered_state = m_shadow_state.fetch_add(ShadowEntryUnit);
        const bool entered_alone = (entered_state & 0xFFFFFFFFULL) == 0;
        const u64 entry_count = (entered_state >> 32) + 1;
        const u64 space_start = GetInteger(m_page_table.GetAddressSpaceStart());
        const auto before = ObserveBlocks(space_start, space_start + m_page_table.GetAddressSpaceSize());
        const u64 normal_bytes_before = m_page_table.GetNormalMemorySize();
        const Result real = real_fn();
        const auto after = ObserveBlocks(address, SaturatedEnd(address, size));
        const auto second_after = second_address == 0
                                      ? std::vector<NativeMemory::StoredBlock>{}
                                      : ObserveBlocks(second_address, SaturatedEnd(second_address, size));
        const bool overlapped = !entered_alone || (m_shadow_state.load() >> 32) != entry_count;
        m_shadow_state.fetch_sub(1);

        {
            std::scoped_lock lk{m_shadow->Mutex()};
            if (overlapped) {
                m_shadow->CountSkipped(op);
            } else {
                m_shadow->Sync(before, normal_bytes_before);
                const bool real_hit_limit =
                    ClassifyResult(real) == NativeMemory::Result::LimitReached;
                const u64 saved_capacity_bytes =
                    real_hit_limit ? m_shadow->Space().SetPhysicalCapacityBytes(normal_bytes_before)
                                   : 0;
                const NativeMemory::Result shadow = shadow_fn(m_shadow->Space());
                if (real_hit_limit) {
                    m_shadow->Space().SetPhysicalCapacityBytes(saved_capacity_bytes);
                }
                m_shadow->CompareResult(op, address, size, ClassifyResult(real), shadow);
                m_shadow->CompareBlocks(op, address, size, after, before);
                if (second_address != 0) {
                    m_shadow->CompareBlocks(op, second_address, size, second_after, before);
                }
            }
            LogShadowReports();
        }
        R_RETURN(real);
    }

public:
    KProcessPageTable(KernelCore& kernel) : m_page_table(kernel) {}

    Result Initialize(Svc::CreateProcessFlag as_type, bool enable_aslr, bool enable_das_merge,
                      bool from_back, KMemoryManager::Pool pool, KProcessAddress code_address,
                      size_t code_size, KSystemResource* system_resource,
                      KResourceLimit* resource_limit, Core::Memory::Memory& memory,
                      KProcessAddress aslr_space_start) {
        R_TRY(m_page_table.InitializeForProcess(as_type, enable_aslr, enable_das_merge, from_back,
                                                pool, code_address, code_size, system_resource,
                                                resource_limit, memory, aslr_space_start));
        if (IsShadowRequested()) {
            m_shadow = std::make_unique<NativeMemory::Shadow>(ObserveLayout(),
                                                              ShadowPhysicalCapacityBytes);
            LOG_INFO(Kernel, "native_mem_shadow enabled");
        }
        R_SUCCEED();
    }

    void Finalize() {
        if (m_shadow) {
            LOG_INFO(Kernel, "native_mem_shadow final compared/result/state/boundary/skipped: {}",
                     m_shadow->Summary());
            m_shadow.reset();
        }
        m_page_table.Finalize();
    }

    void AllowDeviceMappingOfExecPages() {
        m_page_table.AllowDeviceMappingOfExecPages();
    }

    [[nodiscard]] Core::Memory::Memory& GetMemory() noexcept { return m_page_table.GetMemory(); }
    [[nodiscard]] Core::Memory::Memory const& GetMemory() const noexcept { return m_page_table.GetMemory(); }
    [[nodiscard]] Common::PageTable& GetImpl() noexcept { return m_page_table.GetImpl(); }
    [[nodiscard]] Common::PageTable const& GetImpl() const noexcept { return m_page_table.GetImpl(); }

    size_t GetNumGuardPages() const {
        return m_page_table.GetNumGuardPages();
    }

    KScopedLightLock AcquireDeviceMapLock() {
        return m_page_table.AcquireDeviceMapLock();
    }

    Result SetMemoryPermission(KProcessAddress addr, size_t size, Svc::MemoryPermission perm) {
        const u32 raw_permission = static_cast<u32>(perm);
        const auto shadow_permission = static_cast<NativeMemory::Permission>(
            raw_permission <= 3 ? raw_permission : 0xFF);
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::SetMemoryPermission, GetInteger(addr), size, 0,
            [&] { return m_page_table.SetMemoryPermission(addr, size, perm); },
            [&](NativeMemory::AddressSpace& space) {
                return space.SetMemoryPermission(GetInteger(addr), size, shadow_permission);
            }));
    }

    Result SetProcessMemoryPermission(KProcessAddress addr, size_t size,
                                      Svc::MemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.SetProcessMemoryPermission(addr, size, perm));
    }

    Result SetMemoryAttribute(KProcessAddress addr, size_t size, KMemoryAttribute mask,
                              KMemoryAttribute attr) {
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::SetMemoryAttribute, GetInteger(addr), size, 0,
            [&] { return m_page_table.SetMemoryAttribute(addr, size, mask, attr); },
            [&](NativeMemory::AddressSpace& space) {
                return space.SetMemoryAttribute(GetInteger(addr), size, static_cast<u8>(mask),
                                                static_cast<u8>(attr));
            }));
    }

    Result SetHeapSize(KProcessAddress* out, size_t size) {
        u64 shadow_out = 0;
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::SetHeapSize, GetInteger(m_page_table.GetHeapRegionStart()),
            m_page_table.GetHeapRegionSize(), 0,
            [&] { return m_page_table.SetHeapSize(out, size); },
            [&](NativeMemory::AddressSpace& space) { return space.SetHeapSize(shadow_out, size); }));
    }

    Result SetMaxHeapSize(size_t size) {
        R_TRY(m_page_table.SetMaxHeapSize(size));
        if (m_shadow) {
            std::scoped_lock lk{m_shadow->Mutex()};
            m_shadow->Space().SetMaxHeapSize(size);
        }
        R_SUCCEED();
    }

    Result QueryInfo(KMemoryInfo* out_info, Svc::PageInfo* out_page_info,
                     KProcessAddress addr) const {
        R_RETURN(m_page_table.QueryInfo(out_info, out_page_info, addr));
    }

    Result QueryPhysicalAddress(Svc::lp64::PhysicalMemoryInfo* out, KProcessAddress address) {
        R_RETURN(m_page_table.QueryPhysicalAddress(out, address));
    }

    Result QueryStaticMapping(KProcessAddress* out, KPhysicalAddress address, size_t size) {
        R_RETURN(m_page_table.QueryStaticMapping(out, address, size));
    }

    Result QueryIoMapping(KProcessAddress* out, KPhysicalAddress address, size_t size) {
        R_RETURN(m_page_table.QueryIoMapping(out, address, size));
    }

    Result MapMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::MapMemory, GetInteger(dst_address), size,
            GetInteger(src_address),
            [&] { return m_page_table.MapMemory(dst_address, src_address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.MapMemory(GetInteger(dst_address), GetInteger(src_address), size);
            }));
    }

    Result UnmapMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::UnmapMemory, GetInteger(dst_address), size,
            GetInteger(src_address),
            [&] { return m_page_table.UnmapMemory(dst_address, src_address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.UnmapMemory(GetInteger(dst_address), GetInteger(src_address), size);
            }));
    }

    Result MapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapCodeMemory(dst_address, src_address, size));
    }

    Result UnmapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnmapCodeMemory(dst_address, src_address, size));
    }

    Result MapIo(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapIo(phys_addr, size, perm));
    }

    Result MapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr, size_t size,
                       Svc::MemoryMapping mapping, Svc::MemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapIoRegion(dst_address, phys_addr, size, mapping, perm));
    }

    Result UnmapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr, size_t size,
                         Svc::MemoryMapping mapping) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnmapIoRegion(dst_address, phys_addr, size, mapping));
    }

    Result MapStatic(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapStatic(phys_addr, size, perm));
    }

    Result MapRegion(KMemoryRegionType region_type, KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapRegion(region_type, perm));
    }

    Result MapInsecureMemory(KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapInsecureMemory(address, size));
    }

    Result UnmapInsecureMemory(KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnmapInsecureMemory(address, size));
    }

    Result MapPageGroup(KProcessAddress addr, const KPageGroup& pg, KMemoryState state,
                        KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapPageGroup(addr, pg, state, perm));
    }

    Result UnmapPageGroup(KProcessAddress address, const KPageGroup& pg, KMemoryState state) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnmapPageGroup(address, pg, state));
    }

    Result MapPages(KProcessAddress* out_addr, size_t num_pages, size_t alignment,
                    KPhysicalAddress phys_addr, KMemoryState state, KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapPages(out_addr, num_pages, alignment, phys_addr, state, perm));
    }

    Result MapPages(KProcessAddress* out_addr, size_t num_pages, KMemoryState state,
                    KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapPages(out_addr, num_pages, state, perm));
    }

    Result MapPages(KProcessAddress address, size_t num_pages, KMemoryState state,
                    KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapPages(address, num_pages, state, perm));
    }

    Result UnmapPages(KProcessAddress addr, size_t num_pages, KMemoryState state) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnmapPages(addr, num_pages, state));
    }

    Result MakeAndOpenPageGroup(KPageGroup* out, KProcessAddress address, size_t num_pages,
                                KMemoryState state_mask, KMemoryState state,
                                KMemoryPermission perm_mask, KMemoryPermission perm,
                                KMemoryAttribute attr_mask, KMemoryAttribute attr) {
        R_RETURN(m_page_table.MakeAndOpenPageGroup(out, address, num_pages, state_mask, state,
                                                   perm_mask, perm, attr_mask, attr));
    }

    Result InvalidateProcessDataCache(KProcessAddress address, size_t size) {
        R_RETURN(m_page_table.InvalidateProcessDataCache(address, size));
    }

    Result ReadDebugMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        R_RETURN(m_page_table.ReadDebugMemory(dst_address, src_address, size));
    }

    Result ReadDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size,
                             KMemoryState state) {
        R_RETURN(m_page_table.ReadDebugIoMemory(dst_address, src_address, size, state));
    }

    Result WriteDebugMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        R_RETURN(m_page_table.WriteDebugMemory(dst_address, src_address, size));
    }

    Result WriteDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size,
                              KMemoryState state) {
        R_RETURN(m_page_table.WriteDebugIoMemory(dst_address, src_address, size, state));
    }

    Result LockForMapDeviceAddressSpace(bool* out_is_io, KProcessAddress address, size_t size,
                                        KMemoryPermission perm, bool is_aligned, bool check_heap) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.LockForMapDeviceAddressSpace(out_is_io, address, size, perm,
                                                           is_aligned, check_heap));
    }

    Result LockForUnmapDeviceAddressSpace(KProcessAddress address, size_t size, bool check_heap) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.LockForUnmapDeviceAddressSpace(address, size, check_heap));
    }

    Result UnlockForDeviceAddressSpace(KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnlockForDeviceAddressSpace(address, size));
    }

    Result UnlockForDeviceAddressSpacePartialMap(KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnlockForDeviceAddressSpacePartialMap(address, size));
    }

    Result OpenMemoryRangeForMapDeviceAddressSpace(KPageTableBase::MemoryRange* out,
                                                   KProcessAddress address, size_t size,
                                                   KMemoryPermission perm, bool is_aligned) {
        R_RETURN(m_page_table.OpenMemoryRangeForMapDeviceAddressSpace(out, address, size, perm,
                                                                      is_aligned));
    }

    Result OpenMemoryRangeForUnmapDeviceAddressSpace(KPageTableBase::MemoryRange* out,
                                                     KProcessAddress address, size_t size) {
        R_RETURN(m_page_table.OpenMemoryRangeForUnmapDeviceAddressSpace(out, address, size));
    }

    Result LockForIpcUserBuffer(KPhysicalAddress* out, KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.LockForIpcUserBuffer(out, address, size));
    }

    Result UnlockForIpcUserBuffer(KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnlockForIpcUserBuffer(address, size));
    }

    Result LockForTransferMemory(KPageGroup* out, KProcessAddress address, size_t size,
                                 KMemoryPermission perm) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.LockForTransferMemory(out, address, size, perm));
    }

    Result UnlockForTransferMemory(KProcessAddress address, size_t size, const KPageGroup& pg) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnlockForTransferMemory(address, size, pg));
    }

    Result LockForCodeMemory(KPageGroup* out, KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.LockForCodeMemory(out, address, size));
    }

    Result UnlockForCodeMemory(KProcessAddress address, size_t size, const KPageGroup& pg) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnlockForCodeMemory(address, size, pg));
    }

    Result OpenMemoryRangeForProcessCacheOperation(KPageTableBase::MemoryRange* out,
                                                   KProcessAddress address, size_t size) {
        R_RETURN(m_page_table.OpenMemoryRangeForProcessCacheOperation(out, address, size));
    }

    Result CopyMemoryFromLinearToUser(KProcessAddress dst_addr, size_t size,
                                      KProcessAddress src_addr, KMemoryState src_state_mask,
                                      KMemoryState src_state, KMemoryPermission src_test_perm,
                                      KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
        R_RETURN(m_page_table.CopyMemoryFromLinearToUser(dst_addr, size, src_addr, src_state_mask,
                                                         src_state, src_test_perm, src_attr_mask,
                                                         src_attr));
    }

    Result CopyMemoryFromLinearToKernel(void* dst_addr, size_t size, KProcessAddress src_addr,
                                        KMemoryState src_state_mask, KMemoryState src_state,
                                        KMemoryPermission src_test_perm,
                                        KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
        R_RETURN(m_page_table.CopyMemoryFromLinearToKernel(dst_addr, size, src_addr, src_state_mask,
                                                           src_state, src_test_perm, src_attr_mask,
                                                           src_attr));
    }

    Result CopyMemoryFromUserToLinear(KProcessAddress dst_addr, size_t size,
                                      KMemoryState dst_state_mask, KMemoryState dst_state,
                                      KMemoryPermission dst_test_perm,
                                      KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                      KProcessAddress src_addr) {
        R_RETURN(m_page_table.CopyMemoryFromUserToLinear(dst_addr, size, dst_state_mask, dst_state,
                                                         dst_test_perm, dst_attr_mask, dst_attr,
                                                         src_addr));
    }

    Result CopyMemoryFromKernelToLinear(KProcessAddress dst_addr, size_t size,
                                        KMemoryState dst_state_mask, KMemoryState dst_state,
                                        KMemoryPermission dst_test_perm,
                                        KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                        void* src_addr) {
        R_RETURN(m_page_table.CopyMemoryFromKernelToLinear(dst_addr, size, dst_state_mask,
                                                           dst_state, dst_test_perm, dst_attr_mask,
                                                           dst_attr, src_addr));
    }

    Result CopyMemoryFromHeapToHeap(KProcessPageTable& dst_page_table, KProcessAddress dst_addr,
                                    size_t size, KMemoryState dst_state_mask,
                                    KMemoryState dst_state, KMemoryPermission dst_test_perm,
                                    KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                    KProcessAddress src_addr, KMemoryState src_state_mask,
                                    KMemoryState src_state, KMemoryPermission src_test_perm,
                                    KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
        R_RETURN(m_page_table.CopyMemoryFromHeapToHeap(
            dst_page_table.m_page_table, dst_addr, size, dst_state_mask, dst_state, dst_test_perm,
            dst_attr_mask, dst_attr, src_addr, src_state_mask, src_state, src_test_perm,
            src_attr_mask, src_attr));
    }

    Result CopyMemoryFromHeapToHeapWithoutCheckDestination(
        KProcessPageTable& dst_page_table, KProcessAddress dst_addr, size_t size,
        KMemoryState dst_state_mask, KMemoryState dst_state, KMemoryPermission dst_test_perm,
        KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr, KProcessAddress src_addr,
        KMemoryState src_state_mask, KMemoryState src_state, KMemoryPermission src_test_perm,
        KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
        R_RETURN(m_page_table.CopyMemoryFromHeapToHeapWithoutCheckDestination(
            dst_page_table.m_page_table, dst_addr, size, dst_state_mask, dst_state, dst_test_perm,
            dst_attr_mask, dst_attr, src_addr, src_state_mask, src_state, src_test_perm,
            src_attr_mask, src_attr));
    }

    Result SetupForIpc(KProcessAddress* out_dst_addr, size_t size, KProcessAddress src_addr,
                       KProcessPageTable& src_page_table, KMemoryPermission test_perm,
                       KMemoryState dst_state, bool send) {
        ShadowBystander bystander{*this};
        ShadowBystander source_bystander{src_page_table};
        R_RETURN(m_page_table.SetupForIpc(out_dst_addr, size, src_addr, src_page_table.m_page_table,
                                          test_perm, dst_state, send));
    }

    Result CleanupForIpcServer(KProcessAddress address, size_t size, KMemoryState dst_state) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.CleanupForIpcServer(address, size, dst_state));
    }

    Result CleanupForIpcClient(KProcessAddress address, size_t size, KMemoryState dst_state) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.CleanupForIpcClient(address, size, dst_state));
    }

    Result MapPhysicalMemory(KProcessAddress address, size_t size) {
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::MapPhysicalMemory, GetInteger(address), size, 0,
            [&] { return m_page_table.MapPhysicalMemory(address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.MapPhysicalMemory(GetInteger(address), size);
            }));
    }

    Result UnmapPhysicalMemory(KProcessAddress address, size_t size) {
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::UnmapPhysicalMemory, GetInteger(address), size, 0,
            [&] { return m_page_table.UnmapPhysicalMemory(address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.UnmapPhysicalMemory(GetInteger(address), size);
            }));
    }

    Result MapPhysicalMemoryUnsafe(KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.MapPhysicalMemoryUnsafe(address, size));
    }

    Result UnmapPhysicalMemoryUnsafe(KProcessAddress address, size_t size) {
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table.UnmapPhysicalMemoryUnsafe(address, size));
    }

    Result UnmapProcessMemory(KProcessAddress dst_address, size_t size,
                              KProcessPageTable& src_page_table, KProcessAddress src_address) {
        ShadowBystander bystander{*this};
        ShadowBystander source_bystander{src_page_table};
        R_RETURN(m_page_table.UnmapProcessMemory(dst_address, size, src_page_table.m_page_table,
                                                 src_address));
    }

    bool GetPhysicalAddress(KPhysicalAddress* out, KProcessAddress address) {
        return m_page_table.GetPhysicalAddress(out, address);
    }

    bool Contains(KProcessAddress addr, size_t size) const {
        return m_page_table.Contains(addr, size);
    }

    bool IsInAliasRegion(KProcessAddress addr, size_t size) const {
        return m_page_table.IsInAliasRegion(addr, size);
    }
    bool IsInHeapRegion(KProcessAddress addr, size_t size) const {
        return m_page_table.IsInHeapRegion(addr, size);
    }
    bool IsInUnsafeAliasRegion(KProcessAddress addr, size_t size) const {
        return m_page_table.IsInUnsafeAliasRegion(addr, size);
    }

    bool CanContain(KProcessAddress addr, size_t size, KMemoryState state) const {
        return m_page_table.CanContain(addr, size, state);
    }

    KProcessAddress GetAddressSpaceStart() const {
        return m_page_table.GetAddressSpaceStart();
    }
    KProcessAddress GetHeapRegionStart() const {
        return m_page_table.GetHeapRegionStart();
    }
    KProcessAddress GetAliasRegionStart() const {
        return m_page_table.GetAliasRegionStart();
    }
    KProcessAddress GetStackRegionStart() const {
        return m_page_table.GetStackRegionStart();
    }
    KProcessAddress GetKernelMapRegionStart() const {
        return m_page_table.GetKernelMapRegionStart();
    }
    KProcessAddress GetCodeRegionStart() const {
        return m_page_table.GetCodeRegionStart();
    }
    KProcessAddress GetAliasCodeRegionStart() const {
        return m_page_table.GetAliasCodeRegionStart();
    }

    size_t GetAddressSpaceSize() const {
        return m_page_table.GetAddressSpaceSize();
    }
    size_t GetHeapRegionSize() const {
        return m_page_table.GetHeapRegionSize();
    }
    size_t GetAliasRegionSize() const {
        return m_page_table.GetAliasRegionSize();
    }
    size_t GetAliasRegionExtraSize() const {
        return m_page_table.GetAliasRegionExtraSize();
    }
    size_t GetStackRegionSize() const {
        return m_page_table.GetStackRegionSize();
    }
    size_t GetKernelMapRegionSize() const {
        return m_page_table.GetKernelMapRegionSize();
    }
    size_t GetCodeRegionSize() const {
        return m_page_table.GetCodeRegionSize();
    }
    size_t GetAliasCodeRegionSize() const {
        return m_page_table.GetAliasCodeRegionSize();
    }

    size_t GetNormalMemorySize() const {
        return m_page_table.GetNormalMemorySize();
    }

    size_t GetCodeSize() const {
        return m_page_table.GetCodeSize();
    }
    size_t GetCodeDataSize() const {
        return m_page_table.GetCodeDataSize();
    }

    size_t GetAliasCodeSize() const {
        return m_page_table.GetAliasCodeSize();
    }
    size_t GetAliasCodeDataSize() const {
        return m_page_table.GetAliasCodeDataSize();
    }

    u32 GetAllocateOption() const {
        return m_page_table.GetAllocateOption();
    }

    u32 GetAddressSpaceWidth() const {
        return m_page_table.GetAddressSpaceWidth();
    }

    KPhysicalAddress GetHeapPhysicalAddress(KVirtualAddress address) {
        return m_page_table.GetHeapPhysicalAddress(address);
    }

    u8* GetHeapVirtualPointer(KPhysicalAddress address) {
        return m_page_table.GetHeapVirtualPointer(address);
    }

    KVirtualAddress GetHeapVirtualAddress(KPhysicalAddress address) {
        return m_page_table.GetHeapVirtualAddress(address);
    }

    KBlockInfoManager* GetBlockInfoManager() {
        return m_page_table.GetBlockInfoManager();
    }

    KPageTable& GetBasePageTable() {
        return m_page_table;
    }

    const KPageTable& GetBasePageTable() const {
        return m_page_table;
    }
};

} // namespace Kernel
