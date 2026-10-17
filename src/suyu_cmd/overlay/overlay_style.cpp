// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <imgui.h>

#include "common/logging.h"
#include "suyu_cmd/overlay/overlay_style.h"

namespace SuyuCmd {

namespace {

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

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    ImFont* const font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<std::uint64_t*>(aevora_overlay_font_words),
        static_cast<int>(aevora_overlay_font_size_bytes), 0.0f, &font_config);
    if (font == nullptr) {
        LOG_ERROR(Frontend, "Debug overlay could not load the embedded BIZ UDPGothic font");
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
