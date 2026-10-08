// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Overlay behaviour that needs neither ImGui nor Vulkan: when the command bar shows, and how
// mouse events travel from the window's event thread to the overlay's UI thread. Kept free of
// both so tests/recompiler_smoke/overlay_logic_unit.cpp can exercise it without a GPU.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace SuyuCmd {

/// Height of the strip at the top of the window that reveals the command bar.
inline constexpr float kCommandBarHoverZoneHeightPx = 20.0f;

/// Upper bound on queued events. The UI thread drains once per presented frame; this only
/// matters if presentation stalls while the mouse keeps moving.
inline constexpr std::size_t kOverlayInputQueueCapacity = 256;

struct CommandBarInput {
    bool mouse_in_window{};
    float mouse_y_px{};
    bool was_visible{};
    /// Height the bar had when last drawn; only read while it is visible.
    float bar_height_px{};
    /// A popup opened from the bar is up; the bar must not vanish underneath it.
    bool menu_open{};
};

/// The bar appears when the pointer enters the hover zone and stays while the pointer is over
/// the bar itself or a menu from it is open.
inline bool ShouldShowCommandBar(const CommandBarInput& input) {
    if (input.menu_open) {
        return true;
    }
    if (!input.mouse_in_window) {
        return false;
    }
    const float zone_height_px =
        input.was_visible ? std::max(input.bar_height_px, kCommandBarHoverZoneHeightPx)
                          : kCommandBarHoverZoneHeightPx;
    return input.mouse_y_px >= 0.0f && input.mouse_y_px < zone_height_px;
}

/// A screen area, in framebuffer pixels, where the overlay owns the mouse.
struct OverlayRect {
    float x{};
    float y{};
    float width{};
    float height{};

    bool Contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

struct OverlayInputEvent {
    enum class Kind : std::uint8_t {
        MouseMove,   ///< x, y: position in framebuffer pixels
        MouseButton, ///< x, y: where it happened; button: 0 left, 1 right, 2 middle, 3+ extra; down
        MouseWheel,  ///< x, y: horizontal and vertical scroll steps
        MouseLeave,  ///< the pointer left the window
        Reset,       ///< drop all pointer and press state: focus lost, minimized, or events lost
        FocusGained, ///< the window can take input again
    };

    Kind kind{Kind::MouseMove};
    float x{};
    float y{};
    std::uint8_t button{};
    bool down{};
};

/// Carries mouse events from the window's event thread to the overlay and decides, per event,
/// whether the overlay or the game owns it.
///
/// Ownership is settled on the event thread from the event's own position against the regions
/// the overlay last drew, so a click is never acted on by both. A press belongs to whoever it
/// started on until its release: a drag that began on the overlay keeps the overlay's moves even
/// outside its regions, and a drag the game started keeps the game's moves over the overlay. The
/// queue only ever holds what the overlay should see - every move, but presses, releases and
/// wheel steps only when the overlay owns them.
///
/// Thread contract: `Route` is called by the event thread, `Drain` and `SetCaptureRegions` by the
/// overlay's UI thread. The regions lag the UI by up to one frame, which can cost a click made in
/// the frame a window appears, never a double action.
class OverlayInputRouter {
public:
    /// Queues what the overlay needs from `event`. Returns true if the game must not receive it.
    bool Route(const OverlayInputEvent& event) {
        std::lock_guard lock{mutex};
        switch (event.kind) {
        case OverlayInputEvent::Kind::MouseMove:
            SetPointerLocked(event.x, event.y);
            PushLocked(event);
            return OwnsMotionLocked();
        case OverlayInputEvent::Kind::MouseButton:
            SetPointerLocked(event.x, event.y);
            return RouteButtonLocked(event);
        case OverlayInputEvent::Kind::MouseWheel:
            if (OwnsMotionLocked()) {
                PushLocked(event);
                return true;
            }
            return false;
        case OverlayInputEvent::Kind::MouseLeave:
            pointer_known = false;
            PushLocked(event);
            return false;
        case OverlayInputEvent::Kind::Reset:
            ResetLocked();
            return false;
        case OverlayInputEvent::Kind::FocusGained:
            PushLocked(event);
            return false;
        }
        return false;
    }

    /// Moves all queued events, oldest first, into `out` (which is cleared first).
    void Drain(std::vector<OverlayInputEvent>& out) {
        out.clear();
        std::lock_guard lock{mutex};
        out.assign(queue.begin(), queue.end());
        queue.clear();
    }

    /// Published by the UI thread after each frame: where the overlay owns the mouse. An empty
    /// list hands everything back to the game.
    void SetCaptureRegions(std::vector<OverlayRect> new_regions) {
        std::lock_guard lock{mutex};
        regions = std::move(new_regions);
    }

private:
    static constexpr std::size_t kTrackedButtons = 32;

    void SetPointerLocked(float x, float y) {
        pointer_x = x;
        pointer_y = y;
        pointer_known = true;
    }

    bool PointerOverRegionLocked() const {
        return pointer_known &&
               std::any_of(regions.begin(), regions.end(), [this](const OverlayRect& region) {
                   return region.Contains(pointer_x, pointer_y);
               });
    }

    /// Whether pointer motion and wheel steps go to the overlay right now.
    bool OwnsMotionLocked() const {
        if (overlay_buttons != 0) {
            return true;
        }
        return game_buttons == 0 && PointerOverRegionLocked();
    }

    bool RouteButtonLocked(const OverlayInputEvent& event) {
        if (event.button >= kTrackedButtons) {
            return false;
        }
        const std::uint32_t bit = std::uint32_t{1} << event.button;
        if (event.down) {
            if (OwnsMotionLocked()) {
                overlay_buttons |= bit;
                PushLocked(event);
                return true;
            }
            game_buttons |= bit;
            return false;
        }
        if ((overlay_buttons & bit) != 0) {
            overlay_buttons &= ~bit;
            PushLocked(event);
            return true;
        }
        game_buttons &= ~bit;
        return false;
    }

    /// Forgets who holds which button and where the pointer is, and tells the overlay to do the
    /// same.
    void ResetLocked() {
        overlay_buttons = 0;
        game_buttons = 0;
        pointer_known = false;
        RestartQueueLocked();
    }

    /// Replaces the queue with a Reset. Ownership of presses already made is kept: their
    /// releases still have to be recognised.
    void RestartQueueLocked() {
        queue.clear();
        queue.push_back({OverlayInputEvent::Kind::Reset, 0.0f, 0.0f, 0, false});
    }

    void PushLocked(const OverlayInputEvent& event) {
        if (!queue.empty() && queue.back().kind == event.kind) {
            // Only the latest pointer position matters, and wheel steps add up.
            if (event.kind == OverlayInputEvent::Kind::MouseMove) {
                queue.back() = event;
                return;
            }
            if (event.kind == OverlayInputEvent::Kind::MouseWheel) {
                queue.back().x += event.x;
                queue.back().y += event.y;
                return;
            }
        }
        if (queue.size() >= kOverlayInputQueueCapacity && !MakeRoomLocked()) {
            // Nothing expendable is left, so a press or release is about to be lost and the
            // overlay could keep a button down for good. Start it over from a clean state
            // instead: the Reset cancels whatever it holds.
            RestartQueueLocked();
            queue.push_back({OverlayInputEvent::Kind::FocusGained, 0.0f, 0.0f, 0, false});
        }
        queue.push_back(event);
    }

    /// Drops one position update, else one wheel step. Returns false if neither exists.
    bool MakeRoomLocked() {
        for (const OverlayInputEvent::Kind expendable :
             {OverlayInputEvent::Kind::MouseMove, OverlayInputEvent::Kind::MouseWheel}) {
            const auto it = std::find_if(queue.begin(), queue.end(), [expendable](const auto& e) {
                return e.kind == expendable;
            });
            if (it != queue.end()) {
                queue.erase(it);
                return true;
            }
        }
        return false;
    }

    std::mutex mutex;
    std::deque<OverlayInputEvent> queue;
    std::vector<OverlayRect> regions;
    float pointer_x{};
    float pointer_y{};
    bool pointer_known{};
    std::uint32_t overlay_buttons{}; ///< Buttons whose press the overlay took.
    std::uint32_t game_buttons{};    ///< Buttons whose press the game received.
};

} // namespace SuyuCmd
