# Aevora Work Plan

Handoff prepared: 2026-10-06. HEAD: `1d9ba61` (v0.0.1-dev11). Last session handoff: 2026-10-06, eighth session (see [Session handoff](#session-handoff-2026-10-06-eighth-session-complete)).
Older history (2026-10-04 handoff through the fourth session close on 2026-10-05, plus earlier export evidence) is archived in [OLD_HISTORY.md](OLD_HISTORY.md).

## Objective

Determine whether Pokemon Legends: Arceus can run through Suyu-derived static CPU recompilation, then develop the requested rendering and gameplay improvements on a verified runtime. Keep the code understandable and maintainable by humans, with human review and hands-on testing.

Start with a reproducible Windows x64 build and a correctly running game. Proceed to graphics improvements after the execution path is understood. Fully static PLA compatibility remains uncertain.

## Handoff status

| Item | Evidence/status |
|---|---|
| Source repository | Root contains the Suyu-derived source, build files, dependency metadata, and archived upstream documentation. |
| Git baseline | Initial commit `9f932d5`. As of 2026-10-06, HEAD is `1d9ba61` (v0.0.1-dev11); the user commits and pushes. |
| Configured origin | `https://github.com/Azuloma/Aevora.git`; remote publication was not independently verified during this handoff. |
| README | English draft describes feasibility status, planned features, support scope, versioning, and the hobby/no-donations policy. |
| License | Root `LICENSE.txt` is copied unchanged from the archived upstream license; README specifies `GPL-3.0-or-later`. Existing notices are retained. |
| Documentation | Project documents belong in `docs/Aevora/`; upstream documents are archived in `docs/Suyu/`. |
| Project name | Renamed from HisuiWake to Aevora on 2026-10-05 (README in `a3063a2`, other documents in this change); the local repository folder is still named HisuiWake. |
| Dependencies | Acquired by CPMUtil from `cpmfile.json` (bundled Qt 6.11.1, SDL3, FFmpeg, OpenSSL on MSVC); `.gitmodules` is not the acquisition path. Verified on 2026-10-04. |
| Host source build | Verified: MSVC 14.51 (Build Tools 2026) + Ninja Release; `suyu.exe`, `suyu-cmd.exe`, and the export build kit build and start. `docs/Aevora/BUILD.md` is not written yet. |
| PLA export/run | JIT: 30 FPS. Hybrid (dev6-era, translate-all, budget 256): 15-20 FPS. v0.0.1-dev8 static AOT + Build (clang-cl): 25-30 FPS with no fallback in the observed overworld scene; conditions were not fully matched. Dev9 FMI export failed to link: image about 2.19 GB exceeds the 2 GB x64 limit (LNK1248). |
| Proposed path patch | Superseded. The observed non-ASCII failure was the launcher's window-title conversion (fixed in v0.0.1-dev5). Filename sanitization was not implemented; the package name still contains `é`. |

The cleanup already removed upstream automation, temporary material, old screenshots, and an isolated `tools/static_recompiler` demo. Do not perform another broad deletion pass before a working baseline identifies the files actually required.


## Key facts from archived history

- **Host/tool baseline:** Windows 11 Home 10.0.26300, Intel Core i5-12400F, 15.8 GB RAM, RTX 2060 SUPER; MSVC 14.51 via Build Tools 2026 `vcvars64`, Ninja 1.13.2, CMake 4.4.3, LLVM/clang-cl 22.1.8, Python 3.14.7, Git for Windows 2.56.0 `patch`, Vulkan SDK 1.4.363.0 `glslangValidator`; use a short ASCII `CPM_SOURCE_CACHE`. ([details](OLD_HISTORY.md#2026-10-04-milestone-1-environment-and-first-configure-attempt))
- **Configure/build:** use the recorded CMake configure with explicit Git/glslang paths and `cmake --build <build-dir> --target suyu suyu-cmd -j 8`. ([details](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **Dependencies:** CPMUtil uses `cpmfile.json`; extraction uses `cmake -E tar xf` because `file(ARCHIVE_EXTRACT)` failed on non-ASCII entries. ([details](OLD_HISTORY.md#2026-10-04-configure-success-cpmutil-extraction-fix-and-first-build))
- **VMA:** implementation moved to executable targets to avoid static-Qt duplicate symbols. ([details](OLD_HISTORY.md#2026-10-04-vma-implementation-moved-to-executables-suyuexe-links))
- **JIT/GPL:** PLA 1.1.1 ran at 30 FPS; the GPL rendering defect was isolated and GPL disabled by default. ([details](OLD_HISTORY.md#2026-10-04-first-pla-jit-run-on-the-source-build))
- **Export/build:** the export kit was produced; clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before packaging. ([details](OLD_HISTORY.md#2026-10-04-export-build-kit-produced))
- **Generated-code scaling:** job-pool sizing and unit splitting use 8 MiB/5,000-block defaults and compiler-aware budgets. ([details](OLD_HISTORY.md#2026-10-04-generated-code-build-parallelism-milestone-2))
- **Launcher/Hybrid:** the non-ASCII crash was fixed in dev5; SHL was implemented but gated off for Hybrid; Hybrid results were 17, 14, and 15-20 FPS at budget 256. ([details](OLD_HISTORY.md#2026-10-05-shl-already-implemented-but-gated-off-for-hybrid))
- **Chain budget:** 256 improved long-slice behavior from about 14 FPS at 4096 to 15-20 FPS. ([details](OLD_HISTORY.md#2026-10-05-long-slice-chain-budget-lowered-to-256-v001-dev6-unverified-in-export))
- **AOT conclusion:** Hybrid executed AOT-recompiled native application code in the observed run, while runtime services and GPU macro JIT/shader compilation remain; static AOT with fallback disabled is the stronger check. ([details](OLD_HISTORY.md#2026-10-05-is-the-hybrid-cpu-path-ahead-of-time-static-recompilation))
- **Dev6 static AOT:** The fallback-disabled export reached 16-20 FPS in the observed scene. ([details](OLD_HISTORY.md#2026-10-05-first-static-aot-export-v001-dev6-runs-with-no-fallback-16-20-fps))
- **Static hot block:** The dev6 hot block contributed 15-19% of busy-core samples, and guest FPCR.FZ was set in about 96% of JIT context saves. ([details](OLD_HISTORY.md#2026-10-05-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld)) ([details](OLD_HISTORY.md#2026-10-05-guest-fpcr-measured-pla-runs-with-fz-set-fpx1-gate-closed))
- **Dev8 FZ-aware FPX1 gate:** The harness recorded 0 mismatches over 100.6 M cases. ([details](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-v001-dev8-unverified-in-pla))
- **Dev8 static AOT:** The fallback-disabled export reached 25-30 FPS with zero fallback in the observed scene. ([details](OLD_HISTORY.md#2026-10-05-fz-aware-fpx1-gate-verified-in-pla-static-aot-25-30-fps))
- **Dev8 static sampling:** The dominant hot block fell from 15-19% to 2-3.5% of busy-core samples, and CPU cores waited 35-49% of samples. ([details](OLD_HISTORY.md#2026-10-05-dev8-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld))
- **Same-scene JIT comparison:** JIT process CPU was 275.7% versus 320.3% dev8 static; HostTiming, GPU-thread load, and `NtGdiDdDDIQueryAdapterInfo` are not AOT-specific; the remaining gap is guest CPU execution cost (static about 1.5-2x JIT CPU per core, estimate). ([details](OLD_HISTORY.md#2026-10-05-same-scene-jit-sampling-remaining-gap-is-guest-cpu-cost))
- **FMI (dev9):** Opt-in `SUYU_AOT_FM_INLINE=1` inlines the FM1 fast path; targeted tests passed (0 mismatches); out-of-line memory helpers were about 11% of busy-core samples. ([details](OLD_HISTORY.md#2026-10-05-source-inspection-of-memory-access-and-the-opt-in-fmi-inline-fast-path-v001-dev9-unmeasured-in-pla))
- **FMI LNK1248:** The PLA FMI static launcher reached image size `0x8C0E0000` (about 2.19 GB) and failed LNK1248; the MSVC fallback path could not help and diagnostics were lost (addressed in dev10). ([details](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248))

## Milestone 1: Establish a reproducible host build

- [x] Record the checkout revision, Windows version, CPU, RAM, GPU/driver, compiler, SDK, CMake, Ninja, and Qt versions.
- [ ] Verify the source lineage using the README, archived provenance, and upstream revision information. Do not assume the new Git history contains Suyu's original commits.
- [x] Inspect `CMakeLists.txt`, `CMakeModules/`, `cpmfile.json`, `.gitmodules`, and package configuration to identify the actual dependency acquisition paths and pinned versions.
- [x] Obtain required dependencies through the build's intended mechanism. Record downloads, patches, versions, and failures; do not substitute arbitrary latest revisions.
- [x] Configure and build the Suyu-derived application in a local ASCII-only build path. Save the exact commands and options.
- [x] Build or locate the matching export build kit if required by the selected exporter path. Verify object/library/compiler compatibility.
- [ ] Confirm the application starts and can load PLA through its ordinary JIT path with the user's local inputs. Record the game update/module build IDs and settings without publishing game material or keys. (Startup, PLA 1.1.1 gameplay, and settings are recorded; module build IDs are not.)
- [ ] Write the successful setup in `docs/Aevora/BUILD.md` when verified. Repair relevant archived documentation links as needed.

**Acceptance:** a documented source build produces a launchable application; required dependencies and the export kit can be reproduced; PLA's JIT behavior is recorded as the comparison baseline. A configure failure or build-only result does not satisfy the gameplay check.

## Milestone 2: Resolve export build failures

- [x] Inspect `src/suyu/game_export.cpp` and its current generator/build helpers. Reproduce the failure with a fixed source revision, game update, backend, and toolchain.
- [ ] Export Source separately when useful to inspect generated inputs. Distinguish source generation, compilation, linking, packaging, and execution failures.
- [ ] Check whether `recomp_registration.c.obj` exists, where it is written, and which command/response file references it.
- [ ] Trace QString/filesystem conversions, process arguments, working directories, and response-file encoding. Preserve display titles independently of generated filenames.
- [ ] Compare ASCII-only paths, paths with spaces, an accented game title, and a Japanese parent path. Use synthetic filenames where possible.
- [ ] Implement the smallest supported fix. Test filename collisions and empty sanitized names if sanitization is used.
- [ ] Diagnose `C1002` independently. Inspect compiler bitness, translation-unit sizes, memory use, optimization settings, and existing generator partitioning before choosing a fix.
- [x] If unit splitting is required, make it deterministic and verify symbol linkage, registration completeness, and execution behavior. Do not hand-edit generated PLA files as the lasting solution.
- [x] Re-export with the modified application and matching build kit; record whether either compiler path still fails or leaves modules to JIT.

**Acceptance:** the affected export can compile, link, and produce a package with documented toolchain settings. The missing-object and compiler-memory issues each have evidence of resolution or an explicit remaining limitation. Packaging success alone does not establish gameplay correctness.

## Milestone 3: Establish Hybrid runtime behavior

- [ ] Run the new Hybrid package with the same PLA update and comparable settings used for the JIT baseline.
- [x] Confirm it loads the intended generated code and matching runtime. Identify any modules compiled entirely through JIT fallback.
- [ ] Exercise boot, menus, opening gameplay, movement, combat, area transitions, saving/loading, and a longer play session.
- [ ] Record crashes, visual/audio errors, save behavior, timing, frame rate, and memory use.
- [x] Collect local recompilation gap/guard diagnostics. Separate unsupported CPU operations, indirect branches, service/runtime failures, and GPU issues.
- [ ] Use fallback frequency and exercised code paths to prioritize fixes. Record which gameplay areas remain untested.

**Acceptance:** Hybrid passes a documented representative gameplay checklist, with fallback behavior and known failures reported. It remains a compatibility investigation milestone.

## Milestone 4: Evaluate fully static execution

- [ ] Classify the gaps exposed by Hybrid and implement supported fixes in the generator or runtime, with targeted regression checks.
- [x] Re-generate and compile a static AOT export against the same game/update and runtime revision. (The v0.0.1-dev6 export completed with five modules and was packaged successfully.)
- [x] Inspect configuration and diagnostics to verify that guest CPU execution uses no JIT or interpreter fallback. Do not infer this solely from the UI label or executable name. (Strict static mode, zero static/JIT transitions, zero lookup misses, zero unimplemented opcodes, and no guard bumps were recorded in the exercised run.)
- [ ] Repeat the representative gameplay checklist and add coverage across regions, battles, transitions, and save/load cycles. (The run reached gameplay, but the complete checklist was not confirmed.)
- [ ] Compare correctness and performance with JIT and Hybrid under matched conditions. (Dev8 observations were about 25-30 FPS static AOT, 15-20 FPS Hybrid at budget 256, and 30 FPS JIT; conditions were not fully matched.)
- [ ] If static execution remains blocked, document the specific unsupported behavior, evidence, and remaining work. Do not promise that full compatibility is guaranteed.

**Acceptance:** the tested scenarios run correctly with guest CPU fallback disabled, and remaining coverage limits are disclosed. Passing those scenarios is evidence for those scenarios, not proof that every game path works. GPU shaders and runtime services remain separate architectural concerns.

## Milestone 4a: Main-module DLL split (design plan)

**Goal:** Build `main` as its own DLL image loaded by the static launcher while `rtld`, `sdk`, `subsdk0`, and `subsdk1` stay statically linked, so each image stays under the 2 GB MSVC x64 limit, including with FMI. Guest CPU fallback must remain disabled; the export must still run without JIT.

**Why (evidence):** Estimated image budget (see the image-budget progress entry): dev8 static launcher about 1,820 MiB (about 228 MiB margin); `main` is about 1,455 MiB (no FMI) / 1,780 MiB (FMI) of generated code. With `main` split out, the estimate is main DLL about 1,455 / 1,780 MiB and the launcher about 365 / 460 MiB (other modules plus runtime, estimate). Risk: `main` alone keeps only about 268 MiB margin with FMI; further growth of `main` would require splitting `main` itself, which is harder (intra-module direct calls).

**Existing building blocks (survey, 2026-10-05; verify line numbers before use):**

- The generator already emits a per-module shared library (`recompiled_image` for `main`, `recompiled_<module>` otherwise) with `SUYU_HOSTED_RECOMP=1` / `RECOMP_SHARED_MODULE=1` and an exported ABI surface (image ABI, guard v2, optional guard_gen/features/fastmem/fpx, image_lookup, run_slice, index, set_base, entry) in `src/core/recompiler/arm64_to_c.h` (~5570-5909). `RECOMP_STATIC_ONLY` suppresses it for static exports.
- `src/suyu_cmd/suyu.cpp` (~2021-2186) loads module DLLs from the executable directory in NSO order with `GetProcAddress` handshakes (ABI, ABI6), combines lookup/run_slice, and routes bases by load-order index - but only when the static registry is empty (~2024).
- Static registration: `game_export.cpp` (~4043-4210) writes `recomp_registration.c` (NSO-order externs/table, guard and ABI6 registries); `src/suyu_cmd/CMakeLists.txt` (~107-218) imports `recomp_runtime_shared` once and each `recomp_static_<module>` with per-module renames, and links `suyu-cmd-static`; `src/suyu_cmd/recomp_modules/CMakeLists.txt` repeats the renames for the clang-cl prebuild.
- Generated code reaches the host through `RecompHostMem` callbacks and `GuestContext` fields filled by `src/core/arm/recomp/arm_recomp.cpp` (~908-1043), not host symbol imports. Cross-module guest calls go through the dispatcher (generated direct calls only within one module, `arm64_to_c.h` ~378-410) and guest GOT/PLT relocation (`arm_recomp.cpp` ~1357-1480); no native cross-module symbol references were found.
- Per-image state exists: `g_module_base`, GG1 watch word, FPX shadow static/_Thread_local state, exclusive-monitor fallback state (`arm64_to_c.h` ~6836-6847, ~7630). CRT is /MD in the project and the export build kit.
- Packaging assumes one file: `game_export.cpp` (~5190-5486) packages `static_launcher.exe` only and README_NATIVE_EXPORT says one executable; the export build kit (`tools/export_build_kit/`) imports static libraries only and `package.py` rejects `recomp_static_*` inputs; manifests carry no module binary topology.

**Design decisions to make (each recorded with rationale before implementation):**

- D1 Runtime placement: the main DLL carries its own `recomp_runtime.c` copy (memory helpers stay DLL-local calls) versus importing helpers from the launcher (DLL-to-exE call per access). Default proposal: DLL-local copy; verify that no runtime global must be process-wide.
- D2 Per-image globals: list every global/TLS in `recomp_runtime.c`/`.h` and the generated units; classify as per-image (fine duplicated) or shared (must move into `GuestContext`/host callbacks). Must cover GG1 watch word, FPX shadow, exclusive state, guard generation, chain budget.
- D3 Mixed registry and loading: how `recomp_registration.c` represents a DLL module (placeholder entry filled at load, or a separate DLL list) while keeping NSO order, base routing, and the ABI6 all-module handshake; the launcher must load the DLL even when the static registry is non-empty; error behaviour when the DLL is missing or mismatched (fail fast; no JIT fallback in strict mode).
- D4 Build: generator/CMake mode to build `main` as a DLL and others as static libs in both the in-tree MSVC path and the clang-cl `recomp_modules` prebuild (clang-cl compiles, link.exe or lld-link links the DLL); module renames; import library handling; /MD; PDB size.
- D5 Packaging and manifest: the Build export ships `static_launcher.exe` plus the main DLL; update the package layout, README_NATIVE_EXPORT and UI “single-file” wording, package policy, manifest field for module binary topology, cache reuse, and the LNK1248 handling (per-image size check).
- D6 Export build kit: decide whether the kit supports the split now or is explicitly documented as static-only for now.
- D7 Selection: always split main for PLA static exports, or split only when the estimated image (from `dumpbin /summary` sizes) exceeds a threshold; name the threshold constant.

**Design work steps:**

1. Code Explorer (Orca): enumerate globals/TLS and every exe<->module symbol dependency for D1/D2; confirm the launcher DLL loader path and ABI6 handshake details for D3. (done 2026-10-06)
2. Parent session: write the design note (sections D1-D7, data flow at load time, failure modes, test plan) as a new subsection of this milestone; Code reviewer reviews the design against CLAUDE.md before any code change. (done 2026-10-06)
3. Prototype measurement before full implementation: from a Source-format export, build the generated `main` module's existing SHARED target with `RECOMP_STATIC_ONLY` off, without and with FMI; measure the DLL image and dependency evidence, then record it. (done 2026-10-06; see progress entry)
4. Implementation (code-quick-implementer), in reviewable steps: (a) generator/CMake mixed mode ((a) done 2026-10-06; (b) done 2026-10-06), (c) exporter packaging/manifest/wording, (d) build-kit decision. Code reviewer reviews each step.
5. Tests: extend `tests/recompiler_smoke` with a mixed static+DLL variant (synthetic modules) covering load order, base routing, cross-module dispatch, guard checks, GG1, and FPX; keep existing goldens unchanged for non-split exports; run the fastmem and FPX harnesses.
6. PLA verification: export static AOT (fallback disabled) without FMI, then with FMI; record per-image sizes, link time, zero JIT transitions, FPS in the same overworld scene, and sampling; compare with dev8 (25-30 FPS) and JIT (30 FPS).

**Acceptance:** design note reviewed with no unresolved findings; main DLL and launcher each link under 2 GB for PLA with FMI; strict static mode shows zero JIT transitions and no lookup misses in the exercised scene; synthetic mixed-mode tests pass; packaging, manifest, and documentation describe the two-file layout; remaining limits (main margin, build-kit support) are documented.

### Design note (accepted 2026-10-06, draft 3)

Review history: draft 1: 1 blocker/9 major/1 minor (R1-R11); draft 2: 0/2/1; draft 3: 0/0/0, implementation may start.

#### Verified starting facts

- The shared-module shape already exists: `add_library(recompiled_image SHARED recomp_export.c recomp_runtime.c ...)` with `SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1` (`src/core/recompiler/arm64_to_c.h` ~5718-5730). Without `RECOMP_STATIC_HOST`, the DLL's own `recomp_runtime.c` defines `g_module_base` (~7798-7804) and the load/store/exclusive/SVC helpers.
- The static shape builds `recomp_runtime.c` once as `recomp_runtime_shared` (`RECOMP_STATIC_HOST=1`) and renames each module's image symbols through `-D` (~5733-5785).
- Generated code reaches the emulator through `GuestContext::host_mem` (`RecompHostMem`) callbacks for memory, exclusive, and counter operations (mapped fastmem executes locally) and through `GuestContext::pending_svc` plus a return to the host for SVCs; the callbacks are installed per `ArmRecomp::Impl` (`src/core/arm/recomp/arm_recomp.cpp` ~909-926, ~1020-1050); there are no native imports of host C++ symbols and direct chains are intra-module (~380-410). A null exclusive/counter callback makes generated code use private fallbacks (`arm64_to_c.h` ~7633-7671).
- Launcher (`src/suyu_cmd/suyu.cpp`): the static path calls `suyu_recomp_static_modules_v4` (~1933-2017); the DLL path runs only if `s_recomp_modules.empty()` (~2025), probes fixed file names, silently skips missing files and DLLs without `recomp_image_lookup`, and warns-and-continues on `LoadLibraryW` failure. `SuyuRecompStaticModule` is declared separately in `suyu.cpp` (~196) and emitted as text by `game_export.cpp` (~4108).
- Base routing: `ArmRecomp::ModuleBaseFor` (~1187-1201) calls the setter with `(index, name, base)` over `FindModules` results (`src/core/arm/debug.cpp` ~254-313), which drop modules whose embedded path is unreadable; the launcher routes by index (`suyu.cpp` ~2181). Embedded names do not match exporter names (`arm_recomp.h` ~100-110).
- The loader keeps a canonical inventory: `AppLoader_DeconstructedRomDirectory` records `rtld`, `main`, `subsdk0..9`, `sdk` by load address (`src/core/loader/deconstructed_rom_directory.cpp` ~191-265, `ReadNSOModules` ~334); NCA/NAX forward it, and NSP (`nsp.cpp` ~263) and XCI (`xci.cpp` ~211) forward to their nested loaders `[R12]`; a standalone NSO records `file->GetName()` rather than a canonical name (`nso.cpp` ~242-252); classes that do not override it return `ErrorNotImplemented` (`src/core/loader/loader.h` ~339). `FindModules` results also feed the host pre-relocator through `Impl::modules` (`arm_recomp.cpp` ~1191, ~1883). `Core::Load` already reads this inventory before process publication on the explicit-prepare path (`src/core/core.cpp` ~355-365); that path also changes relocation behavior (`arm_recomp.cpp` ~1715-1730) and is not reused here.

#### D1 Runtime placement

Decision: the main DLL carries its own `recomp_runtime.c`, compiled exactly as the existing shared shape. Static modules keep `recomp_runtime_shared`. Rationale: helpers are per-access and intentionally out of line (~7692); the DLL copy needs no new exe-export ABI. Cost is measured in step 3.

Constraint `[R10]`: hosted code must never take the private fallbacks. The host validates bridge completeness itself: when each `ArmRecomp::Impl` builds its `RecompHostMem`, it checks that every exclusive (single and pair), clear-exclusive, and counter callback is non-null and fails initialization with a named diagnostic otherwise. This is a host-side check after per-core bridge construction, not a DLL export, and applies to all bundles.

#### D2 Per-image globals

| State | Placement | Reason |
|---|---|---|
| `g_module_base`, `g_recomp_guard_host_v2`, `g_recomp_gg_word`, `g_recomp_gg_host`, `recomp_gg_seen[]`, lookup index `_idx*` | Per image | One image's base, handshake, generation epoch, or address range; the host already keeps per-image pointers. |
| FPX shadow statics/TLS | Per image | Diagnostics only; mixed layout yields one report per runtime copy. Documented, not aggregated. |
| `g_excl_addr`, `g_excl_valid`, standalone `base_ns` | Never used in hosted mode | Guaranteed by the D1 bridge check. |
| Chain budget, combined lookup, readiness flags, GG1 registry, counters | Process-wide host state | Already host-owned; unchanged. |

No global moves into `GuestContext`. Step 3 records the DLL's exports and imports (`dumpbin /exports /imports /dependents`).

#### D3 Topology contract, loading, and base binding

**Topology contract `[R2]`.** A new shared C header (proposed `src/core/arm/recomp/recomp_topology.h`, plain C, included by the launcher and by the generated `recomp_registration.c`) defines a versioned registry: entry point `suyu_recomp_topology_v1()` returning a header `{abi_version, struct_size, entry_count, static_count, dll_count, bundle_abi, bundle_id[32]}` and an entry array `{canonical_name, binding (static|dll), dll_file_utf8, image_abi, expected_features, static function pointers (null for dll)}` in NSO order. The struct layout is defined once in that header; the exporter emits only values. Old all-static exports and the existing `suyu_recomp_static_modules_v4` path are unchanged: the exporter emits v1 only when a split is requested, and the launcher prefers v1 when present, otherwise v4. Each entry's features are checked individually against `expected_features` (not only the bundle OR). Static-registry handshakes (GG1, FPX, FM1, guard v2) gain v1 forms that report results per entry index so a mixed bundle has separate total/static/DLL counts and GG1 descriptors are placed at their full topology index.

**Single source.** The exporter computes one topology value (module list in NSO order, binding, DLL file name from one helper, bundle ABI, expected features, bundle id). The generated CMake binding list, `recomp_registration.c`, package file list, AOT cache manifest, and package manifest are all written from that value, and each carries `topology_version` and `bundle_id`; consumers compare them and fail with the mismatched item named.

**Bundle and module identity `[R3]`.** `bundle_id` is SHA-256 over the source ExeFS digest already used for cache identity (`game_export.cpp` ~3594), the generator revision, and the generation options. In split mode the generator emits an extra export in each shared module, `recomp_image_identity_v1(const char** canonical_name, const uint8_t** bundle_id)`, with values from compile definitions written from the topology. The launcher accepts a DLL only if name and bundle id match its table entry. The package manifest also records each DLL's SHA-256 for integrity checks.

**Transactional loading `[R11]`.** The launcher builds a pending bundle: static entries are taken from v1; each DLL entry is loaded (see D3 paths), its exports resolved (`lookup`, `set_base`, `run_slice`, `abi`, `features`, `fastmem_v1`, `guard_gen_v1` if GG1, `fpx_v1` if FPX1, `guard_v2`, `identity_v1`), and checked. Any failure releases all uncommitted handles and exits with the module name, file, and cause (missing file, Win32 error, missing export, identity, ABI, feature, or handshake refusal). Only after the whole bundle succeeds are `SetRecompLookup`, readiness flags, `SetRecompGuardGenModules`, and the binding callback published. Accepted DLL handles are owned by the bundle object and intentionally retained for the process lifetime (no `FreeLibrary` after commit, because lookup/GG1/base pointers stay installed). Strict static mode never continues with a missing slot and never falls back to JIT.

**Base binding `[R1]`.** Bases are bound by canonical identity, not by debug-name index. A new host callback `SetRecompBindCallback(bool (*)(const RecompModules& inventory))` is invoked from `Core::Load` after `MakeApplicationProcess` and before process publication, using `app_loader->ReadNSOModules` (the inventory already used at `core.cpp` ~359), without enabling the explicit-prepare relocation path. The launcher's bind callback matches the inventory's canonical names (`rtld`, `main`, `subsdkN`, `sdk`) against the topology entries; it rejects count mismatch, duplicates, missing slots, and extra slots, then calls each image's `set_base` and records the canonical name and base. Failure makes `Core::Load` stop with `ErrorLoader` and a diagnostic naming the module, before any block runs; the failure path releases the service process before kernel shutdown, as the existing load error path does (`core.cpp` ~320-325).

Per-core module map `[R1b]`: binding is a process-aware host stage (proposed `BindRecompProcess(KProcess&, const RecompModules& inventory)` in `arm_recomp.cpp`). After the launcher callback validates and binds the inventory, the stage installs the same canonical inventory into every application `ArmRecomp::Impl` as `modules` and sets `modules_read`, without setting `rela_applied` or `explicitly_prepared`, so the existing host pre-relocation (`ApplyAllRelocations`, ~1883) still runs and sees every module, including one whose embedded path `FindModules` cannot read. `ModuleBaseFor` then neither calls `FindModules` nor the index setter. Debug/display names are kept separately (from `FindModules`, for logs and `recomp_gaps.json` only). The bound state belongs to that process; a new process starts unbound and runs the stage again. `PrepareRecompProcess` is not reused, because it disables pre-relocation (~1727).

Inventory sources `[R12]`: ExeFS directory, NCA, NAX, NSP, and XCI loaders already supply the canonical inventory through existing delegation; no new forwarding is added, and tests cover the existing delegation. Any `ReadNSOModules` error fails a split bundle fast with the loader named. A standalone NSO's inventory name is its file name and is not normalized; it fails the canonical-name match with a diagnostic, so split exports are not supported for standalone NSO loading. All-static v4 exports keep today's index routing unchanged.

**Legacy all-DLL probe.** Kept unchanged for exports without a registry; not used by Build exports.

**Windows paths `[R9]`.** Registry strings are UTF-8 and converted with the existing `Common::UTF8ToUTF16W`. The executable directory is read with a growing-buffer `GetModuleFileNameW` loop that checks truncation and errors. Each `dll_file_utf8` must be a plain basename (no separators, no `..`, no drive), unique case-insensitively; the exporter validates this when writing the topology and the launcher re-validates. DLLs are loaded with `LoadLibraryExW(absolute_path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`; step 3 confirms the DLL depends only on the CRT and OS DLLs already required by the launcher. Filesystem errors name the module and path.

#### D4 Build `[R6]`

- Generator: split mode is an opt-in emitter path. With the split off, emitted files are byte-identical to today (smoke goldens unchanged) `[R10]`. With it on, the generated module CMake receives the binding list from the topology; DLL-bound modules build only the `SHARED` target plus the identity definitions, static modules build as today.
- Exporter: both build loops (`game_export.cpp` ~4561, ~4782) take target names and artifact paths from the topology (`recomp_static_<m>` or `recompiled_image`). The in-tree MSVC path (`src/suyu_cmd/CMakeLists.txt`) and clang-cl prebuild (`src/suyu_cmd/recomp_modules/CMakeLists.txt`) both read the same binding list; the DLL is built as its own target, copied next to the launcher, and not linked into it.
- Failure stages are logged separately: module compile, DLL link, launcher link, package. LNK1248 (or any link failure) of the DLL in the module-target branch stops the export with the image name and saved output; it does not prune a strict slot and does not retry with MSVC. The dev10 launcher-link stop stays as is.
- Size reporting: after each successful link, read PE `SizeOfImage` from the image header (not file length) and log it against the hard limit constant `kPeImageLimitBytes` (2 GiB) with the remaining margin; record it in the manifest.
- Flags: `/MD`; the DLL uses the same generator variables for FM1/GG1/FPX/FMI defines, and features are still verified per entry at load. Separate PDB, not packaged.

#### D5 Packaging, cache, and manifest `[R5]`

- `ValidateLocalExport` / package policy (`game_export.cpp` ~6625, `src/common/package_policy.cpp` ~1167-1240) receive the required module-DLL set derived from the topology; listed DLLs are required and non-empty, unlisted DLLs remain rejected.
- AOT cache (`aot_manifest.json`, ~3416-3574) stores `topology_version`, `bundle_id`, binding list, and each DLL's path, size, and SHA-256. Reuse requires all of them to match and every required DLL to exist with the recorded digest; otherwise rebuild.
- Package staging copies and verifies DLLs (digest) before the cache is removed (~5393). Cache failure policy `[R5b]`: unchanged from today for both all-static and split exports. The AOT cache lives inside package staging, and a failed, cancelled, or exception-terminated package discards the whole staging directory, including the cache (`StagingCleanup`, ~6368-6374); the next export rebuilds. This milestone does not add cache retention across failed packages; a recoverable cache outside staging would be a separate change with its own ownership and promotion rules.
- `export-package.json` gains `module_topology` (`name`, `binding`, `file`, `size_bytes`, `size_of_image`, `sha256`) and `bundle_id`, written from the same topology value.
- README_NATIVE_EXPORT and UI text describe "launcher plus module DLL(s) in the same folder" for split exports only.

#### D6 Export build kit `[R4]`

The kit stays static-only in this milestone. Rejection happens in the consumer: the kit CMake (`tools/export_build_kit/CMakeLists.txt` ~78-105) rejects a topology with a `dll` entry before creating targets, and exporter preflight rejects a split request on a kit-only installation (`game_export.cpp` ~4319) before compiling modules, with a message that the split requires a source build. The split is never silently undone. `package.py` keeps rejecting game-specific inputs as today. All-static kit exports keep using v4 and are unaffected; the kit revision check is unchanged for them.

#### D7 Selection `[R7]`

Explicit opt-in only in this milestone: the documented generator option `SUYU_AOT_SPLIT_MAIN=1` splits `main` for Windows static AOT exports. On other export types or platforms the option fails the export with a clear message. No automatic size-based selection; it may be proposed later as a separate auto mode with its own decision phase. Reports show each image's `SizeOfImage` against the hard limit and the remaining margin for the main DLL.

#### Load-time data flow

Exporter -> topology value -> (a) generated CMake binding list, (b) `recomp_registration.c` v1, (c) AOT cache manifest, (d) package file list and `export-package.json`. Launcher start -> read v1 -> pending bundle (static entries + DLL loads + identity/ABI/feature/handshake checks) -> commit (publish lookup, readiness, GG1, bind callback) -> `Core::Load` -> loader inventory -> bind callback validates and sets bases -> process published -> guest runs.

#### Failure modes

| Failure | Stage | Behaviour |
|---|---|---|
| DLL missing, unloadable, export missing | Launcher, before commit | Exit; names module, file, Win32 error or export. |
| Identity, ABI, or per-entry feature mismatch | Launcher, before commit | Exit; names module and both values. |
| FM1/GG1/FPX refusal or guard v2 disagreement | Launcher, before commit | Exit (guard v2 "not negotiated" kept only if every entry agrees). |
| Inventory unavailable, count/duplicate/missing/extra slot | `Core::Load`, before publication | `ErrorLoader`; names loader or module. |
| Bridge callback null | `ArmRecomp` init | Fail initialization; names callback. |
| DLL or launcher image over limit / link failure | Export build | Stop; names image, stage, saved log; no retry, no pruning. |
| Packaged DLL missing, empty, digest mismatch, or extra DLL | Export package | Export fails. |
| Split requested with kit-only install | Exporter preflight / kit CMake | Fails with "requires source build". |

#### Test plan `[R10]`

- Refactor the launcher's bundle loading and binding into a testable unit (for example `src/core/arm/recomp/recomp_bundle.{h,cpp}`) used by `suyu.cpp`, so tests run production logic.
- Mixed synthetic fixture (new, alongside `tests/recompiler_smoke`): three or more modules with one real DLL in a middle slot, loaded through the production unit. Checks: NSO order, inventory-based base binding, cross-module dispatch, guard v2, GG1 descriptors at full indices, FPX agreement, single and pair exclusive reservation across the static/DLL boundary, CLREX, counter continuity.
- Negative cases, each asserting the named diagnostic and that no block executes: missing DLL, unloadable DLL, missing export, same-ABI/same-feature DLL from another synthetic bundle or module (identity), ABI mismatch, per-entry feature mismatch, individual FM1/GG1/FPX refusal, guard v2 disagreement, empty and duplicate topology, inventory missing a middle module, extra inventory module, valid loader inventory with an unreadable embedded name in a middle module (all cores keep it for relocation and dispatch, no index setter call) `[R1b]`, standalone NSO inventory rejected by name, NSP/XCI delegation returning the canonical inventory `[R12]`, null bridge callback, strict lookup miss without JIT.
- Paths: Japanese parent directory, spaces, brackets, different current directory, path beyond `MAX_PATH` (works or explicit diagnostic).
- Exporter: package success, extra DLL rejection, missing/empty DLL, cache reuse in both binding directions, stale cached DLL digest, failure injected after DLL verification and before promotion (staging and cache discarded, no partial package promoted) `[R5b]`, kit-only split rejection, unchanged all-static kit exports (strict and Hybrid), injected DLL-link and launcher-link LNK1248 on both compiler paths.
- Regression: existing smoke goldens byte-identical with the split off; `tests/recompiler_fastmem` and `tests/recompiler_fpx` (reduced and full); existing ABI5/ABI6 all-static coverage.
- PLA (step 6): static AOT without FMI, then with FMI; per-image `SizeOfImage`, link time, zero JIT transitions, loader inventory names, FPS in the same overworld scene, sampling.

#### Step 3 prototype measurement (revised) `[R8]`

Do not link static dev8 objects (they carry static renames and no runtime). Instead, from a Source-format export generated outside the repository, configure the generated `main` module CMake with `RECOMP_STATIC_ONLY` off so the existing `recompiled_image` SHARED target builds with the real shared definitions. Measure without FMI and with FMI: `SizeOfImage`, file size, PDB size, exports, imports, dependents, compiler and link flags, link time. Record this as evidence for the main DLL only; the launcher size remains an estimate until a split launcher links.

#### Modifiability and reference correctness

One exporter-owned topology value is the source for module names, bindings, DLL file names, bundle id, ABI, and features; every consumer is written from it and verified at build or load. Moving a module between static and DLL binding is a one-entry change. Generated code is not edited; the split is an opt-in generator/exporter option, and the default output is unchanged.

### Carried acceptance criteria (from the step 4(b) review)

The following criteria are carried from the accepted step 4(b) review and must be completed in steps 4(c) and 4(d); none may be replaced by a PLA observation.

- **4(c):** Produce a real split export through the exporter and execute a linked v1 launcher with fallback disabled; cover an unreadable-embedded-path middle-module variant while checking every core's complete inventory and relocation input; exercise Core load refusals with named diagnostics and zero guest execution; verify lookup-removal unregister and fresh-process rebinding; wire registry, marker, and targets coherently and run the unchanged all-static v4 regression.
- **4(d):** Exercise a three-or-more-module static/DLL/static fixture through the production dispatcher with GG1, guard, and FPX checks; cover cross-image reservation in both directions, STXR success/failure, CLREX, LDXP/STXP, and counter continuity; require bridge-null refusal per callback and strict lookup-miss refusal; complete remaining loader/handshake refusals and kit/package/cache/path tests, goldens, fastmem, and reduced/full FPX.

#### Open questions

1. Resolved by step 3: measured main-DLL `SizeOfImage` is 1,459.1 MiB without FMI and 1,783.7 MiB with FMI, leaving margins of 588.9 MiB and 264.3 MiB to the 2,048 MiB limit, respectively.
2. (Resolved in draft 3 `[R12]`: NSP/XCI already forward `ReadNSOModules`.)
3. Whether the base-binding callback should also replace index routing for all-static exports later (out of scope now; v4 unchanged).

## Milestone 5: Implement improvements one at a time

For each feature, identify the responsible layer, add a clear setting where appropriate, preserve a baseline/default mode, and record gameplay correctness and performance before and after the change.

| Feature | Required checks |
|---|---|
| Up to 4K | Render-target/scaling behavior, UI readability, post-processing, GPU memory, and frame-time cost. |
| Ultrawide | Camera projection, UI placement, menus, cutscenes, and field-of-view behavior without stretching. |
| 60 FPS | Simulation and animation speed, physics, input, timers, scripted events, and cutscenes across variable frame rates. |
| Ambient occlusion | Depth/normal availability, shader integration, temporal artifacts, quality settings, and GPU cost. |
| LoD/draw distance | Identify actual culling/LoD controls; check transitions, streaming, CPU/GPU load, and memory. |
| Additional vegetation | Determine whether this requires game-data changes; inspect placement, collision, streaming, asset provenance, and performance. |

**Acceptance:** each completed feature has repeatable evidence and documented limits. Keep independent features in separate coherent changes and leave unverified features labeled as planned.

## Milestone 6: Prepare a reproducible public release

- [ ] Verify build/setup instructions from a clean checkout using the documented dependency mechanism.
- [ ] Inspect tracked files and release contents for game inputs, generated game code/assets, keys, firmware, private paths, and credentials. Ignore rules do not remove already tracked files.
- [ ] Preserve upstream and third-party notices and provide corresponding source/build instructions for GPL-covered binaries.
- [ ] Publish only supported software artifacts; keep local game exports out of public repository and release contents.
- [ ] Define how `devN` counts development commits for a target version, then automate it if useful. Do not label an unverified build stable.
- [ ] Update the README, compatibility notes, known issues, and changelog to match the verified implementation.
- [ ] Prepare a concrete release for user review before publishing it unless publication is separately authorized.

**Acceptance:** a clean checkout reproduces the documented software build, release contents match the license/input policy, and support claims match testing evidence. Reaching `v1.0.0` requires an explicitly agreed stable scope.


## Session handoff (2026-10-06, eighth session, complete)

### State at handoff

- **Current local state:** The host build tree was rebuilt with export variables cleared; no working static Build export exists locally. Clear `SUYU_CMD_RECOMP_DIR`, `SUYU_RECOMP_HYBRID`, and `SUYU_CMD_RECOMP_PREBUILT_DIR` before host builds, then re-export without FMI to obtain a package; the dev8 module libraries remain available for image inspection outside the repository.

- **Repository:** HEAD is `1d9ba61` (v0.0.1-dev11, committed and pushed by the agent at the user's explicit request while the user was away); the project was renamed from HisuiWake to Aevora on 2026-10-05, and the other documents now follow the rename. The local repository folder remains named HisuiWake. Uncommitted: v0.0.1-dev12 step 4(b) code and docs.
- **Local environment:** The dev8 module libraries built for the image measurement are in a local directory outside the repository; the host build tree was rebuilt with export variables cleared (dev10 `suyu.exe`/`suyu-cmd.exe`). No working static Build export exists locally.
- **Diagnostics:** the local diag directory outside the repository contains `sample_static.ps1` and `RipSampler2.cs`, with `asm_probe` and `fmi_probe` material. Launch from a non-elevated shell; use `powershell -File sample_static.ps1 -ProcId <launcher PID> -Out <file>`.
- **Build/test:** in the Build Tools 2026 `vcvars64` environment, run `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls` (about 100.6 M cases; reduced: add `--cases 1000 --adv 2000 --legs 13`), `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`, and `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl`. Python must read generated files as UTF-8. `rebuild-suyu.bat` clears export variables and builds with `-j 8`; incremental builds use `cmake --build <build-dir> --target suyu suyu-cmd -j 8`.
- **Switches:** `SUYU_FPCR_STATS=1` logs guest FPCR and adds a lock per save; never use it for FPS. `SUYU_RECOMP_CHAIN_BUDGET` is 1-8192, default 256. `SUYU_AOT_TRANSLATE_ALL=1` applies to Hybrid; static AOT forces translate-all and strict mode. `SUYU_AOT_FM_INLINE=1` enables FMI at export only when FM1 is enabled, but currently breaks the PLA static export with LNK1248 and remains off.

### What this session did

1. Completed Milestone 4a step 1: Orca Code Explorer surveyed globals/TLS, exe<->module symbol dependencies, and the launcher DLL loader/ABI6 handshake.
2. Completed step 2: wrote the draft-3 design note and received three reviews: draft 1 (blocker 1, major 9, minor 1; R1-R11), draft 2 (blocker 0, major 2, minor 1; residual R1/R5 and new R12), and draft 3 (blocker 0, major 0, minor 0; implementation may start).
3. Completed step 3: built the generated `main` module's existing SHARED target without and with FMI; measured `SizeOfImage` at 1,459.1 MiB and 1,783.7 MiB, with 588.9 MiB and 264.3 MiB margins.
4. Completed Milestone 4a step 4(a): implemented and reviewed the authoritative module topology, shared-image identity export, and prebuild DLL binding checks; all-static output remains unchanged.
5. Completed Milestone 4a step 4(b): implemented and reviewed registry v1, transactional bundle loading, inventory binding, and host bridge checks; accepted as an intermediate with F3 carried.

### Where things stand

| Observation | Result |
|---|---|
| JIT baseline | 30 FPS. |
| Hybrid first / translate-all / budget 256 | 17 / 14 / 15-20 FPS. |
| Dev6 static AOT | 16-20 FPS, no fallback. |
| Dev8 static AOT | 25-30 FPS, no fallback. |
| [Dev8 static sampling](OLD_HISTORY.md#2026-10-05-dev8-static-aot-per-thread-cpu-and-rip-sampling-in-the-overworld) | Hot block 2-3.5% (was 15-19%), scheduler-fiber symbol about 1% (was 10-12.5%), CPU cores waiting 35-49% of samples. |
| [Same-scene JIT sampling](OLD_HISTORY.md#2026-10-05-same-scene-jit-sampling-remaining-gap-is-guest-cpu-cost) | JIT process CPU 275.7% versus 320.3% dev8 static; remaining gap is guest CPU execution cost after excluding wait shares (estimate). |
| [FMI export](OLD_HISTORY.md#2026-10-05-pla-export-with-fmi-fails-static-launcher-image-exceeds-2-gb-lnk1248) | FMI tests passed, but the PLA static launcher image was about 2.19 GB and failed LNK1248 against the 2 GB x64 limit. |
| Image budget | Dev8 about 1,820 MiB estimated (about 228 MiB margin); FMI +421 MiB does not fit. |
| Main DLL (step 3) | `SizeOfImage`: 1,459.1 MiB without FMI / 1,783.7 MiB with FMI; margins: 588.9 MiB / 264.3 MiB to 2,048 MiB. |
| FPCR measurement | FZ set in about 96% of 3,473,408 JIT context saves; do not measure FPS with `SUYU_FPCR_STATS=1`. |
| Sampling profile | Hot block was 15-19% of busy-core RIP samples; `ScheduleImplFiber` was 10-12.5%. The FZ finding is confirmed, not a hypothesis. |

Key conclusions: static AOT ran without fallback in the exercised scenario; Dynarmic remains linked for Hybrid and GPU macro JIT/shader compilation remain runtime generation. The SHL fallback hypothesis was refuted, and budget 256 addressed long-slice interrupt delay. The FZ-closed FPX1 gate was the main remaining static AOT cost; the smoke golden mismatch came from MSVC without `/utf-8` in the test only, so real exports were unaffected. Build exports delete `aot_cache`; use Source format to inspect generated C. The FPS comparison is not under fully matched conditions.

### Open issues, in priority order

1. Milestone 4a step 4(c): exporter `SUYU_AOT_SPLIT_MAIN` option and bundle id, registry v1/marker emission, in-tree launcher DLL handling, packaging/manifest/cache, and the carried 4(c) acceptance criteria.
2. Exercise the dev10 failure paths synthetically.
3. Remaining items from the old list item 5: code review of `a6b8690`, stale `export-package.json` producer revision `d0be6ce`, later `SignalInterrupt`, verify-always pin, `SUYU_NO_JIT`, FPX1 DN, the pre-existing GG1 `g_recomp_gg_word` link failure, and existing export/GPL/rtld/BUILD.md/version investigations.

### Immediate next action

Milestone 4a step 4(c): implement exporter `SUYU_AOT_SPLIT_MAIN` option and bundle id, registry v1/marker emission, in-tree launcher DLL handling, packaging/manifest/cache, and the carried 4(c) acceptance criteria.

### How to work

- Measure FPS in the same overworld scene and record backend, settings, cache state, and chain-budget condition; never use `SUYU_FPCR_STATS` during FPS measurements.
- Use the test commands above and Source format when generated C must be inspected. Keep build/export paths and diagnostics outside the repository.
- Do not export PLA with `SUYU_AOT_FM_INLINE=1` until the image budget is solved. Read generated files as UTF-8 in Python on Windows, and copy `suyu_log.txt` before restarting suyu after a failed export.
- Follow [CLAUDE.md](../../CLAUDE.md), [CODEBASE_INDEX.md](CODEBASE_INDEX.md), and the Orca coordination notes.

Useful entry points: [agent guide](../../CLAUDE.md), [codebase index](CODEBASE_INDEX.md), [project README](../../README.md), [upstream build guide](../Suyu/Build.md), [upstream export guide](../Suyu/user/GameExport.md), [exporter source](../../src/suyu/game_export.cpp), and [dependency manifest](../../cpmfile.json).

After each meaningful change, add a dated entry with:

- Milestone and source revision.
- Problem and supporting evidence.
- Files changed and reason for the chosen approach.
- Exact build/test commands and relevant settings.
- Observed result, comparison baseline, and untested behavior.
- Next action or blocking information.

Keep raw logs, machine-specific paths, and game-derived artifacts local. Link only to sanitized, shareable evidence from public documentation.



## Progress record

After each meaningful change, add a dated entry with:

- Milestone and source revision.
- Problem and supporting evidence.
- Files changed and reason for the chosen approach.
- Exact build/test commands and relevant settings.
- Observed result, comparison baseline, and untested behavior.
- Next action or blocking information.

Keep raw logs, machine-specific paths, and game-derived artifacts local. Link only to sanitized, shareable evidence from public documentation.

### 2026-10-06: Milestone 4a step 4(b): registry v1, bundle loading, and inventory binding (v0.0.1-dev12)

- **Milestone and revision:** Milestone 4a step 4(b), base HEAD `1d9ba61` (v0.0.1-dev11); implementation remained uncommitted.
- **Problem and evidence:** The split design needed one registry layout and transactional bundle loader before exporter wiring. The implementation validates registry headers and entries, loads DLLs with identity/ABI/feature/handshake checks, binds canonical loader inventories before process publication, and refuses incomplete host bridges; the v1 launcher path remains macro-gated and not yet linked or run.
- **Files changed and reasons:** `src/core/recompiler/recomp_registry_v1.h` defines the shared C registry v1 layout and export names; `src/core/recompiler/recomp_topology.h` writes registry and marker data; `src/core/arm/recomp/recomp_bundle.h/.cpp` provides transactional loading, inventory matching, and base application; `src/core/arm/recomp/arm_recomp.h/.cpp` provides process binding and bridge checks; `src/core/core.cpp` binds before publication; `src/suyu_cmd/suyu.cpp` adds the `SUYU_RECOMP_REGISTRY_V1` launcher path; `tests/recompiler_smoke/bundle_host.cpp` and `bundle_exec.c` cover 21 bundle cases and mixed-image execution.
- **Commands/settings:** `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl`; `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`; reduced FPX with `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls --cases 1000 --adv 2000 --legs 13`; `cmake --build <host build dir> --target suyu suyu-cmd -j 8` after clearing export variables; compile-only `suyu.cpp` with `-DSUYU_RECOMP_REGISTRY_V1`.
- **Result and untested behavior:** Smoke exited 0 with 8 variants, unchanged goldens, and 21 bundle cases; fastmem had zero mismatches; reduced FPX and host build exited 0; the v1 launcher source compiled. A linked/running v1 launcher, Core::Load/BindRecompProcess behavior, three-module fixture, cross-image reservation/CLREX/pair/counter, bridge-null refusal, non-Windows execution, and long paths remain unverified. Re-review accepted step 4(b) as an intermediate with F3 carried at its original major severity.
- **Next action:** Milestone 4a step 4(c): exporter `SUYU_AOT_SPLIT_MAIN` option and bundle-id/registry/marker emission, in-tree launcher DLL handling, packaging/manifest/cache, and the carried 4(c) acceptance criteria.

### 2026-10-06: Milestone 4a step 4(a): module topology, identity export, and prebuild DLL binding (v0.0.1-dev11)

- **Milestone and revision:** Milestone 4a step 4(a), base HEAD `d4c8cf9`; implementation remained uncommitted.
- **Problem and evidence:** The split design required one authoritative module topology and a verifiable identity contract before registry and launcher loading. The implementation validates module names, order, DLL bindings, bundle identity, and generated target/file references; all-static generated CMake remains byte-identical.
- **Files changed and reasons:** `src/core/recompiler/recomp_topology.h` centralizes topology validation, load ordering, image names, bundle-byte conversion, and `recomp_modules.cmake` output; `src/core/recompiler/recomp_image_identity.h` defines the shared-image identity ABI; `src/core/recompiler/arm64_to_c.h` emits the opt-in identity symbol; `src/suyu/game_export.cpp` uses the topology writer; `src/suyu_cmd/recomp_modules/CMakeLists.txt` validates and builds DLL-bound modules; `src/suyu_cmd/CMakeLists.txt` and `tools/export_build_kit/CMakeLists.txt` reject unsupported split configurations; smoke tests cover topology, identity, and prebuild consumers.
- **Commands/settings:** `python -B tests/recompiler_smoke/run.py --cc clang-cl --cxx clang-cl` (8 variants); `python tests/recompiler_fastmem/run.py --generator Ninja --cc clang-cl --cxx clang-cl`; `python tests/recompiler_fpx/run.py --generator Ninja --cc clang-cl --cxx clang-cl --jobs 8 --hits --controls --cases 1000 --adv 2000 --legs 13`; `cmake --build <host build dir> --target suyu suyu-cmd -j 8`, after clearing export variables. The smoke prebuild reported the Visual Studio 18 2026 generator.
- **Result and untested behavior:** All listed commands exited 0; smoke goldens were unchanged, topology/prebuild/identity checks passed, and fastmem had zero mismatches. Review findings were resolved, including DLL artifact-name verification, the raw 32-byte digest contract, and the MSVC-safe negative-array size check. In-tree marker and kit rejection, the main special target and DLL-first ordering, non-Windows paths, Ninja smoke prebuild, cl.exe C-mode identity compilation, and a PLA export remain unverified.
- **Next action:** Milestone 4a step 4(b): implement registry v1 (shared C layout), launcher transactional loading with identity checks, and inventory-based base binding (`BindRecompProcess`).


- **Archive rule:** At each session close, move progress entries older than the previous session (and their session-close entry) verbatim to OLD_HISTORY.md, and add any still-relevant fact to Key facts from archived history.

### 2026-10-06: Milestone 4a step 3: main module DLL image measured without and with FMI

- **Milestone and revision:** Milestone 4a step 3, HEAD `d4c8cf9`.
- **Problem and evidence:** The design needed a measured `main` DLL image before implementation. A local wrapper CMake project outside the repository built only the generated `main` module's existing SHARED target `recompiled_image` with `RECOMP_STATIC_ONLY` off and `RECOMP_BUILD_STATIC_LIB` off, once without FMI and once with FMI.
- **Inputs:** No-FMI used generated code from the dev8 static AOT Source export; FMI used generated code left in the aot_cache of the failed dev9 FMI export, whose `recomp_runtime.h` contains `recomp_fmi`. Non-ASCII source paths were reached through ASCII junctions.
- **Files changed:** No repository source changed; this measurement was recorded in `docs/Aevora/WORK_PLAN.md` and `CHANGELOG.md`.
- **Commands/settings:** Clang-cl 22.1.8 (Build Tools 2026 vcvars64), Ninja, Release, `RECOMP_JOBS=7`, lld-link (`/dll /INCREMENTAL:NO`), 704 build steps (701 generated units plus runtime/export). Compile definitions and options matched `src/suyu_cmd/recomp_modules/CMakeLists.txt`: `BUILD_ID="amd64"`, `_CRT_SECURE_NO_WARNINGS`, `_CRT_NONSTDC_NO_DEPRECATE`, `_SCL_SECURE_NO_WARNINGS`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE`, `_UNICODE`, `/utf-8 /GS- /Gy -Wno-everything`; generated CMake applied `SUYU_HOSTED_RECOMP=1 RECOMP_SHARED_MODULE=1`, `/O2 /Ob2 /bigobj /fp:precise -MD /std:c11`. No `/DEBUG` was used, so no PDB was produced.
- **Result:**

| Item | No FMI (dev8) | FMI |
|---|---:|---:|
| SizeOfImage | 1,530,019,840 bytes (1,459.1 MiB) | 1,870,344,192 bytes (1,783.7 MiB) |
| Margin to 2,048 MiB | 588.9 MiB | 264.3 MiB |
| Size of code | 1,195.4 MiB | 1,520.0 MiB |
| File size | 1,516,892,160 bytes | 1,857,218,560 bytes |
| `.text / .rdata / .pdata / .data / .reloc` (hex) | `4AB6A000 / CF46000 / 2582000 / C81000 / 66F000` | `5EFF9000 / CF46000 / 2582000 / C81000 / 66F000` |
| Wall time, configure-to-link (one run each) | 40.7 min | 47.8 min |
| Link time (DLL mtime minus last object mtime, approximate) | about 23 s | about 25 s |

- **Exports and dependents:** Both DLLs export only `recomp_image_abi`, `recomp_image_entry`, `recomp_image_fastmem_v1`, `recomp_image_features`, `recomp_image_fpx_v1`, `recomp_image_guard_gen_v1`, `recomp_image_guard_v2`, `recomp_image_index`, `recomp_image_lookup`, `recomp_image_run_slice`, and `recomp_image_set_base`. Both depend on `KERNEL32.dll`, `VCRUNTIME140.dll`, `api-ms-win-crt-stdio-l1-1-0.dll`, `api-ms-win-crt-runtime-l1-1-0.dll`, `api-ms-win-crt-heap-l1-1-0.dll`, and `api-ms-win-crt-environment-l1-1-0.dll`; observed KERNEL32 imports were `DisableThreadLibraryCalls`, `GetCurrentProcessId`, `GetCurrentThreadId`, `GetSystemTimeAsFileTime`, `InitializeSListHead`, and `QueryPerformanceCounter`, with no host/launcher symbol imports. This satisfies the design's D2/D3 check that the DLL exports only the image ABI and depends only on CRT/OS DLLs.
- **Comparison and limits:** The image-budget estimate was about 1,455 MiB without FMI / 1,780 MiB with FMI; measured `SizeOfImage` was 1,459.1 / 1,783.7 MiB. DLL only; the split launcher (four static modules plus runtime) was not linked, so its size remains an estimate of about 365 / 460 MiB. No PDB was produced because `/DEBUG` was not used, so PDB size was not measured; the link used lld-link and the exporter's MSVC `link.exe` path was not measured. No runtime load, FPS, or PLA test was run. Timings are single runs on a 12-core / 16 GB machine.
- **Next action:** Milestone 4a step 4(a): implement the generator/CMake opt-in split mode (`SUYU_AOT_SPLIT_MAIN`) and the topology value, by code-quick-implementer and reviewed by Code reviewer.

### 2026-10-06: Milestone 4a design note reviewed and accepted (steps 1-2)

- **Milestone and revision:** Milestone 4a, HEAD `e66f19c`.
- **Problem and evidence:** The dev9 FMI static launcher exceeded the 2 GB x64 image limit; step 1 surveyed globals/TLS, exe<->module dependencies, and the launcher ABI6/DLL path, and the design note was reviewed through draft 3 with no unresolved findings.
- **Files changed:** `docs/Aevora/WORK_PLAN.md` records the accepted D1-D7 design, review history, and handoff; `docs/Aevora/CODEBASE_INDEX.md` records the verified inventory and launcher paths; `CHANGELOG.md` records the documentation closure.
- **Commands/settings:** No build, test, export, or runtime command was run in this session; design review used read-only source inspection and the local report outside the repository.
- **Result and untested behavior:** The design specifies a DLL-local runtime copy; versioned topology registry v1 as the single source (v4 unchanged); bundle identity export; transactional loading; inventory-based base binding via `BindRecompProcess` before process publication; opt-in `SUYU_AOT_SPLIT_MAIN`; kit static-only with consumer-side rejection; package/cache derived from topology; and default output byte-identical. Implementation correctness, DLL image size, dependencies, performance, and PLA compatibility remain untested.
- **Next action:** Milestone 4a step 3 measurement of the main DLL without and with FMI via a Source-format export and the generated SHARED target.





### 2026-10-05: Static launcher image budget: dev8 (no FMI) versus FMI section totals

- **Milestone and revision:** Milestone 4. HEAD `a3063a2`; generated code from the dev8 static AOT Source export (no FMI; its `recomp_runtime.h` contains no `recomp_fmi`), and the dev9 FMI module libraries left by the failed FMI export.
- **Method:** The dev8 Source export's five modules were built with the exporter's own module project (`src/suyu_cmd/recomp_modules`, clang-cl 22.1.8 `/O2`, Ninja, Release, `RECOMP_JOBS=7`) in a local directory outside the repository, through ASCII junctions to the Source export (the project only checks that `recomp_registration.c` exists, so an empty stub was used; no launcher was linked). `dumpbin /summary` was run on each `recomp_static_<module>.lib` for both builds. The build ran in two parts (interrupted at 531/893 and resumed) and overlapped a host rebuild, so compile time was not measured.
- **Result (MiB, sum of five modules):**

| Section | Dev8 (no FMI) | FMI | Change |
|---|---:|---:|---:|
| .text | 1,421.7 | 1,840.6 | +29.5% |
| .rdata | 264.8 | 264.8 | 0 |
| .pdata | 47.3 | 47.3 | 0 |
| .xdata | 33.0 | 34.7 | +5.2% |
| .bss | 15.8 | 15.8 | 0 |
| Image-relevant sum | 1,782.5 | 2,203.2 | +23.6% |

Per module (image-relevant sum, dev8 / FMI): main 1,455.3 / 1,780.5; sdk 131.1 / 165.5; subsdk1 121.9 / 163.0; subsdk0 74.0 / 94.0; rtld 0.1 / 0.2. main is about 82% of the generated code.

- **Calibration and estimate:** The FMI link reported image size 0x8C0E0000 (2,240.9 MiB), 37.7 MiB above the FMI object sum; this difference is attributed to the runtime and launcher code (assumption). Applying the same offset, the dev8 static launcher image is estimated at about 1,820 MiB, leaving about 228 MiB (about 11%) under the 2 GiB limit. This is an estimate; the dev8 launcher was not linked in this measurement.
- **Composition:** `_expected[]` per-block guard arrays, counted from the dev8 generated C (`(3 + instruction count)` 32-bit words per array), total about 112 MiB (main 88.0, sdk 9.2, subsdk1 9.7, subsdk0 5.5 MiB), about 42% of .rdata. The rest of .rdata (dispatch/jump tables and constants) and the .text split between block code, per-instruction entry labels, and inline exact-FP bodies were not attributed.
- **Interpretation:** FMI adds about 421 MiB, roughly twice the remaining margin, so FMI for all generated code cannot fit one image. Earlier per-unit A/B compiles (2-15% object growth) under-represented the whole-image .text growth (+29.5%). Options remain: restrict FMI to hot code, reduce generated-code contributors (`_expected[]`, entry labels, FP bodies), or move `main` to a separate DLL image.
- **Next:** Choose the image-budget strategy; any generated-code change must be checked with the same `dumpbin /summary` method before a full PLA export.

### 2026-10-05: Exporter stops on LNK1248 and keeps build-failure logs (v0.0.1-dev10, unverified by export)

- **Problem:** see the FMI LNK1248 entry above: after the clang-cl link failed with LNK1248, the exporter fell into the MSVC per-module path (about 20 h estimate, cannot fit the same image), and the first compiler/linker output was lost to log rotation and work-directory cleanup.
- **Change (`src/suyu/game_export.cpp`):** new `SaveBuildFailureLog` appends failed compile/link output from the clang-cl and MSVC paths to `export_build_failure.log` in suyu's log directory (UTF-8, write/flush errors checked, truncated before appending once over 8 MiB). In the clang-cl link-failure branch, LNK1248 that the existing Hybrid largest-module retry does not handle now shows a "Build Failed" dialog with the saved log path and fails the export instead of falling back to MSVC.
- **Review:** Code reviewer (Orca, gpt-6.1-sol, high) found one P2 issue (write/flush errors not checked before reporting the log as saved) and a wording caveat (MSVC is not proven unable to produce smaller code; the change now says it does not remove the limit and is costly); both were fixed. Control flow (failure return, staging cleanup, Hybrid retry, one-module Hybrid) was reviewed with no findings.
- **Verification:** `rebuild-suyu.bat` (Build Tools 2026 vcvars64, Ninja, `-j 8`) built suyu and suyu-cmd after the change and after the review fix; no warnings from `game_export.cpp`.
- **Not run:** no export exercised the new paths; injected LNK1248 for static, Hybrid retry, and one-module Hybrid, and log-helper Unicode/append/rotation/open-failure/write-failure cases remain untested.
