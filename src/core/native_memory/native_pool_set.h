// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "core/native_memory/native_physical_pool.h"

// Kernel-free set of physical regions grouped into pools (N2 stage 4a-2), the native counterpart of
// KMemoryManager's managers. Regions are added in order and never change; pool ids are caller-defined.
namespace Core::NativeMemory {

class PhysicalPoolSet {
public:
    // Fails for an invalid region or one that overlaps an existing region.
    bool AddRegion(u32 pool_id, u64 offset_bytes, u64 size_bytes) {
        auto pages = std::make_unique<PhysicalPagePool>(offset_bytes, size_bytes);
        if (!pages->IsValid() || size_bytes == 0) {
            return false;
        }
        for (const Region& region : regions_) {
            if (offset_bytes < region.end_bytes && region.offset_bytes < offset_bytes + size_bytes) {
                return false;
            }
        }
        regions_.push_back({pool_id, offset_bytes, offset_bytes + size_bytes, std::move(pages)});
        return true;
    }

    size_t RegionCount() const {
        return regions_.size();
    }

    // Reserves a range that lies inside one region.
    bool Reserve(u64 offset_bytes, u64 size_bytes) {
        Region* region = FindRegion(offset_bytes);
        return region != nullptr && size_bytes <= region->end_bytes - offset_bytes &&
               region->pages->Reserve(offset_bytes, size_bytes);
    }

    // Takes the first reference on every page of the pool, trying regions front to back or back to front.
    // All or nothing; allocations are serialized so a stale free count cannot fail a fitting request.
    bool AllocateAndOpen(u32 pool_id, u64 size_bytes, PoolDirection direction,
                         std::vector<PhysicalExtent>& out) {
        out.clear();
        if (size_bytes == 0 || size_bytes % PageSize != 0) {
            return false;
        }
        std::lock_guard lock(allocation_mutex_);
        u64 remaining_bytes = size_bytes;
        ForEachRegionOfPool(pool_id, direction, [&](Region& region) {
            const u64 take_bytes = std::min(remaining_bytes, region.pages->FreeBytes());
            std::vector<PhysicalExtent> taken;
            if (take_bytes != 0 && region.pages->AllocateAndOpen(take_bytes, direction, taken)) {
                out.insert(out.end(), taken.begin(), taken.end());
                remaining_bytes -= take_bytes;
            }
            return remaining_bytes == 0;
        });
        if (remaining_bytes != 0) {
            static_cast<void>(Close(out));
            out.clear();
            return false;
        }
        return true;
    }

    bool AllocateAndOpenContinuous(u32 pool_id, u64 size_bytes, u64 align_pages,
                                   PoolDirection direction, PhysicalExtent& out) {
        out = {};
        std::lock_guard lock(allocation_mutex_);
        return ForEachRegionOfPool(pool_id, direction, [&](Region& region) {
            return region.pages->AllocateAndOpenContinuous(size_bytes, align_pages, direction, out);
        });
    }

    // Both calls split the range at region boundaries. The range must be covered by adjacent regions;
    // a refusal part way (a reference-count bug in the caller) leaves earlier pieces applied.
    [[nodiscard]] bool Open(u64 offset_bytes, u64 size_bytes) {
        return ForEachPiece(offset_bytes, size_bytes, [](PhysicalPagePool& pages, u64 piece_offset,
                                                         u64 piece_size) {
            return pages.Open(piece_offset, piece_size);
        });
    }

    [[nodiscard]] bool Close(u64 offset_bytes, u64 size_bytes) {
        return ForEachPiece(offset_bytes, size_bytes, [](PhysicalPagePool& pages, u64 piece_offset,
                                                         u64 piece_size) {
            return pages.Close(piece_offset, piece_size);
        });
    }

    // Continues past a bad extent and reports false if any extent was refused.
    [[nodiscard]] bool Close(const std::vector<PhysicalExtent>& extents) {
        bool all_closed = true;
        for (const PhysicalExtent& extent : extents) {
            all_closed = Close(extent.phys, extent.size) && all_closed;
        }
        return all_closed;
    }

    bool FindPool(u64 offset_bytes, u32& pool_id) {
        const Region* region = FindRegion(offset_bytes);
        if (region == nullptr) {
            return false;
        }
        pool_id = region->pool_id;
        return true;
    }

    u64 TotalBytes() const {
        u64 total_bytes = 0;
        for (const Region& region : regions_) {
            total_bytes += region.pages->TotalBytes();
        }
        return total_bytes;
    }

    u64 TotalBytes(u32 pool_id) const {
        u64 total_bytes = 0;
        for (const Region& region : regions_) {
            total_bytes += region.pool_id == pool_id ? region.pages->TotalBytes() : 0;
        }
        return total_bytes;
    }

    u64 FreeBytes() const {
        u64 free_bytes = 0;
        for (const Region& region : regions_) {
            free_bytes += region.pages->FreeBytes();
        }
        return free_bytes;
    }

    u64 FreeBytes(u32 pool_id) const {
        u64 free_bytes = 0;
        for (const Region& region : regions_) {
            free_bytes += region.pool_id == pool_id ? region.pages->FreeBytes() : 0;
        }
        return free_bytes;
    }

private:
    struct Region {
        u32 pool_id;
        u64 offset_bytes;
        u64 end_bytes;
        std::unique_ptr<PhysicalPagePool> pages;
    };

    Region* FindRegion(u64 offset_bytes) {
        for (Region& region : regions_) {
            if (offset_bytes >= region.offset_bytes && offset_bytes < region.end_bytes) {
                return &region;
            }
        }
        return nullptr;
    }

    // Stops at the first region for which visit returns true; reports whether one did.
    template <typename Visit>
    bool ForEachRegionOfPool(u32 pool_id, PoolDirection direction, Visit&& visit) {
        const size_t count = regions_.size();
        for (size_t step = 0; step < count; ++step) {
            Region& region =
                regions_[direction == PoolDirection::FromFront ? step : count - 1 - step];
            if (region.pool_id == pool_id && visit(region)) {
                return true;
            }
        }
        return false;
    }

    template <typename Apply>
    bool ForEachPiece(u64 offset_bytes, u64 size_bytes, Apply&& apply) {
        if (size_bytes == 0 || size_bytes > ~u64{0} - offset_bytes || !IsCovered(offset_bytes, size_bytes)) {
            return false;
        }
        u64 cursor_bytes = offset_bytes;
        const u64 end_bytes = offset_bytes + size_bytes;
        while (cursor_bytes < end_bytes) {
            Region* region = FindRegion(cursor_bytes);
            if (region == nullptr) {
                return false;
            }
            const u64 piece_end_bytes = std::min(end_bytes, region->end_bytes);
            if (!apply(*region->pages, cursor_bytes, piece_end_bytes - cursor_bytes)) {
                return false;
            }
            cursor_bytes = piece_end_bytes;
        }
        return true;
    }

    bool IsCovered(u64 offset_bytes, u64 size_bytes) {
        u64 cursor_bytes = offset_bytes;
        const u64 end_bytes = offset_bytes + size_bytes;
        while (cursor_bytes < end_bytes) {
            const Region* region = FindRegion(cursor_bytes);
            if (region == nullptr) {
                return false;
            }
            cursor_bytes = region->end_bytes;
        }
        return true;
    }

    std::mutex allocation_mutex_;
    std::vector<Region> regions_;
};

} // namespace Core::NativeMemory
