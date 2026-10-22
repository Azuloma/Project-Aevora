// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

#include "core/native_memory/native_address_space.h"
#include "core/native_memory/native_process_memory.h"

namespace Core::NativeMemory {

// Pure value conversions behind the kernel adapters, so they can be tested without the kernel.
struct HostPermissionFlags {
    bool read = false;
    bool write = false;
    bool execute = false;

    friend bool operator==(const HostPermissionFlags&, const HostPermissionFlags&) = default;
};

// Mirrors ConvertToMemoryPermission in k_page_table_base.cpp, which tests with True(): any bit of
// UserRead (user | kernel read) grants Read, any bit of UserWrite grants Write.
constexpr HostPermissionFlags ToHostPermissionFlags(u8 stored_permission, bool execute_supported) {
    constexpr u8 UserReadBits = 1 | PermKernelRead;
    constexpr u8 UserWriteBits = 2 | PermKernelWrite;
    constexpr u8 UserExecuteBit = 4;
    HostPermissionFlags flags;
    flags.read = (stored_permission & UserReadBits) != 0;
    flags.write = (stored_permission & UserWriteBits) != 0;
    flags.execute = execute_supported && (stored_permission & UserExecuteBit) != 0;
    return flags;
}

// KPhysicalAddress is DramMemoryMap::Base plus the DeviceMemory offset the native layer stores.
constexpr bool PhysicalToDramOffset(u64 physical, u64 dram_base, u64& out_offset) {
    if (physical < dram_base) {
        return false;
    }
    out_offset = physical - dram_base;
    return true;
}

constexpr u64 DramOffsetToPhysical(u64 offset, u64 dram_base) {
    return dram_base + offset;
}

// The native capacity check replaces the KResourceLimit reservation: bytes already in use plus what
// the limit still allows. A negative free value (limit lowered below use) allows nothing more.
constexpr u64 PhysicalCapacityBytes(u64 in_use_bytes, bool has_limit, std::int64_t limit_value,
                                    std::int64_t current_value) {
    if (!has_limit) {
        return ~0ULL;
    }
    const std::int64_t free_value = limit_value - current_value;
    const u64 headroom = free_value > 0 ? static_cast<u64>(free_value) : 0;
    return headroom > ~0ULL - in_use_bytes ? ~0ULL : in_use_bytes + headroom;
}

// The process resource limit as the native layer sees it; the kernel adapter wraps KResourceLimit.
class ResourceBudget {
public:
    virtual ~ResourceBudget() = default;
    virtual bool Reserve(u64 bytes) = 0;
    virtual void Release(u64 bytes) = 0;
};

// Reserves before allocating and releases on free, as the kernel does, so a lost race on a shared
// limit fails the allocation instead of leaving pages that were never accounted.
class AccountingPageSource final : public PhysicalPageSource {
public:
    AccountingPageSource(PhysicalPageSource& inner, ResourceBudget& budget)
        : inner_(inner), budget_(budget) {}

    void SetAccounted(bool accounted) {
        accounted_ = accounted;
        limit_failed_ = false;
    }

    bool Allocate(u64 size, std::vector<PhysicalExtent>& out) override {
        if (!accounted_) {
            return inner_.Allocate(size, out);
        }
        if (!budget_.Reserve(size)) {
            limit_failed_ = true;
            out.clear();
            return false;
        }
        if (!inner_.Allocate(size, out)) {
            budget_.Release(size);
            return false;
        }
        return true;
    }

    void Free(const std::vector<PhysicalExtent>& extents) override {
        inner_.Free(extents);
        if (!accounted_) {
            return;
        }
        u64 total_bytes = 0;
        for (const PhysicalExtent& extent : extents) {
            total_bytes += extent.size;
        }
        budget_.Release(total_bytes);
    }

    // A failed reservation surfaces from the mapper as OutOfMemory; the kernel reports LimitReached.
    Result ClassifyFailure(Result result) const {
        return result == Result::OutOfMemory && limit_failed_ ? Result::LimitReached : result;
    }

private:
    PhysicalPageSource& inner_;
    ResourceBudget& budget_;
    bool accounted_ = false;
    bool limit_failed_ = false;
};

} // namespace Core::NativeMemory
