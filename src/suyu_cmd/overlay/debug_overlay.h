// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <vector>

#include "core/frontend/vulkan_overlay.h"
#include "suyu_cmd/overlay/overlay_logic.h"
#include "suyu_cmd/overlay/overlay_stats.h"

namespace SuyuCmd {

/// Environment variable that turns the overlay on. Any value other than "" and "0" enables it.
inline constexpr const char* kDebugOverlayEnvVar = "SUYU_DEBUG_OVERLAY";

/// Whether the environment asks for the overlay. Off unless explicitly requested.
bool IsDebugOverlayRequested();

/// Dear ImGui layer drawn over the presented frame.
///
/// All ImGui state is owned by the scheduler worker thread: `Record` runs the whole frame
/// (NewFrame, UI, Render, draw submission) there, so ImGui's non-thread-safe context is never
/// shared. `Initialize` and `Shutdown` run on the renderer thread while the worker is quiescent.
class DebugOverlay final : public Core::Frontend::VulkanOverlay {
public:
    DebugOverlay();
    ~DebugOverlay() override;

    bool Initialize(const Core::Frontend::VulkanOverlayContext& context) override;
    void Shutdown() override;
    void Record(void* command_buffer, u64 render_pass, u32 width, u32 height) override;

    /// Mouse events from the window's event thread; its `Route` tells the window which events the
    /// overlay took.
    OverlayInputRouter& GetInputRouter() {
        return input_router;
    }

    /// The window hides the OS pointer (fullscreen); the overlay then draws its own while the
    /// pointer is over it so the bar can be used.
    void SetHardwareCursorHidden(bool hidden) {
        hardware_cursor_hidden = hidden;
    }

private:
    /// Creates the Vulkan backend for `render_pass`, or rebuilds its pipeline if the pass changed.
    bool EnsureBackend(u64 render_pass);
    void ApplyQueuedInput();
    void BuildUi();
    void PublishCaptureRegions();
    void BuildCommandBar();
    void BuildDebugWindow();
    void BuildBackendSection();
    void BuildPerformanceSection();
    void BuildCpuSection();
    void BuildIdentitySection();
    void BuildGpuSection();

    Core::Frontend::VulkanOverlayContext context{};
    bool context_created{};
    bool backend_ready{};
    bool failed{};
    u64 backend_render_pass{};
    std::chrono::steady_clock::time_point last_frame_time{};

    OverlayInputRouter input_router;
    std::vector<OverlayInputEvent> pending_input; ///< Scratch for ApplyQueuedInput.
    std::atomic<bool> hardware_cursor_hidden{false};

    // UI state, touched by the worker thread only.
    bool command_bar_visible{};
    float command_bar_height_px{};
    bool show_debug_window{};
    OverlayRect debug_window_rect{}; ///< Where the Debug window was drawn this frame.
    FrameTimeHistory frame_times;
    u32 target_width_px{};
    u32 target_height_px{};
};

} // namespace SuyuCmd
