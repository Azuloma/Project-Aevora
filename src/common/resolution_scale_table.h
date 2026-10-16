// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "common/settings_enums.h"

namespace Settings {

struct ResolutionScaleEntry {
    ResolutionSetup setup;
    const char* label;
};

/// Every Resolution Scale choice in enum order. The Qt menu repeats it (translated); the smoke
/// test smoke_graphics_settings_unit fails if the two or the enum drift.
inline constexpr std::array<ResolutionScaleEntry, 13> kResolutionScaleEntries{{
    {ResolutionSetup::Res1_4X, "0.25X (180p/270p) [EXPERIMENTAL]"},
    {ResolutionSetup::Res1_2X, "0.5X (360p/540p) [EXPERIMENTAL]"},
    {ResolutionSetup::Res3_4X, "0.75X (540p/810p) [EXPERIMENTAL]"},
    {ResolutionSetup::Res1X, "1X (720p/1080p)"},
    {ResolutionSetup::Res5_4X, "1.25X (900p/1350p) [EXPERIMENTAL]"},
    {ResolutionSetup::Res3_2X, "1.5X (1080p/1620p) [EXPERIMENTAL]"},
    {ResolutionSetup::Res2X, "2X (1440p/2160p)"},
    {ResolutionSetup::Res3X, "3X (2160p/3240p)"},
    {ResolutionSetup::Res4X, "4X (2880p/4320p)"},
    {ResolutionSetup::Res5X, "5X (3600p/5400p)"},
    {ResolutionSetup::Res6X, "6X (4320p/6480p)"},
    {ResolutionSetup::Res7X, "7X (5040p/7560p)"},
    {ResolutionSetup::Res8X, "8X (5760p/8640p)"},
}};

constexpr bool ResolutionScaleTableIsInEnumOrder() {
    for (std::size_t i = 0; i < kResolutionScaleEntries.size(); ++i) {
        if (static_cast<std::size_t>(kResolutionScaleEntries[i].setup) != i) {
            return false;
        }
    }
    return true;
}
static_assert(ResolutionScaleTableIsInEnumOrder());

inline std::optional<std::size_t> FindResolutionScaleIndex(ResolutionSetup setup) {
    const auto index = static_cast<std::size_t>(setup);
    if (index < kResolutionScaleEntries.size()) {
        return index;
    }
    return std::nullopt;
}

} // namespace Settings
