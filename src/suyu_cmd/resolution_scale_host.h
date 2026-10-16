// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/settings.h"
#include "core/core.h"
#include "suyu_cmd/overlay/graphics_settings.h"
#include "suyu_cmd/sdl_config.h"

namespace SuyuCmd {

/// The hooks that change the real setting; shared by the overlay and the F12 panel.
inline ResolutionScaleHooks MakeResolutionScaleHooks(Core::System& system) {
    return {
        .current = [] { return Settings::values.resolution_setup.GetValue(); },
        .store =
            [](Settings::ResolutionSetup setup) {
                Settings::values.resolution_setup.SetValue(setup);
                Settings::UpdateRescalingInfo();
            },
        .commit =
            [&system] {
                system.ApplySettings();
                SaveNativeControls();
            },
    };
}

} // namespace SuyuCmd
