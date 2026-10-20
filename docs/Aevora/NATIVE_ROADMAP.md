# Aevora Native Roadmap

Created: 2026-10-20 (session 38). This document defines the milestones from the current static-CPU export to a fully static, title-specific native application of Pokemon Legends: Arceus (PLA). It is a plan, not a record of completed work; the current status and next action stay in [WORK_PLAN.md](WORK_PLAN.md).

## End goal

A title-specific, standalone native application for PLA with static CPU code and native graphics, platform, audio, input, and storage layers.

The roadmap is complete only when the supported gameplay path (defined in N0) runs without:

- a CPU JIT (dynarmic or any other runtime code generation),
- Maxwell or other emulated GPU processing (GPU command-stream processing, GPU MMU emulation, runtime Maxwell shader translation), and
- a general emulator runtime (`Core::System`, the Horizon kernel emulation and its scheduler, the HLE IPC service framework, or the emulated HID, audio, and filesystem cores).

All Suyu-derived GUIs are removed. They are replaced by two Aevora programs: the **Aevora Launcher**, which starts and configures the native application, and the **Aevora Recompiler**, which turns the user's own game files into that application.

## Starting point (verified 2026-10-20)

- CPU: strict static exports run the five PLA modules (`rtld`, `main`, `sdk`, `subsdk0`, `subsdk1`) as recompiled C; `main` is a DLL (Milestone 4a). An earlier dev6 run recorded zero static-to-JIT transitions. The launcher `suyu-cmd-static` still links dynarmic, and `(JIT-capable)` appears in its title bar.
- Everything else runs inside the Suyu runtime: `Core::System`, kernel emulation with three emulated cores and CoreTiming, HLE IPC services, `video_core` (Maxwell engines, GPU MMU, texture cache, runtime shader recompiler), `audio_core`, `hid_core`/`input_common`, and the NCA/RomFS virtual filesystem with keys at run time.
- Tools: the exporter lives in the Qt GUI (`src/suyu/game_export.cpp`) and depends on the Qt frontend and the core loader. The game window is the SDL frontend `src/suyu_cmd` with the Aevora ImGui overlay.
- Upstream frontends still in `src/CMakeLists.txt`: `src/suyu` (Qt), `qt_common`, `frontend_common`, `libretro_core`, `web_service`, `dedicated_room`, and `yuzu_room_standalone`.

## Relationship to existing milestones

- Milestones 1-3 and the covered Milestone 4a work stay as recorded in [OLD_HISTORY.md](OLD_HISTORY.md). Milestone 4 (static CPU execution) is the CPU part of this roadmap and finishes in N1.
- Milestone 5 (4K, ultrawide, 60 FPS, ambient occlusion, LoD, vegetation) attaches to the native graphics layer from N8 onward through defined extension points, not to `video_core`.
- Milestone 6 (release readiness) applies to the final native application and tools in N10.
- The current host-CPU work (WORK_PLAN B-items) improves the emulator runtime that N2-N9 replace. Results that carry over, such as the per-thread CPU-budget metric and timing lessons, remain useful; runtime-only tuning of HostTiming or the emulated scheduler does not carry over.
- Labels: runtime milestones are `N0`-`N10` and tool/GUI milestones are `G1`-`G4`. They do not reuse the M, B, U, F, or D labels already in use.

## Rules for every milestone

- **Transition with fallback, completion without it.** During migration a native subsystem may run beside the emulated one behind an option, and every fallback to the emulated path is counted and logged, as with Hybrid AOT + JIT. A milestone is complete only when the emulated path is removed from the supported gameplay path, not merely unused.
- **Structural gates.** Completion is checked from the build output (link map, imports, and a symbol deny list for the removed component), not only by observing that a feature works. The deny list lives in one script shared by all milestones.
- **Inventory first.** Each subsystem replacement starts from the N0 inventory of what PLA actually calls along the supported path, and implements only that.
- **One authoritative source.** Guest addresses, SDK symbol bindings, service and command lists, NVN entry points, and asset layouts come from the exporter's single definition and are verified at load or build time (CLAUDE.md "Modding and QoL readiness"). Hooks never hard-code guest addresses in the runtime.
- **Tests.** Each extension point and cross-component path has a synthetic test with a negative control. PLA gameplay checks stay local and need human observation.
- **Measurement.** Each milestone records host CPU ms per game frame for the process and each thread at the 30 FPS cap, with the p99 frame interval, at the same position as the previous measurement.
- **Inputs.** Game files, keys, and firmware come from the user. Keys and firmware are needed only by the Aevora Recompiler at export time, never by the native application at run time. Game-derived output stays in the user's data directory and is never distributed.

## Supported gameplay path

N0 fixes the exact definition and a repeatable check list. The proposed default, which the user confirms in N0, is: boot to the title screen; load an existing save; move through Jubilife Village and the Obsidian Fieldlands; one wild catch and one battle; open the main menu, Pokedex, and bag; save; quit. A new game from the start, cutscenes outside this path, and online features are out of scope until added explicitly.

## Runtime milestones

### N0: Boundary inventory and supported path

Goal: measure what PLA uses along the supported path, so every later milestone has a fixed scope.

- Record the supported gameplay path and its manual check list.
- Inventory, with opt-in diagnostics that are execution-neutral when off: SVCs by number; IPC services and command IDs; nvdrv ioctls; NVN entry points obtained through the bootstrap loader; audio renderer usage; HID state and applets used; filesystem paths and access patterns (RomFS, save data, other).
- Map which SDK functions in the recompiled `sdk` module lead to each SVC or IPC call, as candidate native hook points.
- Add the structural-gate script and its first report on the current launcher.

Done when: the inventory files and the gate report are recorded, and the user has confirmed the supported path.

### N1: CPU without a JIT

Goal: the launcher contains no runtime code generation.

- Build the static launcher without dynarmic. A lookup miss or unimplemented instruction fails fast with a diagnostic naming the guest address and module, instead of falling back.
- Exclusive access and the reservation monitor use native atomics without the dynarmic monitor.
- Hybrid AOT + JIT stays available only in a separate investigation build.

Done when: the dynarmic deny-list check passes on the launcher, the supported path runs, and the title bar no longer shows `(JIT-capable)`.

### N2: Native guest memory model

Goal: a title-specific guest address space without the emulated kernel memory manager.

- Reserve and map code, heap, stacks, TLS, and shared memory directly with host virtual memory, keeping the fastmem layout that generated code expects.
- Replace `KPageTable` and `KMemoryManager` paths used by the supported path with a small native mapper, with the memory SVCs from the N0 inventory implemented on it.

Done when: the kernel memory-manager deny list passes for the native build, and memory tests (map, unmap, protection, shared memory) pass with negative controls.

### N3: Native threads and synchronization

Goal: guest threads run as host threads, scheduled by the host OS.

- Map each guest thread to one host thread; implement the thread, synchronization, wait, and time SVCs from the inventory (mutexes, condition variables, address arbiters, events) on OS primitives.
- Remove the three emulated cores, `KScheduler`, and guest scheduling through CoreTiming. Keep guest-visible time (ticks, system time) consistent.
- This milestone is expected to remove most of the HostTiming and core-loop overhead measured in session 37; confirm by measurement.

Done when: the scheduler deny list passes, synchronization tests with negative controls pass, the supported path runs, and CPU ms per frame is recorded.

### N4: Native platform services

Goal: replace the HLE IPC service framework with direct native implementations.

- A design note chooses the boundary per service: hooking SDK (`nn::`) functions where the N0 map shows stable entry points, or a minimal title-specific IPC table for the remaining services. The note records the reason for each choice.
- Implement the services from the inventory that are not covered by N5-N9 (for example service manager, settings, time, account, applet stubs, power and performance mode).
- Unknown services or commands fail fast with the service and command ID.

Done when: the service-framework deny list passes, every inventoried service has a native implementation or a recorded stub with its observed effect, and the supported path runs.

### N5: Native storage

Goal: no NCA, RomFS virtual filesystem, or keys at run time.

- The Aevora Recompiler extracts or indexes RomFS and update data once, at export time, into a local asset store in the user's data directory.
- Filesystem calls on the supported path resolve to native file I/O on that store. Save data lives in a native per-user folder, with import from and export to the Suyu save layout.
- Mods attach through a defined overlay of the asset store (Milestone 5 extension point), not by editing extracted files.

Done when: the native application runs the supported path with no keys present, the filesystem-core deny list passes, and save import/export round-trips in a test.

### N6: Native input

Goal: host controllers and keyboard feed PLA directly.

- Read input with the host API (SDL or XInput) and write the state the game reads, through the HID shared-memory layout or `nn::hid` hooks, as chosen in N4.
- Replace emulated controller applets on the supported path with a native player-1 configuration owned by the launcher.

Done when: the `hid_core` and `input_common` deny lists pass, input tests pass, and the supported path is playable with a controller and with keyboard and mouse.

### N7: Native audio

Goal: PLA audio plays through a native output without the emulated audio core.

- Implement the audio renderer features the inventory shows, as a title-specific library, and output through WASAPI or SDL.
- Keep audio timing independent of the removed emulated scheduler.

Done when: the `audio_core` deny list passes, a renderer test with a negative control passes, and the user confirms correct audio on the supported path.

### N8: Native graphics

Goal: PLA renders through Vulkan without Maxwell processing. This is the largest milestone and is split into steps.

- **N8a NVN interception.** Intercept the NVN bootstrap loader and procedure table, and log every entry point used on the supported path. During transition, unimplemented calls may still forward to the emulated path, counted and logged.
- **N8b Resources and commands.** Map NVN devices, queues, memory pools, buffers, textures, samplers, and command buffers to Vulkan objects, using the formats and layouts NVN states instead of guessing them from guest memory.
- **N8c Offline shaders.** The Aevora Recompiler translates every PLA shader to SPIR-V at export time with the existing shader recompiler as an offline tool, and stores the result with pipeline metadata in the user's data directory. A shader missing at run time fails with a diagnostic naming it.
- **N8d Presentation and pacing.** Native swapchain, frame pacing at the 30 FPS cap, and resolution scale, with the Aevora overlay rendered on the native swapchain.
- **N8e Removal.** Remove the nvdrv/nvhost path, GPU MMU, Maxwell engines, texture cache, and runtime shader recompilation from the native build.

Done when: the `video_core` Maxwell and runtime-shader deny lists pass, the supported path renders correctly by user observation, and CPU and GPU frame times are recorded. Milestone 5 graphics features attach here.

### N9: Standalone application

Goal: one title-specific native executable package with no general emulator runtime.

- Remove `Core::System` and all remaining emulator wiring from the native build; the application starts directly into PLA.
- Package layout, version identity, and the ABI between the Aevora Recompiler output and the runtime are versioned and checked at start.

Done when: the full deny list (JIT, Maxwell, kernel emulation, service framework, emulated HID, audio, and filesystem cores) passes on the release build, and the supported path passes its check list.

### N10: Release readiness

Goal: Milestone 6 for the native application and tools: clean-checkout build, release-input audit, GPL source instructions, defined `devN`, user documentation, and README and changelog matching the evidence.

## Tool and GUI milestones

The G milestones can run in parallel with N1-N8. G4 must finish before N10.

### G1: Recompiler core and CLI

Goal: the exporter works without the Qt GUI.

- Move export orchestration out of `src/suyu/game_export.cpp` into a library with a command-line program (`aevora-recompiler`), keeping one definition of options, topology, and cache rules.
- The Qt exporter calls the same library until G4.

Done when: a CLI export and a Qt export of the same inputs produce identical manifests and hashes, and the export-integration matrix runs through the CLI.

### G2: Aevora Recompiler GUI

Goal: a dedicated GUI for turning the user's game files into the native application.

- Select game, update, keys, and output location; validate inputs; choose documented export options; show progress, logs, and cache state; report failures with the failing stage.
- Built on the G1 library; no Suyu UI code. The GUI toolkit is chosen in a design note before implementation.

Done when: the GUI produces the same export as the CLI, and the user completes a PLA export through it.

### G3: Aevora Launcher

Goal: a dedicated program that starts and configures the native application.

- Start PLA, edit graphics, input, and audio settings, manage mods and save data, and show the version and logs.
- Settings are stored in Aevora's own format and read through one definition shared with the runtime and the in-game overlay.

Done when: the launcher configures and starts the native application, and its settings take effect in game.

### G4: Suyu GUI removal

Goal: no Suyu-derived GUI remains.

- Remove `src/suyu` (Qt), `qt_common`, and the parts of `frontend_common`, `libretro_core`, `web_service`, `dedicated_room`, and `yuzu_room_standalone` that the native application and tools do not need. Each removal is listed with its reason.
- Remove user-visible Suyu branding from the remaining programs while keeping SPDX headers, copyrights, and license files.

Done when: the repository builds the native runtime, the Aevora Recompiler, and the Aevora Launcher without these targets, and no user-visible Suyu GUI or branding remains.

## Order and dependencies

| Milestone | Depends on | Notes |
| --- | --- | --- |
| N0 | - | Fixes scope for everything after it. |
| N1 | N0 | Can start immediately after N0. |
| N2 | N0 | Before N3, which needs the native mapper for stacks and TLS. |
| N3 | N2 | Largest effect on host CPU per core. |
| N4 | N3 | Defines the hook-or-IPC boundary used by N5-N7. |
| N5, N6, N7 | N4 | Independent of each other. |
| N8 | N4, N5 (shaders and assets), G1 (offline shader stage) | Largest milestone; N8a can start after N0 for inventory. |
| N9 | N1-N8 | Final structural gate. |
| N10 | N9, G4 | Release. |
| G1 | - | Can start any time. |
| G2 | G1 | |
| G3 | G1 (settings definition); N5-N8 for full settings coverage | Can start on the current runtime. |
| G4 | G2, G3 | |

## Open questions

- Confirm the supported gameplay path proposed above.
- Choose the GUI toolkit for G2 and G3 (for example ImGui, which the overlay already uses, or another toolkit), considering license and maintenance.
- Decide whether current WORK_PLAN B-item tuning of the emulator runtime continues, or whether work moves to N0.
