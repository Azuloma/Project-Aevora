// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/native_memory/native_address_space.h"

// Kernel-free region predicates of KPageTableBase (N2 stage 4b-2); connected to nothing yet.
// Arithmetic is unsigned and wraps exactly like the kernel's, so edge cases agree.
namespace Core::NativeMemory {

// Svc::MemoryState values, static_asserted in k_native_memory_adapters.h; Alias and Inaccessible have no region.
enum class RegionState : u32 {
    Free = 0x00,
    Io = 0x01,
    Static = 0x02,
    Code = 0x03,
    CodeData = 0x04,
    Normal = 0x05,
    Shared = 0x06,
    Alias = 0x07,
    AliasCode = 0x08,
    AliasCodeData = 0x09,
    Ipc = 0x0A,
    Stack = 0x0B,
    ThreadLocal = 0x0C,
    Transferred = 0x0D,
    SharedTransferred = 0x0E,
    SharedCode = 0x0F,
    Inaccessible = 0x10,
    NonSecureIpc = 0x11,
    NonDeviceIpc = 0x12,
    Kernel = 0x13,
    GeneratedCode = 0x14,
    CodeOut = 0x15,
    Coverage = 0x16,
    Insecure = 0x17,
};

class AddressRegions {
public:
    explicit AddressRegions(const Layout& layout) : layout_(layout) {}

    const Layout& GetLayout() const {
        return layout_;
    }

    u64 AddressSpaceStart() const {
        return layout_.address_space.start;
    }
    u64 AddressSpaceSize() const {
        return layout_.address_space.size;
    }
    u64 HeapRegionStart() const {
        return layout_.heap.start;
    }
    u64 HeapRegionSize() const {
        return layout_.heap.size;
    }
    u64 AliasRegionStart() const {
        return layout_.alias.start;
    }
    u64 AliasRegionSize() const {
        return layout_.alias.size;
    }
    u64 StackRegionStart() const {
        return layout_.stack.start;
    }
    u64 StackRegionSize() const {
        return layout_.stack.size;
    }
    u64 KernelMapRegionStart() const {
        return layout_.kernel_map.start;
    }
    u64 KernelMapRegionSize() const {
        return layout_.kernel_map.size;
    }
    u64 CodeRegionStart() const {
        return layout_.code.start;
    }
    u64 CodeRegionSize() const {
        return layout_.code.size;
    }
    u64 AliasCodeRegionStart() const {
        return layout_.alias_code.start;
    }
    u64 AliasCodeRegionSize() const {
        return layout_.alias_code.size;
    }

    bool Contains(u64 address) const {
        return layout_.address_space.start <= address &&
               address <= layout_.address_space.end() - 1;
    }

    bool Contains(u64 address, u64 size) const {
        return layout_.address_space.start <= address && address < address + size &&
               address + size - 1 <= layout_.address_space.end() - 1;
    }

    bool IsInAliasRegion(u64 address, u64 size) const {
        return Contains(address, size) && layout_.alias.start <= address &&
               address + size - 1 <= layout_.alias.end() - 1;
    }

    bool IsInHeapRegion(u64 address, u64 size) const {
        return Contains(address, size) && layout_.heap.start <= address &&
               address + size - 1 <= layout_.heap.end() - 1;
    }

    // Unsafe physical memory is Normal but must sit inside the alias code region.
    bool IsInUnsafeAliasRegion(u64 address, u64 size) const {
        return CanContain(address, size, RegionState::AliasCode);
    }

    // False where the kernel hits UNREACHABLE (Alias, Inaccessible, values past Insecure).
    bool TryGetRegion(RegionState state, Range& out) const {
        switch (state) {
        case RegionState::Free:
        case RegionState::Kernel:
            out = layout_.address_space;
            return true;
        case RegionState::Normal:
            out = layout_.heap;
            return true;
        case RegionState::Ipc:
        case RegionState::NonSecureIpc:
        case RegionState::NonDeviceIpc:
            out = layout_.alias;
            return true;
        case RegionState::Stack:
            out = layout_.stack;
            return true;
        case RegionState::Static:
        case RegionState::ThreadLocal:
            out = layout_.kernel_map;
            return true;
        case RegionState::Io:
        case RegionState::Shared:
        case RegionState::AliasCode:
        case RegionState::AliasCodeData:
        case RegionState::Transferred:
        case RegionState::SharedTransferred:
        case RegionState::SharedCode:
        case RegionState::GeneratedCode:
        case RegionState::CodeOut:
        case RegionState::Coverage:
        case RegionState::Insecure:
            out = layout_.alias_code;
            return true;
        case RegionState::Code:
        case RegionState::CodeData:
            out = layout_.code;
            return true;
        case RegionState::Alias:
        case RegionState::Inaccessible:
            break;
        }
        return false;
    }

    // The kernel asserts the Normal range touches the heap and the Ipc ranges touch the alias region;
    // this version does not assert and returns the value the kernel would return.
    bool CanContain(u64 address, u64 size, RegionState state) const {
        Range region;
        if (!TryGetRegion(state, region)) {
            return false;
        }
        const u64 end = address + size;
        const u64 last = end - 1;
        const bool is_in_region =
            region.start <= address && address < end && last <= region.start + region.size - 1;
        const bool is_in_heap = !(end <= layout_.heap.start || layout_.heap.end() <= address ||
                                  layout_.heap.start == layout_.heap.end());
        const bool is_in_alias = !(end <= layout_.alias.start || layout_.alias.end() <= address ||
                                   layout_.alias.start == layout_.alias.end());
        switch (state) {
        case RegionState::Free:
        case RegionState::Kernel:
            return is_in_region;
        case RegionState::Normal:
            return is_in_region && !is_in_alias;
        case RegionState::Ipc:
        case RegionState::NonSecureIpc:
        case RegionState::NonDeviceIpc:
            return is_in_region && !is_in_heap;
        default:
            return is_in_region && !is_in_heap && !is_in_alias;
        }
    }

private:
    Layout layout_;
};

} // namespace Core::NativeMemory
