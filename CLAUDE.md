# Aevora Agent Guide

## Purpose and current status

Aevora (formerly HisuiWake) is a hobby project investigating static recompilation of Pokemon Legends: Arceus (PLA) for PC, using a modified Suyu codebase. The README identifies Suyu v0.0.12 as its base; verify the exact source revision and dependency versions before treating this checkout as a reproducible v0.0.12 build.

As of 2026-10-09, HEAD is `585c886` on `main`; overlay stages A-E and default-on options are committed. Milestone 4a synthetic work is verified; host rebuild, smoke, negative controls, and final integration runs pass (`--only-multimodule` 76/0, `--only-defaults` 24/0, `--only-aot-cache` 308/0). A new default-on PLA export, its image size, and chain-run FPS remain unverified; successful generation, compilation, or startup alone does not establish compatibility.

Start a new session from the latest `Session handoff` section in [WORK_PLAN.md](docs/Aevora/WORK_PLAN.md).

## Resuming work

Read `CLAUDE.md`, then the seventeenth-session handoff in `WORK_PLAN.md`. Start with the full default-on PLA export/image measurement or the user-selected item; hot-RVA mapping and idle-core investigation remain open. Ask the user for FPS and hardware-check details after PLA runs, never fake cache producer identity, and consider U2 only with explicit approval. The main agent investigates, designs, implements, and verifies; the Orca Code reviewer reviews code, Code Explorer performs read-only surveys, and the md Implementer handles Markdown in milestone batches. The user commits, tags, and pushes; agents never do so.

The first target is Windows x64. Planned improvements are up to 4K rendering, ultrawide support, correctly timed 60 FPS, improved ambient occlusion, increased LoD/draw distances, and additional vegetation. These are development goals.

Read [README.md](README.md) and [WORK_PLAN.md](docs/Aevora/WORK_PLAN.md) before changing code. The work plan contains the handoff evidence, ordered milestones, acceptance criteria, and the latest session handoff with open issues and the next action; older history is archived in [OLD_HISTORY.md](docs/Aevora/OLD_HISTORY.md). Update it after meaningful progress, batching related Markdown updates at work milestones and completing an unfinished batch before the final report.
Use the WORK_PLAN `How to work / procedures` section for the current helper commands and PLA sampling procedure.

## Finding files

- To locate a file, symbol, or subsystem inside this repository, consult [CODEBASE_INDEX.md](docs/Aevora/CODEBASE_INDEX.md) first, then open the referenced files directly. Fall back to repository-wide search only when the index does not cover the topic.
- Verify an index reference against the current code before relying on it; line numbers drift.
- When you locate an important file or entry point that the index lacks, or find an index entry is stale, update the index in the same change.

## Architecture and scope

- Work on the Suyu-derived exporter, generated-code build process, and runtime where PLA needs changes.
- Static CPU recompilation still requires the runtime's system services, memory handling, audio, input, and GPU implementation. Establish that architecture before describing an export as independent of the runtime.
- Hybrid AOT + JIT is an investigation tool. Record fallback activity; a successful Hybrid run does not prove fully static execution.
- Separate runtime/exporter fixes from game-specific changes to timing, camera, shaders, LoD, or assets. Identify the responsible layer before implementing a feature.
- Keep changes focused on PLA. Broader game compatibility is outside the initial support scope.

## Modding and QoL readiness

Aevora's improvements (Milestone 5 and later QoL features) depend on a recompilation that can be changed safely. Before substantial recompiler work such as the Milestone 4a main-module DLL split, and in every change to the exporter, generator, or recompiled runtime:

- Make modifications and QoL features attach through defined extension points (settings, runtime hooks/callbacks, documented generator options) instead of editing generated code or hard-coding guest addresses, offsets, or module layouts in the generator or runtime.
- Resolve every reference (guest addresses and module bases, function entries, symbols and renames, registry/registration entries, ABI fields and versions, file and package names) from one authoritative source. Do not duplicate a reference in a second place that can drift; when a value must be shared across the generator, generated code, launcher, build kit, and exporter, derive it from the same definition or verify it at load/build time.
- A small, local change (one hook, one setting, one module, one generated unit) must not break the recompiler tool or unrelated stages. Keep changes local, and when references do not match, fail fast with a diagnostic that names the mismatched item instead of producing silently broken output or falling back without a record.
- Cover each extension point and cross-component reference path with targeted synthetic tests (for example `tests/recompiler_smoke`, `tests/recompiler_fastmem`, `tests/recompiler_fpx`) so a regression is caught before a PLA export.
- Treat this as a design acceptance criterion: designs and code reviews check modifiability and reference correctness, and a change that makes later mods or QoL additions harder must state why and how that is contained.

## Repository map

| Location | Purpose |
|---|---|
| `src/suyu/game_export.cpp` | Export dialog, package generation, and generated-code build orchestration (configure, compile, link, packaging). |
| `src/core/recompiler/arm64_to_c.h` | ARM64-to-C emitter, unit splitting, and the generated module CMake (job pool sizing). |
| `src/core/arm/recomp/` | Recompiled CPU execution, guards, gaps, and diagnostics. |
| `src/dynarmic/` | JIT integration; inspect when investigating Hybrid fallback. |
| `src/core/` | Game loading, memory, and system-service runtime. |
| `src/suyu_cmd/overlay/` | Dear ImGui Vulkan debug overlay, input/statistics logic, build identity, and diagnostics; acquired process-wide by `SuyuCmd::AcquireDebugOverlay`. |
| `src/common/env_option.h` | Shared default-on flag and validated default/0/1 environment-option parsing. |
| `src/video_core/`, `src/shader_recompiler/` | GPU and shader implementation. |
| `tools/export_build_kit/` | Export build-kit support; match kit objects and libraries to the runtime build. |
| `CMakeLists.txt`, `CMakeModules/`, `cpmfile.json` | Build configuration and dependency acquisition. |
| `externals/`, `.patch/`, `vcpkg*.json`, `vcpkg-overlays/` | Dependencies, patches, and alternate package configuration. |
| `src/tests/`, `tests/` | Existing verification code; inspect relevant targets before adding tests. |
| `tests/recompiler_smoke/overlay_logic_unit.cpp`, `build_identity_unit.cpp`, `env_option_unit.cpp` | Standalone overlay/build-identity/default-option smoke tests. |
| `docs/Aevora/` | Current project documentation. |
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
4. Run the required verification named in the task and other checks appropriate to the changed behavior. For exporter changes, exercise the affected stage and inspect its artifacts, not just the host build; do not repeat a passed check for an identical diff unless evidence is missing or there is a doubt.
5. Record the source revision, commands, configuration, results, and remaining uncertainty in the work plan in the next milestone batch. Save full build/test output outside the repository; reports must include pass/fail, exit code, failure context, and relevant warnings or excerpts while keeping sensitive logs and game outputs local.
6. Report what changed and what was verified. Do not claim success for stages that were skipped or blocked.

Continue work within the user's authorized scope. Ask for missing information only when it blocks the next dependent step; continue independent work where possible. Creating these handoff files does not authorize publishing additional commits or releases.

The user makes all commits, tags, and pushes. Do not run `git commit`, `git tag`, or `git push`. For each coherent change, add a `## vX.Y.Z-devN - Unreleased` section (or extend the current one) in [CHANGELOG.md](CHANGELOG.md); write it in the next milestone batch and complete that batch before the final report. Then give the user the list of files to commit and a suggested commit message.

Claude Code auto memory is disabled for this project (`autoMemoryEnabled: false` in `.claude/settings.json`). Record lasting rules in this guide and project state in [WORK_PLAN.md](docs/Aevora/WORK_PLAN.md), not in agent memory.

## Agent orchestration

- Codex loads [AGENTS.md](AGENTS.md), which points it to this guide; keep AGENTS.md as a pointer and put project rules in CLAUDE.md.
- Codex workers are started through Orca orchestration: `orca orchestration worker-start --agent codex` with these roles:
  - Code reviewer: `--model gpt-6.1-sol --effort high`; reviews code changes against the "Readable code", "Architecture and scope", "Working method", "Build and verification rules", and "Modding and QoL readiness" sections and cites the rule for each finding; does not edit files unless the task says so.
  - md Implementer: `--model gpt-5.6-luna --effort medium`; creates, edits, and manages Markdown files (for example WORK_PLAN.md, OLD_HISTORY.md, CHANGELOG.md, CODEBASE_INDEX.md, README.md, CLAUDE.md); does not edit source or build files.
  - Code Explorer: `--model gpt-5.6-luna --effort medium`; explores and checks the codebase (locating files, symbols, call sites, build configuration, dependency definitions) and reports `path:line` references with a concise conclusion; read-only, does not edit files or run builds/exporters/cleanup scripts.
- Use the matching role whenever a task needs it: codebase exploration goes to Code Explorer, Markdown changes go to md Implementer, and code changes are reviewed by Code reviewer.
- Run Orca workers one at a time; start the next worker only after the previous one reports completion.
- Every Codex worker specification tells the worker to read CLAUDE.md first; Code reviewer specifications require citing the CLAUDE.md rule for each finding.
- Do not poll a running Orca worker. Wait with a single blocking `orca orchestration check --wait`, then process the result; the default wait timeout is about 2 minutes, so pass a long `--timeout-ms` (for example `--timeout-ms 3600000`) on that single blocking wait.
- For implementation work, the main agent defines the target files or area, change, required verification, and completion condition before editing. Implement one behavior that can be implemented and verified as one unit, without unrelated improvements; if a design change or scope expansion becomes necessary, preserve changes already made, report the issue, and ask or decide before continuing. Do not start the next work item after completing the requested one unless the user's request already covers it.
- Request related WORK_PLAN, CHANGELOG, CODEBASE_INDEX, or README updates from the md Implementer in milestone batches rather than after every small step.
- No Claude subagents are used; the main Claude agent handles investigation, design, code implementation and fixes, and verification of results. Codex workers are only the three Orca roles; no other agents or subagent types are launched.
- Workers follow this guide, record only verified facts, and never commit, tag, or push.
- For long worker specifications, write the spec with a file tool and start from Bash with `--spec "$(cat <file>)"`; Windows PowerShell 5.1 can break quoted native arguments, and very long heredocs can be truncated.
- A Run permits only one active `check --wait` waiter. Wait with `--types worker_done,escalation` so heartbeat/status messages do not end the wait, and pass `--ack` for the previously processed delivery. Read Orca JSON output as UTF-8.
- After `worker_done`, verify the diff and the evidence yourself, then release the worker with `orca orchestration worker-release --dispatch <dispatch id>` and acknowledge the delivery with `--ack <delivery id>` on the next `check`; do not repeat a verification already passed for the identical diff unless further changes, missing evidence, or doubt requires it.
- If `worker-start` reports `start_unknown` or `turn_start_unobserved`, read the worker terminal; a Codex update banner can leave the spec pasted but unsubmitted, and `orca terminal send --terminal <terminal handle> --enter` submits it.
- The user may also hand work to Codex directly; re-check `git status` and the diff before editing.

## Build and verification rules

- Standing export procedure: the AOT and overlay defaults are on. Set an option to `=0` to disable it; in particular, use `SUYU_AOT_FM_INLINE=0`, `SUYU_AOT_DIRECT_CHAIN=0`, `SUYU_AOT_INDIRECT_CHAIN=0`, or `SUYU_AOT_SPLIT_MAIN=0` when comparing against an all-static or feature-disabled export. Chain experiments now mean switching a chain off to compare. The other procedure rules below are unchanged.

- Suyu's log tail is lost at export exit; use `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused` status fields, never the log tail, as cache evidence.
- Never run two `rebuild-suyu.bat` or ninja invocations on one build tree concurrently.
- After `orca orchestration worker-start`, check that `turnStart` is `observed`; otherwise submit with `orca terminal send --terminal <handle> --enter`.
- Run the full integration suite once per finished set of review fixes, not after every small edit; a test-only expectation fix needs syntax checking and the affected case only.
- When a long test is re-run, delete or truncate its log first; a monitor waiting for the `exit` line otherwise reads the previous run's line and reports a stale result.
- `orca orchestration check --wait` redelivers the previous message unless the last `delivery_*` id is passed with `--ack`; a message with only `heartbeat` content is not a result.
- Do not generate Python or Markdown through shell heredocs with `\\n` escapes (they become literal newlines); use Edit/Write tools, and syntax-check Python edits with `ast.parse`.
- A negative control (deliberately breaking the model or production list) is required before trusting a new test that passed on the first run.
- The persistent AOT cache identifies its producer by git commit hash plus branch and stores the launcher: an uncommitted runtime-only change reuses the old launcher, while a new commit makes the cache unusable and requires a full rebuild/export. Plan relink-only or commit plus full export before exporting.
- `(JIT-capable)` in the title bar is a strict-static qualifier, not Hybrid.
- Orca `check --wait` can replay an earlier message; inspect sender/subject, acknowledge the previous delivery id, pass the run, release each worker with `worker-release`, and do not treat heartbeat messages as results.
- The host rebuild helper is `D:\HisuiWakeBuild\diag\buildsuyu.cmd`; run it through `cmd.exe /c` with an absolute path and keep logs outside the repository.
- PLA samplers are local diagnostics; a recompiled image has no PDB, so attribute DLL samples by RVA. The JIT exclusive lock is inline and cannot be compared with the separate SpinLock page.
- A new standalone test must fail under a deliberately weakened implementation, and that negative control must itself be data-race free.

- The inspected root build requires CMake 3.22 or newer and C++20. A working Windows baseline (MSVC 14.51 from Build Tools 2026, Ninja, Git for Windows for `patch`, Vulkan SDK for `glslangValidator`, short ASCII `CPM_SOURCE_CACHE`) is recorded in the work plan's Key facts from archived history section (details in `OLD_HISTORY.md`); `docs/Aevora/BUILD.md` has not been written yet.
- A static launcher links all recompiled modules into one image; MSVC x64 images are limited to 2 GB (LNK1248). Check the image size impact of generated-code changes before a full PLA export.
- When `suyu.exe` from a source build runs an export, the exporter reuses and reconfigures that build tree. Afterwards, clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before packaging an export build kit, and treat `bin/suyu-cmd-static.exe` and `recomp_clang/` in that tree as local game-derived output.
- After every export-running test, run `rebuild-suyu.bat`-style cleanup from PowerShell, clearing `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR`, then rebuild `suyu` and `suyu-cmd`.
- Keep game-derived caches, including the future persistent AOT cache, under the user-data directory, never in the repository or an output folder.
- Many `externals/` directories were empty at handoff. The active CMake code uses CPM helpers and `cpmfile.json`; `.gitmodules` alone neither supplies dependencies nor records all required pinned revisions in a new repository.
- Do not assume `git submodule update --init --recursive` repairs a source archive. Determine the actual acquisition path and pinned versions for each required dependency.
- Keep build directories and game exports outside tracked source. Use an ASCII-only local build/export path for a controlled reproduction, then test non-ASCII paths where relevant.
- Validate UTF-8/Unicode boundaries, quoting, and paths with spaces for Windows process and linker changes.
- Add targeted tests for substantive fixes where practical. Avoid tests that merely repeat implementation details or require committing game material.
- Prefer existing small fixtures or synthetic inputs for reproducible exporter tests. PLA gameplay tests remain local and require human observation.

## Licensing, inputs, and releases

- Aevora's software license is `GPL-3.0-or-later`. Retain existing file-level SPDX headers, copyrights, dependency licenses, `LICENSES/`, and `.reuse/dep5`.
- Game code/assets, keys, firmware, dumps, saves, and generated game exports are not public repository inputs. Verify tracked files as well as ignore rules before publication.
- Users supply their own game and update files, keys, and any required firmware. Do not hard-code a developer's paths or bundle these inputs.
- Preserve the hobby/no-donations policy. It is a project policy, not an additional software-license restriction.
- Version releases as `vX.Y.Z`; development versions as `vX.Y.Z-devN`. Follow the README definitions and document the counting rule before automating `devN`.
- Keep new developer builds labeled as experimental until their acceptance criteria are met. Source publication does not establish a tested release.
