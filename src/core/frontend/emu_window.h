// SPDX-FileCopyrightText: Copyright 2025 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <utility>

#include "common/common_types.h"
#include "core/frontend/framebuffer_layout.h"

namespace Core::Frontend {

class GraphicsContext;
class VulkanOverlay;

/// @brief Information for the Graphics Backends signifying what type of screen pointer is in
/// WindowInformation
enum class WindowSystemType {
    Headless,
    Windows,
    X11,
    Wayland,
    Xcb,
};


class EmuWindow {
public:
    /// Data structure to store emuwindow configuration
    struct WindowConfig {
        bool fullscreen = false;
        int res_width = 0;
        int res_height = 0;
        std::pair<u32, u32> min_client_area_size;
    };

    /// Data describing host window system information
    struct WindowSystemInfo {
        // Window system type. Determines which GL context or Vulkan WSI is used.
        WindowSystemType type = WindowSystemType::Headless;

        // Connection to a display server. This is used on X11 and Wayland platforms.
        void* display_connection = nullptr;

        void* render_surface = nullptr;

        // Scale of the render surface. For hidpi systems, this will be >1.
        float render_surface_scale = 1.0f;
    };

    /// Called from GPU thread when a frame is displayed.
    virtual void OnFrameDisplayed() {}

    /// Overlay drawn on top of each presented frame by the Vulkan renderer, or nullptr for none.
    /// The window keeps ownership and must outlive the renderer.
    virtual VulkanOverlay* GetVulkanOverlay() {
        return nullptr;
    }


    virtual std::unique_ptr<GraphicsContext> CreateSharedContext() const = 0;

    /// Returns if window is shown (not minimized)
    virtual bool IsShown() const = 0;


    const WindowConfig& GetActiveConfig() const {
        return active_config;
    }

    bool StrictContextRequired() const {
        return strict_context_required;
    }


    void SetConfig(const WindowConfig& val) {
        config = val;
    }


    const WindowSystemInfo& GetWindowInfo() const {
        return window_info;
    }


    const Layout::FramebufferLayout& GetFramebufferLayout() const {
        return framebuffer_layout;
    }


    void UpdateCurrentFramebufferLayout(u32 width, u32 height);

protected:
    explicit EmuWindow();
    virtual ~EmuWindow();


    void ProcessConfigurationChanges() {
        // TODO: For proper thread safety, we should eventually implement a proper
        // multiple-writer/single-reader queue...

        if (config.min_client_area_size != active_config.min_client_area_size) {
            OnMinimalClientAreaChangeRequest(config.min_client_area_size);
            config.min_client_area_size = active_config.min_client_area_size;
        }
    }


    void NotifyFramebufferLayoutChanged(const Layout::FramebufferLayout& layout) {
        framebuffer_layout = layout;
    }


    void NotifyClientAreaSizeChanged(std::pair<u32, u32> size) {
        client_area_width = size.first;
        client_area_height = size.second;
    }


    std::pair<f32, f32> MapToTouchScreen(u32 framebuffer_x, u32 framebuffer_y) const;


    std::pair<u32, u32> ClipToTouchScreen(u32 new_x, u32 new_y) const;

    WindowSystemInfo window_info;

    bool strict_context_required = false;

private:

    virtual void OnMinimalClientAreaChangeRequest(std::pair<u32, u32>) {
        // By default, ignore this request and do nothing.
    }

    Layout::FramebufferLayout framebuffer_layout; ///< Current framebuffer layout

    u32 client_area_width;  ///< Current client width, should be set by window impl.
    u32 client_area_height; ///< Current client height, should be set by window impl.

    WindowConfig config;        ///< Internal configuration (changes pending for being applied in
                                /// ProcessConfigurationChanges)
    WindowConfig active_config; ///< Internal active configuration
};

} // namespace Core::Frontend
