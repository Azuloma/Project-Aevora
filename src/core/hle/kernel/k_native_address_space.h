// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/page_table.h"
#include "core/hle/kernel/k_light_lock.h"
#include "core/hle/kernel/k_memory_manager.h"
#include "core/hle/kernel/k_page_table_base.h"
#include "core/hle/kernel/k_system_resource.h"
#include "core/hle/kernel/k_typed_address.h"
#include "core/hle/kernel/memory_types.h"
#include "core/native_memory/native_address_space.h"

namespace Kernel {

// What KProcessPageTable still takes from KPageTableBase on the native path: the region layout, the
// Common::PageTable, the Memory tracker, the block-info manager and the allocation options.
class KNativeAddressSpace {
public:
    // Matches KPageTableBase::InitializeForProcess, where debug memory fill is disabled.
    static constexpr u8 HeapFillValue = 0;

    explicit KNativeAddressSpace(KernelCore& kernel) : m_device_map_lock{kernel} {}

    KNativeAddressSpace(const KNativeAddressSpace&) = delete;
    KNativeAddressSpace& operator=(const KNativeAddressSpace&) = delete;

    Result Initialize(Svc::CreateProcessFlag as_type, bool enable_aslr, bool from_back,
                      KMemoryManager::Pool pool, KProcessAddress code_address, size_t code_size,
                      KSystemResource* system_resource, Core::Memory::Memory& memory,
                      KProcessAddress aslr_space_start) {
        R_TRY(KPageTableBase::ComputeProcessLayout(as_type, enable_aslr, code_address, code_size,
                                                   aslr_space_start, m_layout));
        m_alias_region_extra_size = KPageTableBase::ComputeAliasRegionExtraSize(as_type, m_layout);
        m_block_info_manager = system_resource->GetBlockInfoManagerPointer();
        m_allocate_option = KMemoryManager::EncodeOption(
            pool, from_back ? KMemoryManager::Direction::FromBack
                            : KMemoryManager::Direction::FromFront);
        m_impl.Resize(m_layout.width, PageBits);
        m_memory = std::addressof(memory);
        R_SUCCEED();
    }

    const Core::NativeMemory::Layout& GetLayout() const {
        return m_layout;
    }
    size_t GetAliasRegionExtraSize() const {
        return m_alias_region_extra_size;
    }
    u32 GetAllocateOption() const {
        return m_allocate_option;
    }
    KBlockInfoManager* GetBlockInfoManager() const {
        return m_block_info_manager;
    }
    Core::Memory::Memory& GetMemory() const {
        return *m_memory;
    }
    Common::PageTable& GetImpl() {
        return m_impl;
    }
    const Common::PageTable& GetImpl() const {
        return m_impl;
    }
    KLightLock& GetDeviceMapLock() {
        return m_device_map_lock;
    }

private:
    Core::NativeMemory::Layout m_layout;
    Common::PageTable m_impl;
    Core::Memory::Memory* m_memory{};
    KBlockInfoManager* m_block_info_manager{};
    KLightLock m_device_map_lock;
    size_t m_alias_region_extra_size{};
    u32 m_allocate_option{};
};

} // namespace Kernel
