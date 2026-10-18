// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <imgui.h>

#include "common/logging.h"
#include "suyu_cmd/overlay/overlay_style.h"

namespace SuyuCmd {

namespace {

ImFont* g_bold_font = nullptr;

ImVec4 ToImVec4(const Rgba& color) {
    return {color.r, color.g, color.b, color.a};
}

} // namespace

void ApplyOverlayStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.ScaleAllSizes(kUiScaleFactor);
    // Set after the scaling: ScaleAllSizes would truncate the rounded value to whole pixels.
    style.WindowRounding = Scaled(kWindowRoundingPx);

    ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
    ImFontConfig latin_config;
    latin_config.FontDataOwnedByAtlas = false;
    if (atlas.AddFontFromMemoryTTF(const_cast<std::uint64_t*>(aevora_overlay_font_latin_words),
                                   static_cast<int>(aevora_overlay_font_latin_size_bytes), 0.0f,
                                   &latin_config) == nullptr) {
        LOG_ERROR(Frontend, "Debug overlay could not load the embedded Roboto font");
    }
    // Merged after Roboto: it only supplies the glyphs Roboto does not have.
    ImFontConfig japanese_config = latin_config;
    japanese_config.MergeMode = true;
    if (atlas.AddFontFromMemoryTTF(const_cast<std::uint64_t*>(aevora_overlay_font_japanese_words),
                                   static_cast<int>(aevora_overlay_font_japanese_size_bytes),
                                   0.0f, &japanese_config) == nullptr) {
        LOG_ERROR(Frontend, "Debug overlay could not load the embedded BIZ UDPGothic font");
    }
    // A separate font, not merged: callers pick it by pointer for text that should be bold.
    ImFontConfig bold_config = latin_config;
    g_bold_font = atlas.AddFontFromMemoryTTF(
        const_cast<std::uint64_t*>(aevora_overlay_font_latin_bold_words),
        static_cast<int>(aevora_overlay_font_latin_bold_size_bytes), 0.0f, &bold_config);
    if (g_bold_font == nullptr) {
        LOG_ERROR(Frontend, "Debug overlay could not load the embedded Roboto Bold font");
    }
    style.FontSizeBase = kBaseFontSizePx;
    style.FontScaleMain = kUiScaleFactor;

    const ThemePalette palette = MakeThemePalette(style.Colors[ImGuiCol_WindowBg].w);
    style.Colors[ImGuiCol_MenuBarBg] = ToImVec4(palette.menu_bar);
    style.Colors[ImGuiCol_TitleBg] = ToImVec4(palette.title_bg);
    style.Colors[ImGuiCol_TitleBgActive] = ToImVec4(palette.title_bg_active);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ToImVec4(palette.title_bg_collapsed);
    style.Colors[ImGuiCol_Button] = ToImVec4(palette.button);
    style.Colors[ImGuiCol_ButtonHovered] = ToImVec4(palette.button_hovered);
    style.Colors[ImGuiCol_ButtonActive] = ToImVec4(palette.button_active);
}

ImFont* OverlayBoldFont() {
    return g_bold_font;
}

bool BeginThemedCombo(const char* label, const char* preview) {
    const ThemePalette palette = MakeThemePalette(ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ToImVec4(palette.combo_frame));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ToImVec4(palette.combo_frame_hovered));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ToImVec4(palette.combo_frame_active));
    const bool open = ImGui::BeginCombo(label, preview);
    ImGui::PopStyleColor(3);
    return open;
}

} // namespace SuyuCmd
