// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/settings.h"
#include "suyu_cmd/overlay/graphics_settings.h"
#include "suyu_cmd/sdl_config.h"

namespace SuyuCmd {

/// Event-thread hooks for the overlay's saved Resolution Scale setting.
inline ResolutionScaleHooks MakeResolutionScaleHooks() {
    return {
        .current = [] { return Settings::values.resolution_setup.GetValue(); },
        .store = [](Settings::ResolutionSetup setup) {
            Settings::values.resolution_setup.SetValue(setup);
        },
        .save = [] { SaveNativeControls(); },
    };
}

} // namespace SuyuCmd
