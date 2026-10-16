// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Non-blocking "press the input you want" capture for one binding. The event thread starts it,
// polls it once per tick and never waits; it is free of the input backend and of ImGui.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace SuyuCmd {

/// How long a capture waits for an input before giving up.
inline constexpr std::uint64_t kRebindTimeoutMs = 5000;

/// Event-wait slice while a capture runs, short enough to catch a brief press.
inline constexpr std::uint32_t kRebindPollIntervalMs = 10;

struct RebindHooks {
    std::function<void(bool analog)> begin; ///< Starts the backend's poller.
    std::function<std::string()> next_input; ///< Serialized input seen, or empty.
    std::function<void()> stop;              ///< Stops the poller.
    /// Whether a capture suits the binding kind; others are ignored. Empty accepts everything.
    std::function<bool(bool analog, const std::string& captured)> accept;
};

struct RebindResult {
    enum class Kind : std::uint8_t { None, Captured, TimedOut, Cancelled };

    Kind kind{Kind::None};
    std::size_t row{};
    std::string captured; ///< Set for Captured only.
};

/// Every `begin` is matched by exactly one `stop`: on a capture, a timeout, a cancel, or when a
/// new capture replaces the running one. Event thread only.
class RebindSession {
public:
    explicit RebindSession(RebindHooks new_hooks) : hooks(std::move(new_hooks)) {}

    bool Active() const {
        return active;
    }

    std::size_t Row() const {
        return row;
    }

    std::uint64_t RemainingMs(std::uint64_t now_ms) const {
        if (!active) {
            return 0;
        }
        const std::uint64_t elapsed_ms = now_ms - started_ms;
        return elapsed_ms >= kRebindTimeoutMs ? 0 : kRebindTimeoutMs - elapsed_ms;
    }

    /// Starts capturing for `target_row`; a capture already running is stopped first.
    void Begin(std::size_t target_row, bool analog, std::uint64_t now_ms) {
        Stop();
        hooks.begin(analog);
        active = true;
        analog_target = analog;
        row = target_row;
        started_ms = now_ms;
    }

    /// Checks for a captured input or the timeout; call once per event-loop tick.
    RebindResult Poll(std::uint64_t now_ms) {
        if (!active) {
            return {};
        }
        std::string captured = hooks.next_input();
        if (!captured.empty() && (!hooks.accept || hooks.accept(analog_target, captured))) {
            const std::size_t captured_row = row;
            Stop();
            return {RebindResult::Kind::Captured, captured_row, std::move(captured)};
        }
        if (now_ms - started_ms >= kRebindTimeoutMs) {
            const std::size_t timed_out_row = row;
            Stop();
            return {RebindResult::Kind::TimedOut, timed_out_row, {}};
        }
        return {};
    }

    /// Abandons a running capture, for example when the window goes away.
    RebindResult Cancel() {
        if (!active) {
            return {};
        }
        const std::size_t cancelled_row = row;
        Stop();
        return {RebindResult::Kind::Cancelled, cancelled_row, {}};
    }

private:
    void Stop() {
        if (active) {
            hooks.stop();
            active = false;
        }
    }

    RebindHooks hooks;
    bool active{};
    bool analog_target{};
    std::size_t row{};
    std::uint64_t started_ms{};
};

} // namespace SuyuCmd
