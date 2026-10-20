// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// OS-free planning and statistics for the HostTiming thread's waits (SUYU_HOST_TIMING_BLOCK and
// SUYU_HOST_TIMING_STATS). Long waits sleep in the OS; only the last margin uses umwait.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core::Timing {

constexpr std::int64_t kHostTimingMarginResolutions = 2;

struct HostTimingWaitPlan {
    std::chrono::nanoseconds os_block_ns{0};
    std::chrono::nanoseconds fine_wait_ns{0};
};

/// Splits a wait into an OS-blocked part and a fine-grained tail; the tail is two timer
/// resolutions. An unknown resolution (0) or a short wait keeps the whole wait fine-grained.
constexpr HostTimingWaitPlan PlanHostTimingWait(std::chrono::nanoseconds wait_ns,
                                                std::chrono::nanoseconds timer_resolution_ns,
                                                bool block_enabled) {
    const auto margin_ns = timer_resolution_ns * kHostTimingMarginResolutions;
    if (!block_enabled || timer_resolution_ns.count() <= 0 || wait_ns <= margin_ns) {
        return {std::chrono::nanoseconds{0}, wait_ns};
    }
    return {wait_ns - margin_ns, margin_ns};
}

/// Single-writer statistics: only the HostTiming thread calls the members.
class HostTimingWaitStats {
public:
    static constexpr std::size_t kLengthBuckets = 8;
    static constexpr std::size_t kLatenessBuckets = 10;
    static constexpr std::array<std::int64_t, kLengthBuckets - 1> kLengthEdgesUs{
        100, 500, 1000, 2000, 5000, 10000, 20000};
    static constexpr std::array<std::int64_t, kLatenessBuckets - 1> kLatenessEdgesUs{
        10, 20, 50, 100, 200, 500, 1000, 2000, 5000};

    /// One finished wait: planned length, whether part of it blocked in the OS, whether the
    /// event signalled early, and (for a timeout) how late the thread woke.
    void Record(std::chrono::nanoseconds wait_ns, bool os_blocked, bool signalled,
                std::chrono::nanoseconds lateness_ns) {
        ++waits_;
        ++length_[Bucket(kLengthEdgesUs, wait_ns)];
        if (os_blocked) {
            ++os_blocks_;
        }
        if (signalled) {
            ++signalled_;
            return;
        }
        const std::int64_t late_ns = lateness_ns.count() < 0 ? 0 : lateness_ns.count();
        ++timeouts_;
        ++lateness_[Bucket(kLatenessEdgesUs, std::chrono::nanoseconds{late_ns})];
        if (late_ns > max_lateness_ns_) {
            max_lateness_ns_ = late_ns;
        }
    }

    [[nodiscard]] std::uint64_t Waits() const {
        return waits_;
    }

    /// Upper edge of the bucket holding the given lateness percentile, in microseconds; the
    /// open top bucket reports the observed maximum.
    [[nodiscard]] std::int64_t LatenessPercentileUs(int percent) const {
        if (timeouts_ == 0) {
            return 0;
        }
        const std::uint64_t rank = (timeouts_ * static_cast<std::uint64_t>(percent) + 99) / 100;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < kLatenessBuckets; ++i) {
            seen += lateness_[i];
            if (seen >= rank) {
                return i < kLatenessEdgesUs.size() ? kLatenessEdgesUs[i] : max_lateness_ns_ / 1000;
            }
        }
        return max_lateness_ns_ / 1000;
    }

    /// Formats and clears the window; "" when no wait was recorded.
    std::string TakeReport() {
        if (waits_ == 0) {
            return {};
        }
        std::string line = "host-timing waits=" + std::to_string(waits_) +
                           " os_blocked=" + std::to_string(os_blocks_) +
                           " signalled=" + std::to_string(signalled_) +
                           " late_p50_us<=" + std::to_string(LatenessPercentileUs(50)) +
                           " late_p99_us<=" + std::to_string(LatenessPercentileUs(99)) +
                           " late_max_us=" + std::to_string(max_lateness_ns_ / 1000) + " len_us[";
        for (std::size_t i = 0; i < kLengthBuckets; ++i) {
            line += (i == 0 ? "" : " ") + std::to_string(length_[i]);
        }
        line += "]";
        *this = HostTimingWaitStats{};
        return line;
    }

private:
    template <std::size_t N>
    static std::size_t Bucket(const std::array<std::int64_t, N>& edges_us,
                              std::chrono::nanoseconds value_ns) {
        const std::int64_t value_us = value_ns.count() / 1000;
        for (std::size_t i = 0; i < N; ++i) {
            if (value_us < edges_us[i]) {
                return i;
            }
        }
        return N;
    }

    std::uint64_t waits_{};
    std::uint64_t os_blocks_{};
    std::uint64_t signalled_{};
    std::uint64_t timeouts_{};
    std::int64_t max_lateness_ns_{};
    std::array<std::uint64_t, kLengthBuckets> length_{};
    std::array<std::uint64_t, kLatenessBuckets> lateness_{};
};

/// Counts callbacks per event name and their run time; the caller serialises access.
class HostTimingEventStats {
public:
    void Record(const std::string& name, std::chrono::nanoseconds callback_ns) {
        Entry& entry = entries_[name];
        ++entry.count;
        entry.total_ns += callback_ns.count();
    }

    /// Formats the busiest names by count (ties by name) and clears the window; "" if empty.
    std::string TakeReport(std::size_t top_count) {
        if (entries_.empty()) {
            return {};
        }
        std::vector<std::pair<std::string, Entry>> sorted(entries_.begin(), entries_.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
            return left.second.count != right.second.count ? left.second.count > right.second.count
                                                           : left.first < right.first;
        });
        std::string line = "host-timing events";
        std::uint64_t other = 0;
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            const Entry& entry = sorted[i].second;
            if (i >= top_count) {
                other += entry.count;
                continue;
            }
            const std::int64_t avg_ns = entry.total_ns / static_cast<std::int64_t>(entry.count);
            line += " " + sorted[i].first + "=" + std::to_string(entry.count) + "/" +
                    std::to_string(avg_ns / 1000) + "us";
        }
        if (other > 0) {
            line += " other=" + std::to_string(other);
        }
        entries_.clear();
        return line;
    }

private:
    struct Entry {
        std::uint64_t count{};
        std::int64_t total_ns{};
    };
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace Core::Timing
