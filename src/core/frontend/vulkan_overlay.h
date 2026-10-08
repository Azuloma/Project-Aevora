// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

namespace Core::Frontend {

/// Vulkan objects an overlay needs to build its own pipelines. Handles are carried as opaque
/// pointers/integers so core does not depend on the Vulkan headers; the overlay casts them back.
struct VulkanOverlayContext {
    void* get_instance_proc_addr; ///< PFN_vkGetInstanceProcAddr of the renderer's loader
    void* instance;               ///< VkInstance
    void* physical_device;        ///< VkPhysicalDevice
    void* device;                 ///< VkDevice
    void* graphics_queue;         ///< VkQueue the renderer submits to
    u32 graphics_queue_family;
    u32 api_version;
    u32 image_count; ///< Swapchain images, bounds how many frames can be in flight
};

/// Frontend-provided layer drawn on top of the final presented frame.
///
/// The renderer asks the window for it (`EmuWindow::GetVulkanOverlay`); a window that returns
/// nullptr costs the renderer nothing. Everything but `Initialize`/`Shutdown` runs on the
/// scheduler's worker thread, so an overlay must treat that thread as the only one that touches
/// its UI state and take input from other threads through its own synchronized queue.
class VulkanOverlay {
public:
    virtual ~VulkanOverlay() = default;

    /// Called on the renderer thread once the device exists and before the first Record.
    virtual bool Initialize(const VulkanOverlayContext& context) = 0;

    /// Called after the GPU has gone idle and before the Vulkan device is destroyed.
    virtual void Shutdown() = 0;

    /// Records overlay draws into `command_buffer` (VkCommandBuffer) while `render_pass`
    /// (VkRenderPass) is active on a `width` x `height` target. `render_pass` can change when the
    /// presentation path is rebuilt; implementations must rebuild any pipeline tied to it.
    virtual void Record(void* command_buffer, u64 render_pass, u32 width, u32 height) = 0;
};

} // namespace Core::Frontend
