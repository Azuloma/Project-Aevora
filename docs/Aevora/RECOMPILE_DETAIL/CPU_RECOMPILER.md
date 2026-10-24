# CPU Recompiler (ARM64 to C)

This document describes the completed CPU recompilation path: the translator, the exporter that drives it, the generated-code contracts, and the runtime that executes the result. It is a consolidated reference built from [CODEBASE_INDEX.md](CODEBASE_INDEX.md), [NATIVE_ROADMAP.md](NATIVE_ROADMAP.md), and [CHANGELOG.md](../../CHANGELOG.md). Line numbers drift; use the index for current locations and verify against the code.

The repository has no component named "CPUCodeCompiler". In this document the term means the whole game-CPU path: `arm64_to_c.h`, the exporter in `game_export.cpp`, the generated modules, and `ArmRecomp`.

## Scope and status

- Milestone N1 ("CPU without a JIT") is accepted by the user's request. The no-JIT export of PLA ran from launch through the Obsidian Fieldlands for about 110 seconds with zero static-to-JIT and JIT-to-static transitions, zero unresolved import traps, and 7,486,015,732 static blocks; the launcher passed `structural_gate.py --require-absent N1` and contains no `dynarmic` string.
- Performance is below the JIT. N1 proves CPU execution without a JIT, not parity; no same-position JIT/AOT CPU ms/frame measurement exists yet.
- The recompiler only targets Pokemon Legends: Arceus. Static AOT still relies on the runtime for system services, memory, audio, input, and GPU.
- The exported title is unconfirmed and the Linux target is not validated; Windows x64 is the verified platform.

## Pipeline overview

**Export** (Qt exporter, `src/suyu/game_export.cpp`):

1. Extract ExeFS and parse the NSO modules (canonical 13-name order in `src/core/loader/nso_module_names.h`).
2. Discover code blocks per module, optionally seeded from `<user dir>/recomp/gaps/<title>.json` (Hybrid gap addresses).
3. Call `EmitProject()` per module to write C sources, the runtime text, and a module `CMakeLists.txt`.
4. Write the top-level `CMakeLists.txt`, `recomp_modules.cmake`, and the registration source (`suyu_recomp_static_modules_v4()`).
5. For Build output, configure and build with clang-cl (or MSVC) in `<build-dir>/recomp_clang`, then link `suyu-cmd-static` against the export build kit.
6. Publish the persistent AOT cache entry and call `PackageNativeExport()`.

**Run** (`src/core/arm/recomp/`):

1. Load the NSO modules and call `BindRecompProcess` / `PrepareRecompProcess` before process publication.
2. Perform the ABI 6 handshakes (FM1, GG1, FPX1) and set the lookup and base setter.
3. `KProcess::InitializeInterfaces()` creates one `ArmRecomp` per core for the application process when a recomp lookup is set.
4. Each core loops: `lookup(pc)` finds a generated block, the block runs (chaining to further blocks), and SVC or interrupt exits return to the dispatcher.
5. A lookup miss is fatal in strict mode, or a JIT fallback in Hybrid mode.

## Backends

| Backend name | Frontend | Behavior |
|---|---|---|
| `suyu-static` | JIT-capable tree (`msvc-release`) | Strict static AOT; a JIT-capable binary (title qualifier `(JIT-capable)`). |
| `suyu-static-nojit` | No-JIT tree (`SUYU_NO_JIT=ON`, `msvc-release-nojit`) | Strict static AOT; no dynarmic is linked. |
| `suyu-hybrid` | JIT-capable tree | AOT with recorded JIT fallback; an investigation tool, not proof of static execution. |

Backend names never contain an underscore. The exporter title is `Aevora Static AOT` with `(JIT-capable)` or `(no-JIT build)`. Strict mode rejects modules it cannot recompile; the LNK1248 JIT retry is an explicit Hybrid-only fallback. Cache and manifest identity distinguish `suyu-static` from `suyu-static-nojit`.

## Translator: `src/core/recompiler/arm64_to_c.h`

A header-only translator (about 8,600 lines) and the only code generator. `EmitProject()` is the per-module entry point and returns `RecompileStats`.

### Emitter switches

All switches are process-wide inline booleans set once per export; each feature is byte-identical to the previous ABI text while it is off (checked by golden hashes in `tests/recompiler_smoke/run.py`).

| Switch | Meaning | Default |
|---|---|---|
| `g_translate_all` | Translate forms normally left to the JIT; strict exports translate everything (`TranslateAllForExport()`). | Strict exports on |
| `g_emit_standalone_runner` | Emit a per-module `main.c` runner for synthetic tests; the game exporter turns it off so game code runs only inside suyu against the user's files. | On for tests, off for games |
| `g_emit_fastmem` (FM1) | Memory helpers read the host page table directly and fall back to the ABI 5 helpers. | On (`SUYU_AOT_FASTMEM=0` disables) |
| `g_emit_fm_inline` (FMI) | Inline FM1 fast path at each access site; requires FM1. | On (`SUYU_AOT_FM_INLINE=0` disables) |
| `g_emit_guard_gen` (GG1) | Blocks re-run the code guard only when the module generation word moved; requires FM1. | Existing default |
| `g_emit_fpx` (FPX1) | Exact native floating point fast paths. | Existing default |
| `g_emit_fpx_shadow` | Instrumentation: run the exact body after each fast path and compare. | Off |
| `g_emit_fp_shared` (FPE) | One shared exact body per module; requires FPX1. | `SUYU_AOT_FP_SHARED` |
| `g_emit_fpx_vec` (FPXV) | All-lane vector FADD/FSUB fast path; requires FPX1, not shadow. | `SUYU_AOT_FPX_VEC` |
| `g_emit_direct_chain` / `g_emit_indirect_chain` | Tail-chain direct B/BL and indirect RET/BR/BLR. | On (`SUYU_AOT_DIRECT_CHAIN=0` / `SUYU_AOT_INDIRECT_CHAIN=0` disable) |
| `g_emit_chain_start` | Chain-start `_s` entries for short direct targets; requires direct chaining. | On (`SUYU_AOT_CHAIN_START=0` disables) |
| XMOD1 (`SUYU_AOT_CROSS_CHAIN`) | Cross-module lookup fallback for indirect chains; requires FM1, indirect chain, and a split export. | On (`=0` disables) |

Default-on options are read through `Common::EnvFlagDefaultOn` and `Common::ParseEnvChoice` in `src/common/env_option.h`; an option set to anything but its documented values is an error, and explicit `=1` that cannot be met warns.

### Key mechanisms

- **Chaining.** `ChainTo` emits a direct tail call when the target block was emitted, otherwise it parks the PC and returns. `ChainIndirect` looks up the target by subtracting the module base and preserves budget on a miss. `RECOMP_TAIL_CALL` is clang `musttail`, or a bounded plain call/return elsewhere. `kChainBudget` bounds the blocks run before control returns to the dispatcher; the runtime budget is 32 (short slice) or 256 (long slice), with `SUYU_RECOMP_CHAIN_BUDGET` (1-8192) overriding at startup.
- **Chain-start budget.** `chain_start_budget.h` projects guest text plus entry bytes per linked image under the 2 GiB MSVC x64 image limit (LNK1248) with a 128 MiB margin; an export whose projection approaches the limit is refused.
- **Fast memory (FM1 / FMI).** Generated accessors consult the host page table directly (`recomp_fm_entry()`, `recomp_fmi_entry()`) and decline to the out-of-line helper for anything special (unmapped, rasterizer, watched, page-crossing).
- **Code guard (GG1).** A generation word per module lets blocks skip the per-entry code guard; `RecompGuardGen::OnJitFallback()` pins every module to verify-always the first time a JIT fallback is created.
- **Exact floating point (FPX1).** A native fast path is kept only when it provably equals the architectural result and adds no FPSR bit the guest has not set; otherwise the unchanged exact body runs. `RECOMP_FPX_FPCR_MASK` is `0x06C8FF07`; guest FZ is passed to the helpers.
- **Lookup index.** Each module emits a flat 32-bit index (`recomp_image_index32`, `SuyuRecompIndex32V1`); when the index cannot be allocated the module keeps a reason and falls back to segment search.
- **Generated units.** Source is split into units (default 8 MiB / 5,000 blocks; `SUYU_RECOMP_UNIT_KB`, `SUYU_RECOMP_UNIT_BLOCKS` override). The generated CMake sizes its job pool from free RAM (8 GB per cl.exe or 1.5 GB per clang compile; `SUYU_RECOMP_JOBS`, `SUYU_RECOMP_JOB_MEMORY_MB`, `-DRECOMP_JOBS`).

## Image layout and cross-component contracts

These are defined once and shared by the generator, generated code, launcher, build kit, and exporter (CLAUDE.md "Modding and QoL readiness"):

| Contract | Source of truth |
|---|---|
| Registry v1 (`SuyuRecompRegistryV1`, ABI/features, DLL export-name macros) | `src/core/recompiler/recomp_registry_v1.h` |
| Topology (bindings, load order, image names, `recomp_modules.cmake`) | `src/core/recompiler/recomp_topology.h` |
| Image identity (v1 symbol, raw 32-byte bundle digest) | `src/core/recompiler/recomp_image_identity.h` |
| 32-bit lookup index layout and refusal reasons | `src/core/recompiler/recomp_image_index.h` |
| XMOD1 table layout | `recomp_registry_v1.h` |
| PE `SizeOfImage` parsing | `src/core/recompiler/pe_image_size.h` |
| ABI 6 feature bits (FM1 `0x1`, GG1 `0x2`, FPX1 `0x4`, XMOD1 `0x8`) | `src/core/arm/recomp/recomp_image_features.h` |
| Required host bridge callbacks | `src/core/arm/recomp/recomp_bridge_required.h` |

**Split main (`SUYU_AOT_SPLIT_MAIN`).** By default the `main` module is bound as a DLL wherever supported, which keeps the static launcher under the 2 GiB image limit; `0` disables and `1` requests it explicitly. The bundle id is a versioned canonical digest over topology, bindings, and the emitter options that change generated code. Prebuild DLL validation (`src/suyu_cmd/recomp_modules/CMakeLists.txt`, `RecompDllTarget.cmake`) checks version, bundle id and bytes, module membership, and target type before building. At run time `recomp_bundle.cpp` loads and transactionally validates the bundle (`LoadRecompBundleV1`, `BindRecompBundleToInventory`, `ApplyRecompBundleBases`).

## Persistent AOT cache

`src/suyu/aot_export_cache.{h,cpp}` is the single owner of entry naming and layout: `aot_export_cache/<title>_<platform>_<backend>_<split|all-static>`. It provides a producer record, a non-waiting per-entry lock, interrupted-publish recovery, validation, atomic replacement, and size reporting.

- The producer is identified by git commit hash plus branch. An uncommitted runtime-only change reuses the old launcher; a new commit makes the cache unusable and requires a full rebuild and export.
- The cache keeps only the launcher, DLL, manifest, and `recomp_modules.cmake`; generated registration sources and ABI headers are deleted after an export.
- Reuse requires a matching manifest (`fm_inline`, `fpx_vec`, `cross_chain`, direct and indirect chain fields) and a verified launcher SHA-256 and size (`VerifyCachedLauncher()`).
- Evidence of cache behavior is the `aot_cache_outcome`, `aot_cache_publish`, and `aot_cache_reused` status fields, not the log tail.
- The cache lives under the user-data directory, never in the repository.

## Runtime: `src/core/arm/recomp/`

- **`ArmRecomp`** (`arm_recomp.h/.cpp`, an `ArmInterface`): the lookup and dispatch loop, context marshalling (`GuestContextView`, `RecompHostMem` with `static_assert`-locked offsets), and registration hooks (`SetRecompLookup`, `SetRecompBindCallback`, `BindRecompProcess`, `SetRecompPrepareCallback`, `SetRecompBaseSetter`, `PrepareRecompProcess`).
- **Strict mode.** `StrictNoFallback()` reads `SUYU_RECOMP_STRICT` (set explicitly by `suyu_cmd`). A lookup miss, no-fallback event, or unimplemented opcode is fatal and names the module and offset (`DescribeRecompPc`, `recomp_pc_description.h`).
- **Fallback.** `ArmRecomp::EnterFallback()` lazily creates an `ArmDynarmic64` (Hybrid only); it always refuses when built with `SUYU_NO_JIT`.
- **Exclusive access.** The no-JIT tree uses the dynarmic-free `StandaloneExclusiveMonitor` over `NativeExclusiveState`: per-core cache-line-separated reservation slots, a TTAS lock, lock-free read-and-mark (`SUYU_EXCL_LOCKFREE_MARK=0` disables), and plain-page host load/CAS fast paths. Locked `Memory` accesses remain for special and cross-page accesses.
- **Guest FP environment.** `guest_fp_env.h` matches host MXCSR/FPCR modes to the guest.
- **Counter time.** AOT `HostReadCntpct` uses `CoreTiming().GetClockTicks()`.
- **Coverage output.** Exported packages write `user/log/recomp_coverage.txt` (static/JIT transitions, unimplemented opcodes, SVC counts) and `recomp_gaps.json`; the gap files (`recomp_gaps.*`, `recomp_gap_session.*`) include instruction words and stay local.

## Diagnostics (opt-in, execution-neutral when off)

| Option | Output |
|---|---|
| `SUYU_CPU_BUDGET=1` | Process and per-thread CPU ms per game frame and p99 frame interval; summarize with `summarize_cpu_budget.py`. |
| `SUYU_RECOMP_THREAD_TIME=1` | Per-guest-thread `RunThread` wall time (`RecompThreadTimeLedger`). |
| Lookup stats / exit targets | Dispatcher exit classification and top exit pairs (`recomp_lookup_stats.h`, `recomp_exit_targets.h`). |
| Exclusive stats | Timed exclusive bridge callbacks (`recomp_exclusive_stats.h`). |
| `SUYU_RECOMP_ACCESS_SITES` | Guest memory callbacks attributed by host stack frames (`recomp_access_sites.h`, `recomp_host_frames.*`); never attribute by `ctx.pc`, which is stale while blocks chain. |
| `SUYU_FPCR_STATS` | Guest FPCR value statistics. |

Sampling and the access-sites diagnostic perturb timing. Compare AOT and JIT in core-ms per frame, take FPS from unsampled runs, and treat instruction-pattern shares from RIP samples as hints, not removable costs. A recompiled image has no PDB, so attribute DLL samples by RVA.

## Verification

- `tests/recompiler_smoke/` holds standalone units, each with a deterministic negative control, and golden hashes of the emitted text in `run.py`. When generated text changes, compare old and new exporter output first, then recompute the hashes.
- `tests/recompiler_fastmem` and `tests/recompiler_fpx` cover FM1 and FPX1; `tests/exclusive_access/run.py` covers the exclusive monitor against the host build; `tests/export_integration/` exercises the exporter end to end.
- `tools/structural_gate/structural_gate.py` checks a launcher's PDB symbols and imports against component deny lists; `--require-absent N1` is the JIT-free acceptance for the no-JIT launcher.
- Export integration uses the host's compiled-in generator, so rebuild the host after any generator change.

## Known limits and open items

- AOT performance is below the JIT; do not claim parity.
- The full `tests/recompiler_smoke/run.py` suite and `tests/exclusive_access` have not been re-run since the latest N2 work, and `StandaloneExclusiveMonitor` has no ThreadSanitizer stress coverage.
- The recorded compiler name in reports is wrong (`last_recomp_compiler` records `O2`).
- Eight unresolved GOT/PLT symbol errors and one `UnpinHandle: Pin count imbalance` warning have not been compared with a JIT log.
- Bare-PC fatal logs in the recompiler and guard paths are unnamed by module.
- The no-JIT export build kit was not packaged, because it cannot provide a usable Hybrid mode.
- An exported static launcher links all modules into one image, so generated-code growth must be checked against the 2 GiB limit before a full PLA export.
