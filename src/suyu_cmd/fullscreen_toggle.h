// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace SuyuCmd {

/// The scancodes that switch fullscreen; the host supplies them so this stays SDL-free.
struct FullscreenKeys {
    std::uint32_t toggle;       ///< pressed alone
    std::uint32_t enter;        ///< pressed with Alt
    std::uint32_t keypad_enter; ///< pressed with Alt
};

/// True for F11, or Enter / keypad Enter while Alt is held.
constexpr bool IsFullscreenChord(std::uint32_t scancode, bool alt_down,
                                 const FullscreenKeys& keys) {
    return scancode == keys.toggle ||
           (alt_down && (scancode == keys.enter || scancode == keys.keypad_enter));
}

/// Scancodes at or above this are not tracked; SDL's table ends at 512.
inline constexpr std::size_t kHotkeyTrackedKeys = 512;

/// Decides per key event whether the fullscreen hotkey or the game owns it. A press belongs to
/// whoever took it until its release, so Alt changing mid-hold never splits a key between them.
class HotkeyKeyTracker {
public:
    enum class Route : std::uint8_t {
        Game,    ///< not a hotkey
        Toggle,  ///< a fresh hotkey press: switch fullscreen
        Consume, ///< the hotkey's own repeat; nobody acts on it
    };

    /// `repeat` is SDL's auto-repeat flag; only a fresh press can start a toggle.
    Route OnKeyDown(std::uint32_t scancode, bool alt_down, bool repeat, const FullscreenKeys& keys) {
        const bool tracked = scancode < kHotkeyTrackedKeys;
        const bool chord = IsFullscreenChord(scancode, alt_down, keys);
        if (repeat) {
            if (tracked && hotkey_keys.test(scancode)) {
                return Route::Consume;
            }
            if (tracked && game_keys.test(scancode)) {
                return Route::Game;
            }
            // Owner unknown (for example after a focus reset): never toggle on a repeat.
            return chord ? Route::Consume : Route::Game;
        }
        if (tracked) {
            hotkey_keys.reset(scancode);
            game_keys.reset(scancode);
        }
        if (chord) {
            if (tracked) {
                hotkey_keys.set(scancode);
            }
            return Route::Toggle;
        }
        if (tracked) {
            game_keys.set(scancode);
        }
        return Route::Game;
    }

    /// Returns true if the hotkey owned this key, so the game must not see its release.
    bool OnKeyUp(std::uint32_t scancode) {
        if (scancode >= kHotkeyTrackedKeys) {
            return false;
        }
        game_keys.reset(scancode);
        if (!hotkey_keys.test(scancode)) {
            return false;
        }
        hotkey_keys.reset(scancode);
        return true;
    }

    /// Forgets all ownership; releases may never arrive after a focus loss.
    void Reset() {
        hotkey_keys.reset();
        game_keys.reset();
    }

private:
    std::bitset<kHotkeyTrackedKeys> hotkey_keys;
    std::bitset<kHotkeyTrackedKeys> game_keys;
};

} // namespace SuyuCmd
