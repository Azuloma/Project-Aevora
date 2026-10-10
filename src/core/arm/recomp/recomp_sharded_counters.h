// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Core {

// Counters bumped on the dispatch hot path. Each thread owns a cache-line-aligned slot, so an
// increment is a plain load+store with no shared line and no lock; reads sum every slot.
template <std::size_t kCounterCount>
class RecompShardedCounters {
public:
    struct alignas(64) Slot {
        std::array<std::atomic<std::uint64_t>, kCounterCount> value{};
    };

    // Slots are never freed: a counter must survive the thread that incremented it.
    Slot& Acquire() {
        auto* slot = new Slot{};
        std::scoped_lock lock{registry_lock_};
        slots_.push_back(slot);
        return *slot;
    }

    // Only the owning thread may call this; the store is not an atomic read-modify-write.
    static void Increment(Slot& slot, std::size_t counter) {
        auto& cell = slot.value[counter];
        cell.store(cell.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    std::uint64_t Sum(std::size_t counter) const {
        std::scoped_lock lock{registry_lock_};
        std::uint64_t sum = 0;
        for (const Slot* slot : slots_) {
            sum += slot->value[counter].load(std::memory_order_relaxed);
        }
        return sum;
    }

private:
    mutable std::mutex registry_lock_;
    std::vector<Slot*> slots_;
};

} // namespace Core
