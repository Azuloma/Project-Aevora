// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

// Standalone model of KPageTable's guest address space (N2 stage 1); connected to nothing yet.
// Callers serialize access; state, permission and attribute encodings equal the kernel's.
namespace Core::NativeMemory {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u64 PageSize = 0x1000;
constexpr u64 RegionAlignment = 0x200000;
constexpr u64 HeapSizeAlignment = 0x200000;
constexpr u64 MiB = 1ULL << 20;
constexpr u64 GiB = 1ULL << 30;
constexpr u64 InvalidAddressValue = ~0ULL;

enum class AddressSpaceType : u32 { Bits32, Bits36, Bits39, Bits32WithoutAlias };

enum class MemoryState : u32 {
    Free = 0x00000000,
    Code = 0x04DC7E03,
    CodeData = 0x0FFEBD04,
    Normal = 0x077EBD05,
    Shared = 0x04402006,
    Io = 0x00182001,
    Stack = 0x045C3C0B,
    ThreadLocal = 0x0400000C,
    Inaccessible = 0x00000010,
};

constexpr u32 StateAny = 0xFFFFFFFF;
constexpr u32 FlagCanReprotect = 1u << 8;
constexpr u32 FlagCode = 1u << 14;
constexpr u32 FlagCanAlias = 1u << 15;
constexpr u32 FlagCanTransfer = 1u << 17;
constexpr u32 FlagCanDeviceMap = 1u << 19;
constexpr u32 FlagCanAlignedDeviceMap = 1u << 20;
constexpr u32 FlagReferenceCounted = 1u << 22;
constexpr u32 FlagCanCodeMemory = 1u << 25;
constexpr u32 FlagCanChangeAttribute = 1u << 24;
constexpr u32 FlagCanPermissionLock = 1u << 27;

// Svc permission as passed by the guest; stored permissions use the kernel encoding below.
enum class Permission : u8 { None = 0, Read = 1, ReadWrite = 3, ReadExecute = 5 };

constexpr u8 PermUserMask = 7;
constexpr u8 PermKernelRead = 1 << 3;
constexpr u8 PermKernelWrite = 2 << 3;
constexpr u8 PermNotMapped = 1 << 6;
constexpr u8 PermAny = 0xFF;
constexpr u8 PermUserReadWrite = 3 | PermKernelRead | PermKernelWrite;
constexpr u8 PermLockedSource = PermKernelRead | PermNotMapped;

constexpr u8 ToStoredPermission(Permission permission) {
    const u8 user = static_cast<u8>(permission);
    return static_cast<u8>((user & PermUserMask) | PermKernelRead | ((user & 2) << 3) |
                           (user == 0 ? PermNotMapped : 0));
}

constexpr u8 AttributeNone = 0x00;
constexpr u8 AttributeLocked = 0x01;
constexpr u8 AttributeIpcLocked = 0x02;
constexpr u8 AttributeDeviceShared = 0x04;
constexpr u8 AttributeUncached = 0x08;
constexpr u8 AttributePermissionLocked = 0x10;
constexpr u8 AttributeSetMask = AttributeUncached | AttributePermissionLocked;
constexpr u8 AttributeAny = 0xFF;

enum class Result : u32 {
    Success,
    InvalidAddress,
    InvalidSize,
    InvalidMemoryRegion,
    InvalidCurrentMemory,
    InvalidNewMemoryPermission,
    InvalidCombination,
    OutOfMemory,
    LimitReached,
    NotSupported,
    Other,
};

constexpr std::string_view ResultName(Result result) {
    switch (result) {
    case Result::Success:
        return "Success";
    case Result::InvalidAddress:
        return "InvalidAddress";
    case Result::InvalidSize:
        return "InvalidSize";
    case Result::InvalidMemoryRegion:
        return "InvalidMemoryRegion";
    case Result::InvalidCurrentMemory:
        return "InvalidCurrentMemory";
    case Result::InvalidNewMemoryPermission:
        return "InvalidNewMemoryPermission";
    case Result::InvalidCombination:
        return "InvalidCombination";
    case Result::OutOfMemory:
        return "OutOfMemory";
    case Result::LimitReached:
        return "LimitReached";
    case Result::NotSupported:
        return "NotSupported";
    case Result::Other:
        return "Other";
    }
    return "Unknown";
}

struct Range {
    u64 start = 0;
    u64 size = 0;

    constexpr u64 end() const {
        return start + size;
    }
    constexpr bool Contains(u64 address, u64 length) const {
        return size != 0 && length != 0 && start <= address && address + length > address &&
               address + length - 1 <= end() - 1;
    }
    constexpr bool Overlaps(u64 address, u64 length) const {
        return size != 0 && !(address + length <= start || end() <= address);
    }
    friend constexpr bool operator==(const Range&, const Range&) = default;
};

struct LayoutParams {
    AddressSpaceType type = AddressSpaceType::Bits39;
    u64 code_address = 0;
    u64 code_size = 0;
    u64 aslr_space_start = 0;
    bool enable_aslr = false;
    bool extra_alias_region = false;
};

struct Layout {
    u32 width = 0;
    Range address_space;
    Range code;
    Range alias_code;
    Range alias;
    Range heap;
    Range stack;
    Range kernel_map;
};

enum class SpaceKind { MapSmall, MapLarge, Map39Bit, Heap, Alias, Stack };

// Mirrors KAddressSpaceInfo; a start of InvalidAddressValue means the start is chosen per process.
constexpr Range GetSpaceInfo(u32 width, SpaceKind kind) {
    if (width == 32) {
        switch (kind) {
        case SpaceKind::MapSmall:
            return {2 * MiB, GiB - 2 * MiB};
        case SpaceKind::MapLarge:
            return {GiB, 3 * GiB};
        case SpaceKind::Alias:
        case SpaceKind::Heap:
            return {InvalidAddressValue, GiB};
        default:
            break;
        }
    } else if (width == 36) {
        switch (kind) {
        case SpaceKind::MapSmall:
            return {128 * MiB, 2 * GiB - 128 * MiB};
        case SpaceKind::MapLarge:
            return {2 * GiB, 62 * GiB};
        case SpaceKind::Heap:
            return {InvalidAddressValue, 8 * GiB};
        case SpaceKind::Alias:
            return {InvalidAddressValue, 6 * GiB};
        default:
            break;
        }
    } else if (width == 39) {
        switch (kind) {
        case SpaceKind::Map39Bit:
            return {128 * MiB, 512 * GiB - 128 * MiB};
        case SpaceKind::MapSmall:
            return {InvalidAddressValue, 64 * GiB};
        case SpaceKind::Heap:
            return {InvalidAddressValue, 8 * GiB};
        case SpaceKind::Alias:
            return {InvalidAddressValue, 64 * GiB};
        case SpaceKind::Stack:
            return {InvalidAddressValue, 2 * GiB};
        default:
            break;
        }
    }
    return {InvalidAddressValue, 0};
}

constexpr u32 AddressSpaceWidth(AddressSpaceType type) {
    switch (type) {
    case AddressSpaceType::Bits39:
        return 39;
    case AddressSpaceType::Bits36:
        return 36;
    default:
        return 32;
    }
}

constexpr u64 AlignDown(u64 value, u64 alignment) {
    return value & ~(alignment - 1);
}

constexpr u64 AlignUp(u64 value, u64 alignment) {
    return AlignDown(value + alignment - 1, alignment);
}

// Same placement rules as KPageTableBase::InitializeForProcess; random_range(min, max) is inclusive.
template <class RandomRange>
Result ComputeLayout(const LayoutParams& params, RandomRange&& random_range, Layout& out) {
    const u32 width = AddressSpaceWidth(params.type);
    const u64 end = 1ULL << width;
    const u64 code_end = params.code_address + params.code_size;
    if (params.code_size == 0 || code_end < params.code_address || code_end - 1 > end - 1) {
        return Result::InvalidAddress;
    }

    u64 alias_size = GetSpaceInfo(width, SpaceKind::Alias).size;
    u64 heap_size = GetSpaceInfo(width, SpaceKind::Heap).size;
    if (params.type == AddressSpaceType::Bits32WithoutAlias) {
        heap_size += alias_size;
        alias_size = 0;
    }

    Layout layout;
    layout.width = width;
    layout.address_space = {0, end};
    u64 stack_size = 0;
    u64 kernel_map_size = 0;
    u64 process_code_start = 0;
    u64 process_code_end = 0;
    if (width == 39) {
        const Range map39 = GetSpaceInfo(width, SpaceKind::Map39Bit);
        stack_size = GetSpaceInfo(width, SpaceKind::Stack).size;
        kernel_map_size = GetSpaceInfo(width, SpaceKind::MapSmall).size;
        layout.code = {params.aslr_space_start + map39.start, map39.size};
        layout.alias_code = layout.code;
        process_code_start = AlignDown(params.code_address, RegionAlignment);
        process_code_end = AlignUp(code_end, RegionAlignment);
    } else {
        const Range map_small = GetSpaceInfo(width, SpaceKind::MapSmall);
        const Range map_large = GetSpaceInfo(width, SpaceKind::MapLarge);
        layout.code = map_small;
        layout.alias_code = {map_small.start, map_large.end() - map_small.start};
        layout.stack = map_small;
        layout.kernel_map = map_small;
        process_code_start = layout.code.start;
        process_code_end = layout.code.end();
    }

    u64 alloc_start = 0;
    u64 alloc_size = 0;
    if (process_code_start - layout.code.start >= end - process_code_end) {
        alloc_start = layout.code.start;
        alloc_size = process_code_start - layout.code.start;
    } else {
        alloc_start = process_code_end;
        alloc_size = end - process_code_end;
    }

    if (params.type == AddressSpaceType::Bits39 && params.extra_alias_region && alias_size != 0) {
        alias_size += end >> 3;
    }

    const u64 needed_size = alias_size + heap_size + stack_size + kernel_map_size;
    if (alloc_size < needed_size) {
        return Result::OutOfMemory;
    }

    const u64 remaining_size = alloc_size - needed_size;
    u64 alias_rnd = 0;
    u64 heap_rnd = 0;
    u64 stack_rnd = 0;
    u64 kmap_rnd = 0;
    if (params.enable_aslr) {
        const u64 slot_count = remaining_size / RegionAlignment;
        alias_rnd = random_range(u64{0}, slot_count) * RegionAlignment;
        heap_rnd = random_range(u64{0}, slot_count) * RegionAlignment;
        stack_rnd = random_range(u64{0}, slot_count) * RegionAlignment;
        kmap_rnd = random_range(u64{0}, slot_count) * RegionAlignment;
    }

    u64 alias_start = alloc_start + alias_rnd;
    u64 heap_start = alloc_start + heap_rnd;
    if (alias_rnd <= heap_rnd) {
        heap_start += alias_size;
    } else {
        alias_start += heap_size;
    }

    u64 stack_start = layout.stack.start;
    if (stack_size != 0) {
        stack_start = alloc_start + stack_rnd;
        if (alias_rnd < stack_rnd) {
            stack_start += alias_size;
        } else {
            alias_start += stack_size;
        }
        if (heap_rnd < stack_rnd) {
            stack_start += heap_size;
        } else {
            heap_start += stack_size;
        }
    }

    u64 kernel_map_start = layout.kernel_map.start;
    if (kernel_map_size != 0) {
        kernel_map_start = alloc_start + kmap_rnd;
        if (alias_rnd < kmap_rnd) {
            kernel_map_start += alias_size;
        } else {
            alias_start += kernel_map_size;
        }
        if (heap_rnd < kmap_rnd) {
            kernel_map_start += heap_size;
        } else {
            heap_start += kernel_map_size;
        }
        if (stack_size != 0) {
            if (stack_rnd < kmap_rnd) {
                kernel_map_start += stack_size;
            } else {
                stack_start += kernel_map_size;
            }
        }
    }

    layout.alias = {alias_start, alias_size};
    layout.heap = {heap_start, heap_size};
    if (stack_size != 0) {
        layout.stack = {stack_start, stack_size};
    }
    if (kernel_map_size != 0) {
        layout.kernel_map = {kernel_map_start, kernel_map_size};
    }

    // Below 39 bits the stack and kernel map are the code region, so only placed regions are checked.
    std::vector<Range> placed = {layout.alias, layout.heap};
    if (width == 39) {
        placed.push_back(layout.stack);
        placed.push_back(layout.kernel_map);
    }
    for (size_t i = 0; i < placed.size(); ++i) {
        if (placed[i].end() > end) {
            return Result::InvalidMemoryRegion;
        }
        for (size_t j = i + 1; j < placed.size(); ++j) {
            if (placed[i].Overlaps(placed[j].start, placed[j].size)) {
                return Result::InvalidMemoryRegion;
            }
        }
    }

    out = layout;
    return Result::Success;
}

struct MemoryInfo {
    u64 base = 0;
    u64 size = 0;
    MemoryState state = MemoryState::Free;
    u8 attribute = AttributeNone;
    u8 permission = 0;
};

struct StoredBlock {
    u64 base = 0;
    u64 size = 0;
    MemoryState state = MemoryState::Free;
    u8 permission = 0;
    u8 attribute = AttributeNone;
};

class AddressSpace {
public:
    AddressSpace(const Layout& layout, u64 physical_capacity_bytes)
        : layout_(layout), physical_capacity_bytes_(physical_capacity_bytes),
          current_heap_end_(layout.heap.start) {
        blocks_[0] = Block{layout.address_space.end(), MemoryState::Free, 0, AttributeNone};
    }

    const Layout& GetLayout() const {
        return layout_;
    }
    u64 PhysicalBytesInUse() const {
        return physical_bytes_in_use_;
    }
    u64 HeapSizeBytes() const {
        return current_heap_end_ - layout_.heap.start;
    }
    size_t BlockCount() const {
        return blocks_.size();
    }
    void SetMaxHeapSize(u64 size) {
        max_heap_size_ = size;
    }

    // Returns the previous capacity so a caller can restore it after one operation.
    u64 SetPhysicalCapacityBytes(u64 capacity_bytes) {
        const u64 previous = physical_capacity_bytes_;
        physical_capacity_bytes_ = capacity_bytes;
        return previous;
    }

    // Adopts an observed table (stored permissions, boundaries kept); false if it does not tile the space.
    bool ReplaceBlocks(const std::vector<StoredBlock>& observed, u64 physical_bytes_in_use) {
        BlockMap rebuilt;
        const u64 space_start = layout_.address_space.start;
        if (space_start != 0) {
            rebuilt[0] = Block{space_start, MemoryState::Free, 0, AttributeNone};
        }
        u64 cursor = space_start;
        for (const StoredBlock& block : observed) {
            if (block.base != cursor || block.size == 0) {
                return false;
            }
            cursor += block.size;
            rebuilt[block.base] = Block{cursor, block.state, block.permission, block.attribute};
        }
        if (cursor != layout_.address_space.end()) {
            return false;
        }
        blocks_ = std::move(rebuilt);
        physical_bytes_in_use_ = physical_bytes_in_use;
        current_heap_end_ = layout_.heap.start;
        const auto heap_first = blocks_.find(layout_.heap.start);
        for (auto it = heap_first; it != blocks_.end() && it->second.state == MemoryState::Normal &&
                                   it->first < layout_.heap.end();
             ++it) {
            current_heap_end_ = it->second.end < layout_.heap.end() ? it->second.end : layout_.heap.end();
        }
        return true;
    }

    MemoryInfo QueryInfo(u64 address) const {
        const u64 end = layout_.address_space.end();
        if (address >= end) {
            return {end, 0 - end, MemoryState::Inaccessible, AttributeNone, 0};
        }
        const auto it = FindBlock(address);
        return {it->first, it->second.end - it->first, it->second.state, it->second.attribute,
                static_cast<u8>(it->second.permission & PermUserMask)};
    }

    Result MapProcessMemory(u64 address, u64 size, MemoryState state, Permission permission) {
        if (state != MemoryState::Code && state != MemoryState::CodeData) {
            return Result::InvalidCombination;
        }
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (!CanContain(address, size, state)) {
            return Result::InvalidMemoryRegion;
        }
        if (const Result result = CheckMemoryState(address, size, StateAny, 0, PermAny, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        Update(address, size, state, ToStoredPermission(permission), AttributeNone);
        return Result::Success;
    }

    Result MapMemory(u64 dst_address, u64 src_address, u64 size) {
        if (const Result result = ValidateRange(src_address, size); result != Result::Success) {
            return result;
        }
        if (const Result result = ValidateRange(dst_address, size); result != Result::Success) {
            return result;
        }
        if (!CanContain(dst_address, size, MemoryState::Stack)) {
            return Result::InvalidMemoryRegion;
        }
        if (const Result result = CheckMemoryState(src_address, size, FlagCanAlias, FlagCanAlias,
                                                   PermAny, PermUserReadWrite, AttributeAny,
                                                   AttributeNone);
            result != Result::Success) {
            return result;
        }
        if (const Result result = CheckMemoryState(dst_address, size, StateAny, 0, PermAny, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        const MemoryState src_state = FindBlock(src_address)->second.state;
        Update(src_address, size, src_state, PermLockedSource, AttributeLocked);
        Update(dst_address, size, MemoryState::Stack, PermUserReadWrite, AttributeNone);
        aliases_.push_back({dst_address, src_address, size});
        return Result::Success;
    }

    Result UnmapMemory(u64 dst_address, u64 src_address, u64 size) {
        if (const Result result = ValidateRange(src_address, size); result != Result::Success) {
            return result;
        }
        if (const Result result = ValidateRange(dst_address, size); result != Result::Success) {
            return result;
        }
        if (const Result result = CheckMemoryState(src_address, size, FlagCanAlias, FlagCanAlias,
                                                   PermAny, PermLockedSource, AttributeAny,
                                                   AttributeLocked);
            result != Result::Success) {
            return result;
        }
        if (const Result result = CheckMemoryState(dst_address, size, StateAny,
                                                   static_cast<u32>(MemoryState::Stack), 0, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        if (!ReleaseAlias(dst_address, src_address, size)) {
            return Result::InvalidMemoryRegion;
        }
        const MemoryState src_state = FindBlock(src_address)->second.state;
        Update(dst_address, size, MemoryState::Free, 0, AttributeNone);
        Update(src_address, size, src_state, PermUserReadWrite, AttributeNone);
        return Result::Success;
    }

    Result SetMemoryPermission(u64 address, u64 size, Permission permission) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (permission != Permission::None && permission != Permission::Read &&
            permission != Permission::ReadWrite) {
            return Result::InvalidNewMemoryPermission;
        }
        if (const Result result = CheckMemoryState(address, size, FlagCanReprotect, FlagCanReprotect,
                                                   0, 0, AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        const u8 new_permission = ToStoredPermission(permission);
        if (FindBlock(address)->second.permission == new_permission) {
            return Result::Success;
        }
        const MemoryState state = FindBlock(address)->second.state;
        Update(address, size, state, new_permission, AttributeNone);
        return Result::Success;
    }

    Result SetMemoryAttribute(u64 address, u64 size, u8 mask, u8 attribute) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if ((mask | AttributeSetMask) != AttributeSetMask) {
            return Result::InvalidCombination;
        }
        constexpr u8 AttributeTestMask = static_cast<u8>(~(AttributeSetMask | AttributeDeviceShared));
        const u32 state_test_mask =
            ((mask & AttributeUncached) != 0 ? FlagCanChangeAttribute : 0u) |
            ((mask & AttributePermissionLocked) != 0 ? FlagCanPermissionLock : 0u);
        if (const Result result = CheckMemoryState(address, size, state_test_mask, state_test_mask,
                                                   0, 0, AttributeTestMask, AttributeNone,
                                                   static_cast<u8>(~AttributeTestMask));
            result != Result::Success) {
            return result;
        }
        ApplyToBlocks(address, size, [&](Block& block) {
            block.attribute = static_cast<u8>((block.attribute & ~mask) | (attribute & mask));
        });
        return Result::Success;
    }

    // Everything SetHeapSize validates, without changing state, so callers can allocate in between.
    Result CheckSetHeapSize(u64 size) const {
        if (size % HeapSizeAlignment != 0) {
            return Result::InvalidSize;
        }
        if (size > layout_.heap.size || size > max_heap_size_) {
            return Result::OutOfMemory;
        }
        const u64 current_size = HeapSizeBytes();
        if (size == current_size) {
            return Result::Success;
        }
        if (size < current_size) {
            return CheckMemoryState(layout_.heap.start + size, current_size - size, StateAny,
                                    static_cast<u32>(MemoryState::Normal), PermAny,
                                    PermUserReadWrite, AttributeAny, AttributeNone);
        }
        const u64 grow_size = size - current_size;
        if (physical_bytes_in_use_ + grow_size > physical_capacity_bytes_) {
            return Result::LimitReached;
        }
        return CheckMemoryState(current_heap_end_, grow_size, StateAny, 0, PermAny, 0,
                                AttributeAny, AttributeNone);
    }

    Result SetHeapSize(u64& out_address, u64 size) {
        if (const Result result = CheckSetHeapSize(size); result != Result::Success) {
            return result;
        }
        out_address = layout_.heap.start;
        const u64 current_size = HeapSizeBytes();
        if (size < current_size) {
            const u64 release_start = layout_.heap.start + size;
            const u64 release_size = current_size - size;
            Update(release_start, release_size, MemoryState::Free, 0, AttributeNone);
            physical_bytes_in_use_ -= release_size;
            current_heap_end_ = release_start;
        } else if (size > current_size) {
            const u64 grow_size = size - current_size;
            Update(current_heap_end_, grow_size, MemoryState::Normal, PermUserReadWrite,
                   AttributeNone);
            physical_bytes_in_use_ += grow_size;
            current_heap_end_ += grow_size;
        }
        return Result::Success;
    }

    // Everything MapPhysicalMemory validates, without changing state, so callers can allocate in between.
    Result CheckMapPhysicalMemory(u64 address, u64 size) const {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (!layout_.alias.Contains(address, size)) {
            return Result::InvalidMemoryRegion;
        }
        const u64 free_bytes = CountFreeBytes(address, size);
        if (free_bytes == 0) {
            return Result::Success;
        }
        if (physical_bytes_in_use_ + free_bytes > physical_capacity_bytes_) {
            return Result::LimitReached;
        }
        return Result::Success;
    }

    Result MapPhysicalMemory(u64 address, u64 size) {
        if (const Result result = CheckMapPhysicalMemory(address, size);
            result != Result::Success) {
            return result;
        }
        const u64 free_bytes = CountFreeBytes(address, size);
        if (free_bytes == 0) {
            return Result::Success;
        }
        ApplyToBlocks(address, size, [](Block& block) {
            if (block.state == MemoryState::Free) {
                block = Block{block.end, MemoryState::Normal, PermUserReadWrite, AttributeNone};
            }
        });
        physical_bytes_in_use_ += free_bytes;
        return Result::Success;
    }

    Result UnmapPhysicalMemory(u64 address, u64 size) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (!layout_.alias.Contains(address, size)) {
            return Result::InvalidMemoryRegion;
        }
        if (const Result result = CheckUnmappablePhysical(address, size);
            result != Result::Success) {
            return result;
        }
        const u64 mapped_bytes = size - CountFreeBytes(address, size);
        if (mapped_bytes == 0) {
            return Result::Success;
        }
        ApplyToBlocks(address, size, [](Block& block) {
            if (block.state == MemoryState::Normal) {
                block = Block{block.end, MemoryState::Free, 0, AttributeNone};
            }
        });
        physical_bytes_in_use_ -= mapped_bytes;
        return Result::Success;
    }

    Result MapSharedMemory(u64 address, u64 size, Permission permission) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (permission != Permission::Read && permission != Permission::ReadWrite) {
            return Result::InvalidNewMemoryPermission;
        }
        if (!CanContain(address, size, MemoryState::Shared)) {
            return Result::InvalidMemoryRegion;
        }
        if (const Result result = CheckMemoryState(address, size, StateAny, 0, PermAny, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        Update(address, size, MemoryState::Shared, ToStoredPermission(permission), AttributeNone);
        return Result::Success;
    }

    Result UnmapSharedMemory(u64 address, u64 size) {
        return ReleaseTo(address, size, MemoryState::Shared);
    }

    Result MapThreadLocalPage(u64 address) {
        if (const Result result = ValidateRange(address, PageSize); result != Result::Success) {
            return result;
        }
        if (!CanContain(address, PageSize, MemoryState::ThreadLocal)) {
            return Result::InvalidMemoryRegion;
        }
        if (const Result result = CheckMemoryState(address, PageSize, StateAny, 0, PermAny, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        Update(address, PageSize, MemoryState::ThreadLocal, PermUserReadWrite, AttributeNone);
        return Result::Success;
    }

    Result UnmapThreadLocalPage(u64 address) {
        return ReleaseTo(address, PageSize, MemoryState::ThreadLocal);
    }

    // Like SetProcessMemoryPermission: a writable mapping turns Code into CodeData.
    Result SetProcessMemoryPermission(u64 address, u64 size, Permission permission) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (permission != Permission::None && permission != Permission::Read &&
            permission != Permission::ReadWrite && permission != Permission::ReadExecute) {
            return Result::InvalidNewMemoryPermission;
        }
        if (const Result result = CheckMemoryState(address, size, FlagCode, FlagCode, 0, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        MemoryState state = FindBlock(address)->second.state;
        if (permission == Permission::ReadWrite && state == MemoryState::Code) {
            state = MemoryState::CodeData;
        }
        Update(address, size, state, ToStoredPermission(permission), AttributeNone);
        return Result::Success;
    }

    Result MapIoMemory(u64 address, u64 size, Permission permission) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (permission != Permission::Read && permission != Permission::ReadWrite) {
            return Result::InvalidNewMemoryPermission;
        }
        if (!CanContain(address, size, MemoryState::Io)) {
            return Result::InvalidMemoryRegion;
        }
        if (const Result result = CheckMemoryState(address, size, StateAny, 0, PermAny, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        Update(address, size, MemoryState::Io, ToStoredPermission(permission), AttributeNone);
        return Result::Success;
    }

    Result UnmapIoMemory(u64 address, u64 size) {
        return ReleaseTo(address, size, MemoryState::Io);
    }

    // Like LockMemoryAndOpen: a uniform user-read-write range gains Locked and, if non-zero, a stored permission.
    Result LockMemory(u64 address, u64 size, u32 state_flag, u8 new_stored_permission) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        const u32 test = state_flag | FlagReferenceCounted;
        if (const Result result = CheckMemoryState(address, size, test, test, PermAny,
                                                   PermUserReadWrite, AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        const MemoryState state = FindBlock(address)->second.state;
        const u8 stored = new_stored_permission != 0 ? new_stored_permission : PermUserReadWrite;
        Update(address, size, state, stored, AttributeLocked);
        return Result::Success;
    }

    Result UnlockMemory(u64 address, u64 size, u32 state_flag) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        const u32 test = state_flag | FlagReferenceCounted;
        if (const Result result = CheckMemoryState(address, size, test, test, 0, 0, AttributeAny,
                                                   AttributeLocked);
            result != Result::Success) {
            return result;
        }
        const MemoryState state = FindBlock(address)->second.state;
        Update(address, size, state, PermUserReadWrite, AttributeNone);
        return Result::Success;
    }

    // Like LockForMapDeviceAddressSpace: bumps the per-block device use count and sets DeviceShared.
    Result LockForMapDevice(u64 address, u64 size, u8 required_permission, bool aligned,
                            bool check_heap, bool& out_is_io) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        const u32 test = (aligned ? FlagCanAlignedDeviceMap : FlagCanDeviceMap) |
                         (check_heap ? FlagReferenceCounted : 0u);
        constexpr u8 BlockedAttributes = AttributeIpcLocked | AttributeLocked;
        if (const Result result = CheckMemoryState(address, size, test, test, required_permission,
                                                   required_permission, BlockedAttributes,
                                                   AttributeNone,
                                                   AttributeDeviceShared);
            result != Result::Success) {
            return result;
        }
        out_is_io = FindBlock(address)->second.state == MemoryState::Io;
        ApplyToBlocks(address, size, [](Block& block) {
            block.device_count++;
            block.attribute |= AttributeDeviceShared;
        });
        return Result::Success;
    }

    Result UnlockForDevice(u64 address, u64 size) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        const u64 range_end = address + size;
        for (auto it = FindBlock(address); it != blocks_.end() && it->first < range_end; ++it) {
            const Block& block = it->second;
            const bool shared = (block.attribute & (AttributeDeviceShared | AttributeLocked)) ==
                                AttributeDeviceShared;
            if ((static_cast<u32>(block.state) & FlagCanDeviceMap) == 0 || !shared ||
                block.device_count == 0) {
                return Result::InvalidCurrentMemory;
            }
        }
        ApplyToBlocks(address, size, [](Block& block) {
            if (--block.device_count == 0) {
                block.attribute &= static_cast<u8>(~AttributeDeviceShared);
            }
        });
        return Result::Success;
    }

    // IPC map-alias is outside the supported path; callers must fail fast on this result.
    Result SetupForIpc(u64, u64) {
        return Result::NotSupported;
    }

private:
    struct Block {
        u64 end = 0;
        MemoryState state = MemoryState::Free;
        u8 permission = 0;
        u8 attribute = AttributeNone;
        std::uint16_t device_count = 0;
    };
    struct AliasRecord {
        u64 dst;
        u64 src;
        u64 size;
    };
    using BlockMap = std::map<u64, Block>;

    BlockMap::const_iterator FindBlock(u64 address) const {
        return std::prev(blocks_.upper_bound(address));
    }
    BlockMap::iterator FindBlock(u64 address) {
        return std::prev(blocks_.upper_bound(address));
    }

    Result ValidateRange(u64 address, u64 size) const {
        if (address % PageSize != 0) {
            return Result::InvalidAddress;
        }
        if (size == 0 || size % PageSize != 0) {
            return Result::InvalidSize;
        }
        if (!layout_.address_space.Contains(address, size)) {
            return Result::InvalidMemoryRegion;
        }
        return Result::Success;
    }

    bool CanContain(u64 address, u64 size, MemoryState state) const {
        const bool in_heap = layout_.heap.Overlaps(address, size);
        const bool in_alias = layout_.alias.Overlaps(address, size);
        switch (state) {
        case MemoryState::Normal:
            return layout_.heap.Contains(address, size) && !in_alias;
        case MemoryState::Stack:
            return layout_.stack.Contains(address, size) && !in_heap && !in_alias;
        case MemoryState::ThreadLocal:
            return layout_.kernel_map.Contains(address, size) && !in_heap && !in_alias;
        case MemoryState::Shared:
        case MemoryState::Io:
            return layout_.alias_code.Contains(address, size) && !in_heap && !in_alias;
        case MemoryState::Code:
        case MemoryState::CodeData:
            return layout_.code.Contains(address, size) && !in_heap && !in_alias;
        default:
            return false;
        }
    }

    // Like KPageTableBase::CheckMemoryState: state, permission and attribute are uniform over the range.
    Result CheckMemoryState(u64 address, u64 size, u32 state_mask, u32 state, u8 perm_mask, u8 perm,
                            u8 attr_mask, u8 attr, u8 ignore_attr = 0) const {
        auto it = FindBlock(address);
        const u64 range_end = address + size;
        const Block& first = it->second;
        for (; it != blocks_.end() && it->first < range_end; ++it) {
            const Block& block = it->second;
            const bool uniform = block.state == first.state &&
                                 block.permission == first.permission &&
                                 (block.attribute & ~ignore_attr) == (first.attribute & ~ignore_attr);
            if (!uniform || (static_cast<u32>(block.state) & state_mask) != state ||
                (block.permission & perm_mask) != perm ||
                (block.attribute & ~ignore_attr & attr_mask) != attr) {
                return Result::InvalidCurrentMemory;
            }
        }
        return Result::Success;
    }

    // MapPhysicalMemory counts any non-Free page as already mapped, as the kernel does.
    u64 CountFreeBytes(u64 address, u64 size) const {
        u64 free_bytes = 0;
        const u64 range_end = address + size;
        for (auto it = FindBlock(address); it != blocks_.end() && it->first < range_end; ++it) {
            if (it->second.state == MemoryState::Free) {
                const u64 overlap_start = it->first < address ? address : it->first;
                const u64 overlap_end = it->second.end < range_end ? it->second.end : range_end;
                free_bytes += overlap_end - overlap_start;
            }
        }
        return free_bytes;
    }

    // UnmapPhysicalMemory accepts Free pages and Normal pages without attributes, any permission.
    Result CheckUnmappablePhysical(u64 address, u64 size) const {
        const u64 range_end = address + size;
        for (auto it = FindBlock(address); it != blocks_.end() && it->first < range_end; ++it) {
            const Block& block = it->second;
            const bool is_normal =
                block.state == MemoryState::Normal && block.attribute == AttributeNone;
            if (!is_normal && block.state != MemoryState::Free) {
                return Result::InvalidCurrentMemory;
            }
        }
        return Result::Success;
    }

    Result ReleaseTo(u64 address, u64 size, MemoryState expected) {
        if (const Result result = ValidateRange(address, size); result != Result::Success) {
            return result;
        }
        if (const Result result = CheckMemoryState(address, size, StateAny,
                                                   static_cast<u32>(expected), 0, 0,
                                                   AttributeAny, AttributeNone);
            result != Result::Success) {
            return result;
        }
        Update(address, size, MemoryState::Free, 0, AttributeNone);
        return Result::Success;
    }

    const AliasRecord* FindAlias(u64 dst, u64 src) const {
        for (const AliasRecord& record : aliases_) {
            const bool contains_dst = record.dst <= dst && dst < record.dst + record.size;
            if (contains_dst && src - dst == record.src - record.dst) {
                return &record;
            }
        }
        return nullptr;
    }

    // The range may span several adjacent records as long as each page pair was mapped together.
    bool ReleaseAlias(u64 dst, u64 src, u64 size) {
        const u64 range_end = dst + size;
        for (u64 cursor = dst; cursor < range_end;) {
            const AliasRecord* record = FindAlias(cursor, src + (cursor - dst));
            if (record == nullptr) {
                return false;
            }
            const u64 record_end = record->dst + record->size;
            cursor = record_end < range_end ? record_end : range_end;
        }
        std::vector<AliasRecord> kept;
        for (const AliasRecord& record : aliases_) {
            const u64 record_end = record.dst + record.size;
            if (record_end <= dst || range_end <= record.dst) {
                kept.push_back(record);
                continue;
            }
            if (record.dst < dst) {
                kept.push_back({record.dst, record.src, dst - record.dst});
            }
            if (range_end < record_end) {
                kept.push_back({range_end, record.src + (range_end - record.dst), record_end - range_end});
            }
        }
        aliases_ = std::move(kept);
        return true;
    }

    void SplitAt(u64 address) {
        const auto it = FindBlock(address);
        if (it->first == address) {
            return;
        }
        Block tail = it->second;
        it->second.end = address;
        blocks_[address] = tail;
    }

    void Coalesce(u64 address, u64 size) {
        auto it = FindBlock(address);
        if (it != blocks_.begin()) {
            --it;
        }
        const u64 range_end = address + size;
        while (it != blocks_.end() && it->first <= range_end) {
            auto next = std::next(it);
            if (next != blocks_.end() && it->second.end == next->first &&
                it->second.state == next->second.state &&
                it->second.permission == next->second.permission &&
                it->second.attribute == next->second.attribute &&
                it->second.device_count == next->second.device_count) {
                it->second.end = next->second.end;
                blocks_.erase(next);
            } else {
                it = next;
            }
        }
    }

    template <class Fn>
    void ApplyToBlocks(u64 address, u64 size, Fn&& fn) {
        const u64 range_end = address + size;
        SplitAt(address);
        if (range_end < layout_.address_space.end()) {
            SplitAt(range_end);
        }
        for (auto it = FindBlock(address); it != blocks_.end() && it->first < range_end; ++it) {
            fn(it->second);
        }
        Coalesce(address, size);
    }

    void Update(u64 address, u64 size, MemoryState state, u8 permission, u8 attribute) {
        ApplyToBlocks(address, size, [&](Block& block) {
            block = Block{block.end, state, permission, attribute};
        });
    }

    Layout layout_;
    u64 physical_capacity_bytes_;
    u64 physical_bytes_in_use_ = 0;
    u64 current_heap_end_;
    u64 max_heap_size_ = 0;
    BlockMap blocks_;
    std::vector<AliasRecord> aliases_;
};

} // namespace Core::NativeMemory
