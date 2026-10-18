// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "core/frontend/vulkan_overlay.h"
#include "suyu_cmd/overlay/controller_settings.h"
#include "suyu_cmd/overlay/mods_folders.h"
#include "suyu_cmd/overlay/overlay_actions.h"
#include "suyu_cmd/overlay/overlay_logic.h"
#include "suyu_cmd/overlay/overlay_stats.h"
#include "suyu_cmd/overlay/perf_hud.h"
#include "suyu_cmd/overlay/perf_hud_sampler.h"

namespace SuyuCmd {

/// Environment variable that controls the overlay. It is on unless the value is "0".
inline constexpr const char* kDebugOverlayEnvVar = "SUYU_DEBUG_OVERLAY";

/// Whether the overlay should be created: on by default, off with SUYU_DEBUG_OVERLAY=0.
bool IsDebugOverlayRequested();

class DebugOverlay;

/// The process-wide overlay, or nullptr when it is switched off. Never destroyed: the renderer
/// holds a raw pointer and may outlive the window.
DebugOverlay* AcquireDebugOverlay();

/// Dear ImGui layer drawn over the presented frame. `Record` runs the whole ImGui frame on the
/// scheduler worker; `Initialize` and `Shutdown` run on the renderer thread while it is quiescent.
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

    /// UI actions the window's event thread runs, and the status messages they send back.
    UiActionChannel& GetActions() {
        return actions;
    }

    /// Whether the bottom-left performance HUD is shown; the event thread toggles it.
    PerfHudVisibility& GetPerfHud() {
        return perf_hud;
    }

    /// Applies a Resolution Scale pick; set and called on the event thread only.
    using ResolutionScaleApplier = std::function<UiActionResult(std::size_t)>;
    void SetResolutionScaleApplier(ResolutionScaleApplier applier) {
        resolution_scale_applier = std::move(applier);
    }

    /// Applies a Controller window request; set and called on the event thread only.
    using ControllerApplier = std::function<UiActionResult(const ControllerRequest&)>;
    void SetControllerApplier(ControllerApplier applier) {
        controller_applier = std::move(applier);
    }

    /// Applies a Mods and folders request; set and called on the event thread only.
    using ModsFoldersApplier = std::function<UiActionResult(const ModsFoldersRequest&)>;
    void SetModsFoldersApplier(ModsFoldersApplier applier) {
        mods_folders_applier = std::move(applier);
    }

    /// What the Mods and folders window shows; published by the event thread.
    SnapshotBox<ModsFoldersSnapshot>& GetModsFoldersSnapshot() {
        return mods_folders_snapshot;
    }

    /// What the Controller window shows; published by the event thread.
    SnapshotBox<ControllerSnapshot>& GetControllerSnapshot() {
        return controller_snapshot;
    }

    /// Whether the Controller window is open, so the event thread only builds its data then.
    bool ControllerWindowOpen() const {
        return controller_window_open.load(std::memory_order_relaxed);
    }

    /// The table index the Graphics window shows as current; written by the event thread.
    void PublishResolutionScaleIndex(std::size_t index) {
        resolution_scale_index.store(index, std::memory_order_relaxed);
    }

    /// Switches fullscreen; set and called on the event thread only.
    using FullscreenToggler = std::function<UiActionResult()>;
    void SetFullscreenToggler(FullscreenToggler toggler) {
        fullscreen_toggler = std::move(toggler);
    }

    /// Whether the window is fullscreen, as the event thread last saw it.
    void PublishFullscreen(bool fullscreen) {
        window_fullscreen.store(fullscreen, std::memory_order_relaxed);
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
    void CollectActionStatus();
    void BuildUi();
    void BuildPerfHud();
    void PublishCaptureRegions();
    void BuildCommandBar();
    void BuildDebugWindow();
    void BuildGraphicsWindow();
    void BuildControllerWindow();
    void BuildModsFoldersWindow();
    void PostModsFolders(ModsFoldersRequest request);
    void BuildGameSection();
    void PostFullscreenToggle();
    void PostController(ControllerRequest request);
    void PostResolutionScale(std::size_t index);
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
    UiActionChannel actions;
    PerfHudVisibility perf_hud;
    std::unique_ptr<PerfHudSampler> perf_hud_sampler; ///< Created in Initialize.
    ResolutionScaleApplier resolution_scale_applier; ///< Event thread only.
    std::atomic<std::size_t> resolution_scale_index{0};
    ControllerApplier controller_applier; ///< Event thread only.
    FullscreenToggler fullscreen_toggler;   ///< Event thread only.
    std::atomic<bool> window_fullscreen{false};
    ModsFoldersApplier mods_folders_applier; ///< Event thread only.
    SnapshotBox<ModsFoldersSnapshot> mods_folders_snapshot;
    SnapshotBox<ControllerSnapshot> controller_snapshot;
    std::atomic<bool> controller_window_open{false};
    std::vector<UiStatusMessage> status_messages; ///< Scratch for the per-frame status drain.
    std::vector<UiStatusMessage> recent_status;   ///< Newest last; worker thread only.
    std::vector<OverlayInputEvent> pending_input; ///< Scratch for ApplyQueuedInput.
    std::atomic<bool> hardware_cursor_hidden{false};

    // UI state, touched by the worker thread only.
    bool command_bar_visible{};
    float command_bar_height_px{};
    bool show_debug_window{};
    bool show_graphics_window{};
    OverlayRect graphics_window_rect{};
    bool show_controller_window{};
    bool show_mods_folders_window{};
    bool mods_folders_window_was_open{};
    OverlayRect mods_folders_window_rect{};
    OverlayRect controller_window_rect{};
    std::string controller_device_id; ///< Empty until the player picks one.
    OverlayRect debug_window_rect{}; ///< Where the Debug window was drawn this frame.
    FrameTimeHistory frame_times;
    u32 target_width_px{};
    u32 target_height_px{};
};

} // namespace SuyuCmd
