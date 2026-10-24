// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "common/env_option.h"
#include "common/logging.h"
#include "core/hle/kernel/k_native_address_space.h"
#include "core/hle/kernel/k_native_memory_adapters.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_scoped_lock.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/kernel/svc_types.h"
#include "core/native_memory/native_address_regions.h"
#include "core/native_memory/native_shadow.h"

namespace Kernel {

namespace NativeMemory = Core::NativeMemory;

// Declared in k_thread.h; the IPC copies write to and read from the calling process.
Core::Memory::Memory& GetCurrentMemory(KernelCore& kernel);

class KProcessPageTable;
// Drops cached translations of the range in every core's JIT of the process that owns the table.
void InvalidateInstructionCacheOfProcessTable(KernelCore& kernel, const KProcessPageTable* owner,
                                              KProcessAddress address, size_t size);

// Native model active: block state is in m_native, layout and Common::PageTable in m_native_space, and
// m_page_table is never built. Unsupported operations fail with a counted log line.
class KProcessPageTable {
private:
    // Matches the default DeviceMemory backing; the real limit is the process resource limit.
    static constexpr u64 ShadowPhysicalCapacityBytes = 4ULL << 30;
    static constexpr u64 ShadowSummaryInterval = 64;
    static constexpr u32 NativeAddressSpaceWidth = 39;

    // Exactly one of m_page_table (fallback) and m_native_space (native) exists after Initialize.
    std::unique_ptr<KPageTable> m_page_table;
    KernelCore& m_kernel;
    std::unique_ptr<KNativeAddressSpace> m_native_space;
    // The one source of region predicates and getters for both modes.
    std::optional<NativeMemory::AddressRegions> m_regions;
    size_t m_alias_region_extra_size{};
    std::unique_ptr<NativeAdapters::KNativeProcessMemory> m_native;
    std::atomic<u64> m_native_unsupported_ops{0};
    std::atomic<bool> m_native_final_logged{false};
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

    static bool IsNativeRequested() {
        const char* value = std::getenv("SUYU_NATIVE_MEM");
        return Common::EnvFlagDefaultOn(value ? value : "");
    }

    Result Unsupported(const char* operation, u64 address) {
        m_native_unsupported_ops.fetch_add(1);
        LOG_ERROR(Kernel,
                  "native_mem: {} at {:#x} is not supported by the native model (SUYU_NATIVE_MEM=0 "
                  "restores the kernel page table)",
                  operation, address);
        return ResultNotImplemented;
    }

    static bool ToNativePermission(KMemoryPermission permission, NativeMemory::Permission& out) {
        return NativeMemory::FromStoredPermission(static_cast<u8>(permission), out);
    }

    static NativeMemory::CopyRule MakeCopyRule(KMemoryState state_mask, KMemoryState state,
                                               KMemoryPermission test_permission,
                                               KMemoryAttribute attribute_mask,
                                               KMemoryAttribute attribute) {
        return {static_cast<u32>(state_mask), static_cast<u32>(state),
                static_cast<u8>(test_permission), static_cast<u8>(attribute_mask),
                static_cast<u8>(attribute)};
    }

    static bool IsAnonymousMapState(KMemoryState state) {
        return state == KMemoryState::Code || state == KMemoryState::CodeData ||
               state == KMemoryState::Stack;
    }

    // The linear side is read and written straight in DeviceMemory; the user side stays with the callers.
    Result ReadNativeLinear(KProcessAddress address, size_t size, const NativeMemory::CopyRule& rule,
                            void* out) {
        NativeAdapters::KernelPhysicalAccess access{m_kernel.System().DeviceMemory()};
        return NativeAdapters::ToKernelResult(
            m_native->WithSpace([&](const NativeMemory::MappedAddressSpace& space) {
                return space.ReadLinear(GetInteger(address), size, rule, access, out);
            }));
    }

    Result WriteNativeLinear(KProcessAddress address, size_t size, const NativeMemory::CopyRule& rule,
                             const void* data) {
        NativeAdapters::KernelPhysicalAccess access{m_kernel.System().DeviceMemory()};
        return NativeAdapters::ToKernelResult(
            m_native->WithSpace([&](const NativeMemory::MappedAddressSpace& space) {
                return space.WriteLinear(GetInteger(address), size, rule, access, data);
            }));
    }

    // A null destination rule skips the destination check; both tables must be native or neither.
    Result CopyNativeHeapToHeap(KProcessPageTable& dst_page_table, KProcessAddress dst_addr,
                                size_t size, const NativeMemory::CopyRule* dst_rule,
                                KProcessAddress src_addr, const NativeMemory::CopyRule& src_rule) {
        if (!m_native || !dst_page_table.m_native) {
            return Unsupported("CopyMemoryFromHeapToHeap between native and kernel page tables",
                               GetInteger(src_addr));
        }
        NativeAdapters::KernelPhysicalAccess access{m_kernel.System().DeviceMemory()};
        return NativeAdapters::ToKernelResult(NativeAdapters::KNativeProcessMemory::WithBoth(
            *m_native, *dst_page_table.m_native,
            [&](const NativeMemory::MappedAddressSpace& src,
                const NativeMemory::MappedAddressSpace& dst) {
                return NativeMemory::MappedAddressSpace::CopyLinear(
                    src, GetInteger(src_addr), src_rule, dst, GetInteger(dst_addr), dst_rule, size,
                    access);
            }));
    }

    void CreateNativeMemory(KResourceLimit* resource_limit) {
        m_native = std::make_unique<NativeAdapters::KNativeProcessMemory>(
            NativeAdapters::KNativeProcessMemory::Config{
                .kernel = m_kernel,
                .device_memory = m_kernel.System().DeviceMemory(),
                .block_info_manager = m_native_space->GetBlockInfoManager(),
                .memory = m_native_space->GetMemory(),
                .page_table = m_native_space->GetImpl(),
                .resource_limit = resource_limit,
                .allocate_option = m_native_space->GetAllocateOption(),
                .fill_value = KNativeAddressSpace::HeapFillValue,
                .layout = m_native_space->GetLayout(),
                .physical_capacity_bytes = ~0ULL,
            });
    }

    u64 NativeStateBytes(NativeMemory::MemoryState state) const {
        return m_native->WithSpace([&](const NativeMemory::MappedAddressSpace& space) {
            const u64 end = m_regions->AddressSpaceStart() + m_regions->AddressSpaceSize();
            u64 bytes = 0;
            for (u64 cursor = m_regions->AddressSpaceStart(); cursor < end;) {
                const NativeMemory::MemoryInfo info = space.Space().QueryInfo(cursor);
                if (info.state == state) {
                    bytes += info.size;
                }
                if (info.size == 0 || info.base + info.size <= cursor) {
                    break;
                }
                cursor = info.base + info.size;
            }
            return bytes;
        });
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
        layout.width = m_page_table->GetAddressSpaceWidth();
        layout.address_space = {GetInteger(m_page_table->GetAddressSpaceStart()),
                                m_page_table->GetAddressSpaceSize()};
        layout.code = {GetInteger(m_page_table->GetCodeRegionStart()),
                       m_page_table->GetCodeRegionSize()};
        layout.alias_code = {GetInteger(m_page_table->GetAliasCodeRegionStart()),
                             m_page_table->GetAliasCodeRegionSize()};
        layout.alias = {GetInteger(m_page_table->GetAliasRegionStart()),
                        m_page_table->GetAliasRegionSize()};
        layout.heap = {GetInteger(m_page_table->GetHeapRegionStart()),
                       m_page_table->GetHeapRegionSize()};
        layout.stack = {GetInteger(m_page_table->GetStackRegionStart()),
                        m_page_table->GetStackRegionSize()};
        layout.kernel_map = {GetInteger(m_page_table->GetKernelMapRegionStart()),
                             m_page_table->GetKernelMapRegionSize()};
        return layout;
    }

    std::vector<NativeMemory::StoredBlock> ObserveBlocks(u64 start, u64 end) const {
        std::vector<NativeMemory::StoredBlock> blocks;
        const u64 space_end =
            GetInteger(m_page_table->GetAddressSpaceStart()) + m_page_table->GetAddressSpaceSize();
        for (u64 cursor = start; cursor < end;) {
            KMemoryInfo info;
            Svc::PageInfo page_info;
            if (m_page_table->QueryInfo(std::addressof(info), std::addressof(page_info), cursor)
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
        const u64 space_start = GetInteger(m_page_table->GetAddressSpaceStart());
        const auto before = ObserveBlocks(space_start, space_start + m_page_table->GetAddressSpaceSize());
        const u64 normal_bytes_before = m_page_table->GetNormalMemorySize();
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
    KProcessPageTable(KernelCore& kernel) : m_kernel(kernel) {}

    Result Initialize(Svc::CreateProcessFlag as_type, bool enable_aslr, bool enable_das_merge,
                      bool from_back, KPhysicalMemory::Pool pool, KProcessAddress code_address,
                      size_t code_size, KSystemResource* system_resource,
                      KResourceLimit* resource_limit, Core::Memory::Memory& memory,
                      KProcessAddress aslr_space_start) {
        const size_t address_space_width = KPageTableBase::GetAddressSpaceWidth(as_type);
        if (IsNativeRequested()) {
            if (address_space_width == NativeAddressSpaceWidth) {
                m_native_space = std::make_unique<KNativeAddressSpace>(m_kernel);
                R_TRY(m_native_space->Initialize(as_type, enable_aslr, from_back, pool, code_address,
                                                 code_size, system_resource, memory,
                                                 aslr_space_start));
                m_regions.emplace(m_native_space->GetLayout());
                m_alias_region_extra_size = m_native_space->GetAliasRegionExtraSize();
                CreateNativeMemory(resource_limit);
                LOG_INFO(Kernel, "native_mem enabled");
                LOG_INFO(Kernel, "native_mem: KPageTable not constructed");
                if (IsShadowRequested()) {
                    LOG_WARNING(Kernel,
                                "native_mem_shadow ignored: the native model is the primary page table");
                }
                R_SUCCEED();
            }
            LOG_INFO(Kernel, "native_mem skipped for a {}-bit address space; using KPageTable",
                     address_space_width);
        }
        m_page_table = std::make_unique<KPageTable>(m_kernel);
        R_TRY(m_page_table->InitializeForProcess(as_type, enable_aslr, enable_das_merge, from_back,
                                                 pool, code_address, code_size, system_resource,
                                                 resource_limit, memory, aslr_space_start));
        m_regions.emplace(ObserveLayout());
        m_alias_region_extra_size = m_page_table->GetAliasRegionExtraSize();
        if (IsShadowRequested()) {
            m_shadow = std::make_unique<NativeMemory::Shadow>(ObserveLayout(),
                                                              ShadowPhysicalCapacityBytes);
            LOG_INFO(Kernel, "native_mem_shadow enabled");
        }
        R_SUCCEED();
    }

    // Logged once, from kernel shutdown or Finalize, whichever comes first.
    void LogNativeFinalStatistics() {
        if (!m_native || m_native_final_logged.exchange(true)) {
            return;
        }
        const u64 inconsistency = m_native->WithSpace([](const NativeMemory::MappedAddressSpace& space) {
            return space.InconsistencyCount();
        });
        LOG_INFO(Kernel, "native_mem final unsupported_ops={} inconsistency={}",
                 m_native_unsupported_ops.load(), inconsistency);
    }

    void Finalize() {
        LogNativeFinalStatistics();
        if (m_native) {
            m_native.reset();
            m_native_space.reset();
        }
        if (m_shadow) {
            LOG_INFO(Kernel, "native_mem_shadow final compared/result/state/boundary/skipped: {}",
                     m_shadow->Summary());
            m_shadow.reset();
        }
        if (m_page_table) {
            m_page_table->Finalize();
        }
    }

    void AllowDeviceMappingOfExecPages() {
        if (m_page_table) {
            m_page_table->AllowDeviceMappingOfExecPages();
        }
    }

    [[nodiscard]] Core::Memory::Memory& GetMemory() noexcept {
        return m_native_space ? m_native_space->GetMemory() : m_page_table->GetMemory();
    }
    [[nodiscard]] Core::Memory::Memory const& GetMemory() const noexcept {
        return m_native_space ? m_native_space->GetMemory() : m_page_table->GetMemory();
    }
    [[nodiscard]] Common::PageTable& GetImpl() noexcept {
        return m_native_space ? m_native_space->GetImpl() : m_page_table->GetImpl();
    }
    [[nodiscard]] Common::PageTable const& GetImpl() const noexcept {
        return m_native_space ? m_native_space->GetImpl() : m_page_table->GetImpl();
    }

    size_t GetNumGuardPages() const {
        return KPageTableBase::ProcessNumGuardPages;
    }

    KScopedLightLock AcquireDeviceMapLock() {
        return m_native_space ? KScopedLightLock(m_native_space->GetDeviceMapLock())
                              : m_page_table->AcquireDeviceMapLock();
    }

    Result SetMemoryPermission(KProcessAddress addr, size_t size, Svc::MemoryPermission perm) {
        if (m_native) {
            NativeMemory::Permission permission;
            R_UNLESS(ToNativePermission(ConvertToKMemoryPermission(perm), permission),
                     ResultInvalidNewMemoryPermission);
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.SetMemoryPermission(GetInteger(addr), size, permission);
            }));
        }
        const u32 raw_permission = static_cast<u32>(perm);
        const auto shadow_permission = static_cast<NativeMemory::Permission>(
            raw_permission <= 3 ? raw_permission : 0xFF);
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::SetMemoryPermission, GetInteger(addr), size, 0,
            [&] { return m_page_table->SetMemoryPermission(addr, size, perm); },
            [&](NativeMemory::AddressSpace& space) {
                return space.SetMemoryPermission(GetInteger(addr), size, shadow_permission);
            }));
    }

    Result SetProcessMemoryPermission(KProcessAddress addr, size_t size,
                                      Svc::MemoryPermission perm) {
        if (m_native) {
            NativeMemory::Permission permission;
            R_UNLESS(ToNativePermission(ConvertToKMemoryPermission(perm), permission),
                     ResultInvalidNewMemoryPermission);
            R_TRY(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.SetProcessMemoryPermission(GetInteger(addr), size, permission);
            }));
            if (perm == Svc::MemoryPermission::ReadExecute) {
                InvalidateInstructionCacheOfProcessTable(m_kernel, this, addr, size);
            }
            R_SUCCEED();
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->SetProcessMemoryPermission(addr, size, perm));
    }

    Result SetMemoryAttribute(KProcessAddress addr, size_t size, KMemoryAttribute mask,
                              KMemoryAttribute attr) {
        if (m_native) {
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.SetMemoryAttribute(GetInteger(addr), size, static_cast<u8>(mask),
                                                static_cast<u8>(attr));
            }));
        }
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::SetMemoryAttribute, GetInteger(addr), size, 0,
            [&] { return m_page_table->SetMemoryAttribute(addr, size, mask, attr); },
            [&](NativeMemory::AddressSpace& space) {
                return space.SetMemoryAttribute(GetInteger(addr), size, static_cast<u8>(mask),
                                                static_cast<u8>(attr));
            }));
    }

    Result SetHeapSize(KProcessAddress* out, size_t size) {
        u64 shadow_out = 0;
        if (m_native) {
            R_TRY(m_native->Run(true, [&](NativeMemory::MappedAddressSpace& space) {
                return space.SetHeapSize(shadow_out, size);
            }));
            *out = KProcessAddress{shadow_out};
            R_SUCCEED();
        }
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::SetHeapSize, GetInteger(m_page_table->GetHeapRegionStart()),
            m_page_table->GetHeapRegionSize(), 0,
            [&] { return m_page_table->SetHeapSize(out, size); },
            [&](NativeMemory::AddressSpace& space) { return space.SetHeapSize(shadow_out, size); }));
    }

    Result SetMaxHeapSize(size_t size) {
        if (m_native) {
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                space.SetMaxHeapSize(size);
                return NativeMemory::Result::Success;
            }));
        }
        R_TRY(m_page_table->SetMaxHeapSize(size));
        if (m_shadow) {
            std::scoped_lock lk{m_shadow->Mutex()};
            m_shadow->Space().SetMaxHeapSize(size);
        }
        R_SUCCEED();
    }

    Result QueryInfo(KMemoryInfo* out_info, Svc::PageInfo* out_page_info,
                     KProcessAddress addr) const {
        if (m_native) {
            *out_info = NativeAdapters::ToKernelMemoryInfo(
                m_native->WithSpace([&](const NativeMemory::MappedAddressSpace& space) {
                    return space.Space().QueryInfo(GetInteger(addr));
                }));
            out_page_info->flags = 0;
            R_SUCCEED();
        }
        R_RETURN(m_page_table->QueryInfo(out_info, out_page_info, addr));
    }

    Result QueryPhysicalAddress(Svc::lp64::PhysicalMemoryInfo* out, KProcessAddress address) {
        if (m_native) {
            return Unsupported("QueryPhysicalAddress", GetInteger(address));
        }
        R_RETURN(m_page_table->QueryPhysicalAddress(out, address));
    }

    Result QueryStaticMapping(KProcessAddress* out, KPhysicalAddress address, size_t size) {
        if (m_native) {
            return Unsupported("QueryStaticMapping", GetInteger(address));
        }
        R_RETURN(m_page_table->QueryStaticMapping(out, address, size));
    }

    Result QueryIoMapping(KProcessAddress* out, KPhysicalAddress address, size_t size) {
        if (m_native) {
            return Unsupported("QueryIoMapping", GetInteger(address));
        }
        R_RETURN(m_page_table->QueryIoMapping(out, address, size));
    }

    Result MapMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        if (m_native) {
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.MapMemory(GetInteger(dst_address), GetInteger(src_address), size);
            }));
        }
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::MapMemory, GetInteger(dst_address), size,
            GetInteger(src_address),
            [&] { return m_page_table->MapMemory(dst_address, src_address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.MapMemory(GetInteger(dst_address), GetInteger(src_address), size);
            }));
    }

    Result UnmapMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        if (m_native) {
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.UnmapMemory(GetInteger(dst_address), GetInteger(src_address), size);
            }));
        }
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::UnmapMemory, GetInteger(dst_address), size,
            GetInteger(src_address),
            [&] { return m_page_table->UnmapMemory(dst_address, src_address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.UnmapMemory(GetInteger(dst_address), GetInteger(src_address), size);
            }));
    }

    Result MapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        if (m_native) {
            return Unsupported("MapCodeMemory", GetInteger(dst_address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapCodeMemory(dst_address, src_address, size));
    }

    Result UnmapCodeMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        if (m_native) {
            return Unsupported("UnmapCodeMemory", GetInteger(dst_address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnmapCodeMemory(dst_address, src_address, size));
    }

    Result MapIo(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm) {
        if (m_native) {
            return Unsupported("MapIo", GetInteger(phys_addr));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapIo(phys_addr, size, perm));
    }

    Result MapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr, size_t size,
                       Svc::MemoryMapping mapping, Svc::MemoryPermission perm) {
        if (m_native) {
            return Unsupported("MapIoRegion", GetInteger(dst_address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapIoRegion(dst_address, phys_addr, size, mapping, perm));
    }

    Result UnmapIoRegion(KProcessAddress dst_address, KPhysicalAddress phys_addr, size_t size,
                         Svc::MemoryMapping mapping) {
        if (m_native) {
            return Unsupported("UnmapIoRegion", GetInteger(dst_address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnmapIoRegion(dst_address, phys_addr, size, mapping));
    }

    Result MapStatic(KPhysicalAddress phys_addr, size_t size, KMemoryPermission perm) {
        if (m_native) {
            return Unsupported("MapStatic", GetInteger(phys_addr));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapStatic(phys_addr, size, perm));
    }

    Result MapRegion(KMemoryRegionType region_type, KMemoryPermission perm) {
        if (m_native) {
            return Unsupported("MapRegion", 0);
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapRegion(region_type, perm));
    }

    Result MapInsecureMemory(KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("MapInsecureMemory", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapInsecureMemory(address, size));
    }

    Result UnmapInsecureMemory(KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("UnmapInsecureMemory", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnmapInsecureMemory(address, size));
    }

    Result MapPageGroup(KProcessAddress addr, const KPageGroup& pg, KMemoryState state,
                        KMemoryPermission perm) {
        if (m_native) {
            if (state != KMemoryState::Shared && state != KMemoryState::IoMemory) {
                return Unsupported("MapPageGroup", GetInteger(addr));
            }
            NativeMemory::Permission permission;
            R_UNLESS(ToNativePermission(perm, permission), ResultInvalidNewMemoryPermission);
            std::vector<NativeMemory::PhysicalExtent> extents;
            NativeAdapters::PageGroupToExtents(pg, extents);
            const u64 size = pg.GetNumPages() * PageSize;
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return state == KMemoryState::Shared
                           ? space.MapSharedMemory(GetInteger(addr), size, permission, extents)
                           : space.MapIoMemory(GetInteger(addr), size, permission, extents);
            }));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapPageGroup(addr, pg, state, perm));
    }

    Result UnmapPageGroup(KProcessAddress address, const KPageGroup& pg, KMemoryState state) {
        if (m_native) {
            if (state != KMemoryState::Shared && state != KMemoryState::IoMemory) {
                return Unsupported("UnmapPageGroup", GetInteger(address));
            }
            const u64 size = pg.GetNumPages() * PageSize;
            std::vector<NativeMemory::PhysicalExtent> expected;
            NativeAdapters::PageGroupToExtents(pg, expected);
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                // Like IsValidPageGroup: only the pages this group mapped may be unmapped.
                std::vector<NativeMemory::PhysicalExtent> mapped;
                if (!space.QueryPhysicalExtents(GetInteger(address), size, mapped) ||
                    !NativeMemory::SamePhysicalPages(mapped, expected)) {
                    return NativeMemory::Result::InvalidCurrentMemory;
                }
                return state == KMemoryState::Shared
                           ? space.UnmapSharedMemory(GetInteger(address), size)
                           : space.UnmapIoMemory(GetInteger(address), size);
            }));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnmapPageGroup(address, pg, state));
    }

    // Only the thread-local page is mapped from a caller-owned physical page.
    Result MapPages(KProcessAddress* out_addr, size_t num_pages, size_t alignment,
                    KPhysicalAddress phys_addr, KMemoryState state, KMemoryPermission perm) {
        if (m_native) {
            if (state != KMemoryState::ThreadLocal || num_pages != 1 || alignment != PageSize ||
                perm != KMemoryPermission::UserReadWrite) {
                return Unsupported("MapPages(physical)", GetInteger(phys_addr));
            }
            u64 address = 0;
            R_TRY(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.MapThreadLocalPageAnywhere(address,
                                                        NativeAdapters::ToDramOffset(phys_addr),
                                                        GetNumGuardPages());
            }));
            *out_addr = KProcessAddress{address};
            R_SUCCEED();
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapPages(out_addr, num_pages, alignment, phys_addr, state, perm));
    }

    Result MapPages(KProcessAddress* out_addr, size_t num_pages, KMemoryState state,
                    KMemoryPermission perm) {
        if (m_native) {
            NativeMemory::Permission permission;
            if (!IsAnonymousMapState(state) || !ToNativePermission(perm, permission)) {
                return Unsupported("MapPages(anywhere)", num_pages);
            }
            u64 address = 0;
            R_TRY(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.MapPagesAnywhere(address, num_pages * PageSize,
                                              static_cast<NativeMemory::MemoryState>(state),
                                              permission, GetNumGuardPages());
            }));
            *out_addr = KProcessAddress{address};
            R_SUCCEED();
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapPages(out_addr, num_pages, state, perm));
    }

    Result MapPages(KProcessAddress address, size_t num_pages, KMemoryState state,
                    KMemoryPermission perm) {
        if (m_native) {
            NativeMemory::Permission permission;
            if (!IsAnonymousMapState(state) || !ToNativePermission(perm, permission)) {
                return Unsupported("MapPages(fixed)", GetInteger(address));
            }
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.MapPages(GetInteger(address), num_pages * PageSize,
                                      static_cast<NativeMemory::MemoryState>(state), permission);
            }));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapPages(address, num_pages, state, perm));
    }

    Result UnmapPages(KProcessAddress addr, size_t num_pages, KMemoryState state) {
        if (m_native) {
            const bool is_thread_local = state == KMemoryState::ThreadLocal && num_pages == 1;
            if (!is_thread_local && !IsAnonymousMapState(state)) {
                return Unsupported("UnmapPages", GetInteger(addr));
            }
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return is_thread_local
                           ? space.UnmapThreadLocalPage(GetInteger(addr))
                           : space.UnmapPages(GetInteger(addr), num_pages * PageSize,
                                              static_cast<NativeMemory::MemoryState>(state));
            }));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnmapPages(addr, num_pages, state));
    }

    Result MakeAndOpenPageGroup(KPageGroup* out, KProcessAddress address, size_t num_pages,
                                KMemoryState state_mask, KMemoryState state,
                                KMemoryPermission perm_mask, KMemoryPermission perm,
                                KMemoryAttribute attr_mask, KMemoryAttribute attr) {
        if (m_native) {
            return Unsupported("MakeAndOpenPageGroup", GetInteger(address));
        }
        R_RETURN(m_page_table->MakeAndOpenPageGroup(out, address, num_pages, state_mask, state,
                                                   perm_mask, perm, attr_mask, attr));
    }

    Result InvalidateProcessDataCache(KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("InvalidateProcessDataCache", GetInteger(address));
        }
        R_RETURN(m_page_table->InvalidateProcessDataCache(address, size));
    }

    Result ReadDebugMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        if (m_native) {
            return Unsupported("ReadDebugMemory", GetInteger(src_address));
        }
        R_RETURN(m_page_table->ReadDebugMemory(dst_address, src_address, size));
    }

    Result ReadDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size,
                             KMemoryState state) {
        if (m_native) {
            return Unsupported("ReadDebugIoMemory", GetInteger(src_address));
        }
        R_RETURN(m_page_table->ReadDebugIoMemory(dst_address, src_address, size, state));
    }

    Result WriteDebugMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size) {
        if (m_native) {
            return Unsupported("WriteDebugMemory", GetInteger(dst_address));
        }
        R_RETURN(m_page_table->WriteDebugMemory(dst_address, src_address, size));
    }

    Result WriteDebugIoMemory(KProcessAddress dst_address, KProcessAddress src_address, size_t size,
                              KMemoryState state) {
        if (m_native) {
            return Unsupported("WriteDebugIoMemory", GetInteger(dst_address));
        }
        R_RETURN(m_page_table->WriteDebugIoMemory(dst_address, src_address, size, state));
    }

    Result LockForMapDeviceAddressSpace(bool* out_is_io, KProcessAddress address, size_t size,
                                        KMemoryPermission perm, bool is_aligned, bool check_heap) {
        if (m_native) {
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.LockForMapDevice(GetInteger(address), size, static_cast<u8>(perm),
                                              is_aligned, check_heap, *out_is_io);
            }));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->LockForMapDeviceAddressSpace(out_is_io, address, size, perm,
                                                           is_aligned, check_heap));
    }

    Result LockForUnmapDeviceAddressSpace(KProcessAddress address, size_t size, bool check_heap) {
        if (m_native) {
            return Unsupported("LockForUnmapDeviceAddressSpace", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->LockForUnmapDeviceAddressSpace(address, size, check_heap));
    }

    Result UnlockForDeviceAddressSpace(KProcessAddress address, size_t size) {
        if (m_native) {
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.UnlockForDevice(GetInteger(address), size);
            }));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnlockForDeviceAddressSpace(address, size));
    }

    Result UnlockForDeviceAddressSpacePartialMap(KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("UnlockForDeviceAddressSpacePartialMap", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnlockForDeviceAddressSpacePartialMap(address, size));
    }

    Result OpenMemoryRangeForMapDeviceAddressSpace(KPageTableBase::MemoryRange* out,
                                                   KProcessAddress address, size_t size,
                                                   KMemoryPermission perm, bool is_aligned) {
        if (m_native) {
            return Unsupported("OpenMemoryRangeForMapDeviceAddressSpace", GetInteger(address));
        }
        R_RETURN(m_page_table->OpenMemoryRangeForMapDeviceAddressSpace(out, address, size, perm,
                                                                      is_aligned));
    }

    Result OpenMemoryRangeForUnmapDeviceAddressSpace(KPageTableBase::MemoryRange* out,
                                                     KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("OpenMemoryRangeForUnmapDeviceAddressSpace", GetInteger(address));
        }
        R_RETURN(m_page_table->OpenMemoryRangeForUnmapDeviceAddressSpace(out, address, size));
    }

    Result LockForIpcUserBuffer(KPhysicalAddress* out, KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("LockForIpcUserBuffer", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->LockForIpcUserBuffer(out, address, size));
    }

    Result UnlockForIpcUserBuffer(KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("UnlockForIpcUserBuffer", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnlockForIpcUserBuffer(address, size));
    }

    Result LockForTransferMemory(KPageGroup* out, KProcessAddress address, size_t size,
                                 KMemoryPermission perm) {
        if (m_native) {
            NativeMemory::Permission permission;
            R_UNLESS(ToNativePermission(perm, permission), ResultInvalidNewMemoryPermission);
            std::vector<NativeMemory::PhysicalExtent> extents;
            R_TRY(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.LockForTransferMemory(GetInteger(address), size, permission, extents);
            }));
            const Result grouped = NativeAdapters::ExtentsToOpenedPageGroup(m_kernel, *out, extents);
            if (grouped.IsError()) {
                out->Finalize();
                R_TRY(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                    return space.UnlockForTransferMemory(GetInteger(address), size);
                }));
            }
            R_RETURN(grouped);
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->LockForTransferMemory(out, address, size, perm));
    }

    Result UnlockForTransferMemory(KProcessAddress address, size_t size, const KPageGroup& pg) {
        if (m_native) {
            R_RETURN(m_native->Run(false, [&](NativeMemory::MappedAddressSpace& space) {
                return space.UnlockForTransferMemory(GetInteger(address), size);
            }));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnlockForTransferMemory(address, size, pg));
    }

    Result LockForCodeMemory(KPageGroup* out, KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("LockForCodeMemory", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->LockForCodeMemory(out, address, size));
    }

    Result UnlockForCodeMemory(KProcessAddress address, size_t size, const KPageGroup& pg) {
        if (m_native) {
            return Unsupported("UnlockForCodeMemory", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnlockForCodeMemory(address, size, pg));
    }

    Result OpenMemoryRangeForProcessCacheOperation(KPageTableBase::MemoryRange* out,
                                                   KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("OpenMemoryRangeForProcessCacheOperation", GetInteger(address));
        }
        R_RETURN(m_page_table->OpenMemoryRangeForProcessCacheOperation(out, address, size));
    }

    Result CopyMemoryFromLinearToUser(KProcessAddress dst_addr, size_t size,
                                      KProcessAddress src_addr, KMemoryState src_state_mask,
                                      KMemoryState src_state, KMemoryPermission src_test_perm,
                                      KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
        if (m_native) {
            std::vector<u8> bytes(size);
            R_TRY(ReadNativeLinear(src_addr, size,
                                   MakeCopyRule(src_state_mask, src_state, src_test_perm,
                                                src_attr_mask, src_attr),
                                   bytes.data()));
            R_UNLESS(GetCurrentMemory(m_kernel).WriteBlock(GetInteger(dst_addr), bytes.data(), size),
                     ResultInvalidCurrentMemory);
            R_SUCCEED();
        }
        R_RETURN(m_page_table->CopyMemoryFromLinearToUser(dst_addr, size, src_addr, src_state_mask,
                                                         src_state, src_test_perm, src_attr_mask,
                                                         src_attr));
    }

    Result CopyMemoryFromLinearToKernel(void* dst_addr, size_t size, KProcessAddress src_addr,
                                        KMemoryState src_state_mask, KMemoryState src_state,
                                        KMemoryPermission src_test_perm,
                                        KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
        if (m_native) {
            R_RETURN(ReadNativeLinear(src_addr, size,
                                      MakeCopyRule(src_state_mask, src_state, src_test_perm,
                                                   src_attr_mask, src_attr),
                                      dst_addr));
        }
        R_RETURN(m_page_table->CopyMemoryFromLinearToKernel(dst_addr, size, src_addr, src_state_mask,
                                                           src_state, src_test_perm, src_attr_mask,
                                                           src_attr));
    }

    Result CopyMemoryFromUserToLinear(KProcessAddress dst_addr, size_t size,
                                      KMemoryState dst_state_mask, KMemoryState dst_state,
                                      KMemoryPermission dst_test_perm,
                                      KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                      KProcessAddress src_addr) {
        if (m_native) {
            std::vector<u8> bytes(size);
            R_UNLESS(GetCurrentMemory(m_kernel).ReadBlock(GetInteger(src_addr), bytes.data(), size),
                     ResultInvalidCurrentMemory);
            R_RETURN(WriteNativeLinear(dst_addr, size,
                                       MakeCopyRule(dst_state_mask, dst_state, dst_test_perm,
                                                    dst_attr_mask, dst_attr),
                                       bytes.data()));
        }
        R_RETURN(m_page_table->CopyMemoryFromUserToLinear(dst_addr, size, dst_state_mask, dst_state,
                                                         dst_test_perm, dst_attr_mask, dst_attr,
                                                         src_addr));
    }

    Result CopyMemoryFromKernelToLinear(KProcessAddress dst_addr, size_t size,
                                        KMemoryState dst_state_mask, KMemoryState dst_state,
                                        KMemoryPermission dst_test_perm,
                                        KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr,
                                        void* src_addr) {
        if (m_native) {
            R_RETURN(WriteNativeLinear(dst_addr, size,
                                       MakeCopyRule(dst_state_mask, dst_state, dst_test_perm,
                                                    dst_attr_mask, dst_attr),
                                       src_addr));
        }
        R_RETURN(m_page_table->CopyMemoryFromKernelToLinear(dst_addr, size, dst_state_mask,
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
        if (m_native || dst_page_table.m_native) {
            const NativeMemory::CopyRule dst_rule =
                MakeCopyRule(dst_state_mask, dst_state, dst_test_perm, dst_attr_mask, dst_attr);
            R_RETURN(CopyNativeHeapToHeap(dst_page_table, dst_addr, size, &dst_rule, src_addr,
                                          MakeCopyRule(src_state_mask, src_state, src_test_perm,
                                                       src_attr_mask, src_attr)));
        }
        R_RETURN(m_page_table->CopyMemoryFromHeapToHeap(
            *dst_page_table.m_page_table, dst_addr, size, dst_state_mask, dst_state, dst_test_perm,
            dst_attr_mask, dst_attr, src_addr, src_state_mask, src_state, src_test_perm,
            src_attr_mask, src_attr));
    }

    Result CopyMemoryFromHeapToHeapWithoutCheckDestination(
        KProcessPageTable& dst_page_table, KProcessAddress dst_addr, size_t size,
        KMemoryState dst_state_mask, KMemoryState dst_state, KMemoryPermission dst_test_perm,
        KMemoryAttribute dst_attr_mask, KMemoryAttribute dst_attr, KProcessAddress src_addr,
        KMemoryState src_state_mask, KMemoryState src_state, KMemoryPermission src_test_perm,
        KMemoryAttribute src_attr_mask, KMemoryAttribute src_attr) {
        if (m_native || dst_page_table.m_native) {
            R_RETURN(CopyNativeHeapToHeap(dst_page_table, dst_addr, size, nullptr, src_addr,
                                          MakeCopyRule(src_state_mask, src_state, src_test_perm,
                                                       src_attr_mask, src_attr)));
        }
        R_RETURN(m_page_table->CopyMemoryFromHeapToHeapWithoutCheckDestination(
            *dst_page_table.m_page_table, dst_addr, size, dst_state_mask, dst_state, dst_test_perm,
            dst_attr_mask, dst_attr, src_addr, src_state_mask, src_state, src_test_perm,
            src_attr_mask, src_attr));
    }

    Result SetupForIpc(KProcessAddress* out_dst_addr, size_t size, KProcessAddress src_addr,
                       KProcessPageTable& src_page_table, KMemoryPermission test_perm,
                       KMemoryState dst_state, bool send) {
        if (m_native || src_page_table.m_native) {
            return Unsupported("SetupForIpc", GetInteger(src_addr));
        }
        ShadowBystander bystander{*this};
        ShadowBystander source_bystander{src_page_table};
        R_RETURN(m_page_table->SetupForIpc(out_dst_addr, size, src_addr, *src_page_table.m_page_table,
                                          test_perm, dst_state, send));
    }

    Result CleanupForIpcServer(KProcessAddress address, size_t size, KMemoryState dst_state) {
        if (m_native) {
            return Unsupported("CleanupForIpcServer", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->CleanupForIpcServer(address, size, dst_state));
    }

    Result CleanupForIpcClient(KProcessAddress address, size_t size, KMemoryState dst_state) {
        if (m_native) {
            return Unsupported("CleanupForIpcClient", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->CleanupForIpcClient(address, size, dst_state));
    }

    Result MapPhysicalMemory(KProcessAddress address, size_t size) {
        if (m_native) {
            R_RETURN(m_native->Run(true, [&](NativeMemory::MappedAddressSpace& space) {
                return space.MapPhysicalMemory(GetInteger(address), size);
            }));
        }
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::MapPhysicalMemory, GetInteger(address), size, 0,
            [&] { return m_page_table->MapPhysicalMemory(address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.MapPhysicalMemory(GetInteger(address), size);
            }));
    }

    Result UnmapPhysicalMemory(KProcessAddress address, size_t size) {
        if (m_native) {
            R_RETURN(m_native->Run(true, [&](NativeMemory::MappedAddressSpace& space) {
                return space.UnmapPhysicalMemory(GetInteger(address), size);
            }));
        }
        R_RETURN(RunShadowed(
            NativeMemory::ShadowOp::UnmapPhysicalMemory, GetInteger(address), size, 0,
            [&] { return m_page_table->UnmapPhysicalMemory(address, size); },
            [&](NativeMemory::AddressSpace& space) {
                return space.UnmapPhysicalMemory(GetInteger(address), size);
            }));
    }

    Result MapPhysicalMemoryUnsafe(KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("MapPhysicalMemoryUnsafe", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->MapPhysicalMemoryUnsafe(address, size));
    }

    Result UnmapPhysicalMemoryUnsafe(KProcessAddress address, size_t size) {
        if (m_native) {
            return Unsupported("UnmapPhysicalMemoryUnsafe", GetInteger(address));
        }
        ShadowBystander bystander{*this};
        R_RETURN(m_page_table->UnmapPhysicalMemoryUnsafe(address, size));
    }

    Result UnmapProcessMemory(KProcessAddress dst_address, size_t size,
                              KProcessPageTable& src_page_table, KProcessAddress src_address) {
        if (m_native || src_page_table.m_native) {
            return Unsupported("UnmapProcessMemory", GetInteger(dst_address));
        }
        ShadowBystander bystander{*this};
        ShadowBystander source_bystander{src_page_table};
        R_RETURN(m_page_table->UnmapProcessMemory(dst_address, size, *src_page_table.m_page_table,
                                                 src_address));
    }

    bool GetPhysicalAddress(KPhysicalAddress* out, KProcessAddress address) {
        if (m_native) {
            return m_native->WithSpace([&](const NativeMemory::MappedAddressSpace&) {
                return m_native_space->GetImpl().GetPhysicalAddress(out, address);
            });
        }
        return m_page_table->GetPhysicalAddress(out, address);
    }

    bool Contains(KProcessAddress addr, size_t size) const {
        return m_regions->Contains(GetInteger(addr), size);
    }

    bool IsInAliasRegion(KProcessAddress addr, size_t size) const {
        return m_regions->IsInAliasRegion(GetInteger(addr), size);
    }
    bool IsInHeapRegion(KProcessAddress addr, size_t size) const {
        return m_regions->IsInHeapRegion(GetInteger(addr), size);
    }
    bool IsInUnsafeAliasRegion(KProcessAddress addr, size_t size) const {
        return m_regions->IsInUnsafeAliasRegion(GetInteger(addr), size);
    }

    bool CanContain(KProcessAddress addr, size_t size, KMemoryState state) const {
        return m_regions->CanContain(GetInteger(addr), size,
                                     static_cast<NativeMemory::RegionState>(
                                         static_cast<u32>(state & KMemoryState::Mask)));
    }

    KProcessAddress GetAddressSpaceStart() const {
        return KProcessAddress{m_regions->AddressSpaceStart()};
    }
    KProcessAddress GetHeapRegionStart() const {
        return KProcessAddress{m_regions->HeapRegionStart()};
    }
    KProcessAddress GetAliasRegionStart() const {
        return KProcessAddress{m_regions->AliasRegionStart()};
    }
    KProcessAddress GetStackRegionStart() const {
        return KProcessAddress{m_regions->StackRegionStart()};
    }
    KProcessAddress GetKernelMapRegionStart() const {
        return KProcessAddress{m_regions->KernelMapRegionStart()};
    }
    KProcessAddress GetCodeRegionStart() const {
        return KProcessAddress{m_regions->CodeRegionStart()};
    }
    KProcessAddress GetAliasCodeRegionStart() const {
        return KProcessAddress{m_regions->AliasCodeRegionStart()};
    }

    size_t GetAddressSpaceSize() const {
        return m_regions->AddressSpaceSize();
    }
    size_t GetHeapRegionSize() const {
        return m_regions->HeapRegionSize();
    }
    size_t GetAliasRegionSize() const {
        return m_regions->AliasRegionSize();
    }
    size_t GetAliasRegionExtraSize() const {
        return m_alias_region_extra_size;
    }
    size_t GetStackRegionSize() const {
        return m_regions->StackRegionSize();
    }
    size_t GetKernelMapRegionSize() const {
        return m_regions->KernelMapRegionSize();
    }
    size_t GetCodeRegionSize() const {
        return m_regions->CodeRegionSize();
    }
    size_t GetAliasCodeRegionSize() const {
        return m_regions->AliasCodeRegionSize();
    }

    size_t GetNormalMemorySize() const {
        if (m_native) {
            return m_native->WithSpace([](const NativeMemory::MappedAddressSpace& space) {
                return space.Space().PhysicalBytesInUse();
            });
        }
        return m_page_table->GetNormalMemorySize();
    }

    size_t GetCodeSize() const {
        return m_native ? NativeStateBytes(NativeMemory::MemoryState::Code)
                        : m_page_table->GetCodeSize();
    }
    size_t GetCodeDataSize() const {
        return m_native ? NativeStateBytes(NativeMemory::MemoryState::CodeData)
                        : m_page_table->GetCodeDataSize();
    }

    // The native model has no alias-code states because MapCodeMemory is unsupported.
    size_t GetAliasCodeSize() const {
        return m_native ? 0 : m_page_table->GetAliasCodeSize();
    }
    size_t GetAliasCodeDataSize() const {
        return m_native ? 0 : m_page_table->GetAliasCodeDataSize();
    }

    u32 GetAllocateOption() const {
        return m_native_space ? m_native_space->GetAllocateOption()
                              : m_page_table->GetAllocateOption();
    }

    u32 GetAddressSpaceWidth() const {
        return m_regions->GetLayout().width;
    }

    KPhysicalAddress GetHeapPhysicalAddress(KVirtualAddress address) {
        return KPageTableBase::GetHeapPhysicalAddress(m_kernel, address);
    }

    u8* GetHeapVirtualPointer(KPhysicalAddress address) {
        return KPageTableBase::GetHeapVirtualPointer(m_kernel, address);
    }

    KVirtualAddress GetHeapVirtualAddress(KPhysicalAddress address) {
        return KPageTableBase::GetHeapVirtualAddress(m_kernel, address);
    }

    KBlockInfoManager* GetBlockInfoManager() {
        return m_native_space ? m_native_space->GetBlockInfoManager()
                              : m_page_table->GetBlockInfoManager();
    }

    bool OwnsBasePageTable(const KPageTableBase* table) const {
        return m_page_table.get() == table;
    }

    KPageTable* GetBasePageTableOrNull() {
        return m_page_table.get();
    }

    // Only the KPageTable fallback has a base table; the native path never constructs one.
    KPageTable& GetBasePageTable() {
        ASSERT(m_page_table != nullptr);
        return *m_page_table;
    }

    const KPageTable& GetBasePageTable() const {
        ASSERT(m_page_table != nullptr);
        return *m_page_table;
    }
};

} // namespace Kernel
