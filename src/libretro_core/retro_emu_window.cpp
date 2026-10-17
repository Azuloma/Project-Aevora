// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/frontend/graphics_context.h"
#include <cstdio>
#include "common/logging/log.h"
#include "common/settings.h"
#include "input_common/drivers/tas_input.h"
#include "libretro_core/retro_emu_window.h"

namespace LibretroCore {



RetroEmuWindow::RetroEmuWindow() {
    window_info.type = Core::Frontend::WindowSystemType::Headless;
    window_info.render_surface = nullptr;
    UpdateCurrentFramebufferLayout(1280, 720);
}

RetroEmuWindow::~RetroEmuWindow() = default;

void RetroEmuWindow::SetTasPlayback(InputCommon::TasInput::Tas* tas) {
    // Change only before GPU start or after shutdown has joined the GPU thread.
    tas_playback = tas;
    tas_completion_generation = tas ? std::get<0>(tas->GetCompletionStatus()) : 0;
    tas_frame_callbacks = 0;
}

void RetroEmuWindow::OnFrameDisplayed() {
    frames_displayed.fetch_add(1, std::memory_order_release);
    if (!tas_playback) {
        return;
    }
    tas_playback->UpdateThread();
    ++tas_frame_callbacks;
    if (tas_frame_callbacks == 1 || tas_frame_callbacks % 600 == 0) {
        const auto [state, cursor, lengths] = tas_playback->GetStatus();
        LOG_INFO(Frontend, "libretro: TAS frame_callback={} cursor={} length={} state={} enabled={}",
                 tas_frame_callbacks, cursor, lengths[0], static_cast<int>(state),
                 Settings::values.tas_enable.GetValue());
        std::fprintf(stderr, "[suyu-libretro] TAS frame_callback=%llu cursor=%zu length=%zu state=%d enabled=%d\n",
                     static_cast<unsigned long long>(tas_frame_callbacks), cursor, lengths[0],
                     static_cast<int>(state), Settings::values.tas_enable.GetValue());
        std::fflush(stderr);
    }
    const auto [generation, commands, looping] = tas_playback->GetCompletionStatus();
    if (generation != tas_completion_generation) {
        tas_completion_generation = generation;
        LOG_INFO(Frontend, "libretro: TAS completed {} commands, looping={}", commands, looping);
        std::fprintf(stderr, "[suyu-libretro] TAS completed %zu commands, looping=%d\n", commands,
                     looping);
        std::fflush(stderr);
    }
}

std::unique_ptr<Core::Frontend::GraphicsContext> RetroEmuWindow::CreateSharedContext() const {
    return std::make_unique<Core::Frontend::GraphicsContext>();
}

bool RetroEmuWindow::IsShown() const {
    // There is no real window to minimize; always report visible so the
    // renderer doesn't pause itself thinking it's hidden.
    return true;
}

} // namespace LibretroCore
