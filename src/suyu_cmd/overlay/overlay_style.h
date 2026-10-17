// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace SuyuCmd {

// ImGui-free overlay look: scale, theme colors, and the embedded font. Applied in overlay_style.cpp.

// 1.25 means 125% of the original 13 px layout; every pixel value is derived from it.
constexpr float kUiScaleFactor = 1.25f;
constexpr float kBaseFontSizePx = 13.0f;
// Rounding at 100%; the applied value is scaled like every other size.
constexpr float kWindowRoundingPx = 6.0f;
// The main menu bar window draws WindowBg under MenuBarBg; this keeps only the themed layer.
constexpr float kMainMenuBarWindowBgAlpha = 0.0f;

constexpr float Scaled(float value_px) {
    return value_px * kUiScaleFactor;
}

struct Rgba {
    float r;
    float g;
    float b;
    float a;
    constexpr bool operator==(const Rgba&) const = default;
};

constexpr std::uint8_t kThemeRed8 = 0x46;
constexpr std::uint8_t kThemeGreen8 = 0x47;
constexpr std::uint8_t kThemeBlue8 = 0x8B;
// Share of white mixed into the theme color for the hovered and pressed states.
constexpr float kHoveredLightenShare = 0.12f;
constexpr float kActiveLightenShare = 0.25f;

constexpr Rgba ThemeColor(float alpha, float lighten_share = 0.0f) {
    const auto channel = [lighten_share](std::uint8_t value8) {
        const float base = static_cast<float>(value8) / 255.0f;
        return base + (1.0f - base) * lighten_share;
    };
    return {channel(kThemeRed8), channel(kThemeGreen8), channel(kThemeBlue8), alpha};
}

// The themed surfaces. Bar and title colors take the window background's alpha; controls are opaque.
struct ThemePalette {
    Rgba menu_bar;
    Rgba title_bg;
    Rgba title_bg_active;
    Rgba title_bg_collapsed;
    Rgba button;
    Rgba button_hovered;
    Rgba button_active;
    Rgba combo_frame;
    Rgba combo_frame_hovered;
    Rgba combo_frame_active;
};

constexpr ThemePalette MakeThemePalette(float window_bg_alpha) {
    return {
        .menu_bar = ThemeColor(window_bg_alpha),
        .title_bg = ThemeColor(window_bg_alpha),
        .title_bg_active = ThemeColor(window_bg_alpha),
        .title_bg_collapsed = ThemeColor(window_bg_alpha),
        .button = ThemeColor(1.0f),
        .button_hovered = ThemeColor(1.0f, kHoveredLightenShare),
        .button_active = ThemeColor(1.0f, kActiveLightenShare),
        .combo_frame = ThemeColor(1.0f),
        .combo_frame_hovered = ThemeColor(1.0f, kHoveredLightenShare),
        .combo_frame_active = ThemeColor(1.0f, kActiveLightenShare),
    };
}

// Embedded TTFs (SIL OFL 1.1), generated from dist/fonts at build time: Roboto for Latin text and
// BIZ UDPGothic Regular for the Japanese glyphs Roboto lacks.
extern "C" const std::uint64_t aevora_overlay_font_latin_words[];
extern "C" const std::size_t aevora_overlay_font_latin_size_bytes;
extern "C" const std::uint64_t aevora_overlay_font_japanese_words[];
extern "C" const std::size_t aevora_overlay_font_japanese_size_bytes;

// Applies the font, scale, colors, and rounding to the current ImGui context.
void ApplyOverlayStyle();

// A combo whose closed frame uses the theme color; the open list keeps the default colors.
bool BeginThemedCombo(const char* label, const char* preview);

} // namespace SuyuCmd
