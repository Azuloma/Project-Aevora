// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Core {

// SUYU_RECOMP_LOOKUP_TARGETS=1 diagnostic: which guest pcs the dispatcher exits land on. Each thread
// owns a fixed hash table (single writer, no lock); the reporter sums every table.
class RecompExitTargets {
public:
    static constexpr std::size_t kTableBits = 13;
    static constexpr std::size_t kTableSize = std::size_t{1} << kTableBits;
    static constexpr std::size_t kMaxProbes = 16;
    static constexpr std::size_t kTopCount = 16;

    struct Slot {
        std::array<std::atomic<std::uint64_t>, kTableSize> keys{}; // pc + 1, 0 when empty
        std::array<std::atomic<std::uint64_t>, kTableSize> counts{};
        std::atomic<std::uint64_t> overflow{0}; // exits whose probe run was full
    };

    // Slots are never freed: a count must survive the thread that made it.
    Slot& Acquire() {
        auto* slot = new Slot{};
        std::scoped_lock lock{registry_lock_};
        slots_.push_back(slot);
        return *slot;
    }

    // Only the owning thread may call this. UINT64_MAX would wrap the key to the empty marker.
    static void Count(Slot& slot, std::uint64_t pc) {
        if (pc == UINT64_MAX) {
            slot.overflow.store(slot.overflow.load(std::memory_order_relaxed) + 1,
                                std::memory_order_relaxed);
            return;
        }
        const std::uint64_t key = pc + 1;
        std::size_t index = Hash(pc);
        for (std::size_t probe = 0; probe < kMaxProbes; ++probe, index = (index + 1) & (kTableSize - 1)) {
            const std::uint64_t held = slot.keys[index].load(std::memory_order_relaxed);
            if (held == 0) {
                slot.counts[index].store(1, std::memory_order_relaxed);
                slot.keys[index].store(key, std::memory_order_release);
                return;
            }
            if (held == key) {
                slot.counts[index].store(slot.counts[index].load(std::memory_order_relaxed) + 1,
                                         std::memory_order_relaxed);
                return;
            }
        }
        slot.overflow.store(slot.overflow.load(std::memory_order_relaxed) + 1,
                            std::memory_order_relaxed);
    }

    // Starts a window: later TakeTop calls report only what is counted after this call.
    void Rebase() {
        TakeTop([](std::uint64_t) { return std::string{}; });
    }

    // One line with the busiest targets since the previous call; describe turns a pc into text.
    std::string TakeTop(const std::function<std::string(std::uint64_t)>& describe) {
        std::scoped_lock lock{report_lock_};
        std::map<std::uint64_t, std::uint64_t> totals;
        std::uint64_t overflow_total = 0;
        {
            std::scoped_lock registry{registry_lock_};
            for (const Slot* slot : slots_) {
                for (std::size_t i = 0; i < kTableSize; ++i) {
                    const std::uint64_t key = slot->keys[i].load(std::memory_order_acquire);
                    if (key != 0) {
                        totals[key - 1] += slot->counts[i].load(std::memory_order_relaxed);
                    }
                }
                overflow_total += slot->overflow.load(std::memory_order_relaxed);
            }
        }
        std::vector<std::pair<std::uint64_t, std::uint64_t>> deltas; // count, pc
        std::uint64_t window_total = 0;
        for (const auto& [pc, total] : totals) {
            const auto before = previous_.find(pc);
            const std::uint64_t delta = total - (before == previous_.end() ? 0 : before->second);
            window_total += delta;
            if (delta != 0) {
                deltas.emplace_back(delta, pc);
            }
        }
        std::sort(deltas.begin(), deltas.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        std::string line = "lookup-targets distinct=" + std::to_string(deltas.size()) +
                           " counted=" + std::to_string(window_total) +
                           " overflow=" + std::to_string(overflow_total - previous_overflow_);
        for (std::size_t n = 0; n < deltas.size() && n < kTopCount; ++n) {
            line += " " + describe(deltas[n].second) + "=" + std::to_string(deltas[n].first);
        }
        previous_ = std::move(totals);
        previous_overflow_ = overflow_total;
        return line;
    }

private:
    static std::size_t Hash(std::uint64_t pc) {
        return static_cast<std::size_t>(((pc >> 2) * 0x9E3779B97F4A7C15ULL) >> (64 - kTableBits));
    }

    std::mutex registry_lock_;
    std::vector<Slot*> slots_;
    std::mutex report_lock_;
    std::map<std::uint64_t, std::uint64_t> previous_;
    std::uint64_t previous_overflow_ = 0;
};

} // namespace Core
