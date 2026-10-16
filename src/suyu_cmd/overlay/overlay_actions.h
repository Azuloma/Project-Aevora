// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// ImGui- and SDL-free channel between the overlay's UI thread and the window's event thread:
// UI-posted actions run on the event thread, whose outcomes come back as status messages.

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace SuyuCmd {

inline constexpr std::size_t kUiActionQueueCapacity = 64;
inline constexpr std::size_t kUiStatusQueueCapacity = 32;

struct UiActionResult {
    bool ok{true};
    std::string message; ///< Shown to the player; empty for a silent success.
};

struct UiAction {
    std::string name; ///< Names the action in a failure message.
    std::function<UiActionResult()> run;
};

struct UiStatusMessage {
    bool ok{true};
    std::string text;
};

/// Event thread: `BindEventThread`, `RunPending`, `PublishStatus`. UI thread: `Post`,
/// `DrainStatus`. Actions run in posting order, only on the bound event thread.
class UiActionChannel {
public:
    /// Declares the calling thread as the one that may run actions.
    void BindEventThread() {
        std::lock_guard lock{mutex};
        event_thread = std::this_thread::get_id();
        event_thread_bound = true;
    }

    /// Called after each accepted post so the event thread can wake up and run it.
    void SetWakeup(std::function<void()> new_wakeup) {
        std::lock_guard lock{mutex};
        wakeup = std::move(new_wakeup);
    }

    /// Queues `action`. Returns false, and reports it as a status message, if the queue is full
    /// or the action has no body.
    bool Post(UiAction action) {
        std::function<void()> wake;
        {
            std::lock_guard lock{mutex};
            if (!action.run || actions.size() >= kUiActionQueueCapacity) {
                PushStatusLocked({false, "Action '" + action.name + "' was not queued"});
                return false;
            }
            actions.push_back(std::move(action));
            wake = wakeup;
        }
        if (wake) {
            wake();
        }
        return true;
    }

    /// Runs what was queued when the call began, oldest first, and returns how many ran. Actions
    /// posted while running wait for the next call. Does nothing on any other thread.
    std::size_t RunPending() {
        std::vector<UiAction> batch;
        {
            std::lock_guard lock{mutex};
            if (!event_thread_bound || std::this_thread::get_id() != event_thread) {
                ++wrong_side_calls;
                return 0;
            }
            batch.assign(std::make_move_iterator(actions.begin()),
                         std::make_move_iterator(actions.end()));
            actions.clear();
        }
        for (UiAction& action : batch) {
            UiActionResult result = action.run();
            if (!result.ok && result.message.empty()) {
                result.message = "Action '" + action.name + "' failed";
            }
            if (!result.ok || !result.message.empty()) {
                PublishStatus({result.ok, std::move(result.message)});
            }
        }
        return batch.size();
    }

    /// Adds a message for the UI; the oldest is dropped when full.
    void PublishStatus(UiStatusMessage message) {
        std::lock_guard lock{mutex};
        PushStatusLocked(std::move(message));
    }

    /// Moves all status messages, oldest first, into `out` (cleared first).
    void DrainStatus(std::vector<UiStatusMessage>& out) {
        out.clear();
        std::lock_guard lock{mutex};
        out.assign(std::make_move_iterator(statuses.begin()),
                   std::make_move_iterator(statuses.end()));
        statuses.clear();
    }

    std::size_t WrongSideCalls() {
        std::lock_guard lock{mutex};
        return wrong_side_calls;
    }

private:
    void PushStatusLocked(UiStatusMessage message) {
        if (statuses.size() >= kUiStatusQueueCapacity) {
            statuses.pop_front();
        }
        statuses.push_back(std::move(message));
    }

    std::mutex mutex;
    std::deque<UiAction> actions;
    std::deque<UiStatusMessage> statuses;
    std::function<void()> wakeup;
    std::thread::id event_thread;
    bool event_thread_bound{};
    std::size_t wrong_side_calls{};
};

} // namespace SuyuCmd
