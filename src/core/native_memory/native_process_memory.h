// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include "core/native_memory/native_address_space.h"

// Standalone physical backing and host-mapping layer over AddressSpace (N2 stage 3a); connected to nothing yet.
// Callers serialize access. Physical addresses are opaque DeviceMemory offsets owned by the caller's source.
namespace Core::NativeMemory {

struct PhysicalExtent {
    u64 phys = 0;
    u64 size = 0;
};

// Reads and writes the physical memory the backing refers to (DeviceMemory in the kernel adapter).
class PhysicalAccess {
public:
    virtual ~PhysicalAccess() = default;
    virtual void Read(u64 phys, void* out, u64 size) = 0;
    virtual void Write(u64 phys, const void* data, u64 size) = 0;
};

class PhysicalPageSource {
public:
    virtual ~PhysicalPageSource() = default;
    virtual bool Allocate(u64 size, std::vector<PhysicalExtent>& out) = 0;
    virtual void Free(const std::vector<PhysicalExtent>& extents) = 0;
};

// Mirrors Core::Memory::MapMemoryRegion/UnmapRegion/ProtectRegion; permissions use the stored encoding.
class HostMemoryMap {
public:
    virtual ~HostMemoryMap() = default;
    virtual void Map(u64 address, u64 size, u64 phys, u8 permission) = 0;
    virtual void Unmap(u64 address, u64 size) = 0;
    virtual void Protect(u64 address, u64 size, u8 permission) = 0;
};

namespace Detail {

struct Backing {
    u64 phys = 0;
    bool host_mapped = true;
    bool owned = false;

    Backing Advanced(u64 delta) const {
        return {phys + delta, host_mapped, owned};
    }
};

// Run-length map over address ranges; a run's value describes its first page.
template <class V>
class RunMap {
public:
    void Assign(u64 address, u64 size, const V& value) {
        Erase(address, size);
        runs_[address] = Run{address + size, value};
    }

    void Erase(u64 address, u64 size) {
        const u64 end = address + size;
        auto it = runs_.lower_bound(address);
        if (it != runs_.begin() && std::prev(it)->second.end > address) {
            --it;
        }
        std::vector<std::pair<u64, Run>> kept;
        while (it != runs_.end() && it->first < end) {
            const u64 start = it->first;
            const Run run = it->second;
            it = runs_.erase(it);
            if (start < address) {
                kept.push_back({start, Run{address, run.value}});
            }
            if (run.end > end) {
                kept.push_back({end, Run{run.end, run.value.Advanced(end - start)}});
            }
        }
        for (const auto& [start, run] : kept) {
            runs_[start] = run;
        }
    }

    // Calls fn(piece_address, piece_size, piece_value) per covered piece; false if the range has a gap.
    template <class Fn>
    bool Visit(u64 address, u64 size, Fn&& fn) const {
        auto it = runs_.upper_bound(address);
        if (it == runs_.begin()) {
            return false;
        }
        --it;
        const u64 end = address + size;
        for (u64 cursor = address; cursor < end; ++it) {
            if (it == runs_.end() || it->first > cursor || it->second.end <= cursor) {
                return false;
            }
            const u64 piece_end = std::min(it->second.end, end);
            fn(cursor, piece_end - cursor, it->second.value.Advanced(cursor - it->first));
            cursor = piece_end;
        }
        return true;
    }

    template <class Fn>
    void ForEach(Fn&& fn) const {
        for (const auto& [address, run] : runs_) {
            fn(address, run.end - address, run.value);
        }
    }

    void Clear() {
        runs_.clear();
    }

private:
    struct Run {
        u64 end = 0;
        V value;
    };
    std::map<u64, Run> runs_;
};

// Hands out consecutive pieces of an extent list.
class ExtentCursor {
public:
    explicit ExtentCursor(const std::vector<PhysicalExtent>& extents) : extents_(extents) {}

    // An exhausted list yields an empty piece so the caller can report the shortfall.
    PhysicalExtent Take(u64 wanted) {
        if (index_ >= extents_.size()) {
            return {};
        }
        const PhysicalExtent& extent = extents_[index_];
        const u64 length = std::min(extent.size - offset_, wanted);
        const PhysicalExtent piece{extent.phys + offset_, length};
        offset_ += length;
        if (offset_ == extent.size) {
            ++index_;
            offset_ = 0;
        }
        return piece;
    }

private:
    const std::vector<PhysicalExtent>& extents_;
    size_t index_ = 0;
    u64 offset_ = 0;
};

} // namespace Detail

class MappedAddressSpace {
public:
    static constexpr size_t CopyChunkBytes = 16 * 1024;

    MappedAddressSpace(const Layout& layout, u64 physical_capacity_bytes, PhysicalPageSource& source,
                       HostMemoryMap& host)
        : space_(layout, physical_capacity_bytes), source_(source), host_(host) {}

    MappedAddressSpace(const MappedAddressSpace&) = delete;
    MappedAddressSpace& operator=(const MappedAddressSpace&) = delete;

    // Read-only: state changes must go through this class so host mappings and backing stay in step.
    const AddressSpace& Space() const {
        return space_;
    }
    void SetMaxHeapSize(u64 size) {
        space_.SetMaxHeapSize(size);
    }
    u64 SetPhysicalCapacityBytes(u64 capacity_bytes) {
        return space_.SetPhysicalCapacityBytes(capacity_bytes);
    }
    // Pages that lost their physical backing after a successful state change; always 0 when consistent.
    u64 InconsistencyCount() const {
        return inconsistency_count_;
    }

    // Unmaps everything on the host and returns the pages this layer allocated; external pages stay untouched.
    void Finalize() {
        std::vector<PhysicalExtent> owned_pages;
        backing_.ForEach([&](u64 address, u64 length, const Detail::Backing& backing) {
            if (backing.host_mapped) {
                host_.Unmap(address, length);
            }
            if (backing.owned) {
                owned_pages.push_back({backing.phys, length});
            }
        });
        backing_.Clear();
        FreeAll(owned_pages);
    }

    // Allocates pages for process code or the main thread stack; UnmapPages or Finalize frees them.
    Result MapPages(u64 address, u64 size, MemoryState state, Permission permission) {
        if (const Result result = space_.CheckMapPages(address, size, state);
            result != Result::Success) {
            return result;
        }
        std::vector<PhysicalExtent> pool;
        if (!AllocateFor(size, pool)) {
            return Result::OutOfMemory;
        }
        const Result result = space_.MapPages(address, size, state, permission);
        if (result != Result::Success) {
            FreeAll(pool);
            return result;
        }
        Detail::ExtentCursor cursor(pool);
        MapOwnedFromCursor(address, size, cursor, ToStoredPermission(permission));
        return Result::Success;
    }

    // Like MapPages at a free address found by AddressSpace::FindFreeArea.
    Result MapPagesAnywhere(u64& out_address, u64 size, MemoryState state, Permission permission,
                            u64 guard_pages) {
        u64 address = 0;
        if (const Result result = space_.FindFreeArea(state, size / PageSize, PageSize, guard_pages,
                                                      address);
            result != Result::Success) {
            return result;
        }
        const Result result = MapPages(address, size, state, permission);
        if (result == Result::Success) {
            out_address = address;
        }
        return result;
    }

    Result UnmapPages(u64 address, u64 size, MemoryState state) {
        const Result result = space_.UnmapPages(address, size, state);
        if (result == Result::Success) {
            ReleaseBacking(address, size);
        }
        return result;
    }

    Result MapPhysicalMemory(u64 address, u64 size) {
        if (const Result result = space_.CheckMapPhysicalMemory(address, size);
            result != Result::Success) {
            return result;
        }
        const std::vector<Range> free_runs = CollectRuns(address, size, MemoryState::Free);
        std::vector<PhysicalExtent> pool;
        if (const u64 total = TotalSize(free_runs); total != 0 && !AllocateFor(total, pool)) {
            return Result::OutOfMemory;
        }
        const Result result = space_.MapPhysicalMemory(address, size);
        if (result != Result::Success) {
            FreeAll(pool);
            return result;
        }
        Detail::ExtentCursor cursor(pool);
        for (const Range& run : free_runs) {
            MapOwnedFromCursor(run.start, run.size, cursor);
        }
        return Result::Success;
    }

    Result UnmapPhysicalMemory(u64 address, u64 size) {
        const std::vector<Range> normal_runs = CollectRuns(address, size, MemoryState::Normal);
        const Result result = space_.UnmapPhysicalMemory(address, size);
        if (result != Result::Success) {
            return result;
        }
        for (const Range& run : normal_runs) {
            ReleaseBacking(run.start, run.size);
        }
        return Result::Success;
    }

    Result SetHeapSize(u64& out_address, u64 size) {
        const u64 before = space_.HeapSizeBytes();
        const u64 heap_start = space_.GetLayout().heap.start;
        std::vector<PhysicalExtent> pool;
        if (const Result result = space_.CheckSetHeapSize(size); result != Result::Success) {
            return result;
        }
        if (size > before && !AllocateFor(size - before, pool)) {
            return Result::OutOfMemory;
        }
        const Result result = space_.SetHeapSize(out_address, size);
        if (result != Result::Success) {
            FreeAll(pool);
            return result;
        }
        const u64 after = space_.HeapSizeBytes();
        if (after > before) {
            Detail::ExtentCursor cursor(pool);
            MapOwnedFromCursor(heap_start + before, after - before, cursor);
        } else if (after < before) {
            ReleaseBacking(heap_start + after, before - after);
        }
        return Result::Success;
    }

    Result MapMemory(u64 dst_address, u64 src_address, u64 size) {
        const Result result = space_.MapMemory(dst_address, src_address, size);
        if (result != Result::Success) {
            return result;
        }
        std::vector<PhysicalExtent> extents;
        if (!CollectBacking(src_address, size, extents)) {
            ++inconsistency_count_;
            return Result::Success;
        }
        host_.Unmap(src_address, size);
        SetHostMapped(src_address, size, false);
        MapExtents(dst_address, extents, PermUserReadWrite);
        return Result::Success;
    }

    Result UnmapMemory(u64 dst_address, u64 src_address, u64 size) {
        const Result result = space_.UnmapMemory(dst_address, src_address, size);
        if (result != Result::Success) {
            return result;
        }
        host_.Unmap(dst_address, size);
        backing_.Erase(dst_address, size);
        std::vector<PhysicalExtent> extents;
        if (!CollectBacking(src_address, size, extents)) {
            ++inconsistency_count_;
            return Result::Success;
        }
        u64 cursor = src_address;
        for (const PhysicalExtent& extent : extents) {
            host_.Map(cursor, extent.size, extent.phys, PermUserReadWrite);
            cursor += extent.size;
        }
        SetHostMapped(src_address, size, true);
        return Result::Success;
    }

    Result SetMemoryPermission(u64 address, u64 size, Permission permission) {
        return ProtectOnSuccess(space_.SetMemoryPermission(address, size, permission), address, size,
                                ToStoredPermission(permission));
    }

    Result SetProcessMemoryPermission(u64 address, u64 size, Permission permission) {
        return ProtectOnSuccess(space_.SetProcessMemoryPermission(address, size, permission),
                                address, size, ToStoredPermission(permission));
    }

    Result SetMemoryAttribute(u64 address, u64 size, u8 mask, u8 attribute) {
        return space_.SetMemoryAttribute(address, size, mask, attribute);
    }

    // The mapped physical pages stay owned by the caller (kernel objects), not by this layer.
    Result MapProcessMemory(u64 address, u64 size, MemoryState state, Permission permission,
                            const std::vector<PhysicalExtent>& extents) {
        if (TotalSize(extents) != size) {
            return Result::InvalidSize;
        }
        const Result result = space_.MapProcessMemory(address, size, state, permission);
        if (result == Result::Success) {
            MapExtents(address, extents, ToStoredPermission(permission));
        }
        return result;
    }

    Result MapSharedMemory(u64 address, u64 size, Permission permission,
                           const std::vector<PhysicalExtent>& extents) {
        if (TotalSize(extents) != size) {
            return Result::InvalidSize;
        }
        const Result result = space_.MapSharedMemory(address, size, permission);
        if (result == Result::Success) {
            MapExtents(address, extents, ToStoredPermission(permission));
        }
        return result;
    }

    Result UnmapSharedMemory(u64 address, u64 size) {
        return UnmapExternal(space_.UnmapSharedMemory(address, size), address, size);
    }

    Result MapIoMemory(u64 address, u64 size, Permission permission,
                       const std::vector<PhysicalExtent>& extents) {
        if (TotalSize(extents) != size) {
            return Result::InvalidSize;
        }
        const Result result = space_.MapIoMemory(address, size, permission);
        if (result == Result::Success) {
            MapExtents(address, extents, ToStoredPermission(permission));
        }
        return result;
    }

    Result UnmapIoMemory(u64 address, u64 size) {
        return UnmapExternal(space_.UnmapIoMemory(address, size), address, size);
    }

    Result MapThreadLocalPage(u64 address, u64 phys) {
        const Result result = space_.MapThreadLocalPage(address);
        if (result == Result::Success) {
            MapExtents(address, {{phys, PageSize}}, PermUserReadWrite);
        }
        return result;
    }

    Result MapThreadLocalPageAnywhere(u64& out_address, u64 phys, u64 guard_pages) {
        u64 address = 0;
        if (const Result result = space_.FindFreeArea(MemoryState::ThreadLocal, 1, PageSize,
                                                      guard_pages, address);
            result != Result::Success) {
            return result;
        }
        const Result result = MapThreadLocalPage(address, phys);
        if (result == Result::Success) {
            out_address = address;
        }
        return result;
    }

    Result UnmapThreadLocalPage(u64 address) {
        return UnmapExternal(space_.UnmapThreadLocalPage(address), address, PageSize);
    }

    // The IPC message copies: the rule is checked first and nothing is copied on failure.
    Result ReadLinear(u64 address, u64 size, const CopyRule& rule, PhysicalAccess& access,
                      void* out) const {
        std::vector<PhysicalExtent> extents;
        if (const Result result = CollectCopyExtents(address, size, &rule, extents);
            result != Result::Success) {
            return result;
        }
        u8* cursor = static_cast<u8*>(out);
        for (const PhysicalExtent& extent : extents) {
            access.Read(extent.phys, cursor, extent.size);
            cursor += extent.size;
        }
        return Result::Success;
    }

    Result WriteLinear(u64 address, u64 size, const CopyRule& rule, PhysicalAccess& access,
                       const void* data) const {
        std::vector<PhysicalExtent> extents;
        if (const Result result = CollectCopyExtents(address, size, &rule, extents);
            result != Result::Success) {
            return result;
        }
        const u8* cursor = static_cast<const u8*>(data);
        for (const PhysicalExtent& extent : extents) {
            access.Write(extent.phys, cursor, extent.size);
            cursor += extent.size;
        }
        return Result::Success;
    }

    // dst_rule == nullptr skips the destination check (CopyMemoryFromHeapToHeapWithoutCheckDestination).
    static Result CopyLinear(const MappedAddressSpace& src, u64 src_address, const CopyRule& src_rule,
                             const MappedAddressSpace& dst, u64 dst_address, const CopyRule* dst_rule,
                             u64 size, PhysicalAccess& access) {
        std::vector<PhysicalExtent> src_extents;
        std::vector<PhysicalExtent> dst_extents;
        if (const Result result = src.CollectCopyExtents(src_address, size, &src_rule, src_extents);
            result != Result::Success) {
            return result;
        }
        if (dst_rule != nullptr) {
            if (const Result result = dst.CollectCopyExtents(dst_address, size, dst_rule, dst_extents);
                result != Result::Success) {
                return result;
            }
        } else if (const Result result = dst.CollectCopyExtents(dst_address, size, nullptr, dst_extents);
                   result != Result::Success) {
            return result;
        }
        std::array<u8, CopyChunkBytes> chunk;
        size_t src_index = 0;
        size_t dst_index = 0;
        u64 src_used = 0;
        u64 dst_used = 0;
        for (u64 copied = 0; copied < size;) {
            const u64 length = std::min({size - copied, src_extents[src_index].size - src_used,
                                         dst_extents[dst_index].size - dst_used,
                                         static_cast<u64>(CopyChunkBytes)});
            access.Read(src_extents[src_index].phys + src_used, chunk.data(), length);
            access.Write(dst_extents[dst_index].phys + dst_used, chunk.data(), length);
            copied += length;
            src_used += length;
            dst_used += length;
            if (src_used == src_extents[src_index].size) {
                ++src_index;
                src_used = 0;
            }
            if (dst_used == dst_extents[dst_index].size) {
                ++dst_index;
                dst_used = 0;
            }
        }
        return Result::Success;
    }

    Result LockForTransferMemory(u64 address, u64 size, Permission new_permission,
                                 std::vector<PhysicalExtent>& out_extents) {
        const u8 stored_permission = ToStoredPermission(new_permission);
        const Result result = space_.LockMemory(address, size, FlagCanTransfer, stored_permission);
        if (result != Result::Success) {
            return result;
        }
        out_extents.clear();
        if (!CollectBacking(address, size, out_extents)) {
            ++inconsistency_count_;
        }
        host_.Protect(address, size, stored_permission);
        return Result::Success;
    }

    Result UnlockForTransferMemory(u64 address, u64 size) {
        const Result result = space_.UnlockMemory(address, size, FlagCanTransfer);
        if (result == Result::Success) {
            host_.Protect(address, size, PermUserReadWrite);
        }
        return result;
    }

    // required_permission is a stored-permission mask the pages must carry; 0 skips the check.
    Result LockForMapDevice(u64 address, u64 size, u8 required_permission, bool aligned,
                            bool check_heap, bool& out_is_io) {
        return space_.LockForMapDevice(address, size, required_permission, aligned, check_heap,
                                       out_is_io);
    }

    Result UnlockForDevice(u64 address, u64 size) {
        return space_.UnlockForDevice(address, size);
    }

    bool GetPhysicalAddress(u64 address, u64& out_phys) const {
        return backing_.Visit(address, 1, [&](u64, u64, const Detail::Backing& backing) {
            out_phys = backing.phys;
        });
    }

    // Equivalent of MakePageGroup: false if any page of the range has no backing.
    bool QueryPhysicalExtents(u64 address, u64 size, std::vector<PhysicalExtent>& out) const {
        out.clear();
        return CollectBacking(address, size, out);
    }

private:
    // Rule check (when given) plus the physical pieces of the range; any unbacked page fails it.
    Result CollectCopyExtents(u64 address, u64 size, const CopyRule* rule,
                              std::vector<PhysicalExtent>& out) const {
        if (size == 0) {
            return CheckContained(address, 1);
        }
        const Result checked = rule != nullptr ? space_.CheckCopyRange(address, size, *rule)
                                               : CheckContained(address, size);
        if (checked != Result::Success) {
            return checked;
        }
        return CollectBacking(address, size, out) ? Result::Success : Result::InvalidCurrentMemory;
    }

    Result CheckContained(u64 address, u64 size) const {
        const bool contained = address + size >= address &&
                               space_.GetLayout().address_space.Contains(address, size);
        return contained ? Result::Success : Result::InvalidCurrentMemory;
    }

    static u64 TotalSize(const std::vector<PhysicalExtent>& extents) {
        u64 total = 0;
        for (const PhysicalExtent& extent : extents) {
            total += extent.size;
        }
        return total;
    }
    static u64 TotalSize(const std::vector<Range>& ranges) {
        u64 total = 0;
        for (const Range& range : ranges) {
            total += range.size;
        }
        return total;
    }

    // Runs inside a valid range whose block state matches; an invalid range gives no runs.
    std::vector<Range> CollectRuns(u64 address, u64 size, MemoryState state) const {
        std::vector<Range> runs;
        const u64 end = address + size;
        if (address % PageSize != 0 || size == 0 || end < address ||
            !space_.GetLayout().address_space.Contains(address, size)) {
            return runs;
        }
        for (u64 cursor = address; cursor < end;) {
            const MemoryInfo info = space_.QueryInfo(cursor);
            const u64 piece_end = std::min(info.base + info.size, end);
            if (info.state == state) {
                runs.push_back({cursor, piece_end - cursor});
            }
            cursor = piece_end;
        }
        return runs;
    }

    bool AllocateFor(u64 size, std::vector<PhysicalExtent>& out) {
        out.clear();
        if (!source_.Allocate(size, out)) {
            return false;
        }
        if (TotalSize(out) != size) {
            FreeAll(out);
            return false;
        }
        return true;
    }

    void FreeAll(std::vector<PhysicalExtent>& extents) {
        if (!extents.empty()) {
            source_.Free(extents);
            extents.clear();
        }
    }

    bool CollectBacking(u64 address, u64 size, std::vector<PhysicalExtent>& out) const {
        return backing_.Visit(address, size, [&](u64, u64 length, const Detail::Backing& backing) {
            out.push_back({backing.phys, length});
        });
    }

    void MapExtents(u64 address, const std::vector<PhysicalExtent>& extents, u8 permission) {
        for (const PhysicalExtent& extent : extents) {
            host_.Map(address, extent.size, extent.phys, permission);
            backing_.Assign(address, extent.size, Detail::Backing{extent.phys});
            address += extent.size;
        }
    }

    // Alias sources keep their pages but leave the host table while the alias exists.
    void SetHostMapped(u64 address, u64 size, bool host_mapped) {
        std::vector<std::pair<Range, Detail::Backing>> pieces;
        backing_.Visit(address, size, [&](u64 piece, u64 length, const Detail::Backing& backing) {
            pieces.push_back({{piece, length}, backing});
        });
        for (auto& [range, backing] : pieces) {
            backing.host_mapped = host_mapped;
            backing_.Assign(range.start, range.size, backing);
        }
    }

    void MapOwnedFromCursor(u64 address, u64 size, Detail::ExtentCursor& cursor,
                            u8 permission = PermUserReadWrite) {
        for (u64 mapped = 0; mapped < size;) {
            const PhysicalExtent piece = cursor.Take(size - mapped);
            if (piece.size == 0) {
                ++inconsistency_count_;
                return;
            }
            host_.Map(address + mapped, piece.size, piece.phys, permission);
            backing_.Assign(address + mapped, piece.size, Detail::Backing{piece.phys, true, true});
            mapped += piece.size;
        }
    }

    // Unmaps the range on the host and returns its pages to the source; only anonymous memory comes here.
    void ReleaseBacking(u64 address, u64 size) {
        std::vector<PhysicalExtent> pages;
        std::vector<Range> mapped;
        const bool covered = backing_.Visit(address, size, [&](u64 piece, u64 length,
                                                              const Detail::Backing& backing) {
            if (backing.owned) {
                pages.push_back({backing.phys, length});
            }
            if (backing.host_mapped) {
                mapped.push_back({piece, length});
            }
        });
        if (!covered) {
            ++inconsistency_count_;
        }
        for (const Range& run : mapped) {
            host_.Unmap(run.start, run.size);
        }
        backing_.Erase(address, size);
        FreeAll(pages);
    }

    Result ProtectOnSuccess(Result result, u64 address, u64 size, u8 permission) {
        if (result == Result::Success) {
            host_.Protect(address, size, permission);
        }
        return result;
    }

    Result UnmapExternal(Result result, u64 address, u64 size) {
        if (result == Result::Success) {
            host_.Unmap(address, size);
            backing_.Erase(address, size);
        }
        return result;
    }

    AddressSpace space_;
    PhysicalPageSource& source_;
    HostMemoryMap& host_;
    Detail::RunMap<Detail::Backing> backing_;
    u64 inconsistency_count_ = 0;
};

} // namespace Core::NativeMemory
