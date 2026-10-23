// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <iterator>
#include <map>
#include <mutex>
#include <vector>

#include "core/native_memory/native_process_memory.h"

// Standalone reference-counted physical page pool replacing KMemoryManager (N2 stage 4a), unconnected.
// Offsets are byte offsets; operations are self-locking and never call out under the lock.
namespace Core::NativeMemory {

enum class PoolDirection { FromFront, FromBack };

class PhysicalPagePool final : public PhysicalPageSource {
public:
    // An unaligned or wrapping region gives an empty pool with IsValid() false, so every call fails.
    PhysicalPagePool(u64 region_offset, u64 region_size_bytes)
        : valid_(IsValidRegion(region_offset, region_size_bytes)), region_offset_(region_offset),
          page_count_(valid_ ? region_size_bytes / PageSize : 0), ref_counts_(page_count_, 0),
          reserved_(page_count_, false), free_pages_(page_count_) {
        if (page_count_ != 0) {
            free_runs_.emplace(0, page_count_);
        }
    }

    bool IsValid() const {
        return valid_;
    }

    // Marks a fixed range as used for good; Open and Close reject it and Allocate never returns it.
    bool Reserve(u64 offset, u64 size) {
        std::lock_guard lock(mutex_);
        u64 first = 0;
        u64 pages = 0;
        if (!ToPageRange(offset, size, first, pages)) {
            return false;
        }
        for (u64 page = first; page < first + pages; ++page) {
            if (ref_counts_[page] != 0) {
                return false;
            }
        }
        RemoveFreeRange(first, pages);
        OpenFirstReference(first, pages);
        for (u64 page = first; page < first + pages; ++page) {
            reserved_[page] = true;
        }
        return true;
    }

    // Takes the first reference on every page; the result may be split over several extents.
    bool AllocateAndOpen(u64 size, PoolDirection direction, std::vector<PhysicalExtent>& out) {
        out.clear();
        if (size == 0 || size % PageSize != 0) {
            return false;
        }
        const u64 pages = size / PageSize;
        std::lock_guard lock(mutex_);
        if (pages > free_pages_) {
            return false;
        }
        u64 remaining = pages;
        while (remaining != 0) {
            const auto run = direction == PoolDirection::FromFront ? free_runs_.begin()
                                                                   : std::prev(free_runs_.end());
            const u64 take = std::min(remaining, run->second);
            const u64 first =
                direction == PoolDirection::FromFront ? run->first : run->first + run->second - take;
            RemoveFreeRange(first, take);
            OpenFirstReference(first, take);
            out.push_back({region_offset_ + first * PageSize, take * PageSize});
            remaining -= take;
        }
        return true;
    }

    // Allocates one run whose first page is aligned to align_pages (a power of two).
    bool AllocateAndOpenContinuous(u64 size, u64 align_pages, PoolDirection direction,
                                   PhysicalExtent& out) {
        out = {};
        if (size == 0 || size % PageSize != 0 || align_pages == 0 ||
            (align_pages & (align_pages - 1)) != 0) {
            return false;
        }
        const u64 pages = size / PageSize;
        std::lock_guard lock(mutex_);
        if (pages > free_pages_) {
            return false;
        }
        u64 first = 0;
        if (!FindAlignedRun(pages, align_pages, direction, first)) {
            return false;
        }
        RemoveFreeRange(first, pages);
        OpenFirstReference(first, pages);
        out = {region_offset_ + first * PageSize, size};
        return true;
    }

    // Adds a reference to pages that are already in use; nothing changes unless the whole range is valid.
    [[nodiscard]] bool Open(u64 offset, u64 size) {
        std::lock_guard lock(mutex_);
        u64 first = 0;
        u64 pages = 0;
        if (!ToPageRange(offset, size, first, pages)) {
            return false;
        }
        for (u64 page = first; page < first + pages; ++page) {
            if (ref_counts_[page] == 0 || ref_counts_[page] == MaxRefCount || reserved_[page]) {
                return false;
            }
        }
        for (u64 page = first; page < first + pages; ++page) {
            ++ref_counts_[page];
        }
        return true;
    }

    // Drops one reference per page; a page whose count reaches zero returns to the free runs.
    [[nodiscard]] bool Close(u64 offset, u64 size) {
        std::lock_guard lock(mutex_);
        u64 first = 0;
        u64 pages = 0;
        if (!ToPageRange(offset, size, first, pages)) {
            return false;
        }
        for (u64 page = first; page < first + pages; ++page) {
            if (ref_counts_[page] == 0 || reserved_[page]) {
                return false;
            }
        }
        u64 span_start = 0;
        u64 span_pages = 0;
        for (u64 page = first; page < first + pages; ++page) {
            if (--ref_counts_[page] != 0) {
                continue;
            }
            if (span_pages != 0 && span_start + span_pages == page) {
                ++span_pages;
            } else {
                AddFreeRange(span_start, span_pages);
                span_start = page;
                span_pages = 1;
            }
        }
        AddFreeRange(span_start, span_pages);
        return true;
    }

    [[nodiscard]] bool Close(const std::vector<PhysicalExtent>& extents) {
        bool all_closed = true;
        for (const PhysicalExtent& extent : extents) {
            all_closed = Close(extent.phys, extent.size) && all_closed;
        }
        return all_closed;
    }

    bool Allocate(u64 size, std::vector<PhysicalExtent>& out) override {
        return AllocateAndOpen(size, PoolDirection::FromFront, out);
    }

    void Free(const std::vector<PhysicalExtent>& extents) override {
        static_cast<void>(Close(extents));
    }

    u64 TotalBytes() const {
        return page_count_ * PageSize;
    }

    u64 FreeBytes() const {
        std::lock_guard lock(mutex_);
        return free_pages_ * PageSize;
    }

    u32 ReferenceCount(u64 offset) const {
        std::lock_guard lock(mutex_);
        u64 first = 0;
        u64 pages = 0;
        return ToPageRange(offset, PageSize, first, pages) ? ref_counts_[first] : 0;
    }

    // Number of separate free runs; one run per contiguous free range.
    size_t FreeRunCount() const {
        std::lock_guard lock(mutex_);
        return free_runs_.size();
    }

private:
    static constexpr u32 MaxRefCount = ~u32{0};

    static bool IsValidRegion(u64 region_offset, u64 region_size_bytes) {
        return region_offset % PageSize == 0 && region_size_bytes % PageSize == 0 &&
               region_size_bytes <= ~u64{0} - region_offset;
    }

    bool ToPageRange(u64 offset, u64 size, u64& first, u64& pages) const {
        if (offset < region_offset_ || (offset - region_offset_) % PageSize != 0 || size == 0 ||
            size % PageSize != 0) {
            return false;
        }
        first = (offset - region_offset_) / PageSize;
        pages = size / PageSize;
        return first < page_count_ && pages <= page_count_ - first;
    }

    void OpenFirstReference(u64 first, u64 pages) {
        for (u64 page = first; page < first + pages; ++page) {
            ref_counts_[page] = 1;
        }
        free_pages_ -= pages;
    }

    bool FindAlignedRun(u64 pages, u64 align_pages, PoolDirection direction, u64& first) const {
        // Alignment applies to the absolute page number, so it is computed in that space.
        const u64 base_page = region_offset_ / PageSize;
        const auto try_run = [&](const std::pair<const u64, u64>& run) {
            const u64 run_start = base_page + run.first;
            const u64 run_end = run_start + run.second;
            if (direction == PoolDirection::FromFront) {
                const u64 candidate = (run_start + align_pages - 1) & ~(align_pages - 1);
                if (candidate + pages > run_end) {
                    return false;
                }
                first = candidate - base_page;
                return true;
            }
            if (run.second < pages) {
                return false;
            }
            const u64 candidate = (run_end - pages) & ~(align_pages - 1);
            if (candidate < run_start) {
                return false;
            }
            first = candidate - base_page;
            return true;
        };
        if (direction == PoolDirection::FromFront) {
            return std::any_of(free_runs_.begin(), free_runs_.end(), try_run);
        }
        return std::any_of(free_runs_.rbegin(), free_runs_.rend(), try_run);
    }

    // Removes [first, first + pages), which must lie inside one free run.
    void RemoveFreeRange(u64 first, u64 pages) {
        auto run = std::prev(free_runs_.upper_bound(first));
        const u64 run_start = run->first;
        const u64 run_end = run->first + run->second;
        free_runs_.erase(run);
        if (run_start < first) {
            free_runs_.emplace(run_start, first - run_start);
        }
        if (first + pages < run_end) {
            free_runs_.emplace(first + pages, run_end - (first + pages));
        }
    }

    // Adds a range that is free in none of its pages and merges it with adjacent free runs.
    void AddFreeRange(u64 first, u64 pages) {
        if (pages == 0) {
            return;
        }
        free_pages_ += pages;
        u64 start = first;
        u64 length = pages;
        auto next = free_runs_.lower_bound(first);
        if (next != free_runs_.end() && first + pages == next->first) {
            length += next->second;
            next = free_runs_.erase(next);
        }
        if (next != free_runs_.begin()) {
            const auto previous = std::prev(next);
            if (previous->first + previous->second == first) {
                start = previous->first;
                length += previous->second;
                free_runs_.erase(previous);
            }
        }
        free_runs_.emplace(start, length);
    }

    mutable std::mutex mutex_;
    bool valid_;
    u64 region_offset_;
    u64 page_count_;
    std::vector<u32> ref_counts_;
    std::vector<bool> reserved_;
    std::map<u64, u64> free_runs_;
    u64 free_pages_;
};

} // namespace Core::NativeMemory
