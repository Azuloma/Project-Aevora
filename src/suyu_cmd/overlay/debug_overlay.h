// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>

#include "core/frontend/vulkan_overlay.h"

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

private:
    /// Creates the Vulkan backend for `render_pass`, or rebuilds its pipeline if the pass changed.
    bool EnsureBackend(u64 render_pass);
    void BuildUi();

    Core::Frontend::VulkanOverlayContext context{};
    bool context_created{};
    bool backend_ready{};
    bool failed{};
    u64 backend_render_pass{};
    std::chrono::steady_clock::time_point last_frame_time{};
};

} // namespace SuyuCmd
