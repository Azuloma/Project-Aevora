// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <memory>
#include <string>

#ifdef SUYU_CMD_STATIC_RECOMP
#include <cstdint>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#include <fmt/format.h>

#include "common/fs/fs_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "suyu_cmd/emu_window/emu_window_sdl2_vk.h"
#include "suyu_cmd/overlay/debug_overlay.h"
#include "suyu_cmd/resolution_scale_host.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"

#include <SDL3/SDL.h>

EmuWindow_SDL2_VK::EmuWindow_SDL2_VK(InputCommon::InputSubsystem* input_subsystem_,
                                     Core::System& system_, bool fullscreen)
    : EmuWindow_SDL2{input_subsystem_, system_} {
    debug_overlay = SuyuCmd::AcquireDebugOverlay();
    if (debug_overlay) {
        debug_overlay->GetActions().BindEventThread();
        InstallOverlayResolutionScale();
        InstallOverlayController();
        InstallOverlayModsFolders();
    }
#ifdef SUYU_CMD_STATIC_RECOMP
    // Exports are renamed after the game at packaging time; reuse that name as the title.
    const std::string window_title = [] {
        std::filesystem::path exe;
#if defined(_WIN32)
        wchar_t exe_w[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe_w, MAX_PATH);
        exe = exe_w;
#elif defined(__APPLE__)
        // Ask for the length first; the answer can exceed PATH_MAX once
        // symlinks and bundle nesting are in play.
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer;
        buffer.resize(size);
        if (size != 0 && _NSGetExecutablePath(buffer.data(), &size) == 0) {
            exe = buffer.c_str();
        }
#else
        std::error_code ec;
        exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (ec) {
            exe.clear();
        }
#endif
        // A generic name beats an empty title; path::string() throws on unmappable ANSI text.
        return exe.empty() ? std::string{"suyu"} : Common::FS::PathToUTF8String(exe.stem());
    }();
#else
    const std::string window_title = fmt::format("suyu {} | {}-{} (Vulkan)", Common::g_build_name,
                                                 Common::g_scm_branch, Common::g_scm_desc);
#endif
    // Outside an .app bundle macOS treats the binary as a background app; request foreground
    // before the window exists so the later raise works.
    const bool headless_capture = std::getenv("SUYU_CMD_CAPTURE_HEADLESS") != nullptr;
    if (!headless_capture) {
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
    }

    auto window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (headless_capture) {
        window_flags |= SDL_WINDOW_HIDDEN;
    }

    render_window =
        SDL_CreateWindow(window_title.c_str(),
                         Layout::ScreenUndocked::Width, Layout::ScreenUndocked::Height,
                         window_flags);

    if (render_window == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create SDL3 window: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }

    SetWindowIcon();

    if (fullscreen) {
        Fullscreen();
        ShowCursor(false);
        if (debug_overlay) {
            debug_overlay->SetHardwareCursorHidden(true);
        }
    }

    // Retrieve native window handles via SDL3 property system
    SDL_PropertiesID props = SDL_GetWindowProperties(render_window);

#if defined(SDL_PLATFORM_WIN32)
    window_info.type = Core::Frontend::WindowSystemType::Windows;
    window_info.render_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!window_info.render_surface) {
        LOG_CRITICAL(Frontend, "Failed to get Win32 HWND from window properties: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_FREEBSD)
    {
        const char* driver = SDL_GetCurrentVideoDriver();
        if (driver && SDL_strcmp(driver, "x11") == 0) {
            window_info.type = Core::Frontend::WindowSystemType::X11;
            window_info.display_connection = SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            window_info.render_surface = reinterpret_cast<void*>(
                SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        } else if (driver && SDL_strcmp(driver, "wayland") == 0) {
            window_info.type = Core::Frontend::WindowSystemType::Wayland;
            window_info.display_connection = SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            window_info.render_surface = SDL_GetPointerProperty(
                props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        } else {
            LOG_CRITICAL(Frontend, "Unsupported video driver: {}", driver ? driver : "(null)");
            std::exit(EXIT_FAILURE);
        }
    }
#elif defined(SDL_PLATFORM_MACOS)
    window_info.type = Core::Frontend::WindowSystemType::Cocoa;
    // VK_EXT_metal_surface takes a CAMetalLayer. The view SDL_Metal_CreateView returns is an
    // NSView, and MoltenVK raises an Objective-C exception for anything that is not a layer.
    metal_view = SDL_Metal_CreateView(render_window);
    window_info.render_surface = metal_view ? SDL_Metal_GetLayer(metal_view) : nullptr;
    if (window_info.render_surface == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to get the CAMetalLayer for the window: {}", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }
#elif defined(SDL_PLATFORM_ANDROID)
    window_info.type = Core::Frontend::WindowSystemType::Android;
    window_info.render_surface = SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
#else
    LOG_CRITICAL(Frontend, "Window manager subsystem not implemented for this platform");
    std::exit(EXIT_FAILURE);
#endif
    (void)props;

    // Diagnostic captures can run without entering the user's active desktop.
    // The Vulkan surface still exists and the renderer supplies frame captures.
    if (!headless_capture) {
        SDL_ShowWindow(render_window);
        // Showing a window does not focus it; a non-frontmost macOS window is throttled.
        SDL_RaiseWindow(render_window);
    }
    OnResize();
    OnMinimalClientAreaChangeRequest(GetActiveConfig().min_client_area_size);
    SDL_PumpEvents();
    LOG_INFO(Frontend, "suyu Version: {} | {}-{} (Vulkan)", Common::g_build_name,
             Common::g_scm_branch, Common::g_scm_desc);
}

namespace {

// SDL numbers buttons left, middle, right, back, forward from 1; ImGui numbers them left,
// right, middle, back, forward from 0.
std::optional<std::uint8_t> ToOverlayButton(Uint8 sdl_button) {
    switch (sdl_button) {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    case SDL_BUTTON_X1:
        return 3;
    case SDL_BUTTON_X2:
        return 4;
    default:
        return std::nullopt;
    }
}

} // namespace

bool EmuWindow_SDL2_VK::RouteOverlayEvent(const SDL_Event& event) {
    if (!debug_overlay) {
        return false;
    }
    using Kind = SuyuCmd::OverlayInputEvent::Kind;
    SuyuCmd::OverlayInputRouter& router = debug_overlay->GetInputRouter();

    // The overlay lays itself out in framebuffer pixels; SDL reports window coordinates.
    const float density = SDL_GetWindowPixelDensity(render_window);
    const auto at = [&](Kind kind, float window_x, float window_y, std::uint8_t button = 0,
                        bool down = false) {
        return SuyuCmd::OverlayInputEvent{kind, window_x * density, window_y * density, button,
                                          down};
    };

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.which == SDL_TOUCH_MOUSEID) {
            return false;
        }
        return router.Route(at(Kind::MouseMove, event.motion.x, event.motion.y));
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const std::optional<std::uint8_t> button = ToOverlayButton(event.button.button);
        if (event.button.which == SDL_TOUCH_MOUSEID || !button) {
            return false;
        }
        return router.Route(
            at(Kind::MouseButton, event.button.x, event.button.y, *button, event.button.down));
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.which == SDL_TOUCH_MOUSEID) {
            return false;
        }
        return router.Route({Kind::MouseWheel, event.wheel.x, event.wheel.y, 0, false});
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return router.RouteKey(static_cast<std::uint16_t>(event.key.scancode), event.key.down,
                               event.key.repeat);
    case SDL_EVENT_TEXT_INPUT:
        return event.text.text != nullptr && router.RouteText(event.text.text);
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        router.Route({Kind::MouseLeave, 0.0f, 0.0f, 0, false});
        return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
        // No release may ever arrive for a button held across this; drop what the overlay holds.
        router.Route({Kind::Reset, 0.0f, 0.0f, 0, false});
        return false;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        router.Route({Kind::FocusGained, 0.0f, 0.0f, 0, false});
        return false;
    default:
        return false;
    }
}

void EmuWindow_SDL2_VK::InstallOverlayResolutionScale() {
    const SuyuCmd::ResolutionScaleHooks hooks = SuyuCmd::MakeResolutionScaleHooks();
    debug_overlay->SetResolutionScaleApplier([hooks](std::size_t index) {
        return SuyuCmd::ApplyResolutionScale(hooks, index);
    });
    PublishOverlayResolutionScale();
}

void EmuWindow_SDL2_VK::InstallOverlayModsFolders() {
    mods_scanner = std::make_unique<SuyuCmd::ModsScanner>();
    debug_overlay->SetModsFoldersApplier([this](const SuyuCmd::ModsFoldersRequest& request) {
        if (request.kind == SuyuCmd::ModsFoldersRequest::Kind::OpenFolder) {
            return OpenOverlayFolder(request.folder);
        }
        const SuyuCmd::FolderPaths paths = OverlayFolderPaths();
        SuyuCmd::ModsFoldersSnapshot snapshot;
        snapshot.user_folder = SuyuCmd::Utf8(paths.user);
        snapshot.mods_folder = SuyuCmd::Utf8(paths.mods);
        snapshot.keys_folder = SuyuCmd::Utf8(paths.keys);
        SuyuCmd::DebugOverlay* const overlay = debug_overlay;
        overlay->GetModsFoldersSnapshot().Publish(snapshot);
        mods_scanner->Request(paths.mods, [overlay, snapshot](SuyuCmd::ModsScan scan) mutable {
            snapshot.scan = std::move(scan);
            snapshot.scanned = true;
            overlay->GetModsFoldersSnapshot().Publish(std::move(snapshot));
        });
        return SuyuCmd::UiActionResult{};
    });
}

void EmuWindow_SDL2_VK::InstallOverlayController() {
    debug_overlay->SetControllerApplier([this](const SuyuCmd::ControllerRequest& request) {
        return HandleControllerRequest(request);
    });
}

void EmuWindow_SDL2_VK::ServiceOverlayController(bool actions_ran) {
    constexpr std::uint64_t kIdleRefreshMs = 250;
    constexpr std::uint64_t kRebindRefreshMs = 100;
    if (const auto outcome = PollControllerRebind()) {
        controller_dirty = true;
        if (!outcome->ok || !outcome->message.empty()) {
            debug_overlay->GetActions().PublishStatus({outcome->ok, outcome->message});
        }
    }
    controller_dirty = controller_dirty || actions_ran;
    if (!debug_overlay->ControllerWindowOpen()) {
        return;
    }
    const std::uint64_t now_ms = SDL_GetTicks();
    const bool rebinding = rebind_session && rebind_session->Active();
    const std::uint64_t refresh_ms = rebinding ? kRebindRefreshMs : kIdleRefreshMs;
    if (!controller_dirty && now_ms - last_controller_publish_ms < refresh_ms) {
        return;
    }
    debug_overlay->GetControllerSnapshot().Publish(BuildControllerSnapshot());
    controller_dirty = false;
    last_controller_publish_ms = now_ms;
}

void EmuWindow_SDL2_VK::PublishOverlayResolutionScale() {
    const auto index = Settings::FindResolutionScaleIndex(
        Settings::values.resolution_setup.GetValue());
    debug_overlay->PublishResolutionScaleIndex(index.value_or(0));
}

void EmuWindow_SDL2_VK::RunOverlayActions() {
    if (debug_overlay) {
        // Also catches changes made elsewhere, such as the F12 panel.
        PublishOverlayResolutionScale();
        const std::size_t ran = debug_overlay->GetActions().RunPending();
        PublishOverlayResolutionScale();
        ServiceOverlayController(ran > 0);
    }
}

void EmuWindow_SDL2_VK::SyncOverlayTextInput() {
    if (!debug_overlay) {
        return;
    }
    const bool wanted = debug_overlay->GetInputRouter().TextInputWanted();
    if (wanted == SDL_TextInputActive(render_window)) {
        return;
    }
    const bool changed = wanted ? SDL_StartTextInput(render_window)
                                : SDL_StopTextInput(render_window);
    if (!changed) {
        LOG_ERROR(Frontend, "Overlay text input {} failed: {}", wanted ? "start" : "stop",
                  SDL_GetError());
    }
}

Core::Frontend::VulkanOverlay* EmuWindow_SDL2_VK::GetVulkanOverlay() {
    return debug_overlay;
}

EmuWindow_SDL2_VK::~EmuWindow_SDL2_VK() {
    if (debug_overlay) {
        debug_overlay->SetResolutionScaleApplier({});
        debug_overlay->SetControllerApplier({});
        debug_overlay->SetModsFoldersApplier({});
        mods_scanner.reset();
        CancelControllerRebind();
        debug_overlay->GetActions().DetachEventThread();
    }
#if defined(SDL_PLATFORM_MACOS)
    if (metal_view) {
        SDL_Metal_DestroyView(metal_view);
        metal_view = nullptr;
    }
#endif
}

std::unique_ptr<Core::Frontend::GraphicsContext> EmuWindow_SDL2_VK::CreateSharedContext() const {
    return std::make_unique<DummyContext>();
}
