# HisuiWake - Pokemon Legends: Arceus Recompiled

HisuiWake is an experimental project aiming to statically recompile **Pokemon Legends: Arceus**, the first entry in the Pokemon Legends series, for PC. It is based on **Suyu v0.0.12** and developed with the assistance of AI. The exact upstream lineage is still being verified; the recompiler work appears to descend from an `mk8-recomp` fork (unverified; see [CODEBASE_INDEX](docs/HisuiWake/CODEBASE_INDEX.md)).

As of October 5, 2026, the source build works on the initial target, Windows x64. The JIT pass runs PLA at 30 FPS in observed scenes, and the v0.0.1-dev8 static AOT export with guest CPU fallback disabled achieved approximately 25-30 FPS for gameplay with zero JIT transitions in the overworld scene. No releases or build artifacts are public, and development builds are experimental. The features described below are goals, not a list of completed features.

## Development Approach

HisuiWake uses AI-assisted coding, including "vibe coding." The goal is to keep the code understandable and maintainable by humans, with human review and hands-on testing guiding development. Generated code and successful compilation alone are not treated as proof that the game works correctly.

Bug reports, questions, and feature requests are welcome. Useful reports include the build version, game update version, steps to reproduce the problem, and relevant logs.

## Planned Features

- Rendering resolutions up to 4K.
- Ultrawide aspect ratios.
- 60 FPS with correct game and animation timing.
- Improved ambient occlusion.
- Increased level-of-detail and draw distances.
- Additional or denser vegetation.

The intended setup requires users to supply their own base game NSP and the supported update file. Keys and any required firmware are also supplied by the user; they are not included with HisuiWake.

## Recompiler Scope

HisuiWake's modified Suyu recompiler is focused on the changes needed to statically recompile Pokemon Legends: Arceus. Other games may work, but they are outside this project's intended support scope, and using HisuiWake to recompile them is not recommended.

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

## Hobby Project and Financial Support

HisuiWake is a personal hobby project. It is not run for financial gain or recognition.

There will be no Patreon, Ko-fi, or other fundraising campaign, and the project does not accept donations or other monetary support. Thank you for respecting this policy.

## License

HisuiWake's Suyu-derived software and original code contributions are licensed under the **GNU General Public License, version 3 or later (`GPL-3.0-or-later`)**. See [LICENSE.txt](LICENSE.txt) for the full license text.

Existing source files, third-party dependencies, and assets retain their respective copyright notices and license terms. See individual file headers, [LICENSES/](LICENSES/), and [.reuse/dep5](.reuse/dep5) for the retained notices.

When distributing HisuiWake binaries covered by the GPL, provide access to the corresponding source code, including the modifications and build scripts required by the license.

The project's decision not to accept financial support is a project policy and does not impose additional restrictions on the software license.

This license does not grant rights to Pokemon Legends: Arceus game code or assets, Nintendo firmware, or keys supplied by users. Recompiling a game does not place its code or assets under this license.

## Documentation and Upstream Attribution

- [Work plan](docs/HisuiWake/WORK_PLAN.md), [Changelog](CHANGELOG.md), [Old history](docs/HisuiWake/OLD_HISTORY_HW.md), and [Codebase index](docs/HisuiWake/CODEBASE_INDEX.md).
- Build instructions (`BUILD.md`) are not written yet.
- [HisuiWake documentation](docs/HisuiWake/): project-specific documentation.
- [Archived Suyu documentation](docs/Suyu/): upstream documentation retained for reference.
- [Upstream provenance](docs/Suyu/_main-folder/PROVENANCE.md): the original Suyu lineage record.
- [Upstream license](docs/Suyu/_main-folder/LICENSE.txt) and [license notices](LICENSES/).

The original upstream provenance document describes Suyu's history; it is retained as a reference rather than rewritten as a history of HisuiWake. Upstream copyright and license notices are preserved.
