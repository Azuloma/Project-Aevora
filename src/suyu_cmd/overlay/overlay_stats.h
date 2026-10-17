// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// What the Debug window derives from raw counters, kept free of ImGui and the emulator core so
// tests/recompiler_smoke/overlay_logic_unit.cpp can check it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "suyu_cmd/native_status.h"

namespace SuyuCmd {

/// Frames kept for the frame-time graph.
inline constexpr std::size_t kFrameTimeHistoryLength = 120;

/// The most recent frame times in a ring, laid out for ImGui::PlotLines (values plus the offset
/// of the oldest sample).
class FrameTimeHistory {
public:
    void Add(float frame_time_ms) {
        samples[next] = frame_time_ms;
        next = (next + 1) % kFrameTimeHistoryLength;
        count = std::min(count + 1, kFrameTimeHistoryLength);
    }

    const float* Values() const {
        return samples.data();
    }

    /// Index of the oldest sample once the ring has wrapped, 0 before that.
    int Offset() const {
        return count < kFrameTimeHistoryLength ? 0 : static_cast<int>(next);
    }

    /// Samples recorded so far, up to kFrameTimeHistoryLength.
    std::size_t Count() const {
        return count;
    }

    float Max() const {
        return count == 0 ? 0.0f : *std::max_element(samples.begin(), samples.begin() + count);
    }

    float Average() const {
        float sum = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            sum += samples[i];
        }
        return count == 0 ? 0.0f : sum / static_cast<float>(count);
    }

private:
    // Before the ring wraps, samples fill [0, count) in order, so those prefix reads are exact.
    std::array<float, kFrameTimeHistoryLength> samples{};
    std::size_t next{};
    std::size_t count{};
};

/// Longest frame gap fed to ImGui as its animation step; a stall must not become one huge step.
inline constexpr float kMaxAnimationDeltaSeconds = 0.25f;
/// Animation step used when no time has passed since the last frame (the very first one).
inline constexpr float kFirstFrameAnimationDeltaSeconds = 1.0f / 60.0f;

/// One frame's timing, split by purpose: `measured_ms` is the gap exactly as it happened, for the
/// graph and the rates derived from it; `animation_delta_seconds` is clamped, for ImGui only.
struct FrameTiming {
    bool measurable{};
    float measured_ms{};
    float animation_delta_seconds{};
};

inline FrameTiming TimeFrame(float delta_seconds) {
    if (!(delta_seconds > 0.0f)) {
        return {false, 0.0f, kFirstFrameAnimationDeltaSeconds};
    }
    return {true, delta_seconds * 1000.0f, std::min(delta_seconds, kMaxAnimationDeltaSeconds)};
}

/// A title id as 16 uppercase hex digits, the way the F12 panel and game folders write it.
inline std::string FormatTitleId(std::uint64_t title_id) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text(16, '0');
    for (std::size_t i = 0; i < text.size(); ++i) {
        text[text.size() - 1 - i] = kDigits[(title_id >> (4 * i)) & 0xF];
    }
    return text;
}

/// A guest address with the nearest module base below it (see ResolveGuestPc).
struct GuestLocation {
    bool found{};
    std::string module;
    std::uint64_t offset{};
};

/// Names the module with the highest base at or below `pc` (module sizes are unknown, so this
/// is not an in-module check). `modules` maps load address to name; pc 0 is never placed.
inline GuestLocation ResolveGuestPc(std::uint64_t pc,
                                    const std::map<std::uint64_t, std::string>& modules) {
    if (pc == 0) {
        return {};
    }
    auto above = modules.upper_bound(pc);
    if (above == modules.begin()) {
        return {};
    }
    --above;
    return {true, above->second, pc - above->first};
}

/// Counters for leaving recompiled code; any one nonzero proves a fallback, none proves nothing.
struct FallbackCounters {
    std::uint64_t jit_transitions{};
    std::uint64_t lookup_misses{};
    std::uint64_t unhandled{};
    std::uint64_t no_fallback{};
};

inline bool FallbackObserved(const FallbackCounters& counters) {
    return counters.jit_transitions != 0 || counters.lookup_misses != 0 ||
           counters.unhandled != 0 || counters.no_fallback != 0;
}

/// What the counters can say: they count attempts to leave recompiled code, not completed
/// handoffs; `no_fallback` counts attempts a strict or JIT-less run refused.
enum class FallbackVerdict : std::uint8_t {
    None,      ///< Nothing has tried to leave recompiled code so far.
    Attempted, ///< Uncovered code was reached and a JIT handoff was requested.
    Refused,   ///< Uncovered code was reached and no fallback was available to take it.
};

inline FallbackVerdict ClassifyFallback(const FallbackCounters& counters) {
    if (counters.no_fallback != 0) {
        return FallbackVerdict::Refused;
    }
    return FallbackObserved(counters) ? FallbackVerdict::Attempted : FallbackVerdict::None;
}

/// The execution-policy line for a backend class. Policy only exists for recompiled code; for
/// every other class saying "hybrid" would imply recompiled dispatch that is not there.
inline const char* ExecutionModeText(NativeBackendClass kind) {
    switch (kind) {
    case NativeBackendClass::StrictAot:
        return "strict (no JIT fallback permitted)";
    case NativeBackendClass::HybridAot:
        return "hybrid (JIT fallback permitted)";
    case NativeBackendClass::Pending:
        return "pending (recompiled code not active yet)";
    case NativeBackendClass::DynarmicJit:
    case NativeBackendClass::Nce:
    case NativeBackendClass::Unknown:
        break;
    }
    return "not applicable (not running recompiled code)";
}

} // namespace SuyuCmd
