# Aevora Dear ImGui overlay

## Purpose and scope

The Aevora overlay is the Vulkan `suyu-cmd` window's developer and quality-of-life surface. It is acquired by the SDL2 Vulkan window and rendered through the Vulkan presentation path; the OpenGL and Null windows do not provide this overlay. The overlay uses Dear ImGui, whose core and Vulkan backend are forced into the `Aevora::imgui` CPM package (`externals/CMakeLists.txt:435-465`, `cpmfile.json:120-125`).

## Architecture

The SDL event thread routes input and runs queued actions. The renderer/scheduler UI thread drains input, builds ImGui, publishes capture regions, and records draw data. `AcquireDebugOverlay` returns the process-wide overlay (`src/suyu_cmd/overlay/debug_overlay.cpp:103-114`); `DebugOverlay::Record` applies input, builds the UI, and renders it (`src/suyu_cmd/overlay/debug_overlay.cpp:208-245`). The Vulkan renderer obtains the optional frontend overlay (`src/video_core/renderer_vulkan/renderer_vulkan.cpp:187-205`), and `WindowAdaptPass::Draw` records it inside the presentation render pass (`src/video_core/renderer_vulkan/present/window_adapt_pass.cpp:37-45,99-120`).

The main files are:

- `debug_overlay.{h,cpp}`: ImGui lifetime, command bar, windows, snapshots, and action hooks (`src/suyu_cmd/overlay/debug_overlay.h:31-125`).
- `overlay_logic.h`: ImGui-free event routing, capture ownership, bounded input queue, and hover rule (`src/suyu_cmd/overlay/overlay_logic.h:21-96,184-343`).
- `overlay_actions.h`: bounded UI-to-event-thread FIFO and status channel (`src/suyu_cmd/overlay/overlay_actions.h:21-160`).
- `graphics_settings.h`, `controller_settings.h`, `rebind_session.h`: setting and Player 1 controller contracts (`src/suyu_cmd/overlay/graphics_settings.h:18-37`, `controller_settings.h:39-82`, `rebind_session.h:18-111`).
- `mods_folders.h`, `overlay_stats.h`, `build_identity.{h,cpp}`: folder scans, runtime statistics, and packaged build identity.
- `fullscreen_toggle.h`, `resolution_scale_host.h`, and `common/resolution_scale_table.h`: hotkey ownership, event-thread scale hooks, and the shared 13-entry scale table.

## Look and style

`src/suyu_cmd/overlay/overlay_style.h` and `overlay_style.cpp` are the single source for overlay style. They apply a 1.25 UI scale (`ScaleAllSizes` and `FontScaleMain`) with a 13 px base font, load embedded Roboto as the primary Latin font, then merge BIZ UDPGothic Regular behind it through `ImFontConfig::MergeMode` for Japanese glyphs Roboto lacks. A separate embedded Roboto Bold font is available for emphasized Latin text; it is not merged, and callers select it through `OverlayBoldFont()`. The three embedded sources are `aevora_overlay_font_latin_*`, `aevora_overlay_font_latin_bold_*`, and `aevora_overlay_font_japanese_*`; to swap one, replace the corresponding TTF under `dist/fonts` and update its `font_spec` entry in both the `src/suyu_cmd/CMakeLists.txt:341-344` and `tests/recompiler_smoke/CMakeLists.txt:145-147` loops, keeping the source symbol's role aligned with the loading behavior. ImGui's `stb_truetype` ignores variable axes, so the default Roboto instance (Regular) is drawn. The style uses the `#46478B` theme with 6 px window rounding at 100% (7.5 px after scaling). The main-menu-bar window background is alpha 0 so only `MenuBarBg` is drawn; a font-load failure logs an error and falls back to ImGui's default font. Change a color or scale in `overlay_style.{h,cpp}` and keep fixed overlay dimensions routed through `Scaled()`.

## Input routing

`OverlayInputRouter` decides on the event thread whether mouse, keyboard, and text belong to the overlay or game; a press remains owned by its first recipient until release (`src/suyu_cmd/overlay/overlay_logic.h:94-138,223-286`). Text is split at UTF-8 code-point boundaries, and the input queue is bounded at 256 events with mouse move/wheel coalescing and reset recovery (`overlay_logic.h:27-32,149-167,299-329`). Focus loss, minimization, or queue loss resets pointer and key state; the UI thread publishes keyboard/text capture and regions after each frame (`overlay_logic.h:172-218`, `debug_overlay.cpp:319-347`).

The command bar is revealed by the top hover strip and remains while the pointer is over it. A combo or popup opened by a window is never treated as a command-bar menu: it can keep an already-visible bar captured, but cannot summon a hidden bar (`overlay_logic.h:35-63`; `tests/recompiler_smoke/overlay_logic_unit.cpp:93-110`). Fullscreen hotkeys are F11 and Alt+Enter (including keypad Enter); `HotkeyKeyTracker` gives a fresh chord to fullscreen and owns repeats/releases until the press ends (`fullscreen_toggle.h:12-51`). The SDL event path invokes this ownership before game dispatch (`emu_window_sdl2.cpp:406-459`).

## UI surface

The command bar exposes Debug, Graphics, Controller, Mods and folders, Game, and View > Fullscreen (`debug_overlay.cpp:349-386`).

- Debug/status reads native status, overlay statistics, build identity, recompiled CPU, and GPU diagnostics (`debug_overlay.cpp:388-424,684-735`).
- Graphics reads the current shared Resolution Scale index and posts a save-only scale action (`debug_overlay.cpp:424-452`, `graphics_settings.h:18-37`).
- Controller is Player 1 only: it reads the event-thread `ControllerSnapshot` and posts device, mapping, clear, combine/split, and rebind requests (`controller_settings.h:39-82`, `debug_overlay.cpp:454-544`).
- Mods and folders reads the bounded runtime-folder and cheat-file snapshot and posts open/rescan requests (`debug_overlay.cpp:546-617`, `mods_folders.h`).
- Game reads the launcher/game identity section (`debug_overlay.cpp:646-681`).
- View > Fullscreen posts the fullscreen action; the event-thread host applies it (`debug_overlay.cpp:369-376,619-627`, `emu_window_sdl2_vk.cpp:281-289`).

### Performance HUD

The performance HUD is off by default on every launch and is toggled by F12. It draws one bottom-left line on ImGui's foreground draw list, outside any window and capture region, so it takes no input; it uses white 70% text, no background/shadow, and an 8 px scaled margin. `kPerfHudTextScale` makes the HUD text 150% of the overlay's regular text size; `BuildPerfHud` uses the separate Roboto Bold font from `OverlayBoldFont()` and falls back to the regular font if bold loading failed, while measuring with the same font and size used for `AddText` (`perf_hud.h:18-23`, `debug_overlay.cpp:372-390`). `HotkeyKeyTracker` owns F12 only when `EmuWindow_SDL2::HasPerfHud` is true; `ActiveHotkeys` disables ownership with `kNoScancode` otherwise, and the SDL route consumes press/repeat/release even when ImGui has keyboard focus (`fullscreen_toggle.h:13-73`, `emu_window_sdl2.cpp:306-466`). Thus OpenGL/Null windows pass F12 to the game, while the Vulkan window with an overlay toggles the HUD without the UI action queue (`PerfHudVisibility`). `PerfHudSampler` samples once per second on its own thread only while visible and is joined by `DebugOverlay::Shutdown` before Vulkan teardown (`perf_hud_sampler.h:15-37`, `debug_overlay.cpp:205-208`). Values unavailable from the platform or counters display `N/A`, and the first second after enabling is all N/A; CPU/GPU/SysMem are Windows-only (`perf_hud.h:19-104`, `perf_hud_sampler.h:15-18`).

## Contracts

- The action queue is the only UI-to-setting mutation path; actions run in posting order on the bound event thread (`overlay_actions.h:39-100`).
- Settings are applied on the event thread, while the UI reads published snapshots only (`debug_overlay.h:55-100`, `emu_window_sdl2_vk.cpp:309-333`).
- Resolution Scale is save-only until the next launch because the running renderer reads rescaling information without synchronization (`graphics_settings.h:24-37`).
- Every capture/rebind begin has exactly one stop, including replacement, capture, timeout, and cancel (`rebind_session.h:39-111`).
- Controller UI is Player 1 only (`controller_settings.h:6-8,71-82`).
- First-use overlay windows intentionally share position `(16, 48)` (`debug_overlay.cpp:392-395,428,464,556`); overlap is by design.

## Extension pattern

To add a window or setting, define a small immutable snapshot/request contract, expose a bounded action through `overlay_actions.h`, bind its applier on the event thread, publish the result back as a snapshot/status, and build the window in `debug_overlay.cpp`. Keep modifications attached through defined extension points; resolve each reference from one authoritative source; fail fast with a diagnostic naming the mismatched item; and cover each extension point with a synthetic test. Add a standalone smoke unit with a deliberately weakened negative control (`tests/recompiler_smoke/overlay_actions_unit.cpp:53-116`). Do not edit generated code or unrelated runtime paths.

## Tests and input replay

The standalone smoke units cover routing and popup ownership (`overlay_logic_unit`), queue ordering/overflow/thread affinity (`overlay_actions_unit`), the shared scale table and event-thread application (`graphics_settings_unit`), Player 1 rebind and binding rules (`controller_settings_unit`), fullscreen and HUD chord ownership (`fullscreen_toggle_unit`), performance HUD math/formatting/visibility (`perf_hud_unit`), and all three embedded overlay fonts plus style byte streams and theme rules (`overlay_style_unit`). Each includes a negative control; the relevant test sources are indexed in [CODEBASE_INDEX.md](CODEBASE_INDEX.md).

For an input bug, replay the real router/command-bar headers with ImGui 1.92.9b sources from `D:\HisuiWakeBuild\cpm\imgui\v1.92.9b` (`imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`) before changing code ([WORK_PLAN.md](WORK_PLAN.md#how-to-work--procedures)).

## Known limitations

The current open Group E items are: a held key/axis can remain stuck when capture starts and release occurs during capture; `controller_assign.h` has no unit test because it needs `ParamPackage`; the Qt Resolution Scale table is a translated second copy; and a maximize fallback that the user undoes by hand needs one extra toggle ([WORK_PLAN.md](WORK_PLAN.md#group-e---overlay-hand-test)). These are documented limitations, not claims that the overlay is fully hand-tested.

