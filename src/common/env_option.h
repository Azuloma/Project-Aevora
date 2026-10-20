// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// How Aevora reads on/off environment variables whose default is "on". One definition, so the
// exporter, the launcher and their tests agree on what an unset or odd value means.

#include <optional>
#include <string_view>

namespace Common {

/// On unless the variable is exactly "0"; unset, empty, and any other text are all "on", so a
/// stray value cannot silently switch an option off.
constexpr bool EnvFlagDefaultOn(std::string_view value) {
    return value != "0";
}

/// An option that is off unless the variable is exactly "1"; any other text leaves it off.
constexpr bool EnvFlagDefaultOff(std::string_view value) {
    return value == "1";
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

/// A decimal integer within [min, max]; nullopt for empty, signed, padded, or out-of-range text.
constexpr std::optional<int> ParseEnvIntInRange(std::string_view value, int min, int max) {
    if (value.empty() || value.size() > 9) {
        return std::nullopt;
    }
    int parsed = 0;
    for (const char digit : value) {
        if (digit < '0' || digit > '9') {
            return std::nullopt;
        }
        parsed = parsed * 10 + (digit - '0');
    }
    return parsed >= min && parsed <= max ? std::optional<int>{parsed} : std::nullopt;
}

} // namespace Common
