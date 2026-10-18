// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// What the bottom-left performance HUD derives from raw counters, kept free of ImGui and the OS
// so tests/recompiler_smoke/perf_hud_unit.cpp can check it.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace SuyuCmd {

/// Interval between HUD samples while it is visible.
inline constexpr std::uint32_t kPerfHudSampleIntervalMs = 1000;
/// Text opacity of the HUD (white at this alpha).
inline constexpr float kPerfHudTextAlpha = 0.70f;
/// HUD text size relative to the overlay's regular text size.
inline constexpr float kPerfHudTextScale = 1.5f;

/// A percentage that may be unavailable; unavailable is shown as N/A, never as 0%.
struct HudPercent {
    bool available{};
    double percent{};
};

inline HudPercent Unavailable() {
    return {};
}

inline HudPercent MakePercent(double percent) {
    return {true, std::clamp(percent, 0.0, 100.0)};
}

/// `used` of `total`, unavailable when `total` is 0.
inline HudPercent RatioPercent(std::uint64_t used, std::uint64_t total) {
    if (total == 0) {
        return Unavailable();
    }
    return MakePercent(100.0 * static_cast<double>(used) / static_cast<double>(total));
}

/// Process CPU use over one interval: CPU time spent divided by wall time times logical cores.
/// Counters are in the same unit (100 ns ticks on Windows).
inline HudPercent CpuPercent(std::uint64_t cpu_ticks_delta, std::uint64_t wall_ticks_delta,
                             std::uint32_t logical_cores) {
    if (wall_ticks_delta == 0 || logical_cores == 0) {
        return Unavailable();
    }
    return RatioPercent(cpu_ticks_delta, wall_ticks_delta * logical_cores);
}

/// CPU use between two counter reads; N/A unless both reads succeeded and time moved forward.
inline HudPercent CpuPercentBetween(std::optional<std::uint64_t> previous_cpu_ticks,
                                    std::optional<std::uint64_t> current_cpu_ticks,
                                    std::uint64_t wall_ticks_delta, std::uint32_t logical_cores) {
    if (!previous_cpu_ticks || !current_cpu_ticks || *current_cpu_ticks < *previous_cpu_ticks) {
        return Unavailable();
    }
    return CpuPercent(*current_cpu_ticks - *previous_cpu_ticks, wall_ticks_delta, logical_cores);
}

/// All five HUD values for one sample.
struct PerfHudSample {
    HudPercent cpu;
    HudPercent gpu;
    HudPercent gpu_memory;
    HudPercent guest_memory;
    HudPercent system_memory;
};

inline std::string FormatHudPercent(const HudPercent& value) {
    if (!value.available) {
        return "N/A";
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%.2f%%", value.percent);
    return text;
}

inline std::string FormatPerfHudLine(const PerfHudSample& sample) {
    return "CPU: " + FormatHudPercent(sample.cpu) + ", GPU: " + FormatHudPercent(sample.gpu) +
           ", GMem: " + FormatHudPercent(sample.gpu_memory) +
           ", MMem: " + FormatHudPercent(sample.guest_memory) +
           ", SysMem: " + FormatHudPercent(sample.system_memory);
}

/// The HUD's on/off state; starts off every launch. Only the event thread toggles it (single
/// writer); the UI thread reads.
class PerfHudVisibility {
public:
    bool Visible() const {
        return visible.load(std::memory_order_relaxed);
    }

    void Toggle() {
        visible.store(!Visible(), std::memory_order_relaxed);
    }

private:
    std::atomic<bool> visible{false};
};

} // namespace SuyuCmd
