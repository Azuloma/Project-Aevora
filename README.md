<p align="center">
  <img alt="aevora-icon" src="Assets/Aevora-git.ICON.png" width="300px" />
<p>
<p align="center">
  <img src="https://img.shields.io/badge/platform-windows%20x64%20%7C%20linux%20amd64-blue?style=for-the-badge&logo=archlinux" alt="Badge">
  <img src="https://img.shields.io/badge/recompile%20type-static%20aot-b84b3a?style=for-the-badge&logo=ebox" alt="Badge">
  <img src="https://img.shields.io/badge/status-alpha-orange?style=for-the-badge&logo=penpot" alt="Badge">
  <img src="https://img.shields.io/badge/main%20language-c%2B%2B-purple?style=for-the-badge&logo=cplusplus" alt="Badge">
  <img src="https://img.shields.io/badge/ui%20framework-tauri-8069ff?style=for-the-badge&logo=tauri" alt="Badge">
  <img src="https://img.shields.io/badge/license-GPL--3.0%20(v3)-lightgrey?style=for-the-badge&logo=unlicense" alt="Badge">
<p>
<p align="center">
  <strong>An experimental static recompiled version of "Pokémon Legends: Arceus," the landmark first game in the Pokémon Legends series.</strong><br>
  Aevora is not limited to performing static recompilation via the Aevora Recompiler, and it also serves as a launcher that provides quality-of-life improvements and graphics enhancements specifically tailored for PLA.
<p>
<p align="center">
  <a href="#Status">Status</a> |
  <a href="#Features">Features</a> |
  <a href="#License">License</a>
<p>

> [!IMPORTANT]
> At Aevora, we utilize AI-powered coding (including so-called "vibe coding"). Our development is guided by human review and real-world testing, prioritizing code that is easy for humans to understand and maintain. We do not consider AI-generated code or successful compilation alone to be proof that the game is functioning correctly. If you disagree with the use of AI or do not trust AI-written code, that is your right. I do not disagree with or find those opinions unfair.

> [!NOTE]
> In the expected setup, the user will need to provide the base game's NSP file and corresponding update files separately. The user will also need to provide the key and any necessary firmware, which are not included with Aevora.
> Furthermore, <strong>we do not condone the use of pirated sources on the internet to obtain these files. Please dump and acquire them from your own Switch.</strong>

## Status

We've established a milestone to completely remove Suyu branding and make the project function exclusively as Aevora and a static recompiler for PLA. Work on this is currently underway.([ROADMAP](docs/Aevora/NATIVE_ROADMAP.md))<br>
Suyu references will remain in SPDX headers, copyright notices, license information, and upstream attribution, but everything else is expected to be fully replaced.

<br>This is the current roadmap. While it may evolve based on future progress and outcomes, our immediate goal is to achieve static recompilation that does not rely on full emulation.

| Component | Current implementation | Planned native implementation | Milestone | Progress |
| --- | --- | --- | --- | --- |
| Game CPU code | Statically recompiled ARM64 game code running in a JIT-capable runtime. | Fully static execution with no CPU JIT linked into the native application. Hybrid AOT + JIT remains available only in a separate investigation build. | N1 | Done |
| Memory management | Guest memory managed by the emulated Horizon kernel. | A title-specific memory mapper using host virtual memory for code, heaps, stacks, TLS, and shared memory. | N2 | Work in progress |
| Threads, synchronization, and timing | Three emulated CPU cores, guest scheduling, and CoreTiming. | Guest threads mapped to host threads, with host OS scheduling and native synchronization while preserving game-visible timing. | N3 | Not yet |
| Platform services | Horizon services provided through the general HLE IPC framework. | Direct native implementations through SDK hooks or a minimal PLA-specific IPC table. Unneeded services may use documented stubs. | N4 | Not yet |
| Assets and save data | NCA/RomFS virtual filesystem and runtime key handling. | Assets prepared at export time and accessed through native file I/O; saves stored in a native per-user folder. Keys and firmware are required only by the recompiler. | N5 | Not yet |
| Input | Emulated HID services and controller state through `hid_core` and `input_common`. | Host controller, keyboard, and mouse input delivered through native hooks or the game's expected HID memory layout. | N6 | Not yet |
| Audio | Suyu's emulated audio core. | A PLA-specific audio implementation with native output through WASAPI or SDL. | N7 | Not yet |
| Graphics | Emulated Maxwell GPU engines, GPU MMU, nvdrv/nvhost, and texture tracking. | NVN API interception mapped to Vulkan resources and commands, with native presentation and frame pacing. | N8 | Not yet |
| Game shaders | Maxwell shaders translated at runtime. | Game shaders translated to SPIR-V at export time and packaged with pipeline metadata. Runtime Maxwell shader translation is removed. | N8c | Not yet |
| Application runtime | Static CPU execution hosted inside `Core::System` and the general emulator runtime. | A standalone, title-specific application that starts directly into PLA without the general emulator runtime. | N9 | Not yet |
| Launcher and recompiler tools | Suyu-derived frontends and a Qt-based exporter. | Dedicated Tauri-based Aevora Launcher and Aevora Recompiler, backed by a shared recompiler core and CLI. | G1–G4 | Not yet |

## Platform

| Platform      | Current status                                                    | Statically recompiled components |
| ------------- | ----------------------------------------------------------------- | -------------------------------- |
| Windows (x64) | Experimental; gameplay verified, performance improvements ongoing | Game CPU code (ARM64 → x64)     |
| Linux (amd64) | Not yet validated                                                 | No verified AOT build            |

## Features

- Native CPU processing in Static AOT recompiler
- An implementation of ImGUI, useful for debugging and quick configuration changes

## Planned Features

These planned features are not guaranteed to be implemented, and may change depending on the circumstances, potentially increasing or decreasing in the number of features

- Remove the GUI associated with Suyu and provide GUIs for the Aevora launcher and Aevora Recompiler
- Ultrawide support
- No Dynamic resolution
- Implementation of anti-aging (TAA, FXAA, SMAA)
- Interpolated 60 FPS and 120 FPS modes
- Increased shadow resolution and distance
- Increased LoD and draw distance
- Enhanced ambient occlusion
- Reducing stutter that occurs during shader compilation
- Native controller and keyboard support

## Releases and Versioning

When releases are available, users should choose a released version. Development builds are intended for developers and are not recommended for general use.

Release versions use **`vX.Y.Z`**. Development versions use **`vX.Y.Z-devN`**, where `N` identifies the number of development commits for that version. This keeps development progress visible without increasing the release version for every commit.

| Component | Meaning                                                                     |
| --------- | --------------------------------------------------------------------------- |
| `X`     | Large project milestones. Reaching`1` marks the first stable release.     |
| `Y`     | Major feature additions and development milestones toward a stable release. |
| `Z`     | Recompiler adjustments, fixes, and other bug fixes.                         |
| `devN`  | The development commit count for the version currently being worked on.     |

For example, `v0.1.0-dev7` denotes a development build for version `v0.1.0`. The corresponding release is named `v0.1.0`, without the development suffix. `v1.0.0` marks the first stable release.

## About Aevora Recompiler

Aevora Recompiler is an improved version of the Suyu Recompiler, specifically designed to make the necessary changes for statically recompiling Pokémon Legends: Arceus. It does not support recompiling any games other than Pokémon Legends: Arceus, and <strong>Aevora Recompiler assumes no responsibility for any compatibility issues or data loss that may occur.</strong>

For other games, consider [Suyu](https://github.com/suyu-emu/suyu-main) or [Drippu](https://github.com/suyu-emu/drippu). Compatibility must be evaluated per game.

Static AOT still relies on the runtime for system services, memory, audio, input, and GPU; GPU macro JIT and shader compilation remain runtime generation.

## Project policies and support

Aevora is a personal project. It is not run for financial gain or recognition.

There will be no Patreon, Ko-fi, or other fundraising campaign, and the project does not accept donations or other monetary support.

## License

Aevora is Suyu-derived software and original code contributions are licensed under the **GNU General Public License, version 3 or later (`GPL-3.0-or-later`)**. See [LICENSE.txt](LICENSE.txt) for the full license text.

The ImGui overlay embeds Roboto, designed by Christian Robertson, ParaType, and Font Bureau, with BIZ UDPGothic Regular designed by Morisawa Inc. and Type Bank Co. for Japanese glyphs Roboto lacks; both are under the SIL Open Font License 1.1. See [LICENSES/OFL-1.1.txt](LICENSES/OFL-1.1.txt), [Roboto&#39;s OFL.txt](dist/fonts/Roboto/OFL.txt), and [BIZ UDPGothic&#39;s OFL.txt](dist/fonts/BIZUDPGothic/OFL.txt).

Existing source files, third-party dependencies, and assets retain their respective copyright notices and license terms. See individual file headers, [LICENSES/](LICENSES/), and [.reuse/dep5](.reuse/dep5) for the retained notices.

When distributing Aevora binaries covered by the GPL, provide access to the corresponding source code, including the modifications and build scripts required by the license.

The project's decision not to accept financial support is a project policy and does not impose additional restrictions on the software license.

This license does not grant rights to Pokemon Legends: Arceus game code or assets, Nintendo firmware, or keys supplied by users. Recompiling a game does not place its code or assets under this license.

## Documentation and Upstream Attribution

- [Work plan](docs/Aevora/WORK_PLAN.md), [Changelog](CHANGELOG.md), [Old history](docs/Aevora/OLD_HISTORY.md), and [Codebase index](docs/Aevora/CODEBASE_INDEX.md).
- Build instructions (`BUILD.md`) are not written yet.
- [Aevora documentation](docs/Aevora/): project-specific documentation.
- [Archived Suyu documentation](docs/Suyu/): upstream documentation retained for reference.
- [Upstream provenance](docs/Suyu/_main-folder/PROVENANCE.md): the original Suyu lineage record.
- [Upstream license](docs/Suyu/_main-folder/LICENSE.txt) and [license notices](LICENSES/).

The original upstream provenance document describes Suyu's history; it is retained as a reference rather than rewritten as a history of Aevora. Upstream copyright and license notices are preserved.
