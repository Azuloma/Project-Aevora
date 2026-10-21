// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#include <SDL3/SDL.h>
// SDL3 removed these constants; define compat shims
static constexpr Uint8 SDL_PRESSED = 1;
static constexpr Uint8 SDL_RELEASED = 0;

#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/keyboard.h"
#include "input_common/drivers/mouse.h"
#include "input_common/drivers/tas_input.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/main.h"
#include "common/param_package.h"
#include "common/settings_input.h"
#include "suyu_cmd/controller_assign.h"
#include "suyu_cmd/emu_window/emu_window_sdl2.h"
#include "suyu_cmd/native_status.h"
#include "suyu_cmd/sdl_config.h"
#include "suyu_cmd/suyu_icon.h"

namespace {
constexpr u64 kStatusRefreshMs = 750;
constexpr u64 kControlsSaveMs = 2000;
constexpr Sint32 kEventWaitSliceMs = 250;

constexpr SuyuCmd::FullscreenKeys kFullscreenKeys{SDL_SCANCODE_F11,      SDL_SCANCODE_RETURN,
                                                  SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_F12,
                                                  SDL_SCANCODE_F9,       SDL_SCANCODE_F10};

// Gamepads SDL recognises plus suyu's own Joy-Con / Pro Controller driver; wheels and virtual
// devices take no player. @p sdl_gamepads, when given, gets how many gamepads SDL sees.
std::vector<Common::ParamPackage> ControllerPads(const std::vector<Common::ParamPackage>& devices,
                                                 std::size_t* sdl_gamepads = nullptr) {
    std::set<std::string> gamepad_guids;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) {
        SDL_GUID guid = SDL_GetJoystickGUIDForID(ids[i]);
        guid.data[2] = guid.data[3] = 0;
        char text[33]{};
        SDL_GUIDToString(guid, text, sizeof(text));
        gamepad_guids.insert(text);
    }
    SDL_free(ids);
    if (sdl_gamepads) {
        *sdl_gamepads = gamepad_guids.size();
    }
    std::vector<Common::ParamPackage> pads;
    for (const auto& device : devices) {
        const std::string engine = device.Get("engine", "");
        if ((engine == "sdl" && gamepad_guids.contains(device.Get("guid", ""))) ||
            engine == "joycon") {
            pads.push_back(device);
        }
    }
    return pads;
}

// The input backend's own default mapping for a controller, as the settings screen's
// auto-map writes it; for gamepads it includes Home and, where the pad has one, Capture.
void MapDefault(InputCommon::InputSubsystem& input, const Common::ParamPackage& pad,
                Settings::PlayerInput& player) {
    for (const auto& [button, param] : input.GetButtonMappingForDevice(pad)) {
        player.buttons[button] = param.Serialize();
    }
    for (const auto& [analog, param] : input.GetAnalogMappingForDevice(pad)) {
        player.analogs[analog] = param.Serialize();
    }
    for (const auto& [motion, param] : input.GetMotionMappingForDevice(pad)) {
        player.motions[motion] = param.Serialize();
    }
}

constexpr SuyuCmd::BindingLayout kBindingLayout{Settings::NativeButton::NumButtons,
                                                Settings::NativeAnalog::NumAnalogs};

// The layout the emulator ships as its keyboard default.
void ApplyDefaultKeyboardLayout(Settings::PlayerInput& player) {
    static constexpr std::array<int, Settings::NativeButton::NumButtons> kButtons = {
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_Z, SDL_SCANCODE_X,
        SDL_SCANCODE_T, SDL_SCANCODE_G, SDL_SCANCODE_F, SDL_SCANCODE_H,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_M, SDL_SCANCODE_N,
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_B,
    };
    static constexpr std::array<std::array<int, 4>, Settings::NativeAnalog::NumAnalogs> kAnalogs{{
        {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT},
        {SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L},
    }};
    for (std::size_t i = 0; i < kButtons.size() && i < player.buttons.size(); ++i) {
        player.buttons[i] = InputCommon::GenerateKeyboardParam(kButtons[i]);
    }
    for (std::size_t i = 0; i < kAnalogs.size() && i < player.analogs.size(); ++i) {
        player.analogs[i] = InputCommon::GenerateAnalogParamFromKeys(
            kAnalogs[i][0], kAnalogs[i][1], kAnalogs[i][2], kAnalogs[i][3], 0, 0.5f);
    }
}

// A choice made by the player: auto-assign leaves the controls alone from here on.
void CommitPlayerChoice(Core::System& system) {
    if (g_export_package) {
        SdlConfig::auto_assign_controllers = false;
    }
    SaveNativeControls();
    system.HIDCore().ReloadInputDevices();
}

SuyuCmd::Player1Hooks MakePlayer1Hooks(Core::System& system) {
    return {
        .set =
            [](bool analog, std::size_t index, const std::string& value) {
                auto& player = Settings::values.players.GetValue()[0];
                (analog ? player.analogs[index] : player.buttons[index]) = value;
            },
        .mark_connected = [] { Settings::values.players.GetValue()[0].connected = true; },
        .commit = [&system] { CommitPlayerChoice(system); },
    };
}

std::string ShownBinding(const std::string& param) {
    if (param.empty()) {
        return "(unset)";
    }
    std::string shown = Common::ParamPackage{param}.Get("display", param);
    if (shown.size() > 60) {
        shown.resize(60);
    }
    return shown;
}
} // namespace

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

EmuWindow_SDL2::EmuWindow_SDL2(InputCommon::InputSubsystem* input_subsystem_, Core::System& system_)
    : input_subsystem{input_subsystem_}, system{system_} {
    input_subsystem->Initialize();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        LOG_CRITICAL(Frontend, "Failed to initialize SDL3: {}, Exiting...", SDL_GetError());
        exit(1);
    }
}

EmuWindow_SDL2::~EmuWindow_SDL2() {
    // An assignment made in the last couple of seconds is still waiting to be written.
    if (controls_save_pending) {
        SaveNativeControls();
    }
    system.HIDCore().UnloadInputDevices();
    input_subsystem->Shutdown();
    SDL_Quit();
}

InputCommon::MouseButton EmuWindow_SDL2::SDLButtonToMouseButton(u32 button) const {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return InputCommon::MouseButton::Left;
    case SDL_BUTTON_RIGHT:
        return InputCommon::MouseButton::Right;
    case SDL_BUTTON_MIDDLE:
        return InputCommon::MouseButton::Wheel;
    case SDL_BUTTON_X1:
        return InputCommon::MouseButton::Backward;
    case SDL_BUTTON_X2:
        return InputCommon::MouseButton::Forward;
    default:
        return InputCommon::MouseButton::Undefined;
    }
}

std::pair<float, float> EmuWindow_SDL2::MouseToTouchPos(s32 touch_x, s32 touch_y) const {
    int w, h;
    SDL_GetWindowSize(render_window, &w, &h);
    const float fx = static_cast<float>(touch_x) / w;
    const float fy = static_cast<float>(touch_y) / h;

    return {std::clamp<float>(fx, 0.0f, 1.0f), std::clamp<float>(fy, 0.0f, 1.0f)};
}

void EmuWindow_SDL2::OnMouseButton(u32 button, u8 state, s32 x, s32 y) {
    const auto mouse_button = SDLButtonToMouseButton(button);
    if (state == SDL_PRESSED) {
        const auto [touch_x, touch_y] = MouseToTouchPos(x, y);
        input_subsystem->GetMouse()->PressButton(x, y, mouse_button);
        input_subsystem->GetMouse()->PressMouseButton(mouse_button);
        input_subsystem->GetMouse()->PressTouchButton(touch_x, touch_y, mouse_button);
    } else {
        input_subsystem->GetMouse()->ReleaseButton(mouse_button);
    }
}

void EmuWindow_SDL2::OnMouseMotion(s32 x, s32 y) {
    const auto [touch_x, touch_y] = MouseToTouchPos(x, y);
    input_subsystem->GetMouse()->Move(x, y, 0, 0);
    input_subsystem->GetMouse()->MouseMove(touch_x, touch_y);
    input_subsystem->GetMouse()->TouchMove(touch_x, touch_y);
}

void EmuWindow_SDL2::OnFingerDown(float x, float y, std::size_t id) {
    input_subsystem->GetTouchScreen()->TouchPressed(x, y, id);
}

void EmuWindow_SDL2::OnFingerMotion(float x, float y, std::size_t id) {
    input_subsystem->GetTouchScreen()->TouchMoved(x, y, id);
}

void EmuWindow_SDL2::OnFingerUp() {
    input_subsystem->GetTouchScreen()->ReleaseAllTouch();
}

void EmuWindow_SDL2::OnKeyEvent(int key, u8 state) {
    if (state == SDL_PRESSED) {
        input_subsystem->GetKeyboard()->PressKey(static_cast<std::size_t>(key));
    } else if (state == SDL_RELEASED) {
        input_subsystem->GetKeyboard()->ReleaseKey(static_cast<std::size_t>(key));
    }
}

bool EmuWindow_SDL2::IsOpen() const {
    return is_open;
}

void EmuWindow_SDL2::EnableTasPlayback() {
    tas_playback = true;
}

void EmuWindow_SDL2::OnFrameDisplayed() {
    if (!tas_playback) {
        return;
    }
    // Render thread, once per presented frame: one TAS command per call keeps a replay
    // deterministic against the frames the guest renders.
    auto* const tas = input_subsystem->GetTas();
    tas->UpdateThread();

    const auto [state, progress, lengths] = tas->GetStatus();
    if (!tas_started) {
        // Start on the first displayed frame; Reset reloads the script so it begins at the top.
        tas->Reset();
        tas->StartStop();
        tas_started = true;
        LOG_INFO(Frontend, "TAS playback started, {} frames queued", lengths[0]);
        return;
    }
    if (state != InputCommon::TasInput::TasState::Stopped) {
        tas_progress = progress;
        return;
    }
    // The script ended and tas_loop is off: quit so --tas can run unattended.
    LOG_INFO(Frontend, "TAS playback finished, {} of {} frames, exiting", tas_progress,
             lengths[0]);
    tas_playback = false;
    // SDL_PushEvent is thread safe; the main thread turns SDL_EVENT_QUIT into is_open = false.
    SDL_Event quit_event{};
    quit_event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit_event);
}

bool EmuWindow_SDL2::IsShown() const {
    return is_shown;
}

void EmuWindow_SDL2::OnResize() {
    int width, height;
    SDL_GetWindowSizeInPixels(render_window, &width, &height);
    // A minimized window reports 0x0 and a zero-extent swapchain never presents; keep the last
    // good layout until the next real size.
    if (width <= 0 || height <= 0) {
        return;
    }
    UpdateCurrentFramebufferLayout(width, height);
}

void EmuWindow_SDL2::ShowCursor(bool show_cursor) {
    if (show_cursor) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
}

bool EmuWindow_SDL2::IsFullscreen() const {
    return (SDL_GetWindowFlags(render_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

// F12 is claimed only by a window that has a HUD to toggle; otherwise the game keeps it.
SuyuCmd::FullscreenKeys EmuWindow_SDL2::ActiveHotkeys() const {
    SuyuCmd::FullscreenKeys keys = kFullscreenKeys;
    if (!HasPerfHud()) {
        keys.perf_hud = SuyuCmd::kNoScancode;
    }
    if (!HasInventory()) {
        keys.inventory_mark = SuyuCmd::kNoScancode;
        keys.inventory_pause = SuyuCmd::kNoScancode;
    }
    return keys;
}

SuyuCmd::UiActionResult EmuWindow_SDL2::ToggleFullscreen() {
    SuyuCmd::UiActionResult result;
    bool left_fullscreen = false;
    if (IsFullscreen()) {
        left_fullscreen = true;
        if (!SDL_SetWindowFullscreen(render_window, false)) {
            result = {false, std::string{"Leaving fullscreen failed: "} + SDL_GetError()};
        }
    } else if (fullscreen_fell_back) {
        fullscreen_fell_back = false;
        SDL_RestoreWindow(render_window);
    } else if (!Fullscreen()) {
        fullscreen_fell_back = true;
        result = {false, "Fullscreen was refused; the window was maximized instead"};
    }
    if (!SDL_SyncWindow(render_window)) {
        LOG_WARNING(Frontend, "The fullscreen change is still pending: {}", SDL_GetError());
    }
    if (left_fullscreen && windowed_geometry.valid) {
        SDL_SetWindowSize(render_window, windowed_geometry.width, windowed_geometry.height);
        SDL_SetWindowPosition(render_window, windowed_geometry.x, windowed_geometry.y);
    }
    ApplyFullscreenState();
    return result;
}

void EmuWindow_SDL2::ApplyFullscreenState() {
    const bool fullscreen = IsFullscreen();
    ShowCursor(!fullscreen);
    OnFullscreenChanged(fullscreen);
}

bool EmuWindow_SDL2::Fullscreen() {
    const bool maximized = (SDL_GetWindowFlags(render_window) & SDL_WINDOW_MAXIMIZED) != 0;
    windowed_geometry.valid = !maximized && !IsFullscreen();
    if (windowed_geometry.valid) {
        SDL_GetWindowPosition(render_window, &windowed_geometry.x, &windowed_geometry.y);
        SDL_GetWindowSize(render_window, &windowed_geometry.width, &windowed_geometry.height);
    }
    switch (Settings::values.fullscreen_mode.GetValue()) {
    case Settings::FullscreenMode::Exclusive:
        // Set window size to render size before entering fullscreen -- SDL3 does not resize window
        // to display dimensions automatically in this mode.
        {
            const SDL_DisplayMode* display_mode =
                SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
            if (display_mode) {
                SDL_SetWindowSize(render_window, display_mode->w, display_mode->h);
            } else {
                LOG_ERROR(Frontend, "SDL_GetDesktopDisplayMode failed: {}", SDL_GetError());
            }
        }

        if (SDL_SetWindowFullscreen(render_window, true)) {
            return true;
        }

        LOG_ERROR(Frontend, "Fullscreening failed: {}", SDL_GetError());
        LOG_INFO(Frontend, "Attempting to use borderless fullscreen...");
        [[fallthrough]];
    case Settings::FullscreenMode::Borderless:
        if (SDL_SetWindowFullscreen(render_window, true)) {
            return true;
        }

        LOG_ERROR(Frontend, "Borderless fullscreening failed: {}", SDL_GetError());
        [[fallthrough]];
    default:
        // Fallback algorithm: Maximise window.
        // Works on all systems (unless something is seriously wrong), so no fallback for this one.
        LOG_INFO(Frontend, "Falling back on a maximised window...");
        SDL_MaximizeWindow(render_window);
        return false;
    }
}

void EmuWindow_SDL2::WaitEvent() {
    // Called on main thread
    SDL_Event event;

    SyncOverlayTextInput();
    RunOverlayActions();

    // Wait with a deadline so the idle status refresh runs; a timeout is a tick, not a failure.
    SDL_ClearError();
    const Sint32 wait_slice_ms = rebind_session && rebind_session->Active()
                                     ? static_cast<Sint32>(SuyuCmd::kRebindPollIntervalMs)
                                     : kEventWaitSliceMs;
    if (!SDL_WaitEventTimeout(&event, wait_slice_ms)) {
        if (const char* error = SDL_GetError(); error != nullptr && error[0] != '\0') {
            LOG_ERROR(Frontend, "SDL_WaitEventTimeout failed: {}", error);
            SDL_ClearError();
        }
        const u64 idle_time = SDL_GetTicks();
        if (idle_time > last_time + kStatusRefreshMs) {
            last_time = idle_time;
            RefreshWindowStatus();
        }
        return;
    }

    SyncOverlayTextInput();
    const bool taken_by_overlay = RouteOverlayEvent(event);

    switch (event.type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        // Restore raises only RESTORED, so is_shown would stay false after a minimize.
        is_shown = true;
        OnResize();
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        is_shown = false;
        OnResize();
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
        is_shown = true;
        OnResize();
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        is_open = false;
        break;
    case SDL_EVENT_KEY_DOWN: {
        const SuyuCmd::FullscreenKeys hotkeys = ActiveHotkeys();
        // The HUD and inventory keys work even while an ImGui window holds the keyboard.
        if (taken_by_overlay &&
            !SuyuCmd::IsOverlayIndependentHotkey(static_cast<std::uint32_t>(event.key.scancode),
                                                 hotkeys)) {
            break;
        }
        switch (hotkey_keys.OnKeyDown(static_cast<std::uint32_t>(event.key.scancode),
                                      (event.key.mod & SDL_KMOD_ALT) != 0, event.key.repeat,
                                      hotkeys)) {
        case SuyuCmd::HotkeyKeyTracker::Route::Toggle:
            if (const auto toggled = ToggleFullscreen(); !toggled.ok) {
                LOG_ERROR(Frontend, "{}", toggled.message);
            }
            break;
        case SuyuCmd::HotkeyKeyTracker::Route::ToggleHud:
            TogglePerfHud();
            break;
        case SuyuCmd::HotkeyKeyTracker::Route::InventoryMark:
            InventoryHotkey(true);
            break;
        case SuyuCmd::HotkeyKeyTracker::Route::InventoryPause:
            InventoryHotkey(false);
            break;
        case SuyuCmd::HotkeyKeyTracker::Route::Consume:
            break;
        case SuyuCmd::HotkeyKeyTracker::Route::Game:
            OnKeyEvent(static_cast<int>(event.key.scancode), SDL_PRESSED);
            break;
        }
        break;
    }
    case SDL_EVENT_KEY_UP: {
        const bool hud_key = SuyuCmd::IsOverlayIndependentHotkey(
            static_cast<std::uint32_t>(event.key.scancode), ActiveHotkeys());
        if ((!taken_by_overlay || hud_key) &&
            !hotkey_keys.OnKeyUp(static_cast<std::uint32_t>(event.key.scancode))) {
            OnKeyEvent(static_cast<int>(event.key.scancode), SDL_RELEASED);
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        hotkey_keys.Reset();
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        ApplyFullscreenState();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        // ignore if it came from touch
        if (event.motion.which != SDL_TOUCH_MOUSEID && !taken_by_overlay)
            OnMouseMotion(static_cast<s32>(event.motion.x), static_cast<s32>(event.motion.y));
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        // ignore if it came from touch
        if (event.button.which != SDL_TOUCH_MOUSEID && !taken_by_overlay) {
            OnMouseButton(event.button.button,
                          event.button.down ? SDL_PRESSED : SDL_RELEASED,
                          static_cast<s32>(event.button.x), static_cast<s32>(event.button.y));
        }
        break;
    case SDL_EVENT_FINGER_DOWN:
        OnFingerDown(event.tfinger.x, event.tfinger.y,
                     static_cast<std::size_t>(event.tfinger.fingerID));
        break;
    case SDL_EVENT_FINGER_MOTION:
        OnFingerMotion(event.tfinger.x, event.tfinger.y,
                       static_cast<std::size_t>(event.tfinger.fingerID));
        break;
    case SDL_EVENT_FINGER_UP:
        OnFingerUp();
        break;
    case SDL_EVENT_QUIT:
        is_open = false;
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
        gamepad_check_pending = true;
        gamepad_retries = kGamepadRetries;
        break;
    default:
        break;
    }

    const u64 current_time = SDL_GetTicks();
    if (current_time > last_time + kStatusRefreshMs) {
        last_time = current_time;
        RefreshWindowStatus();
    }
}

#ifdef _WIN32
namespace {
// What the progress window paints. Written and read on the main thread only.
std::size_t g_build_progress_built = 0;
std::size_t g_build_progress_total = 0;

LRESULT CALLBACK BuildProgressProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg != WM_PAINT) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));

    wchar_t text[96];
    if (g_build_progress_total == 0) {
        std::swprintf(text, std::size(text), L"Preparing shaders...");
    } else {
        std::swprintf(text, std::size(text), L"Preparing shaders: %zu of %zu",
                      g_build_progress_built, g_build_progress_total);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    RECT text_rect{16, 12, client.right - 16, 34};
    DrawTextW(dc, text, -1, &text_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT bar{16, 42, client.right - 16, 62};
    FrameRect(dc, &bar, GetSysColorBrush(COLOR_BTNSHADOW));
    if (g_build_progress_total != 0) {
        RECT fill = bar;
        InflateRect(&fill, -2, -2);
        const auto done = std::min(g_build_progress_built, g_build_progress_total);
        fill.right = fill.left + static_cast<LONG>((fill.right - fill.left) * done /
                                                   g_build_progress_total);
        FillRect(dc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
    }
    EndPaint(hwnd, &ps);
    return 0;
}
} // namespace
#endif

void EmuWindow_SDL2::ShowBuildProgress(std::size_t built, std::size_t total) {
    // Throttle the title; thousands of pipelines shouldn't mean thousands of title updates.
    const u64 now = SDL_GetTicks();
    if (built == 0 || built == total || now >= last_build_title_ticks + 100) {
        last_build_title_ticks = now;
        char title[64];
        if (total == 0) {
            std::snprintf(title, sizeof(title), "Preparing shaders...");
        } else {
            std::snprintf(title, sizeof(title), "Building shaders %zu/%zu", built, total);
        }
        SDL_SetWindowTitle(render_window, title);
    }
#ifdef _WIN32
    g_build_progress_built = built;
    g_build_progress_total = total;
    if (!build_progress_window) {
        static const wchar_t* const kClass = L"SuyuBuildProgress";
        static const bool registered = [] {
            WNDCLASSW wc{};
            wc.lpfnWndProc = BuildProgressProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_WAIT);
            wc.lpszClassName = kClass;
            return RegisterClassW(&wc) != 0;
        }();
        if (!registered) {
            return;
        }
        const auto owner = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(render_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        RECT owner_rect{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        if (owner) {
            GetWindowRect(owner, &owner_rect);
        }
        constexpr int width = 420;
        constexpr int height = 78;
        const int x = owner_rect.left + (owner_rect.right - owner_rect.left - width) / 2;
        const int y = owner_rect.top + (owner_rect.bottom - owner_rect.top - height) / 2;
        // Owned by the game window, so it stays above it and closes with it.
        build_progress_window =
            CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"", WS_POPUP | WS_BORDER | WS_VISIBLE,
                            x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
    }
    if (build_progress_window) {
        InvalidateRect(static_cast<HWND>(build_progress_window), nullptr, FALSE);
        UpdateWindow(static_cast<HWND>(build_progress_window));
    }
#endif
}

void EmuWindow_SDL2::HideBuildProgress() {
#ifdef _WIN32
    if (build_progress_window) {
        DestroyWindow(static_cast<HWND>(build_progress_window));
        build_progress_window = nullptr;
    }
#endif
}

SuyuCmd::ControllerSnapshot EmuWindow_SDL2::BuildControllerSnapshot() {
    SuyuCmd::ControllerSnapshot snapshot;
    const auto devices = input_subsystem->GetInputDevices();
    for (const auto& device : devices) {
        const std::string engine = device.Get("engine", "");
        if (engine == "sdl" || engine == "joycon" || engine == "gcpad") {
            snapshot.devices.push_back(
                {device.Serialize(), device.Get("display", device.Get("class", "Unknown"))});
        }
    }
    const auto& player = Settings::values.players.GetValue()[0];
    for (std::size_t i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        snapshot.rows.push_back(
            {Settings::NativeButton::mapping[i], ShownBinding(player.buttons[i])});
    }
    for (std::size_t i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        snapshot.rows.push_back(
            {Settings::NativeAnalog::mapping[i], ShownBinding(player.analogs[i])});
    }
    const auto pads = ControllerPads(devices);
    auto& players = Settings::values.players.GetValue();
    snapshot.can_combine =
        SuyuCmd::CombineJoycons(players, pads, SdlConfig::default_buttons, {}, 0).has_value();
    snapshot.can_split =
        SuyuCmd::SplitJoycons(players, pads, SdlConfig::default_buttons, {}, 0).has_value();
    if (rebind_session && rebind_session->Active()) {
        snapshot.rebinding = true;
        snapshot.rebind_row = rebind_session->Row();
        snapshot.rebind_remaining_ms = rebind_session->RemainingMs(SDL_GetTicks());
    }
    return snapshot;
}

SuyuCmd::UiActionResult EmuWindow_SDL2::HandleControllerRequest(
    const SuyuCmd::ControllerRequest& request) {
    using Kind = SuyuCmd::ControllerRequest::Kind;
    if (SuyuCmd::RequestCancelsRebind(request.kind)) {
        CancelControllerRebind();
    }
    const SuyuCmd::Player1Hooks hooks = MakePlayer1Hooks(system);
    auto& player = Settings::values.players.GetValue()[0];
    switch (request.kind) {
    case Kind::MapDevice: {
        const auto devices = input_subsystem->GetInputDevices();
        const auto device = std::find_if(devices.begin(), devices.end(), [&](const auto& d) {
            return d.Serialize() == request.device_id;
        });
        if (device == devices.end()) {
            return {false, "The controller is no longer connected"};
        }
        MapDefault(*input_subsystem, *device, player);
        hooks.mark_connected();
        hooks.commit();
        return {true, "Controller mapped to Player 1."};
    }
    case Kind::UseKeyboard:
        ApplyDefaultKeyboardLayout(player);
        hooks.mark_connected();
        hooks.commit();
        return {true, "Keyboard controls restored for Player 1."};
    case Kind::BeginRebind:
        if (request.row >= kBindingLayout.Rows()) {
            return {false, "Unknown controller binding"};
        }
        if (!rebind_session) {
            rebind_session = std::make_unique<SuyuCmd::RebindSession>(MakeRebindHooks());
        }
        rebind_session->Begin(request.row, kBindingLayout.IsAnalog(request.row), SDL_GetTicks());
        return {};
    case Kind::ClearBinding:
        return SuyuCmd::ClearBinding(hooks, kBindingLayout, request.row);
    case Kind::CancelRebind:
        CancelControllerRebind();
        return {};
    case Kind::CombineJoycons:
    case Kind::SplitJoycons: {
        const bool combine = request.kind == Kind::CombineJoycons;
        const auto pads = ControllerPads(input_subsystem->GetInputDevices());
        auto& players = Settings::values.players.GetValue();
        const auto map_pad = [this](const Common::ParamPackage& pad,
                                    Settings::PlayerInput& target) {
            MapDefault(*input_subsystem, pad, target);
        };
        const auto slot =
            combine ? SuyuCmd::CombineJoycons(players, pads, SdlConfig::default_buttons, map_pad, 0)
                    : SuyuCmd::SplitJoycons(players, pads, SdlConfig::default_buttons, map_pad, 0);
        if (!slot) {
            return {false, "No Joy-Cons to change for Player 1 right now."};
        }
        hooks.commit();
        const std::string player_number = std::to_string(*slot + 1);
        return {true, combine ? "Joy-Cons combined into Player " + player_number + "."
                              : "Joy-Cons split: the left one is Player " + player_number +
                                    ", the right one the next player."};
    }
    }
    return {false, "Unknown controller request"};
}

SuyuCmd::RebindHooks EmuWindow_SDL2::MakeRebindHooks() {
    return {
        .begin =
            [this](bool analog) {
                input_subsystem->BeginMapping(analog ? InputCommon::Polling::InputType::Stick
                                                     : InputCommon::Polling::InputType::Button);
            },
        .next_input =
            [this] {
                const auto captured = input_subsystem->GetNextInput();
                return captured.Has("engine") ? captured.Serialize() : std::string{};
            },
        .stop = [this] { input_subsystem->StopMapping(); },
        .accept =
            [](bool analog, const std::string& captured) {
                // A stick needs both axes; a button or key would store a dead stick binding.
                const Common::ParamPackage package{captured};
                return !analog || (package.Has("axis_x") && package.Has("axis_y"));
            },
    };
}

std::optional<SuyuCmd::UiActionResult> EmuWindow_SDL2::PollControllerRebind() {
    if (!rebind_session || !rebind_session->Active()) {
        return std::nullopt;
    }
    const SuyuCmd::RebindResult result = rebind_session->Poll(SDL_GetTicks());
    switch (result.kind) {
    case SuyuCmd::RebindResult::Kind::Captured:
        return SuyuCmd::ApplyBinding(MakePlayer1Hooks(system), kBindingLayout, result.row,
                                     result.captured);
    case SuyuCmd::RebindResult::Kind::TimedOut:
        return SuyuCmd::UiActionResult{false, "No input detected - nothing changed"};
    default:
        return std::nullopt;
    }
}

void EmuWindow_SDL2::CancelControllerRebind() {
    if (rebind_session) {
        rebind_session->Cancel();
    }
}

SuyuCmd::FolderPaths EmuWindow_SDL2::OverlayFolderPaths() {
    // The runtime's own path table: the export bootstrap points LoadDir at <exe>/mods.
    return {Common::FS::GetSuyuPath(Common::FS::SuyuPath::UserDir),
            Common::FS::GetSuyuPath(Common::FS::SuyuPath::LoadDir),
            Common::FS::GetSuyuPath(Common::FS::SuyuPath::KeysDir)};
}

SuyuCmd::UiActionResult EmuWindow_SDL2::OpenOverlayFolder(SuyuCmd::FolderKind folder) {
    const SuyuCmd::FolderPaths paths = OverlayFolderPaths();
    const std::filesystem::path path = folder == SuyuCmd::FolderKind::User   ? paths.user
                                       : folder == SuyuCmd::FolderKind::Mods ? paths.mods
                                                                             : paths.keys;
    if (path.empty()) {
        return {false, "The folder location is unknown"};
    }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return {false, "Could not create " + SuyuCmd::Utf8(path) + ": " + ec.message()};
    }
#ifdef _WIN32
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"explore", path.wstring().c_str(), nullptr, nullptr, SW_SHOW));
    const bool opened = result > 32;
    const std::string failure = "Windows error code " + std::to_string(result);
#else
    const bool opened = SDL_OpenURL(SuyuCmd::FileUrlFromPath(SuyuCmd::Utf8(path)).c_str());
    const std::string failure = SDL_GetError();
#endif
    if (!opened) {
        return {false, "Could not open " + SuyuCmd::Utf8(path) + ": " + failure};
    }
    return {};
}

bool EmuWindow_SDL2::PumpEventsWhileLoading() {
    SDL_PumpEvents();
    RunOverlayActions();
    // Leave the events queued: the main loop handles a close request as usual.
    return !SDL_HasEvent(SDL_EVENT_QUIT) && !SDL_HasEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
}

void EmuWindow_SDL2::RefreshWindowStatus() {
    // Same tick as the title: cheap, and a controller plugged in mid-game is
    // picked up within a second.
    if (gamepad_check_pending) {
        AutoAssignControllers();
    }
    if (controls_save_pending && SDL_GetTicks() >= last_controls_save + kControlsSaveMs) {
        controls_save_pending = false;
        last_controls_save = SDL_GetTicks();
        SaveNativeControls();
    }

    const bool benchmark_owns_stats = std::getenv("SUYU_CMD_PERF_SAMPLE") != nullptr;
    const auto snap = SuyuCmd::SampleNativeStatus(system, !benchmark_owns_stats);
    SuyuCmd::StoreNativeStatusSnapshot(snap);

    SuyuCmd::NativeStatusSnapshot display{};
    SuyuCmd::TryGetNativeStatusSnapshot(display);
    const std::string title = SuyuCmd::FormatNativeTitle(display);
    SDL_SetWindowTitle(render_window, title.c_str());
}

// Standalone exports auto-assign pads until the player picks controls in the overlay
// Controller (Player 1) window (auto_assign_controllers).
void EmuWindow_SDL2::AutoAssignControllers() {
    gamepad_check_pending = false;
    // A TAS replay drives the players itself; their mappings stay as configured.
    if (!g_export_package || !SdlConfig::auto_assign_controllers ||
        Settings::values.tas_enable.GetValue()) {
        return;
    }
    const auto devices = input_subsystem->GetInputDevices();
    std::size_t sdl_gamepads = 0;
    const auto pads = ControllerPads(devices, &sdl_gamepads);
    const auto sdl_listed = static_cast<std::size_t>(
        std::count_if(pads.begin(), pads.end(),
                      [](const auto& pad) { return pad.Get("engine", "") == "sdl"; }));
    // SDL lists a gamepad before the backend registers it: retry a few ticks, no more, since
    // Joy-Cons under suyu's own driver never show up here.
    if (gamepad_retries > 0 && sdl_listed < sdl_gamepads) {
        --gamepad_retries;
        gamepad_check_pending = true;
    }

    auto bindings = SuyuCmd::ParsePadBindings(SdlConfig::auto_assigned_pads);
    // GetValue() hands back a reference to the live array, so the mappings are
    // written straight into the setting.
    auto& players = Settings::values.players.GetValue();
    const auto map_pad = [this](const Common::ParamPackage& pad, Settings::PlayerInput& player) {
        MapDefault(*input_subsystem, pad, player);
        LOG_INFO(Frontend, "Assigned {}", pad.Get("display", std::string{"a controller"}));
    };
    // The layout SdlConfig gives a player with nothing configured.
    const auto restore_keyboard = [](Settings::PlayerInput& player) {
        for (std::size_t i = 0; i < player.buttons.size(); ++i) {
            player.buttons[i] = InputCommon::GenerateKeyboardParam(SdlConfig::default_buttons[i]);
        }
        for (std::size_t i = 0; i < player.analogs.size(); ++i) {
            const auto& keys = SdlConfig::default_analogs[i];
            player.analogs[i] = InputCommon::GenerateAnalogParamFromKeys(
                keys[0], keys[1], keys[2], keys[3], SdlConfig::default_stick_mod[i], 0.5f);
        }
        for (std::size_t i = 0; i < player.motions.size(); ++i) {
            player.motions[i] = InputCommon::GenerateKeyboardParam(SdlConfig::default_motions[i]);
        }
        LOG_INFO(Frontend, "No controller left: Player 1 uses the keyboard");
    };
    if (!SuyuCmd::AssignControllers(players, devices, pads, bindings, pads_seen,
                                    SdlConfig::default_buttons, map_pad, restore_keyboard)) {
        return;
    }
    SdlConfig::auto_assigned_pads = SuyuCmd::SerializePadBindings(bindings);
    for (const auto& binding : bindings) {
        LOG_INFO(Frontend, "Player {}: pad {} port {}, {}", binding.player + 1, binding.guid,
                 binding.port, players[binding.player].connected ? "connected" : "disconnected");
    }
    // The game sees the change now; the file is written at most every couple
    // of seconds, so a pad that keeps dropping out does not rewrite it each time.
    system.HIDCore().ReloadInputDevices();
    controls_save_pending = true;
}

// Credits to Samantas5855 and others for this function.
void EmuWindow_SDL2::SetWindowIcon() {
#ifdef _WIN32
    // Exports embed the game's icon as RT_GROUP_ICON id 1 (suyu/game_export.cpp); prefer it.
    if (g_native_export_mode) {
        const HICON hicon = static_cast<HICON>(
            LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256,
                       LR_DEFAULTCOLOR));
        if (hicon != nullptr) {
            ICONINFO info{};
            if (GetIconInfo(hicon, &info)) {
                BITMAP bmp{};
                GetObjectW(info.hbmColor, sizeof(bmp), &bmp);
                const int w = bmp.bmWidth;
                const int h = bmp.bmHeight;
                std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = w;
                bi.bmiHeader.biHeight = -h; // top-down
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                const HDC hdc = GetDC(nullptr);
                if (GetDIBits(hdc, info.hbmColor, 0, h, pixels.data(), &bi, DIB_RGB_COLORS)) {
                    // BGRA -> RGBA
                    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
                        std::swap(pixels[i], pixels[i + 2]);
                    }
                    SDL_Surface* const icon_surface = SDL_CreateSurfaceFrom(
                        w, h, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
                    if (icon_surface != nullptr) {
                        SDL_SetWindowIcon(render_window, icon_surface);
                        SDL_DestroySurface(icon_surface);
                        ReleaseDC(nullptr, hdc);
                        DeleteObject(info.hbmColor);
                        DeleteObject(info.hbmMask);
                        DestroyIcon(hicon);
                        return;
                    }
                }
                ReleaseDC(nullptr, hdc);
                DeleteObject(info.hbmColor);
                DeleteObject(info.hbmMask);
            }
            DestroyIcon(hicon);
        }
        LOG_WARNING(Frontend, "Native export: failed to load game icon from exe resources, "
                               "falling back to suyu icon.");
    }
#endif
    SDL_IOStream* const suyu_icon_stream = SDL_IOFromConstMem((void*)suyu_icon, suyu_icon_size);
    if (suyu_icon_stream == nullptr) {
        LOG_WARNING(Frontend, "Failed to create suyu icon stream.");
        return;
    }
    SDL_Surface* const window_icon = SDL_LoadBMP_IO(suyu_icon_stream, true);
    if (window_icon == nullptr) {
        LOG_WARNING(Frontend, "Failed to read BMP from stream.");
        return;
    }
    // The icon is attached to the window pointer
    SDL_SetWindowIcon(render_window, window_icon);
    SDL_DestroySurface(window_icon);
}

void EmuWindow_SDL2::OnMinimalClientAreaChangeRequest(std::pair<u32, u32> minimal_size) {
    SDL_SetWindowMinimumSize(render_window, minimal_size.first, minimal_size.second);
}
