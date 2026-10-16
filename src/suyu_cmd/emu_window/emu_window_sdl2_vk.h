// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

#include "core/frontend/emu_window.h"
#include "suyu_cmd/emu_window/emu_window_sdl2.h"

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

namespace SuyuCmd {
class DebugOverlay;
}

class EmuWindow_SDL2_VK final : public EmuWindow_SDL2 {
public:
    explicit EmuWindow_SDL2_VK(InputCommon::InputSubsystem* input_subsystem_, Core::System& system,
                               bool fullscreen);
    ~EmuWindow_SDL2_VK() override;

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override;

    /// Null only when the overlay is switched off with SUYU_DEBUG_OVERLAY=0.
    Core::Frontend::VulkanOverlay* GetVulkanOverlay() override;

#if defined(__APPLE__)
    /// Owns the CAMetalLayer in render_surface. SDL_MetalView is a void*.
    void* metal_view{};
#endif

protected:
    bool RouteOverlayEvent(const SDL_Event& event) override;
    void SyncOverlayTextInput() override;
    void RunOverlayActions() override;
    void InstallOverlayResolutionScale();
    void PublishOverlayResolutionScale();
    void InstallOverlayController();
    void InstallOverlayModsFolders();
    void ServiceOverlayController(bool actions_ran);

    bool controller_dirty{true};
    std::uint64_t last_controller_publish_ms{};

private:
    /// Not owned: see SuyuCmd::AcquireDebugOverlay.
    SuyuCmd::DebugOverlay* debug_overlay{};
};
