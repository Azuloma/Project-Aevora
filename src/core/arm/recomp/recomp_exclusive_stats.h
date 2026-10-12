// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

#include "core/arm/recomp/recomp_sharded_counters.h"

namespace Core {

// SUYU_RECOMP_EXCL_STATS=1 diagnostic: guest exclusive load/store callbacks and the time spent in
// them, lock waits included, per reporting window.
class RecompExclusiveStats {
public:
    enum Counter : std::size_t {
        Loads,
        Stores,
        StoresFailed,
        LoadTicks,
        StoreTicks,
        kCounterCount,
    };
    using Counters = RecompShardedCounters<kCounterCount>;

    static std::uint64_t ReadTicks() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        return __rdtsc();
#elif defined(__x86_64__) || defined(__i386__)
        return __builtin_ia32_rdtsc();
#else
        return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
    }

    Counters::Slot& Acquire() {
        return counters_.Acquire();
    }

    static void CountLoad(Counters::Slot& slot, std::uint64_t ticks) {
        Counters::Increment(slot, Loads);
        Counters::Add(slot, LoadTicks, ticks);
    }

    static void CountStore(Counters::Slot& slot, bool succeeded, std::uint64_t ticks) {
        Counters::Increment(slot, Stores);
        if (!succeeded) {
            Counters::Increment(slot, StoresFailed);
        }
        Counters::Add(slot, StoreTicks, ticks);
    }

    // One line once window_ns has passed since the previous report; the first call opens the window.
    // The tick rate is measured from the window itself, so no TSC frequency is assumed.
    std::optional<std::string> TakeReport(std::uint64_t now_ns, std::uint64_t now_ticks,
                                          std::uint64_t window_ns) {
        std::scoped_lock lock{report_lock_};
        if (!window_started_) {
            window_started_ = true;
            Rebase(now_ns, now_ticks);
            return std::nullopt;
        }
        if (now_ns < window_start_ns_ || now_ns - window_start_ns_ < window_ns ||
            now_ticks <= window_start_ticks_) {
            return std::nullopt;
        }
        const std::uint64_t elapsed_ns = now_ns - window_start_ns_;
        const double ticks_per_ns =
            static_cast<double>(now_ticks - window_start_ticks_) / static_cast<double>(elapsed_ns);
        std::uint64_t delta[kCounterCount];
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            const std::uint64_t total = counters_.Sum(i);
            delta[i] = total - reported_[i];
            reported_[i] = total;
        }
        const auto to_ns = [ticks_per_ns](std::uint64_t ticks) {
            return static_cast<std::uint64_t>(static_cast<double>(ticks) / ticks_per_ns);
        };
        const std::uint64_t load_ns = to_ns(delta[LoadTicks]);
        const std::uint64_t store_ns = to_ns(delta[StoreTicks]);
        std::string line = "excl-stats window_ms=" + std::to_string(elapsed_ns / 1000000) +
                           " loads=" + std::to_string(delta[Loads]) +
                           " stores=" + std::to_string(delta[Stores]) +
                           " stores_failed=" + std::to_string(delta[StoresFailed]) +
                           " load_ms=" + std::to_string(load_ns / 1000000) +
                           " store_ms=" + std::to_string(store_ns / 1000000) +
                           " load_avg_ns=" + std::to_string(Average(load_ns, delta[Loads])) +
                           " store_avg_ns=" + std::to_string(Average(store_ns, delta[Stores]));
        Rebase(now_ns, now_ticks);
        return line;
    }

private:
    static std::uint64_t Average(std::uint64_t total_ns, std::uint64_t count) {
        return count == 0 ? 0 : total_ns / count;
    }

    void Rebase(std::uint64_t now_ns, std::uint64_t now_ticks) {
        window_start_ns_ = now_ns;
        window_start_ticks_ = now_ticks;
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            reported_[i] = counters_.Sum(i);
        }
    }

    Counters counters_;
    std::mutex report_lock_;
    bool window_started_ = false;
    std::uint64_t window_start_ns_ = 0;
    std::uint64_t window_start_ticks_ = 0;
    std::uint64_t reported_[kCounterCount] = {};
};

} // namespace Core
