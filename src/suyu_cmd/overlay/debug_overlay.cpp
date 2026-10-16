// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "common/env_option.h"
#include "common/logging.h"
#include "common/resolution_scale_table.h"
#include "common/scm_rev.h"
#include "core/arm/recomp/arm_recomp.h"
#include "suyu_cmd/native_status.h"
#include "suyu_cmd/overlay/build_identity.h"
#include "suyu_cmd/overlay/debug_overlay.h"

namespace SuyuCmd {

namespace {

// The backend creates one sampled-image descriptor per font atlas texture; this is the library's
// documented minimum with headroom for atlas regrowth.
constexpr u32 kDescriptorPoolSize = 16;
// Frames the backend may still have in flight beyond the swapchain images.
constexpr u32 kExtraFramesInFlight = 1;
// Action outcomes kept for the settings windows to show.
constexpr std::size_t kRecentStatusCount = 8;

// Keys ImGui navigates and edits text with; every other scancode is ignored by the overlay.
ImGuiKey ToImGuiKey(std::uint16_t scancode) {
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return static_cast<ImGuiKey>(ImGuiKey_A + (scancode - SDL_SCANCODE_A));
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
        return static_cast<ImGuiKey>(ImGuiKey_1 + (scancode - SDL_SCANCODE_1));
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (scancode - SDL_SCANCODE_F1));
    }
    switch (scancode) {
    case SDL_SCANCODE_0: return ImGuiKey_0;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_INSERT: return ImGuiKey_Insert;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_LALT: return ImGuiKey_LeftAlt;
    case SDL_SCANCODE_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

// Ends the event thread's wait so it services the overlay now rather than at the next tick.
void WakeEventThread() {
    SDL_Event wake{};
    wake.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake);
}

// Set by the ImGui backend's error callback, which carries no user data. The backend keeps going
// after a failed call, so Record checks this at each stage and disables the overlay for good.
std::atomic<bool> vulkan_error_seen{false};

void LogVulkanResult(VkResult result) {
    if (result != VK_SUCCESS) {
        vulkan_error_seen = true;
        LOG_ERROR(Frontend, "Debug overlay Vulkan call failed: VkResult {}",
                  static_cast<int>(result));
    }
}

PFN_vkVoidFunction LoadVulkanFunction(const char* name, void* user_data) {
    const auto* context = static_cast<const Core::Frontend::VulkanOverlayContext*>(user_data);
    const auto get_proc_addr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(context->get_instance_proc_addr);
    return get_proc_addr(static_cast<VkInstance>(context->instance), name);
}

} // namespace

bool IsDebugOverlayRequested() {
    const char* value = std::getenv(kDebugOverlayEnvVar);
    return Common::EnvFlagDefaultOn(value != nullptr ? value : "");
}

DebugOverlay* AcquireDebugOverlay() {
    if (!IsDebugOverlayRequested()) {
        return nullptr;
    }
    static DebugOverlay* const overlay = new DebugOverlay;
    return overlay;
}

DebugOverlay::DebugOverlay() {
    actions.SetWakeup(WakeEventThread);
}

DebugOverlay::~DebugOverlay() {
    Shutdown();
}

bool DebugOverlay::Initialize(const Core::Frontend::VulkanOverlayContext& new_context) {
    Shutdown();
    context = new_context;
    failed = false;
    vulkan_error_seen = false;
    input_router.SetCaptureRegions({});
    input_router.SetKeyboardCapture(false, false);
    command_bar_visible = false;
    command_bar_height_px = 0.0f;
    show_debug_window = false;

    if (!ImGui_ImplVulkan_LoadFunctions(context.api_version, LoadVulkanFunction, &context)) {
        LOG_ERROR(Frontend, "Debug overlay could not load the Vulkan entry points ImGui needs");
        return false;
    }

    ImGui::CreateContext();
    context_created = true;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // The window owns the OS pointer; ImGui only draws its own while the window hides it.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();

    last_frame_time = std::chrono::steady_clock::now();
    LOG_INFO(Frontend, "Debug overlay initialized (ImGui {})", ImGui::GetVersion());
    return true;
}

void DebugOverlay::Shutdown() {
    if (backend_ready) {
        ImGui_ImplVulkan_Shutdown();
        backend_ready = false;
        backend_render_pass = 0;
    }
    if (context_created) {
        ImGui::DestroyContext();
        context_created = false;
    }
}

bool DebugOverlay::EnsureBackend(u64 render_pass) {
    const VkRenderPass vk_render_pass = reinterpret_cast<VkRenderPass>(render_pass);

    if (backend_ready) {
        if (render_pass == backend_render_pass) {
            return true;
        }
        // The presentation path was rebuilt (it waits for the GPU first), so the old pipeline is
        // no longer in use.
        ImGui_ImplVulkan_PipelineInfo pipeline_info{};
        pipeline_info.RenderPass = vk_render_pass;
        pipeline_info.Subpass = 0;
        ImGui_ImplVulkan_CreateMainPipeline(&pipeline_info);
        backend_render_pass = render_pass;
        return true;
    }

    const u32 min_image_count = std::max(context.image_count, 2u);
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = context.api_version;
    init_info.Instance = static_cast<VkInstance>(context.instance);
    init_info.PhysicalDevice = static_cast<VkPhysicalDevice>(context.physical_device);
    init_info.Device = static_cast<VkDevice>(context.device);
    init_info.QueueFamily = context.graphics_queue_family;
    init_info.Queue = static_cast<VkQueue>(context.graphics_queue);
    init_info.DescriptorPoolSize = kDescriptorPoolSize;
    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = min_image_count + kExtraFramesInFlight;
    init_info.PipelineInfoMain.RenderPass = vk_render_pass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.CheckVkResultFn = LogVulkanResult;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        LOG_ERROR(Frontend, "Debug overlay Vulkan backend failed to initialize");
        return false;
    }
    backend_ready = true;
    backend_render_pass = render_pass;
    return true;
}

void DebugOverlay::Record(void* command_buffer, u64 render_pass, u32 width, u32 height) {
    if (failed || !context_created || width == 0 || height == 0) {
        return;
    }
    if (!EnsureBackend(render_pass) || vulkan_error_seen) {
        LOG_ERROR(Frontend, "Debug overlay disabled: Vulkan backend setup failed");
        failed = true;
        input_router.SetCaptureRegions({});
        input_router.SetKeyboardCapture(false, false);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
    last_frame_time = now;
    // The measurement keeps long stalls as they were; only ImGui's animation step is clamped.
    const FrameTiming timing = TimeFrame(delta_seconds);
    if (timing.measurable) {
        frame_times.Add(timing.measured_ms);
    }
    target_width_px = width;
    target_height_px = height;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = timing.animation_delta_seconds;

    ApplyQueuedInput();
    CollectActionStatus();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    BuildUi();
    PublishCaptureRegions();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    static_cast<VkCommandBuffer>(command_buffer));
    if (vulkan_error_seen) {
        LOG_ERROR(Frontend, "Debug overlay disabled: a Vulkan call failed while drawing");
        failed = true;
        input_router.SetCaptureRegions({});
        input_router.SetKeyboardCapture(false, false);
    }
}

void DebugOverlay::ApplyQueuedInput() {
    ImGuiIO& io = ImGui::GetIO();
    input_router.Drain(pending_input);
    for (const OverlayInputEvent& event : pending_input) {
        switch (event.kind) {
        case OverlayInputEvent::Kind::MouseMove:
            io.AddMousePosEvent(event.x, event.y);
            break;
        case OverlayInputEvent::Kind::MouseButton:
            // The press says where it landed; the position may not have been queued before it.
            io.AddMousePosEvent(event.x, event.y);
            io.AddMouseButtonEvent(event.button, event.down);
            break;
        case OverlayInputEvent::Kind::MouseWheel:
            io.AddMouseWheelEvent(event.x, event.y);
            break;
        case OverlayInputEvent::Kind::MouseLeave:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        case OverlayInputEvent::Kind::Reset:
            // Cleared directly: a Reset followed by FocusGained in one batch would merge in ImGui.
            io.ClearInputKeys();
            io.ClearInputMouse();
            io.AddFocusEvent(false);
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        case OverlayInputEvent::Kind::FocusGained:
            io.AddFocusEvent(true);
            break;
        case OverlayInputEvent::Kind::KeyDown:
        case OverlayInputEvent::Kind::KeyUp:
            if (const ImGuiKey key = ToImGuiKey(event.key); key != ImGuiKey_None) {
                io.AddKeyEvent(key, event.kind == OverlayInputEvent::Kind::KeyDown);
            }
            break;
        case OverlayInputEvent::Kind::Text:
            io.AddInputCharactersUTF8(event.text);
            break;
        }
    }
}

void DebugOverlay::CollectActionStatus() {
    actions.DrainStatus(status_messages);
    for (UiStatusMessage& message : status_messages) {
        if (!message.ok) {
            LOG_ERROR(Frontend, "Overlay action: {}", message.text);
        }
        if (recent_status.size() >= kRecentStatusCount) {
            recent_status.erase(recent_status.begin());
        }
        recent_status.push_back(std::move(message));
    }
}

void DebugOverlay::BuildUi() {
    BuildCommandBar();
    BuildDebugWindow();
    BuildGraphicsWindow();
    BuildControllerWindow();

    // A hidden OS pointer would leave nothing to aim with; draw one only while it is over the UI.
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = hardware_cursor_hidden && (command_bar_visible || io.WantCaptureMouse);
}

void DebugOverlay::PublishCaptureRegions() {
    std::vector<OverlayRect> regions;
    if (command_bar_visible) {
        regions.push_back({0.0f, 0.0f, ImGui::GetIO().DisplaySize.x, command_bar_height_px});
    }
    if (show_debug_window) {
        regions.push_back(debug_window_rect);
    }
    if (show_graphics_window) {
        regions.push_back(graphics_window_rect);
    }
    if (show_controller_window) {
        regions.push_back(controller_window_rect);
    }
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        // An open menu or combo list lies outside the windows; keep clicks on it with the UI.
        regions.push_back({0.0f, 0.0f, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y});
    }
    // What was just built is what the event thread tests the next events against.
    input_router.SetCaptureRegions(std::move(regions));
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput != input_router.TextInputWanted()) {
        WakeEventThread();
    }
    input_router.SetKeyboardCapture(io.WantCaptureKeyboard, io.WantTextInput);
}

void DebugOverlay::BuildCommandBar() {
    const ImGuiIO& io = ImGui::GetIO();
    const CommandBarInput bar_input{
        .mouse_in_window = ImGui::IsMousePosValid(&io.MousePos),
        .mouse_y_px = io.MousePos.y,
        .was_visible = command_bar_visible,
        .bar_height_px = command_bar_height_px,
        .menu_open = ImGui::IsPopupOpen(
            "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
    };
    command_bar_visible = ShouldShowCommandBar(bar_input);
    if (!command_bar_visible) {
        return;
    }

    if (ImGui::BeginMainMenuBar()) {
        command_bar_height_px = ImGui::GetWindowHeight();
        if (ImGui::BeginMenu("Settings")) {
            ImGui::MenuItem("Graphics", nullptr, &show_graphics_window);
            ImGui::MenuItem("Controller (Player 1)", nullptr, &show_controller_window);
            ImGui::EndMenu();
        }
        // Placeholder: the entry exists so the layout is settled, but does nothing yet.
        ImGui::BeginDisabled();
        ImGui::MenuItem("Cheats");
        ImGui::EndDisabled();
        ImGui::MenuItem("Debug", nullptr, &show_debug_window);
        ImGui::EndMainMenuBar();
    }
}

void DebugOverlay::BuildDebugWindow() {
    if (!show_debug_window) {
        return;
    }
    constexpr ImVec2 kFirstPosition{16.0f, 48.0f};
    constexpr ImVec2 kFirstSize{320.0f, 120.0f};

    ImGui::SetNextWindowPos(kFirstPosition, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(kFirstSize, ImGuiCond_FirstUseEver);
    const bool expanded = ImGui::Begin("Debug", &show_debug_window);
    // Recorded even when collapsed: the title bar still takes clicks.
    debug_window_rect = {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                         ImGui::GetWindowSize().x, ImGui::GetWindowSize().y};
    if (expanded) {
        if (ImGui::CollapsingHeader("Backend", ImGuiTreeNodeFlags_DefaultOpen)) {
            BuildBackendSection();
        }
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            BuildPerformanceSection();
        }
        if (ImGui::CollapsingHeader("CPU", ImGuiTreeNodeFlags_DefaultOpen)) {
            BuildCpuSection();
        }
        if (ImGui::CollapsingHeader("Build identity")) {
            BuildIdentitySection();
        }
        if (ImGui::CollapsingHeader("GPU")) {
            BuildGpuSection();
        }
    }
    ImGui::End();
}

void DebugOverlay::BuildGraphicsWindow() {
    if (!show_graphics_window) {
        return;
    }
    ImGui::SetNextWindowPos({16.0f, 48.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({420.0f, 150.0f}, ImGuiCond_FirstUseEver);
    const bool expanded = ImGui::Begin("Graphics", &show_graphics_window);
    graphics_window_rect = {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                            ImGui::GetWindowSize().x, ImGui::GetWindowSize().y};
    if (expanded) {
        const std::size_t current = resolution_scale_index.load(std::memory_order_relaxed);
        const auto& entries = Settings::kResolutionScaleEntries;
        const char* const preview = entries[std::min(current, entries.size() - 1)].label;
        if (ImGui::BeginCombo("Resolution Scale", preview)) {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (ImGui::Selectable(entries[i].label, i == current)) {
                    PostResolutionScale(i);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Takes full effect the next time the game is started.");
        if (!recent_status.empty()) {
            const UiStatusMessage& latest = recent_status.back();
            ImGui::TextWrapped("%s", latest.text.c_str());
        }
    }
    ImGui::End();
}

void DebugOverlay::BuildControllerWindow() {
    const bool was_open = controller_window_open.load(std::memory_order_relaxed);
    controller_window_open.store(show_controller_window, std::memory_order_relaxed);
    if (!show_controller_window) {
        if (was_open) {
            PostController({ControllerRequest::Kind::CancelRebind, 0, {}});
        }
        return;
    }
    using Kind = ControllerRequest::Kind;
    ImGui::SetNextWindowPos({16.0f, 48.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({520.0f, 560.0f}, ImGuiCond_FirstUseEver);
    const bool expanded = ImGui::Begin("Controller (Player 1)", &show_controller_window);
    controller_window_rect = {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                              ImGui::GetWindowSize().x, ImGui::GetWindowSize().y};
    if (expanded) {
        const ControllerSnapshot view = controller_snapshot.Get();
        if (view.devices.empty()) {
            ImGui::TextWrapped("No controller detected - plug one in.");
        } else {
            const auto chosen = FindDevice(view, controller_device_id);
            const char* const preview =
                chosen ? view.devices[*chosen].name.c_str() : "Select a controller";
            if (ImGui::BeginCombo("Device", preview)) {
                for (std::size_t i = 0; i < view.devices.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(view.devices[i].name.c_str(), chosen == i)) {
                        controller_device_id = view.devices[i].id;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(!chosen);
            if (ImGui::Button("Map to Player 1") && chosen) {
                PostController({Kind::MapDevice, 0, view.devices[*chosen].id});
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Use keyboard")) {
            PostController({Kind::UseKeyboard, 0, {}});
        }
        if (view.can_combine && ImGui::Button("Combine Joy-Cons")) {
            PostController({Kind::CombineJoycons, 0, {}});
        }
        if (view.can_split && ImGui::Button("Split Joy-Cons")) {
            PostController({Kind::SplitJoycons, 0, {}});
        }
        ImGui::Separator();
        if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Bound to", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            for (std::size_t row = 0; row < view.rows.size(); ++row) {
                const bool capturing = view.rebinding && view.rebind_row == row;
                ImGui::PushID(static_cast<int>(row));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(view.rows[row].label.c_str());
                ImGui::TableNextColumn();
                if (capturing) {
                    ImGui::Text("Press an input... (%llu s)",
                                static_cast<unsigned long long>(view.rebind_remaining_ms / 1000 + 1));
                } else {
                    ImGui::TextUnformatted(view.rows[row].shown.c_str());
                }
                ImGui::TableNextColumn();
                if (capturing) {
                    if (ImGui::Button("Cancel")) {
                        PostController({Kind::CancelRebind, 0, {}});
                    }
                } else {
                    if (ImGui::Button("Rebind")) {
                        PostController({Kind::BeginRebind, row, {}});
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) {
                        PostController({Kind::ClearBinding, row, {}});
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!recent_status.empty()) {
            ImGui::TextWrapped("%s", recent_status.back().text.c_str());
        }
    }
    ImGui::End();
}

void DebugOverlay::PostController(ControllerRequest request) {
    actions.Post({"Controller", [this, request = std::move(request)] {
                      if (!controller_applier) {
                          return UiActionResult{false, "Controller settings are not available"};
                      }
                      return controller_applier(request);
                  }});
}

void DebugOverlay::PostResolutionScale(std::size_t index) {
    actions.Post({"Resolution Scale", [this, index] {
                      if (!resolution_scale_applier) {
                          return UiActionResult{false, "Resolution Scale is not available"};
                      }
                      return resolution_scale_applier(index);
                  }});
}

void DebugOverlay::BuildBackendSection() {
    constexpr ImVec4 kWarningColor{1.0f, 0.35f, 0.3f, 1.0f};
    constexpr ImVec4 kOkColor{0.45f, 0.9f, 0.5f, 1.0f};

    NativeStatusSnapshot snapshot;
    const bool have_snapshot = TryGetNativeStatusSnapshot(snapshot);
    const Core::RecompExecutionStats stats = Core::GetRecompExecutionStats();
    const Core::RecompLiveStats live = Core::GetRecompLiveStats();

    if (have_snapshot) {
        ImGui::Text("Backend: %s%s", NativeBackendName(ClassifyGameBackend(snapshot)),
                    GameBackendQualifier(snapshot).c_str());
    } else {
        ImGui::TextUnformatted("Backend: (no status yet)");
    }
    ImGui::Text("Mode: %s", have_snapshot ? ExecutionModeText(ClassifyGameBackend(snapshot))
                                          : "(no status yet)");
    ImGui::Text("JIT in this build: %s", live.jit_available ? "yes" : "no");
    ImGui::Text("Static blocks run: %llu", static_cast<unsigned long long>(stats.blocks));
    ImGui::Text("SVC calls: %llu", static_cast<unsigned long long>(stats.svc_calls));
    // These count attempts to leave recompiled code, not handoffs that completed.
    ImGui::Text("Fallback attempts: %llu", static_cast<unsigned long long>(live.jit_transitions));
    ImGui::Text("  Lookup misses: %llu", static_cast<unsigned long long>(stats.lookup_misses));
    ImGui::Text("  Unhandled: %llu", static_cast<unsigned long long>(stats.unhandled));
    ImGui::Text("Refused (no fallback available): %llu",
                static_cast<unsigned long long>(stats.no_fallback));

    const FallbackCounters counters{live.jit_transitions, stats.lookup_misses, stats.unhandled,
                                    stats.no_fallback};
    switch (ClassifyFallback(counters)) {
    case FallbackVerdict::Refused:
        ImGui::TextColored(kWarningColor,
                           "Uncovered code reached; fallback refused, execution could not continue.");
        break;
    case FallbackVerdict::Attempted:
        ImGui::TextColored(kWarningColor,
                           "Uncovered code reached; a JIT fallback was attempted: not fully static.");
        break;
    case FallbackVerdict::None:
        // Zero is only "not yet" in hybrid, and says nothing about a JIT-only run.
        ImGui::TextColored(kOkColor, "No fallback attempt so far (does not prove static execution).");
        break;
    }
}

void DebugOverlay::BuildPerformanceSection() {
    NativeStatusSnapshot snapshot;
    if (TryGetNativeStatusSnapshot(snapshot) && snapshot.perf_available) {
        ImGui::Text("Game: %.1f FPS (%.2f ms)", snapshot.average_game_fps, snapshot.frametime_ms);
        ImGui::Text("System: %.1f FPS", snapshot.system_fps);
        if (snapshot.speed_meaningful) {
            ImGui::Text("Emulation speed: %.0f%%", snapshot.emulation_speed * 100.0);
        }
    } else {
        ImGui::TextUnformatted("Game: (no sample yet)");
    }

    // The presented-frame interval, as the overlay itself sees it, over the frames graphed below.
    const float average_ms = frame_times.Average();
    ImGui::Text("Presented: %.1f FPS", average_ms > 0.0f ? 1000.0f / average_ms : 0.0f);
    char summary[64];
    std::snprintf(summary, sizeof(summary), "avg %.1f ms  max %.1f ms", average_ms,
                  frame_times.Max());
    ImGui::PlotLines("##frame_times", frame_times.Values(),
                     static_cast<int>(frame_times.Count()), frame_times.Offset(), summary, 0.0f,
                     FLT_MAX, ImVec2(0.0f, 60.0f));
}

void DebugOverlay::BuildCpuSection() {
    const std::array<u64, 4> pcs = Core::GetRecompCurrentPcs();
    const std::map<u64, std::string> modules = Core::GetRecompModules();

    // The backend records a PC at each recompiled dispatch and never clears it, so these are
    // last-seen values: a core that has stopped keeps its old one, and a JIT-only run shows none.
    ImGui::TextUnformatted("Guest PC at each core's last recompiled dispatch:");
    for (std::size_t core = 0; core < pcs.size(); ++core) {
        if (pcs[core] == 0) {
            ImGui::Text("Core %zu: no recompiled dispatch recorded", core);
            continue;
        }
        const GuestLocation location = ResolveGuestPc(pcs[core], modules);
        if (location.found) {
            ImGui::Text("Core %zu: 0x%llx  near %s+0x%llx", core,
                        static_cast<unsigned long long>(pcs[core]), location.module.c_str(),
                        static_cast<unsigned long long>(location.offset));
        } else {
            ImGui::Text("Core %zu: 0x%llx  (below the lowest known module)", core,
                        static_cast<unsigned long long>(pcs[core]));
        }
    }
    ImGui::TextWrapped("\"near\" is the nearest module base below the address; module sizes are "
                       "not known, so it is not a range check. These are guest addresses, not "
                       "host DLL RVAs.");
}

void DebugOverlay::BuildIdentitySection() {
    ImGui::Text("Launcher: %s", Common::g_scm_rev);
    ImGui::Text("Branch: %s", Common::g_scm_branch);
    ImGui::Text("Version: %s", Common::g_build_version);

    BuildIdentity identity;
    if (!TryGetBuildIdentity(identity)) {
        ImGui::TextUnformatted("Export manifest: not found (not an exported package)");
        return;
    }
    const auto text = [](const std::string& value) {
        return value.empty() ? "(not recorded)" : value.c_str();
    };
    const auto flag = [](const std::optional<bool>& value) {
        return !value ? "(not recorded)" : *value ? "on" : "off";
    };
    ImGui::Text("Export backend: %s", text(identity.effective_backend));
    ImGui::Text("Correctness revision: %s", text(identity.correctness_revision));
    ImGui::Text("Bundle id: %s", text(identity.bundle_id));
    if (identity.image_abi) {
        ImGui::Text("Image ABI: %llu", static_cast<unsigned long long>(*identity.image_abi));
    } else {
        ImGui::TextUnformatted("Image ABI: (not recorded)");
    }
    if (identity.image_features) {
        ImGui::Text("Image features: 0x%llx",
                    static_cast<unsigned long long>(*identity.image_features));
    } else {
        ImGui::TextUnformatted("Image features: (not recorded)");
    }
    ImGui::Text("Inline fast memory: %s", flag(identity.fm_inline));
    ImGui::Text("Shared exact FP: %s", flag(identity.fp_shared));
    ImGui::Text("Direct chaining: %s", flag(identity.direct_chain));
    ImGui::Text("Indirect chaining: %s", flag(identity.indirect_chain));
    ImGui::Text("Fallback enabled: %s", flag(identity.fallback_enabled));
    for (const auto& [module, binding] : identity.module_bindings) {
        ImGui::Text("Module %s: %s", module.c_str(), text(binding));
    }
}

void DebugOverlay::BuildGpuSection() {
    NativeStatusSnapshot snapshot;
    if (TryGetNativeStatusSnapshot(snapshot)) {
        ImGui::Text("Shaders building: %d", snapshot.shaders_building);
    }
    ImGui::Text("Presentation target: %u x %u", target_width_px, target_height_px);
}

} // namespace SuyuCmd
