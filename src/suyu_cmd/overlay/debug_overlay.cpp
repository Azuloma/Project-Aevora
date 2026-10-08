// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "common/logging.h"
#include "suyu_cmd/overlay/debug_overlay.h"

namespace SuyuCmd {

namespace {

// The backend creates one sampled-image descriptor per font atlas texture; this is the library's
// documented minimum with headroom for atlas regrowth.
constexpr u32 kDescriptorPoolSize = 16;
// Frames the backend may still have in flight beyond the swapchain images.
constexpr u32 kExtraFramesInFlight = 1;
// Longest frame gap fed to ImGui; a stall must not turn into a huge animation step.
constexpr float kMaxDeltaTimeSeconds = 0.25f;
constexpr float kPlaceholderDeltaTimeSeconds = 1.0f / 60.0f;

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
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

DebugOverlay::DebugOverlay() = default;

DebugOverlay::~DebugOverlay() {
    Shutdown();
}

bool DebugOverlay::Initialize(const Core::Frontend::VulkanOverlayContext& new_context) {
    Shutdown();
    context = new_context;
    failed = false;
    vulkan_error_seen = false;

    if (!ImGui_ImplVulkan_LoadFunctions(context.api_version, LoadVulkanFunction, &context)) {
        LOG_ERROR(Frontend, "Debug overlay could not load the Vulkan entry points ImGui needs");
        return false;
    }

    ImGui::CreateContext();
    context_created = true;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // Input arrives in a later stage; until then the overlay is display-only.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
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
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const float delta_seconds = std::chrono::duration<float>(now - last_frame_time).count();
    last_frame_time = now;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = delta_seconds > 0.0f ? std::min(delta_seconds, kMaxDeltaTimeSeconds)
                                        : kPlaceholderDeltaTimeSeconds;

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    BuildUi();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    static_cast<VkCommandBuffer>(command_buffer));
    if (vulkan_error_seen) {
        LOG_ERROR(Frontend, "Debug overlay disabled: a Vulkan call failed while drawing");
        failed = true;
    }
}

void DebugOverlay::BuildUi() {
    constexpr ImGuiWindowFlags kStatusFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    constexpr float kMargin = 8.0f;

    ImGui::SetNextWindowPos(ImVec2(kMargin, kMargin));
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("Aevora overlay status", nullptr, kStatusFlags)) {
        ImGui::Text("Debug overlay  %.1f FPS", ImGui::GetIO().Framerate);
    }
    ImGui::End();
}

} // namespace SuyuCmd
