// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "suyu_cmd/fullscreen_toggle.h"
#include "suyu_cmd/overlay/controller_settings.h"
#include "suyu_cmd/overlay/mods_folders.h"
#include "suyu_cmd/overlay/rebind_session.h"

struct SDL_Window;
union SDL_Event;

namespace Core {
class System;
}

/// Defined in suyu.cpp. True once native recompiled CPU modules are registered (a standalone
/// game export), so the window title and icon read as the game.
extern bool g_native_export_mode;

/// Defined in suyu.cpp. True when this executable runs as an exported package (a static
/// build, or a copy with the exporter's README beside it), whatever its CPU backend.
extern bool g_export_package;

namespace InputCommon {
class InputSubsystem;
enum class MouseButton;
} // namespace InputCommon

class EmuWindow_SDL2 : public Core::Frontend::EmuWindow {
public:
    explicit EmuWindow_SDL2(InputCommon::InputSubsystem* input_subsystem_, Core::System& system_);
    ~EmuWindow_SDL2();

    /// Whether the window is still open, and a close request hasn't yet been sent
    bool IsOpen() const;

    /// Returns if window is shown (not minimized)
    bool IsShown() const override;

    /// Wait for the next event on the main thread.
    void WaitEvent();

    void RefreshWindowStatus();

    /// Hand connected gamepads to player slots; see SuyuCmd::AssignControllers.
    void AutoAssignControllers();

    /// Replay the TAS script from the user TAS directory instead of waiting for
    /// a hotkey, and quit once it runs out. Call before the system is run.
    void EnableTasPlayback();

    // Sets the window icon from suyu.bmp
    void SetWindowIcon();

    /// Boot-time shader precompile progress: title bar, plus a progress window on Windows.
    /// Main thread only; the precompile thread just reports counts.
    void ShowBuildProgress(std::size_t built, std::size_t total);
    void HideBuildProgress();

    /// Pumps events and overlay actions before the main loop runs. Returns false once a close
    /// was requested; the request stays queued for the main loop.
    bool PumpEventsWhileLoading();

protected:
    /// Offers an input or focus event to the overlay first; true means the game must not get it.
    virtual bool RouteOverlayEvent(const SDL_Event& event) {
        return false;
    }

    /// Starts or stops SDL text input to match what the overlay asks for. Event thread only.
    virtual void SyncOverlayTextInput() {}

    /// Runs the actions the overlay's UI queued. Event thread only.
    virtual void RunOverlayActions() {}

    /// Whether this window has a performance HUD to toggle; only then does F12 belong to it.
    virtual bool HasPerfHud() const {
        return false;
    }

    /// Shows or hides the overlay's performance HUD. Event thread only.
    virtual void TogglePerfHud() {}

    /// Whether this window has the boundary inventory controls; only then do F9 and F10 belong to it.
    virtual bool HasInventory() const {
        return false;
    }

    /// F9 (`mark`) or F10 (pause or resume) for the boundary inventory. Event thread only.
    virtual void InventoryHotkey(bool mark) {
        (void)mark;
    }

    /// What the overlay's Controller window shows. Event thread only.
    SuyuCmd::ControllerSnapshot BuildControllerSnapshot();

    /// Applies one Controller window request to Player 1. Event thread only.
    SuyuCmd::UiActionResult HandleControllerRequest(const SuyuCmd::ControllerRequest& request);

    /// Checks a running rebind once; returns the outcome worth reporting. Event thread only.
    std::optional<SuyuCmd::UiActionResult> PollControllerRebind();

    void CancelControllerRebind();

    /// The user, mods and keys folders as the runtime resolves them.
    SuyuCmd::FolderPaths OverlayFolderPaths();

    /// Opens a folder in the file manager, creating it first. Event thread only.
    SuyuCmd::UiActionResult OpenOverlayFolder(SuyuCmd::FolderKind folder);

    /// Called by WaitEvent when a key is pressed or released.
    void OnKeyEvent(int key, u8 state);

    /// Converts a SDL mouse button into MouseInput mouse button
    InputCommon::MouseButton SDLButtonToMouseButton(u32 button) const;

    /// Translates pixel position to float position
    std::pair<float, float> MouseToTouchPos(s32 touch_x, s32 touch_y) const;

    /// Called by WaitEvent when a mouse button is pressed or released
    void OnMouseButton(u32 button, u8 state, s32 x, s32 y);

    /// Called by WaitEvent when the mouse moves.
    void OnMouseMotion(s32 x, s32 y);

    /// Called by WaitEvent when a finger starts touching the touchscreen
    void OnFingerDown(float x, float y, std::size_t id);

    /// Called by WaitEvent when a finger moves while touching the touchscreen
    void OnFingerMotion(float x, float y, std::size_t id);

    /// Called by WaitEvent when a finger stops touching the touchscreen
    void OnFingerUp();

    /// Called by WaitEvent when any event that may cause the window to be resized occurs
    void OnResize();

    /// Called when users want to hide the mouse cursor
    void ShowCursor(bool show_cursor);

    /// Enters fullscreen (also the --fullscreen flag); false if only a maximized window resulted.
    bool Fullscreen();

    /// Whether the window is fullscreen right now. Event thread only.
    bool IsFullscreen() const;

    /// Fullscreen, or maximized because fullscreen was refused; what the View menu shows.
    bool FullscreenRequested() const {
        return IsFullscreen() || fullscreen_fell_back;
    }

    /// Switches between fullscreen and windowed. Event thread only.
    SuyuCmd::UiActionResult ToggleFullscreen();

    /// The hotkeys in effect for this window.
    SuyuCmd::FullscreenKeys ActiveHotkeys() const;

    /// Matches the OS cursor and the frontend to the window's actual fullscreen state.
    void ApplyFullscreenState();

    /// Called after a toggle with the resulting state, so a frontend can adapt its cursor.
    virtual void OnFullscreenChanged(bool fullscreen) {}

    /// Called when a configuration change affects the minimal size of the window
    void OnMinimalClientAreaChangeRequest(std::pair<u32, u32> minimal_size) override;

    /// Called by the renderer after each presented frame. Drives TAS playback,
    /// which advances one command per displayed frame.
    void OnFrameDisplayed() override;

    /// Whether --tas asked us to replay a script.
    bool tas_playback = false;

    /// Whether playback has been started. Set on the first displayed frame.
    bool tas_started = false;

    /// Last progress seen while running. The driver rewinds its counter to zero
    /// when the script ends, so the final status cannot report how far it got.
    std::size_t tas_progress = 0;

    /// Is the window still open?
    bool is_open = true;

    /// Is the window being shown?
    bool is_shown = true;

    /// Internal SDL2 render window
    SDL_Window* render_window{};

    /// Boot shader precompile progress window (Windows HWND), or null.
    void* build_progress_window{};
    u64 last_build_title_ticks = 0;

    /// Keeps track of how often to update the title bar during gameplay
    u64 last_time = 0;

    SuyuCmd::RebindHooks MakeRebindHooks();

    /// Window geometry before fullscreen, restored when leaving it.
    struct WindowedGeometry {
        int x{};
        int y{};
        int width{};
        int height{};
        bool valid{};
    } windowed_geometry;

    /// Fullscreen was refused and the window was maximized instead; the next toggle restores it.
    bool fullscreen_fell_back{};

    /// Which fullscreen hotkey presses the game must not see.
    SuyuCmd::HotkeyKeyTracker hotkey_keys;

    /// The running "press the input you want" capture, created on first use.
    std::unique_ptr<SuyuCmd::RebindSession> rebind_session;

    /// Set at startup and whenever a gamepad comes or goes; see AutoAssignControllers.
    bool gamepad_check_pending = true;
    /// Further looks for a gamepad SDL lists before the input backend does.
    static constexpr int kGamepadRetries = 4;
    int gamepad_retries = kGamepadRetries;
    /// Pads connected at any point this session ("guid:port").
    std::set<std::string> pads_seen;
    /// Assignments reach the game at once but the config file at most every 2 s.
    bool controls_save_pending = false;
    u64 last_controls_save = 0;

    /// Input subsystem to use with this window.
    InputCommon::InputSubsystem* input_subsystem;

    /// suyu core instance
    Core::System& system;
};

class DummyContext : public Core::Frontend::GraphicsContext {};
