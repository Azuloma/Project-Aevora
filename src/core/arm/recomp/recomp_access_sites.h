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

// SUYU_RECOMP_ACCESS_SITES=1 diagnostic: which host call stacks the host-callback accesses come from.
// A site is the first kFrames return addresses above the callback. Each thread owns a fixed hash
// table (single writer, no lock); the reporter sums every table.
class RecompAccessSites {
public:
    static constexpr std::size_t kFrames = 3;
    static constexpr std::size_t kTableBits = 13;
    static constexpr std::size_t kTableSize = std::size_t{1} << kTableBits;
    static constexpr std::size_t kMaxProbes = 16;
    static constexpr std::size_t kTopCount = 16;
    using Frames = std::array<std::uintptr_t, kFrames>;

    struct Slot {
        std::array<std::atomic<std::uint64_t>, kTableSize> keys{}; // frame hash | 1, 0 when empty
        std::array<std::atomic<std::uint64_t>, kTableSize> counts{};
        std::array<std::array<std::atomic<std::uintptr_t>, kFrames>, kTableSize> frames{};
        std::atomic<std::uint64_t> overflow{0}; // accesses whose probe run was full
    };

    // Slots are never freed: a count must survive the thread that made it.
    Slot& Acquire() {
        auto* slot = new Slot{};
        std::scoped_lock lock{registry_lock_};
        slots_.push_back(slot);
        return *slot;
    }

    static std::uint64_t KeyOf(const Frames& frames) {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (const std::uintptr_t frame : frames) {
            hash = (hash ^ static_cast<std::uint64_t>(frame)) * 0x100000001B3ULL;
            hash ^= hash >> 29;
        }
        return hash | 1;
    }

    // Only the owning thread may call this.
    static void Count(Slot& slot, const Frames& frames) {
        const std::uint64_t key = KeyOf(frames);
        std::size_t index = static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> (64 - kTableBits));
        for (std::size_t probe = 0; probe < kMaxProbes; ++probe, index = (index + 1) & (kTableSize - 1)) {
            const std::uint64_t held = slot.keys[index].load(std::memory_order_relaxed);
            if (held == 0) {
                for (std::size_t f = 0; f < kFrames; ++f) {
                    slot.frames[index][f].store(frames[f], std::memory_order_relaxed);
                }
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
        slot.overflow.store(slot.overflow.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    // Starts a window: later TakeTop calls report only what is counted after this call.
    void Rebase() {
        TakeTop([](std::uintptr_t) { return std::string{}; });
    }

    // One line with the busiest sites since the previous call; describe turns an address into text.
    std::string TakeTop(const std::function<std::string(std::uintptr_t)>& describe) {
        std::scoped_lock lock{report_lock_};
        struct Total {
            std::uint64_t count = 0;
            Frames frames{};
        };
        std::map<std::uint64_t, Total> totals;
        std::uint64_t overflow_total = 0;
        {
            std::scoped_lock registry{registry_lock_};
            for (const Slot* slot : slots_) {
                for (std::size_t i = 0; i < kTableSize; ++i) {
                    const std::uint64_t key = slot->keys[i].load(std::memory_order_acquire);
                    if (key == 0) {
                        continue;
                    }
                    Total& total = totals[key];
                    total.count += slot->counts[i].load(std::memory_order_relaxed);
                    for (std::size_t f = 0; f < kFrames; ++f) {
                        total.frames[f] = slot->frames[i][f].load(std::memory_order_relaxed);
                    }
                }
                overflow_total += slot->overflow.load(std::memory_order_relaxed);
            }
        }
        std::vector<std::pair<std::uint64_t, std::uint64_t>> deltas; // count, key
        std::uint64_t window_total = 0;
        for (const auto& [key, total] : totals) {
            const auto before = previous_.find(key);
            const std::uint64_t delta = total.count - (before == previous_.end() ? 0 : before->second);
            window_total += delta;
            if (delta != 0) {
                deltas.emplace_back(delta, key);
            }
        }
        std::sort(deltas.begin(), deltas.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        std::string line = "access-sites distinct=" + std::to_string(deltas.size()) +
                           " counted=" + std::to_string(window_total) +
                           " overflow=" + std::to_string(overflow_total - previous_overflow_);
        for (std::size_t n = 0; n < deltas.size() && n < kTopCount; ++n) {
            line += " | n=" + std::to_string(deltas[n].first);
            for (const std::uintptr_t frame : totals[deltas[n].second].frames) {
                line += " " + describe(frame);
            }
        }
        previous_.clear();
        for (const auto& [key, total] : totals) {
            previous_[key] = total.count;
        }
        previous_overflow_ = overflow_total;
        return line;
    }

private:
    std::mutex registry_lock_;
    std::vector<Slot*> slots_;
    std::mutex report_lock_;
    std::map<std::uint64_t, std::uint64_t> previous_;
    std::uint64_t previous_overflow_ = 0;
};

} // namespace Core
