// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include "common/assert.h"
#include "common/logging.h"
#include "common/page_table.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/kernel/k_light_lock.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_page_group.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_scoped_lock.h"
#include "core/hle/kernel/k_typed_address.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"
#include "core/native_memory/native_kernel_convert.h"
#include "core/native_memory/native_process_memory.h"

// Adapters from the kernel-free native memory layer to the kernel, used by KProcessPageTable; the
// side effects of the replaced KPageTableBase calls are in N2_DESIGN.
namespace Kernel::NativeAdapters {

namespace Native = Core::NativeMemory;

// The native result table and the kernel result constants must stay one definition.
static_assert(Native::ToKernelResultRaw(Native::Result::Success) == ResultSuccess.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::InvalidSize) == ResultInvalidSize.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::InvalidAddress) == ResultInvalidAddress.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::OutOfMemory) == ResultOutOfMemory.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::InvalidCurrentMemory) ==
              ResultInvalidCurrentMemory.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::InvalidNewMemoryPermission) ==
              ResultInvalidNewMemoryPermission.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::InvalidMemoryRegion) ==
              ResultInvalidMemoryRegion.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::InvalidCombination) ==
              ResultInvalidCombination.GetInnerValue());
static_assert(Native::ToKernelResultRaw(Native::Result::LimitReached) == ResultLimitReached.GetInnerValue());

// Memory state, permission, and attribute encodings are the kernel's own.
static_assert(static_cast<u32>(Native::MemoryState::Free) == static_cast<u32>(KMemoryState::Free));
static_assert(static_cast<u32>(Native::MemoryState::Code) == static_cast<u32>(KMemoryState::Code));
static_assert(static_cast<u32>(Native::MemoryState::CodeData) == static_cast<u32>(KMemoryState::CodeData));
static_assert(static_cast<u32>(Native::MemoryState::Normal) == static_cast<u32>(KMemoryState::Normal));
static_assert(static_cast<u32>(Native::MemoryState::Shared) == static_cast<u32>(KMemoryState::Shared));
static_assert(static_cast<u32>(Native::MemoryState::Io) == static_cast<u32>(KMemoryState::IoMemory));
static_assert(static_cast<u32>(Native::MemoryState::Stack) == static_cast<u32>(KMemoryState::Stack));
static_assert(static_cast<u32>(Native::MemoryState::ThreadLocal) == static_cast<u32>(KMemoryState::ThreadLocal));
static_assert(static_cast<u32>(Native::MemoryState::Inaccessible) ==
              static_cast<u32>(KMemoryState::Inaccessible));
static_assert(Native::PermUserReadWrite == static_cast<u8>(KMemoryPermission::UserReadWrite));
static_assert(Native::PermKernelRead == static_cast<u8>(KMemoryPermission::KernelRead));
static_assert(Native::PermKernelWrite == static_cast<u8>(KMemoryPermission::KernelWrite));
static_assert(Native::PermNotMapped == static_cast<u8>(KMemoryPermission::NotMapped));
static_assert(Native::PermUserMask == static_cast<u8>(KMemoryPermission::UserMask));
static_assert(Native::AttributeLocked == static_cast<u8>(KMemoryAttribute::Locked));
static_assert(Native::AttributeIpcLocked == static_cast<u8>(KMemoryAttribute::IpcLocked));
static_assert(Native::AttributeDeviceShared == static_cast<u8>(KMemoryAttribute::DeviceShared));
static_assert(Native::AttributeUncached == static_cast<u8>(KMemoryAttribute::Uncached));
static_assert(Native::AttributePermissionLocked == static_cast<u8>(KMemoryAttribute::PermissionLocked));
static_assert(Native::PageSize == Kernel::PageSize);

[[nodiscard]] inline Result ToKernelResult(Native::Result result) {
    ASSERT_MSG(Native::HasKernelResult(result), "native memory result {} has no kernel counterpart",
               Native::ResultName(result));
    return Result{Native::ToKernelResultRaw(result)};
}

[[nodiscard]] inline Common::MemoryPermission ToHostPermission(u8 stored_permission) {
#ifdef HAS_NCE
    constexpr bool ExecuteSupported = true;
#else
    constexpr bool ExecuteSupported = false;
#endif
    const Native::HostPermissionFlags flags =
        Native::ToHostPermissionFlags(stored_permission, ExecuteSupported);
    Common::MemoryPermission permission{};
    if (flags.read) {
        permission |= Common::MemoryPermission::Read;
    }
    if (flags.write) {
        permission |= Common::MemoryPermission::Write;
    }
    if (flags.execute) {
        permission |= Common::MemoryPermission::Execute;
    }
    return permission;
}

// IPC lock counts are zero because IPC map-alias is unsupported; page info flags are always zero.
[[nodiscard]] inline KMemoryInfo ToKernelMemoryInfo(const Native::MemoryInfo& info) {
    KMemoryInfo out{};
    out.m_address = info.base;
    out.m_size = info.size;
    out.m_state = static_cast<KMemoryState>(info.state);
    out.m_device_use_count = info.device_count;
    out.m_permission = static_cast<KMemoryPermission>(info.permission);
    out.m_attribute = static_cast<KMemoryAttribute>(info.attribute);
    out.m_original_permission = KMemoryPermission::None;
    out.m_disable_merge_attribute = KMemoryBlockDisableMergeAttribute::None;
    return out;
}

[[nodiscard]] inline KPhysicalAddress ToKernelPhysical(u64 dram_offset) {
    return KPhysicalAddress{Native::DramOffsetToPhysical(dram_offset, Core::DramMemoryMap::Base)};
}

[[nodiscard]] inline u64 ToDramOffset(KPhysicalAddress physical) {
    u64 offset = 0;
    const bool in_dram =
        Native::PhysicalToDramOffset(GetInteger(physical), Core::DramMemoryMap::Base, offset);
    ASSERT_MSG(in_dram, "physical address {:#x} is below the DRAM base", GetInteger(physical));
    return offset;
}

inline void PageGroupToExtents(const KPageGroup& group, std::vector<Native::PhysicalExtent>& out) {
    for (const auto& node : group) {
        out.push_back({ToDramOffset(node.GetAddress()), node.GetSize()});
    }
}

// Builds the group without touching reference counts; the caller opens or adopts the references.
[[nodiscard]] inline Result ExtentsToPageGroup(KPageGroup& group,
                                               const std::vector<Native::PhysicalExtent>& extents) {
    for (const Native::PhysicalExtent& extent : extents) {
        R_TRY(group.AddBlock(ToKernelPhysical(extent.phys), extent.size / PageSize));
    }
    R_SUCCEED();
}

// Mirrors LockMemoryAndOpen: the group holds one added reference per page until the caller closes it.
[[nodiscard]] inline Result ExtentsToOpenedPageGroup(KernelCore& kernel, KPageGroup& group,
                                                     const std::vector<Native::PhysicalExtent>& extents) {
    R_TRY(ExtentsToPageGroup(group, extents));
    group.Open(kernel);
    R_SUCCEED();
}

// Allocation takes the first reference on each page and fills it explicitly, as SetHeapSize and
// AllocateAndMapPagesImpl do; Free drops the reference, so the last user to close returns the page.
class KernelPhysicalPageSource final : public Native::PhysicalPageSource {
public:
    KernelPhysicalPageSource(KernelCore& kernel, Core::DeviceMemory& device_memory,
                             KBlockInfoManager* block_info_manager, u32 allocate_option,
                             u8 fill_value)
        : m_kernel{kernel}, m_device_memory{device_memory},
          m_block_info_manager{block_info_manager}, m_allocate_option{allocate_option},
          m_fill_value{fill_value} {}

    bool Allocate(u64 size, std::vector<Native::PhysicalExtent>& out) override {
        out.clear();
        if (size == 0 || size % PageSize != 0) {
            return false;
        }
        KPageGroup group(m_kernel, m_block_info_manager);
        if (m_kernel.MemoryManager()
                .AllocateAndOpen(std::addressof(group), size / PageSize, m_allocate_option)
                .IsError()) {
            return false;
        }
        PageGroupToExtents(group, out);
        for (const Native::PhysicalExtent& extent : out) {
            m_device_memory.buffer.ClearBackingRegion(extent.phys, extent.size, m_fill_value);
        }
        return true;
    }

    void Free(const std::vector<Native::PhysicalExtent>& extents) override {
        for (const Native::PhysicalExtent& extent : extents) {
            m_kernel.MemoryManager().Close(ToKernelPhysical(extent.phys), extent.size / PageSize);
        }
    }

private:
    KernelCore& m_kernel;
    Core::DeviceMemory& m_device_memory;
    KBlockInfoManager* m_block_info_manager;
    u32 m_allocate_option;
    u8 m_fill_value;
};

class KernelResourceBudget final : public Native::ResourceBudget {
public:
    KernelResourceBudget(KernelCore& kernel, KResourceLimit* resource_limit)
        : m_kernel{kernel}, m_resource_limit{resource_limit} {}

    bool Reserve(u64 bytes) override {
        return m_resource_limit == nullptr ||
               m_resource_limit->Reserve(m_kernel, Resource, static_cast<s64>(bytes));
    }

    void Release(u64 bytes) override {
        if (m_resource_limit != nullptr) {
            m_resource_limit->Release(m_kernel, Resource, static_cast<s64>(bytes));
        }
    }

    [[nodiscard]] u64 CapacityBytes(u64 in_use_bytes) const {
        if (m_resource_limit == nullptr) {
            return Native::PhysicalCapacityBytes(in_use_bytes, false, 0, 0);
        }
        return Native::PhysicalCapacityBytes(in_use_bytes, true,
                                             m_resource_limit->GetLimitValue(Resource),
                                             m_resource_limit->GetCurrentValue(Resource));
    }

private:
    static constexpr auto Resource = Svc::LimitableResource::PhysicalMemoryMax;

    KernelCore& m_kernel;
    KResourceLimit* m_resource_limit;
};

// Reads and writes DeviceMemory directly, like GetLinearMappedVirtualPointer. It is for the linear
// side of an IPC copy only; user-side bytes must go through Core::Memory so cache hooks still run.
class KernelPhysicalAccess final : public Native::PhysicalAccess {
public:
    explicit KernelPhysicalAccess(Core::DeviceMemory& device_memory) : m_device_memory{device_memory} {}

    void Read(u64 phys, void* out, u64 size) override {
        std::memcpy(out, m_device_memory.GetPointer<u8>(ToKernelPhysical(phys)), size);
    }

    void Write(u64 phys, const void* data, u64 size) override {
        std::memcpy(m_device_memory.GetPointer<u8>(ToKernelPhysical(phys)), data, size);
    }

private:
    Core::DeviceMemory& m_device_memory;
};

// HostMemory ignores separate_heap, so every mapping passes false; Core::Memory also updates the
// Common::PageTable and the GG1 page-table notifications the way KPageTableBase::Operate does.
class KernelHostMemoryMap final : public Native::HostMemoryMap {
public:
    KernelHostMemoryMap(Core::Memory::Memory& memory, Common::PageTable& page_table)
        : m_memory{memory}, m_page_table{page_table} {}

    void Map(u64 address, u64 size, u64 phys, u8 permission) override {
        m_memory.MapMemoryRegion(m_page_table, Common::ProcessAddress{address}, size,
                                 ToKernelPhysical(phys), ToHostPermission(permission), false);
    }

    void Unmap(u64 address, u64 size) override {
        m_memory.UnmapRegion(m_page_table, Common::ProcessAddress{address}, size, false);
    }

    void Protect(u64 address, u64 size, u8 permission) override {
        m_memory.ProtectRegion(m_page_table, Common::ProcessAddress{address}, size,
                               ToHostPermission(permission));
    }

private:
    Core::Memory::Memory& m_memory;
    Common::PageTable& m_page_table;
};

// Per-process owner of the native state. Its KLightLock is the outermost lock and, unlike a
// std::mutex, lets the scheduler switch guests while a kernel call inside it waits.
class KNativeProcessMemory {
public:
    struct Config {
        KernelCore& kernel;
        Core::DeviceMemory& device_memory;
        KBlockInfoManager* block_info_manager;
        Core::Memory::Memory& memory;
        Common::PageTable& page_table;
        KResourceLimit* resource_limit;
        u32 allocate_option;
        u8 fill_value;
        Native::Layout layout;
        u64 physical_capacity_bytes;
    };

    explicit KNativeProcessMemory(const Config& config)
        : m_lock{config.kernel}, m_budget{config.kernel, config.resource_limit},
          m_pages{config.kernel, config.device_memory, config.block_info_manager,
                  config.allocate_option, config.fill_value},
          m_accounting{m_pages, m_budget}, m_host{config.memory, config.page_table},
          m_space{config.layout, config.physical_capacity_bytes, m_accounting, m_host} {}

    KNativeProcessMemory(const KNativeProcessMemory&) = delete;
    KNativeProcessMemory& operator=(const KNativeProcessMemory&) = delete;

    // KProcess termination returns the resource-limit usage itself, so teardown only frees pages.
    ~KNativeProcessMemory() {
        KScopedLightLock lock{m_lock};
        m_accounting.SetAccounted(false);
        m_space.Finalize();
    }

    // The native capacity check reports LimitReached early; the reservation inside the page source is
    // the atomic one. Accounted operations are the heap and physical-memory ones the kernel reserves for.
    template <typename Operation>
    [[nodiscard]] Result Run(bool accounted, Operation&& operation) {
        KScopedLightLock lock{m_lock};
        m_accounting.SetAccounted(accounted);
        m_space.SetPhysicalCapacityBytes(m_budget.CapacityBytes(m_space.Space().PhysicalBytesInUse()));
        const Native::Result result =
            m_accounting.ClassifyFailure(std::forward<Operation>(operation)(m_space));
        m_accounting.SetAccounted(false);
        return ToKernelResult(result);
    }

    // The reference must not outlive the callback.
    template <typename Query>
    auto WithSpace(Query&& query) {
        KScopedLightLock lock{m_lock};
        return std::forward<Query>(query)(std::as_const(m_space));
    }

    // Locks both owners in address order so two crossing copies cannot deadlock.
    template <typename Query>
    static auto WithBoth(KNativeProcessMemory& first, KNativeProcessMemory& second, Query&& query) {
        if (&first == &second) {
            return first.WithSpace([&](const Native::MappedAddressSpace& space) {
                return query(space, space);
            });
        }
        const bool first_is_low = std::less<const KNativeProcessMemory*>{}(&first, &second);
        KNativeProcessMemory& low = first_is_low ? first : second;
        KNativeProcessMemory& high = first_is_low ? second : first;
        KScopedLightLock low_lock{low.m_lock};
        KScopedLightLock high_lock{high.m_lock};
        return query(std::as_const(first.m_space), std::as_const(second.m_space));
    }

private:
    KLightLock m_lock;
    KernelResourceBudget m_budget;
    KernelPhysicalPageSource m_pages;
    Native::AccountingPageSource m_accounting;
    KernelHostMemoryMap m_host;
    Native::MappedAddressSpace m_space;
};

} // namespace Kernel::NativeAdapters
