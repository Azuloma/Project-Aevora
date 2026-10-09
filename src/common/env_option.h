// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// How Aevora reads on/off environment variables whose default is "on". One definition, so the
// exporter, the launcher and their tests agree on what an unset or odd value means.

#include <string_view>

namespace Common {

/// An option that is on unless the variable is exactly "0". An unset variable (read as an empty
/// string) and an empty one are both "on"; any other text is also "on", never an error, so a
/// stray value cannot silently switch an option off.
constexpr bool EnvFlagDefaultOn(std::string_view value) {
    return value != "0";
}

/// What a three-way option variable asked for.
enum class EnvChoice {
    Default, ///< Unset or empty: the caller decides, usually "on where it is supported".
    Off,     ///< "0".
    On,      ///< "1": an explicit request, which the caller may refuse if it cannot be met.
    Invalid, ///< Anything else.
};

constexpr EnvChoice ParseEnvChoice(std::string_view value) {
    if (value.empty()) {
        return EnvChoice::Default;
    }
    if (value == "0") {
        return EnvChoice::Off;
    }
    if (value == "1") {
        return EnvChoice::On;
    }
    return EnvChoice::Invalid;
}

} // namespace Common
