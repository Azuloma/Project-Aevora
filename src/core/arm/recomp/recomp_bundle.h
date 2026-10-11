// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/common_types.h"
#include "core/arm/recomp/recomp_guard_gen.h"
#include "core/recompiler/recomp_registry_v1.h"

// Loading and base binding of a split (static + shared image) recompiled bundle
// described by a registry v1 (core/recompiler/recomp_registry_v1.h).
//
// Deliberately free of System, KProcess and ArmRecomp, so a test host can compile
// this unit with the registry alone: every input is a plain value, and nothing is
// published to the emulator from here. The launcher publishes the result.
namespace Core {

/// Same type as Core::RecompBlockFn (arm_recomp.h), repeated so this unit does
/// not pull in the CPU backend headers.
using RecompBundleBlockFn = void (*)(void*);

/// Plain copies of Core::RecompFastmemLayout / Core::RecompFpxLayout (arm_recomp.h);
/// the launcher fills them from Core::GetRecompFastmemLayout() / GetRecompFpxLayout().
struct RecompBundleFastmemLayout {
    u32 page_bits;
    u32 stride_log2;
    u64 pointer_mask;
    u32 off_table;
    u32 off_limit;
};

struct RecompBundleFpxLayout {
    u32 off_fpcr;
    u32 off_fpsr;
    u64 inhibit_bit;
};

/// What the host tells every image during the load handshakes.
struct RecompHostHandshake {
    RecompBundleFastmemLayout fastmem;
    RecompBundleFpxLayout fpx;
    u32 guard_gen_host_version; ///< RecompGuardGen::kHostVersion
};

struct RecompBundleModule {
    std::string name; ///< canonical NSO name (rtld, main, subsdkN, sdk)
    RecompBundleBlockFn (*lookup)(u64){};
    void (*set_base)(u64){};
    RecompBundleBlockFn run_slice{};
    unsigned image_abi{};
    unsigned (*guard_v2)(unsigned){};
    unsigned (*xmod_v1)(const SuyuRecompXmodTableV1*){}; ///< XMOD1 bundles only
    bool is_dll{};
    std::string dll_file; ///< shared-image modules only; for logs
};

/// A loaded bundle: every module in NSO load order plus the results of the
/// handshakes. Move-only.
///
/// Shared-image handles are retained for the process lifetime: the lookup, GG1
/// word/base and set_base pointers published from this bundle point into those
/// images, so the destructor never unloads a successfully loaded image. Only a
/// load that fails part-way releases the handles it had taken (inside
/// LoadRecompBundleV1).
class RecompBundle {
public:
    RecompBundle() = default;
    RecompBundle(const RecompBundle&) = delete;
    RecompBundle& operator=(const RecompBundle&) = delete;
    RecompBundle(RecompBundle&&) noexcept = default;
    RecompBundle& operator=(RecompBundle&&) noexcept = default;

    std::vector<RecompBundleModule> modules;
    /// GG1 descriptors in full-topology order (empty unless the bundle has GG1).
    std::vector<RecompGuardGen::Module> guard_gen_modules;
    unsigned bundle_abi{};
    u32 features{}; ///< OR of every module's features (0 for ABI 5)
    unsigned fpx{}; ///< the FPX1 handshake answer shared by every module; 0 when unused
    bool guard_ready{};  ///< guard v2 negotiated with every module
    bool long_slices{};  ///< every module is ABI >= 4 with run_slice
    /// Shared-image handles (Windows HMODULE as void*), never freed once loaded.
    std::vector<void*> dll_handles;
    /// The XMOD1 table the images currently hold; null when none is published. Never freed
    /// (see PublishRecompXmod), so a guest thread still reading it cannot dangle.
    const SuyuRecompXmodTableV1* xmod_table{};
};

/// Directory of the running executable (where the shared images live). On
/// Windows it uses a growing-buffer GetModuleFileNameW loop that errors on a
/// failed call or a path it cannot fit; other platforms report an error, because
/// shared images are a Windows-only feature.
bool GetRecompExecutableDirectory(std::filesystem::path* directory, std::string* error);

/// Validates `registry`, loads every shared image from `image_dir`, resolves their
/// exports and runs the per-module checks and handshakes (identity, ABI, features,
/// GG1, FM1, FPX1, guard v2). Any failure releases the images loaded by this call
/// and returns nullopt with a message naming the module and the step.
std::optional<RecompBundle> LoadRecompBundleV1(const SuyuRecompRegistryV1& registry,
                                               const std::filesystem::path& image_dir,
                                               const RecompHostHandshake& host,
                                               std::string* error);

/// Matches the loader's canonical inventory (load address -> NSO name) against the
/// bundle: every module must be present exactly once and nothing else. Returns
/// (module index, base) for each module in module order; errors name the
/// offending module(s). Pure: does not call set_base.
std::optional<std::vector<std::pair<size_t, u64>>> BindRecompBundleToInventory(
    const RecompBundle& bundle, const std::map<u64, std::string>& inventory, std::string* error);

/// Calls each module's set_base with the base chosen by BindRecompBundleToInventory.
void ApplyRecompBundleBases(const RecompBundle& bundle,
                            const std::vector<std::pair<size_t, u64>>& bases);

/// XMOD1 only (a no-op for other bundles). Call after ApplyRecompBundleBases and before any guest
/// thread runs; any refusal clears every image. The table is never freed (images may hold it).
bool PublishRecompXmod(RecompBundle& bundle, const std::vector<std::pair<size_t, u64>>& bases,
                       std::string* error);

/// Clears the table in every image. Only while no guest thread runs: a reader that already
/// loaded the pointer may still use the old table, which stays allocated.
void UnpublishRecompXmod(RecompBundle& bundle);

} // namespace Core
