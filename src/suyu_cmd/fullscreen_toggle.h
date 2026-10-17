// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace SuyuCmd {

/// The scancodes that switch fullscreen; the host supplies them so this stays SDL-free.
struct FullscreenKeys {
    std::uint32_t toggle;       ///< pressed alone
    std::uint32_t enter;        ///< pressed with Alt
    std::uint32_t keypad_enter; ///< pressed with Alt
};

/// True for F11, or Enter / keypad Enter while Alt is held. The game never sees these keys.
constexpr bool IsFullscreenChord(std::uint32_t scancode, bool alt_down,
                                 const FullscreenKeys& keys) {
    return scancode == keys.toggle ||
           (alt_down && (scancode == keys.enter || scancode == keys.keypad_enter));
}

} // namespace SuyuCmd
