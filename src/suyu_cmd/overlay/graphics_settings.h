// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Applying a Resolution Scale pick, free of Core::System and ImGui so it can be tested alone.
// The hooks do the real work; the function decides when and in what order they run.

#include <cstddef>
#include <functional>
#include <string>

#include "common/resolution_scale_table.h"
#include "suyu_cmd/overlay/overlay_actions.h"

namespace SuyuCmd {

struct ResolutionScaleHooks {
    std::function<Settings::ResolutionSetup()> current;
    std::function<void(Settings::ResolutionSetup)> store; ///< Sets the value only.
    std::function<void()> save;                           ///< Persists the settings.
};

/// Event thread only. The running renderer reads the rescaling info unsynchronized, so only the
/// saved value changes; the next launch recomputes the info.
inline UiActionResult ApplyResolutionScale(const ResolutionScaleHooks& hooks, std::size_t index) {
    if (index >= Settings::kResolutionScaleEntries.size()) {
        return {false, "Unknown Resolution Scale choice"};
    }
    const Settings::ResolutionScaleEntry& entry = Settings::kResolutionScaleEntries[index];
    if (hooks.current() == entry.setup) {
        return {};
    }
    hooks.store(entry.setup);
    hooks.save();
    return {true, std::string{"Resolution Scale set to "} + entry.label +
                      ". It takes full effect the next time the game is started."};
}

} // namespace SuyuCmd
