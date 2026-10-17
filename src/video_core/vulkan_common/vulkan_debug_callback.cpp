// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string_view>
#include "common/logging.h"
#include "common/settings.h"
#include "video_core/vulkan_common/vulkan_debug_callback.h"
#include "video_core/gpu_logging/gpu_logging.h"

namespace Vulkan {
namespace {

// Helper to get message type as string for GPU logging
const char* GetMessageTypeName(VkDebugUtilsMessageTypeFlagsEXT type) {
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        return "Validation";
    } else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        return "Performance";
    } else {
        return "General";
    }
}

VkBool32 DebugUtilCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                           VkDebugUtilsMessageTypeFlagsEXT type,
                           const VkDebugUtilsMessengerCallbackDataEXT* data,
                           [[maybe_unused]] void* user_data) {
    // Skip logging known false-positive validation errors
    switch (static_cast<u32>(data->messageIdNumber)) {
    case 0x682a878au: // VUID-vkCmdBindVertexBuffers2EXT-pBuffers-parameter
    case 0x99fb7dfdu: // UNASSIGNED-RequiredParameter (vkCmdBindVertexBuffers2EXT pBuffers[0])
    case 0xe8616bf2u: // Bound VkDescriptorSet 0x0[] was destroyed. Likely push_descriptor related
    case 0x1608dec0u: // Image layout in vkUpdateDescriptorSet doesn't match descriptor use
    case 0x55362756u: // Descriptor binding and framebuffer attachment overlap
        return VK_FALSE;
    default:
        break;
    }
    const std::string_view message{data->pMessage};
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_CRITICAL(Render_Vulkan, "{}", message);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARNING(Render_Vulkan, "{}", message);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        LOG_INFO(Render_Vulkan, "{}", message);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        LOG_DEBUG(Render_Vulkan, "{}", message);
    }

    // Route to GPU logger for tracking Vulkan validation messages
    if (GPU::Logging::IsActive() &&
        Settings::values.gpu_log_vulkan_calls.GetValue()) {
        // Convert severity to result code for logging (negative = error)
        int result_code = 0;
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            result_code = -1;
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            result_code = -2;
        }

        // Get message ID name or use generic name
        const char* call_name = data->pMessageIdName ? data->pMessageIdName : "VulkanDebug";

        GPU::Logging::GPULogger::GetInstance().LogVulkanCall(
            call_name,
            std::string(GetMessageTypeName(type)) + ": " + std::string(message),
            result_code
        );
    }

    return VK_FALSE;
}

} // Anonymous namespace

vk::DebugUtilsMessenger CreateDebugUtilsCallback(const vk::Instance& instance) {
    return instance.CreateDebugUtilsMessenger(VkDebugUtilsMessengerCreateInfoEXT{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = DebugUtilCallback,
        .pUserData = nullptr,
    });
}

} // namespace Vulkan
