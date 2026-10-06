# Aevora

<p align="center">
  <img src="Assets/icon-rounded.png" />
<p>
<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20macOS%20%7C%20Linux-1e90ff?logo=apple" alt="Platform">
  <img src="https://img.shields.io/badge/Recomp%20type-Static%20AoT-ff6347" alt="Recomp type">
  <img src="https://img.shields.io/badge/License-GPL%203.0%20(v3)-808080" alt="License">
<p>
<p align="center">
  <strong>An experimental static recompiled version of "Pokémon Legends: Arceus," the landmark first game in the Pokémon Legends series.</strong><br>
  We plan to add various features in the future, such as quality of life improvements and modding capabilities.
<p>
<p align="center">
  <a href="#Development Approach">Development Approach</a> |
  <a href="#Planned Features">Planned Features</a> |
  <a href="#License">License</a>
<p>

## Development Approach

Aevora uses AI-assisted coding, including "vibe coding." The goal is to keep the code understandable and maintainable by humans, with human review and hands-on testing guiding development. Generated code and successful compilation alone are not treated as proof that the game works correctly. Furthermore, for README.md, I had AI complete the incomplete text I had written, and then I rewrote it myself, removing unnecessary parts and making it easier to read.

Bug reports, questions, and feature requests are welcome. Useful reports include the build version, game update version, steps to reproduce the problem, and relevant logs.

## Project policies and support

Aevora is a personal project. It is not run for financial gain or recognition.

There will be no Patreon, Ko-fi, or other fundraising campaign, and the project does not accept donations or other monetary support.

## Planned Features

These planned features are not guaranteed to be implemented, and may change depending on the circumstances, potentially increasing or decreasing in the number of features

- Ultrawide support
- Native 4K
- Stable 60FPS
- Enhanced ambient occlusion
- Increased shadow resolution and distance
- Increased LoD and draw distance

In the expected setup, the user will need to provide the base game's NSP file and corresponding update files separately. The user will also need to provide the key and any necessary firmware, which are not included with Aevora.

## Recompiler Scope

Aevora is modified Suyu recompiler is focused on the changes needed to statically recompile Pokemon Legends: Arceus. Other games may work, but they are outside this project is intended support scope, and using Aevora to recompile them is not recommended.

For other games, consider [upstream Suyu v0.0.12](https://github.com/suyu-emu/suyu-main/releases/tag/v0.0.12) or [Drippu](https://github.com/suyu-emu/drippu). Compatibility must be evaluated per game.

Static AOT still relies on the runtime for system services, memory, audio, input, and GPU; GPU macro JIT and shader compilation remain runtime generation.

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

## License

Aevora is Suyu-derived software and original code contributions are licensed under the **GNU General Public License, version 3 or later (`GPL-3.0-or-later`)**. See [LICENSE.txt](LICENSE.txt) for the full license text.

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
