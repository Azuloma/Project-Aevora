# HisuiWake Agent Guide

## Purpose and current status

HisuiWake is a hobby project investigating static recompilation of Pokemon Legends: Arceus (PLA) for PC, using a modified Suyu codebase. The README identifies Suyu v0.0.12 as its base; verify the exact source revision and dependency versions before treating this checkout as a reproducible v0.0.12 build.

As of 2026-10-05, the source build works on Windows x64, the JIT path runs PLA, and the observed Hybrid export with all modules recompiled runs with zero JIT transitions but below JIT speed (15-20 FPS with chain budget 256 versus 30 FPS JIT). A PLA static AOT export with fallback disabled also builds and runs into gameplay with zero JIT transitions at about 16-20 FPS, versus 30 FPS JIT, but a complete gameplay checklist has not been confirmed. Successful code generation, compilation, or startup alone does not establish game compatibility. Keep public descriptions consistent with the evidence.

Start a new session from the latest `Session handoff` section in [WORK_PLAN.md](docs/HisuiWake/WORK_PLAN.md).

The first target is Windows x64. Planned improvements are up to 4K rendering, ultrawide support, correctly timed 60 FPS, improved ambient occlusion, increased LoD/draw distances, and additional vegetation. These are development goals.

Read [README.md](README.md) and [WORK_PLAN.md](docs/HisuiWake/WORK_PLAN.md) before changing code. The work plan contains the handoff evidence, ordered milestones, acceptance criteria, and the latest session handoff with open issues and the next action. Update it after meaningful progress.

## Finding files

- To locate a file, symbol, or subsystem inside this repository, consult [CODEBASE_INDEX.md](docs/HisuiWake/CODEBASE_INDEX.md) first, then open the referenced files directly. Fall back to repository-wide search only when the index does not cover the topic.
- Verify an index reference against the current code before relying on it; line numbers drift.
- When you locate an important file or entry point that the index lacks, or find an index entry is stale, update the index in the same change.

## Architecture and scope

- Work on the Suyu-derived exporter, generated-code build process, and runtime where PLA needs changes.
- Static CPU recompilation still requires the runtime's system services, memory handling, audio, input, and GPU implementation. Establish that architecture before describing an export as independent of the runtime.
- Hybrid AOT + JIT is an investigation tool. Record fallback activity; a successful Hybrid run does not prove fully static execution.
- Separate runtime/exporter fixes from game-specific changes to timing, camera, shaders, LoD, or assets. Identify the responsible layer before implementing a feature.
- Keep changes focused on PLA. Broader game compatibility is outside the initial support scope.

## Repository map

| Location | Purpose |
|---|---|
| `src/suyu/game_export.cpp` | Export dialog, package generation, and generated-code build orchestration (configure, compile, link, packaging). |
| `src/core/recompiler/arm64_to_c.h` | ARM64-to-C emitter, unit splitting, and the generated module CMake (job pool sizing). |
| `src/core/arm/recomp/` | Recompiled CPU execution, guards, gaps, and diagnostics. |
| `src/dynarmic/` | JIT integration; inspect when investigating Hybrid fallback. |
| `src/core/` | Game loading, memory, and system-service runtime. |
| `src/video_core/`, `src/shader_recompiler/` | GPU and shader implementation. |
| `tools/export_build_kit/` | Export build-kit support; match kit objects and libraries to the runtime build. |
| `CMakeLists.txt`, `CMakeModules/`, `cpmfile.json` | Build configuration and dependency acquisition. |
| `externals/`, `.patch/`, `vcpkg*.json`, `vcpkg-overlays/` | Dependencies, patches, and alternate package configuration. |
| `src/tests/`, `tests/` | Existing verification code; inspect relevant targets before adding tests. |
| `docs/HisuiWake/` | Current project documentation. |
| `AGENTS.md` | Codex entry point; points to `CLAUDE.md`. |
| `docs/Suyu/` | Archived upstream documentation; some relative links may need repair. |

## Readable code

- Follow the surrounding C++20, Qt, CMake, or script conventions. Use the repository's existing formatting rules where available.
- Name variables and functions after their purpose. Include units in names when a value represents time, distance, size, or rate.
- Keep functions focused on one responsibility. Extract a helper when it makes the flow clearer or captures a reusable rule; avoid speculative frameworks.
- Make ownership, lifetime, error handling, and fallback behavior explicit. Preserve context in errors so a failed stage can be diagnosed.
- Use named constants for meaningful thresholds and configuration values. Document the reason for a workaround and the conditions under which it applies.
- Prefer early checks and simple control flow. Avoid deeply nested branches, unrelated refactors, and broad formatting changes.
- Keep comments focused on intent, assumptions, and constraints. Write code identifiers, comments, and project documentation in English; communicate with the user in Japanese unless requested otherwise.
- Put lasting fixes in the exporter/generator or runtime. Treat generated game code as local output, not the source of truth.
- For non-ASCII path fixes, trace filesystem conversion, process invocation, and response-file handling. Keep display titles separate from filesystem-safe names, and account for empty names and collisions.

## Working method

1. Inspect the current branch, diff, applicable instructions, and relevant code. Preserve user changes.
2. State the concrete problem, evidence, and expected behavior. Distinguish observations from hypotheses.
3. Make the smallest coherent change that addresses the cause. Inspect existing cleanup scripts before using them; some clear dependency caches.
4. Run checks appropriate to the changed behavior. For exporter changes, exercise the affected stage and inspect its artifacts, not just the host build.
5. Record the source revision, commands, configuration, results, and remaining uncertainty in the work plan. Keep sensitive logs and game outputs local.
6. Report what changed and what was verified. Do not claim success for stages that were skipped or blocked.

Continue work within the user's authorized scope. Ask for missing information only when it blocks the next dependent step; continue independent work where possible. Creating these handoff files does not authorize publishing additional commits or releases.

The user makes all commits, tags, and pushes. Do not run `git commit`, `git tag`, or `git push`. For each coherent change, add a `## vX.Y.Z-devN - Unreleased` section (or extend the current one) in [CHANGELOG.md](CHANGELOG.md), then give the user the list of files to commit and a suggested commit message.

## Agent orchestration

- Codex loads [AGENTS.md](AGENTS.md), which points it to this guide; keep AGENTS.md as a pointer and put project rules in CLAUDE.md.
- Codex workers are started through Orca orchestration: `orca orchestration worker-start --agent codex` with these roles:
  - Code reviewer: `--model gpt-6.1-sol --effort high`; reviews code changes against the "Readable code", "Architecture and scope", "Working method", and "Build and verification rules" sections and cites the rule for each finding; does not edit files unless the task says so.
  - md Implementer: `--model gpt-5.6-luna --effort medium`; creates, edits, and manages Markdown files (for example WORK_PLAN.md, CHANGELOG.md, CODEBASE_INDEX.md, README.md, CLAUDE.md); does not edit source or build files.
- Use the matching role whenever a task needs it: Markdown changes go to md Implementer; code changes are reviewed by Code reviewer.
- Run Orca workers one at a time; start the next worker only after the previous one reports completion.
- Do not poll a running Orca worker. Wait with a single blocking `orca orchestration check --wait`, then process the result.
- Besides the two Orca roles, only these Claude subagents may be used: `code-explorer` (read-only codebase exploration and locating code) and `code-quick-implementer` (small, well-scoped code changes whose cause and approach are already decided). No other agents or subagent types are launched.
- Workers follow this guide, record only verified facts, and never commit, tag, or push.

## Build and verification rules

- The inspected root build requires CMake 3.22 or newer and C++20. A working Windows baseline (MSVC 14.51 from Build Tools 2026, Ninja, Git for Windows for `patch`, Vulkan SDK for `glslangValidator`, short ASCII `CPM_SOURCE_CACHE`) is recorded in the work plan's progress record; `docs/HisuiWake/BUILD.md` has not been written yet.
- When `suyu.exe` from a source build runs an export, the exporter reuses and reconfigures that build tree. Afterwards, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before packaging an export build kit, and treat `bin/suyu-cmd-static.exe` and `recomp_clang/` in that tree as local game-derived output.
- Many `externals/` directories were empty at handoff. The active CMake code uses CPM helpers and `cpmfile.json`; `.gitmodules` alone neither supplies dependencies nor records all required pinned revisions in a new repository.
- Do not assume `git submodule update --init --recursive` repairs a source archive. Determine the actual acquisition path and pinned versions for each required dependency.
- Keep build directories and game exports outside tracked source. Use an ASCII-only local build/export path for a controlled reproduction, then test non-ASCII paths where relevant.
- Validate UTF-8/Unicode boundaries, quoting, and paths with spaces for Windows process and linker changes.
- Add targeted tests for substantive fixes where practical. Avoid tests that merely repeat implementation details or require committing game material.
- Prefer existing small fixtures or synthetic inputs for reproducible exporter tests. PLA gameplay tests remain local and require human observation.

## Licensing, inputs, and releases

- HisuiWake's software license is `GPL-3.0-or-later`. Retain existing file-level SPDX headers, copyrights, dependency licenses, `LICENSES/`, and `.reuse/dep5`.
- Game code/assets, keys, firmware, dumps, saves, and generated game exports are not public repository inputs. Verify tracked files as well as ignore rules before publication.
- Users supply their own game and update files, keys, and any required firmware. Do not hard-code a developer's paths or bundle these inputs.
- Preserve the hobby/no-donations policy. It is a project policy, not an additional software-license restriction.
- Version releases as `vX.Y.Z`; development versions as `vX.Y.Z-devN`. Follow the README definitions and document the counting rule before automating `devN`.
- Keep new developer builds labeled as experimental until their acceptance criteria are met. Source publication does not establish a tested release.
