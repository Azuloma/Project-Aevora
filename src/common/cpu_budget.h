// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Common {

// SUYU_CPU_BUDGET=1 diagnostic: CPU ms per game frame for the process and each thread, plus
// frame-interval percentiles. OS-free so the arithmetic is covered by smoke_cpu_budget_unit.

inline constexpr std::uint32_t kCpuBudgetDefaultWindowSec = 10;
inline constexpr std::uint32_t kCpuBudgetMaxWindowSec = 60;

/// SUYU_CPU_BUDGET_WINDOW_SEC: a decimal 1..kCpuBudgetMaxWindowSec, or nothing when invalid.
inline std::optional<std::uint32_t> ParseCpuBudgetWindowSec(std::string_view text) {
    if (text.empty() || text.size() > 4) {
        return std::nullopt;
    }
    std::uint32_t seconds = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        seconds = seconds * 10 + static_cast<std::uint32_t>(digit - '0');
    }
    if (seconds == 0 || seconds > kCpuBudgetMaxWindowSec) {
        return std::nullopt;
    }
    return seconds;
}

/// Game frames ended in one window and the frame-to-frame intervals between them.
struct FrameWindow {
    std::uint64_t frames = 0;
    std::vector<std::uint64_t> intervals_ns;
    std::uint64_t dropped_intervals = 0;
};

/// Thread-safe record of frame ends; the first frame ever has no interval.
class FrameIntervalRecorder {
public:
    // Covers a kCpuBudgetMaxWindowSec window up to about 546 FPS without a drop.
    static constexpr std::size_t kMaxIntervalsPerWindow = 32768;

    void RecordFrameEnd(std::uint64_t now_ns) {
        std::scoped_lock lock{mutex};
        ++window.frames;
        if (last_frame_end_ns && now_ns >= *last_frame_end_ns) {
            if (window.intervals_ns.size() < kMaxIntervalsPerWindow) {
                window.intervals_ns.push_back(now_ns - *last_frame_end_ns);
            } else {
                ++window.dropped_intervals;
            }
        }
        last_frame_end_ns = now_ns;
    }

    /// The window since the previous call; the next window keeps the last frame end.
    FrameWindow TakeWindow() {
        std::scoped_lock lock{mutex};
        FrameWindow taken = std::move(window);
        window = {};
        return taken;
    }

private:
    std::mutex mutex;
    FrameWindow window;
    std::optional<std::uint64_t> last_frame_end_ns;
};

/// Cumulative CPU time of one OS thread at one moment.
struct ThreadCpuSample {
    std::uint64_t thread_id = 0;
    std::uint64_t creation_ticks = 0; ///< OS creation time; with the id it names one thread.
    std::string name;
    std::uint64_t cpu_ns = 0;
};

/// The same thread in the previous snapshot, matched by id and creation time (ids are reused).
inline const ThreadCpuSample* FindSameThread(const std::vector<ThreadCpuSample>& previous,
                                             const ThreadCpuSample& current) {
    for (const ThreadCpuSample& old : previous) {
        if (old.thread_id == current.thread_id && old.creation_ticks == current.creation_ticks) {
            return &old;
        }
    }
    return nullptr;
}

/// CPU the thread used in the window; nothing when its baseline is unknown (a missed sample).
/// Only a thread created after the window started may count from zero.
inline std::optional<std::uint64_t> ThreadWindowCpuNs(const ThreadCpuSample* previous,
                                                      const ThreadCpuSample& current,
                                                      std::uint64_t window_start_ticks) {
    if (previous != nullptr) {
        if (current.cpu_ns < previous->cpu_ns) {
            return std::nullopt;
        }
        return current.cpu_ns - previous->cpu_ns;
    }
    if (current.creation_ticks >= window_start_ticks) {
        return current.cpu_ns;
    }
    return std::nullopt;
}

/// Nearest-rank percentile (percent in 1..100); 0 for no values.
inline std::uint64_t NearestRankPercentile(std::vector<std::uint64_t> values, unsigned percent) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t rank = (values.size() * std::clamp(percent, 1u, 100u) + 99) / 100;
    return values[rank - 1];
}

/// Milliseconds per frame, or nothing when no frame ended in the window.
inline std::optional<double> MsPerFrame(std::uint64_t cpu_ns, std::uint64_t frames) {
    if (frames == 0) {
        return std::nullopt;
    }
    return static_cast<double>(cpu_ns) / 1e6 / static_cast<double>(frames);
}

struct ThreadCpuUse {
    std::uint64_t thread_id = 0;
    std::string name;
    std::uint64_t cpu_ns = 0;
};

struct ThreadCpuUses {
    std::vector<ThreadCpuUse> uses; ///< Busiest first.
    std::size_t unknown_baseline_threads = 0;
};

/// Per-thread CPU used between two snapshots; unknown baselines are left to "untracked".
inline ThreadCpuUses CollectThreadCpuUses(const std::vector<ThreadCpuSample>& previous,
                                          const std::vector<ThreadCpuSample>& current,
                                          std::uint64_t window_start_ticks) {
    ThreadCpuUses result;
    result.uses.reserve(current.size());
    for (const ThreadCpuSample& now : current) {
        const std::optional<std::uint64_t> cpu_ns =
            ThreadWindowCpuNs(FindSameThread(previous, now), now, window_start_ticks);
        if (!cpu_ns) {
            ++result.unknown_baseline_threads;
            continue;
        }
        result.uses.push_back({now.thread_id, now.name, *cpu_ns});
    }
    std::stable_sort(result.uses.begin(), result.uses.end(),
                     [](const ThreadCpuUse& a, const ThreadCpuUse& b) { return a.cpu_ns > b.cpu_ns; });
    return result;
}

/// Everything one report line is computed from; ticks share the creation_ticks clock.
struct CpuBudgetInput {
    std::uint64_t wall_ns = 0;
    std::uint64_t window_start_ticks = 0;
    std::optional<std::uint64_t> process_cpu_ns;
    std::vector<ThreadCpuSample> previous_threads;
    std::vector<ThreadCpuSample> current_threads;
    FrameWindow frames;
};

/// Two decimals, or "N/A" when there is no value.
inline std::string FormatFixed2(const std::optional<double>& value) {
    if (!value) {
        return "N/A";
    }
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    text << *value;
    return text.str();
}

/// Frame-interval percentile in ms; nothing when no interval or when some were dropped.
inline std::optional<double> FrameIntervalPercentileMs(const FrameWindow& window, unsigned percent) {
    if (window.intervals_ns.empty() || window.dropped_intervals != 0) {
        return std::nullopt;
    }
    return static_cast<double>(NearestRankPercentile(window.intervals_ns, percent)) / 1e6;
}

/// One "cpu-budget ..." line: process, untracked (exited or unknown threads) and the top threads.
inline std::string FormatCpuBudgetReport(const CpuBudgetInput& input, std::size_t top_count) {
    const ThreadCpuUses collected =
        CollectThreadCpuUses(input.previous_threads, input.current_threads, input.window_start_ticks);
    const std::vector<ThreadCpuUse>& uses = collected.uses;
    std::uint64_t threads_cpu_ns = 0;
    for (const ThreadCpuUse& use : uses) {
        threads_cpu_ns += use.cpu_ns;
    }
    const std::uint64_t frames = input.frames.frames;
    const auto pct_of_core = [&](std::uint64_t cpu_ns) {
        return input.wall_ns ? std::optional<double>(100.0 * static_cast<double>(cpu_ns) /
                                                     static_cast<double>(input.wall_ns))
                             : std::nullopt;
    };

    std::ostringstream line;
    line << "cpu-budget window_ms=" << input.wall_ns / 1000000 << " frames=" << frames
         << " process_ms_per_frame="
         << FormatFixed2(input.process_cpu_ns ? MsPerFrame(*input.process_cpu_ns, frames) : std::nullopt)
         << " threads_ms_per_frame=" << FormatFixed2(MsPerFrame(threads_cpu_ns, frames))
         << " untracked_ms_per_frame="
         << FormatFixed2(input.process_cpu_ns
                         ? MsPerFrame(*input.process_cpu_ns > threads_cpu_ns
                                          ? *input.process_cpu_ns - threads_cpu_ns
                                          : 0,
                                      frames)
                         : std::nullopt)
         << " frame_p50_ms=" << FormatFixed2(FrameIntervalPercentileMs(input.frames, 50))
         << " frame_p99_ms=" << FormatFixed2(FrameIntervalPercentileMs(input.frames, 99))
         << " frame_max_ms=" << FormatFixed2(FrameIntervalPercentileMs(input.frames, 100))
         << " dropped_intervals=" << input.frames.dropped_intervals << " threads=" << uses.size()
         << " unknown_baseline_threads=" << collected.unknown_baseline_threads;
    for (std::size_t i = 0; i < uses.size() && i < top_count; ++i) {
        const ThreadCpuUse& use = uses[i];
        line << " | " << (use.name.empty() ? "unnamed" : use.name) << "#" << use.thread_id
             << " ms_per_frame=" << FormatFixed2(MsPerFrame(use.cpu_ns, frames))
             << " core_pct=" << FormatFixed2(pct_of_core(use.cpu_ns));
    }
    return line.str();
}

} // namespace Common
