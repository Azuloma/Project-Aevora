// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Player 1 controller settings logic that needs neither the input backend nor ImGui: what the
// UI may ask for, what the event thread publishes back, and the rules for changing a binding.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "suyu_cmd/overlay/overlay_actions.h"

namespace SuyuCmd {

/// Buttons first, then analog sticks, as one row index space.
struct BindingLayout {
    std::size_t buttons{};
    std::size_t analogs{};

    std::size_t Rows() const {
        return buttons + analogs;
    }
    bool IsAnalog(std::size_t row) const {
        return row >= buttons;
    }
    /// Index within the button or analog array.
    std::size_t IndexOf(std::size_t row) const {
        return IsAnalog(row) ? row - buttons : row;
    }
};

struct ControllerRequest {
    enum class Kind : std::uint8_t {
        MapDevice,    ///< device_id: the listed device to map to Player 1
        UseKeyboard,  ///< restore the keyboard layout
        BeginRebind,  ///< row: the binding to capture
        ClearBinding, ///< row: the binding to clear
        CancelRebind,
        CombineJoycons,
        SplitJoycons,
    };

    Kind kind{Kind::CancelRebind};
    std::size_t row{};
    std::string device_id;
};

/// Every request except starting a capture ends a running one first, so a late capture cannot
/// overwrite what the request just set.
inline bool RequestCancelsRebind(ControllerRequest::Kind kind) {
    return kind != ControllerRequest::Kind::BeginRebind;
}

struct ControllerRow {
    std::string label;
    std::string shown;
};

struct ControllerDevice {
    std::string id; ///< Serialized device, handed back by MapDevice.
    std::string name;
};

/// What the UI shows; built on the event thread, copied out on the UI thread.
struct ControllerSnapshot {
    std::vector<ControllerDevice> devices;
    std::vector<ControllerRow> rows;
    bool can_combine{};
    bool can_split{};
    bool rebinding{};
    std::size_t rebind_row{};
    std::uint64_t rebind_remaining_ms{};
};

/// Position of the device with `id` in the snapshot, if it is still listed.
inline std::optional<std::size_t> FindDevice(const ControllerSnapshot& snapshot,
                                             const std::string& id) {
    for (std::size_t i = 0; i < snapshot.devices.size(); ++i) {
        if (snapshot.devices[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

/// One value handed from the event thread to the UI thread.
template <class T>
class SnapshotBox {
public:
    void Publish(T value) {
        std::lock_guard lock{mutex};
        current = std::move(value);
    }

    T Get() {
        std::lock_guard lock{mutex};
        return current;
    }

private:
    std::mutex mutex;
    T current{};
};

/// The real Player 1 settings behind the rules below.
struct Player1Hooks {
    /// Writes one binding; an empty `value` clears it.
    std::function<void(bool analog, std::size_t index, const std::string& value)> set;
    std::function<void()> mark_connected;
    /// The player's own choice: turn auto-assign off, save, and reload the input devices.
    std::function<void()> commit;
};

/// Event thread only. A capture with nothing in it changes nothing.
inline UiActionResult ApplyBinding(const Player1Hooks& hooks, const BindingLayout& layout,
                                   std::size_t row, const std::string& serialized) {
    if (row >= layout.Rows()) {
        return {false, "Unknown controller binding"};
    }
    if (serialized.empty()) {
        return {false, "No input detected - nothing changed"};
    }
    hooks.set(layout.IsAnalog(row), layout.IndexOf(row), serialized);
    hooks.mark_connected();
    hooks.commit();
    return {};
}

/// Event thread only.
inline UiActionResult ClearBinding(const Player1Hooks& hooks, const BindingLayout& layout,
                                   std::size_t row) {
    if (row >= layout.Rows()) {
        return {false, "Unknown controller binding"};
    }
    hooks.set(layout.IsAnalog(row), layout.IndexOf(row), {});
    hooks.commit();
    return {};
}

} // namespace SuyuCmd
