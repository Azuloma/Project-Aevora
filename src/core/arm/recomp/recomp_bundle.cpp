// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <set>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/arm/recomp/recomp_bundle.h"
#include "core/arm/recomp/recomp_image_features.h"
#include "core/arm/recomp/recomp_index_view.h"
#include "core/recompiler/recomp_image_identity.h"
#include "core/recompiler/recomp_topology.h"

namespace Core {

namespace {

constexpr unsigned kAbiWithoutFeatures = 5;
constexpr unsigned kAbiWithFeatures = 6;
constexpr unsigned kFastmemHandshakeAccepted = 1;
[[maybe_unused]] constexpr unsigned kIdentityV1Filled = 1; // used by the Windows-only DLL loader
constexpr unsigned kGuardV2Version = 2;

bool Fail(std::string* error, std::string message) {
    if (error) {
        *error = std::move(message);
    }
    return false;
}

std::string Quoted(const std::string& text) {
    return "'" + text + "'";
}

std::string Hex(u64 value) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text = "0x";
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned digit = static_cast<unsigned>((value >> shift) & 0xf);
        started = started || digit != 0 || shift == 0;
        if (started) {
            text += kDigits[digit];
        }
    }
    return text;
}

std::string BundleIdHex(const uint8_t* bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < RECOMP_BUNDLE_ID_BYTES; ++i) {
        text += kDigits[bytes[i] >> 4];
        text += kDigits[bytes[i] & 0xf];
    }
    return text;
}

/// The entry's function pointers and descriptor, whether they came from the
/// registry (static) or from a loaded image (DLL).
struct ResolvedEntry {
    RecompBundleBlockFn (*lookup)(u64){};
    void (*set_base)(u64){};
    RecompBundleBlockFn run_slice{};
    unsigned (*image_abi_fn)(){};
    unsigned (*guard_v2)(unsigned){};
    unsigned (*features)(){};
    unsigned (*fastmem_v1)(u32, u32, u64, u32, u32){};
    u32* (*guard_gen_v1)(u32, u64*, u64*, const u64**){};
    unsigned (*fpx_v1)(u32, u32, u64){};
    unsigned (*xmod_v1)(const SuyuRecompXmodTableV1*){};
    SuyuRecompIndex32V1Fn index32_v1{};
};

/// Shared-image handles taken by one load; released on every path except the one
/// that hands them to the bundle.
class PendingHandles {
public:
    PendingHandles() = default;
    PendingHandles(const PendingHandles&) = delete;
    PendingHandles& operator=(const PendingHandles&) = delete;
    ~PendingHandles() {
        Release();
    }

    void Add(void* handle) {
        handles.push_back(handle);
    }
    std::vector<void*> Take() {
        return std::move(handles);
    }

private:
    void Release() {
#ifdef _WIN32
        for (void* handle : handles) {
            FreeLibrary(static_cast<HMODULE>(handle));
        }
#endif
        handles.clear();
    }

    std::vector<void*> handles;
};

bool ValidateHeader(const SuyuRecompRegistryV1& registry, std::string* error) {
    const auto mismatch = [&](const char* field, u64 found, u64 expected) {
        return Fail(error, std::string("registry field ") + field + " is " + std::to_string(found) +
                               " but this build expects " + std::to_string(expected));
    };
    if (registry.version != SUYU_RECOMP_REGISTRY_V1_VERSION) {
        return mismatch("version", registry.version, SUYU_RECOMP_REGISTRY_V1_VERSION);
    }
    if (registry.struct_size != sizeof(SuyuRecompRegistryV1)) {
        return mismatch("struct_size", registry.struct_size, sizeof(SuyuRecompRegistryV1));
    }
    if (registry.entry_size != sizeof(SuyuRecompRegistryEntryV1)) {
        return mismatch("entry_size", registry.entry_size, sizeof(SuyuRecompRegistryEntryV1));
    }
    if (registry.entry_count == 0 || !registry.entries) {
        return Fail(error, "the registry lists no modules (entry_count 0 or entries NULL)");
    }
    if (registry.entry_count != registry.static_count + registry.dll_count) {
        return Fail(error, "registry entry_count " + std::to_string(registry.entry_count) +
                               " is not static_count " + std::to_string(registry.static_count) +
                               " + dll_count " + std::to_string(registry.dll_count));
    }
    if (registry.bundle_abi != kAbiWithoutFeatures && registry.bundle_abi != kAbiWithFeatures) {
        return Fail(error, "registry bundle_abi " + std::to_string(registry.bundle_abi) +
                               " is not 5 or 6");
    }

    u32 static_found = 0;
    u32 dll_found = 0;
    std::unordered_set<std::string> names;
    std::unordered_set<std::string> files; // lowercase: Windows file names ignore case
    for (u32 i = 0; i < registry.entry_count; ++i) {
        const SuyuRecompRegistryEntryV1& entry = registry.entries[i];
        const std::string where = "registry entry " + std::to_string(i);
        if (!entry.canonical_name || entry.canonical_name[0] == '\0') {
            return Fail(error, where + " has no canonical name");
        }
        const std::string name = entry.canonical_name;
        if (!names.insert(name).second) {
            return Fail(error, "module " + Quoted(name) + " is listed more than once");
        }
        if (entry.binding == SUYU_RECOMP_BINDING_STATIC) {
            ++static_found;
        } else if (entry.binding == SUYU_RECOMP_BINDING_DLL) {
            ++dll_found;
            if (!entry.dll_file_utf8 ||
                !suyu::recomp::detail::IsPlainBasename(entry.dll_file_utf8)) {
                return Fail(error, "module " + Quoted(name) +
                                       " has a DLL file name that is missing or not a plain "
                                       "file name");
            }
            if (!files.insert(suyu::recomp::detail::LowerAscii(entry.dll_file_utf8)).second) {
                return Fail(error, "DLL file " + Quoted(entry.dll_file_utf8) + " of module " +
                                       Quoted(name) + " collides with another module's");
            }
        } else {
            return Fail(error, "module " + Quoted(name) + " has unknown binding " +
                                   std::to_string(entry.binding));
        }
        if (entry.image_abi != registry.bundle_abi) {
            return Fail(error, "module " + Quoted(name) + " expects image ABI " +
                                   std::to_string(entry.image_abi) + " but the bundle ABI is " +
                                   std::to_string(registry.bundle_abi));
        }
        if (registry.bundle_abi == kAbiWithoutFeatures) {
            if (entry.expected_features != 0) {
                return Fail(error, "module " + Quoted(name) + " expects features " +
                                       Hex(entry.expected_features) + " but ABI 5 has none");
            }
            continue;
        }
        // ABI 6: FM1 is mandatory, and the feature set is one bundle-wide
        // contract (FPX1's kill switch and GG1's guard words are all or nothing).
        if (!(entry.expected_features & RecompImageFeature::FastmemPT1)) {
            return Fail(error, "module " + Quoted(name) + " expected features " +
                                   Hex(entry.expected_features) + " lack FM1 (ABI 6 requires it)");
        }
        if (entry.expected_features != registry.entries[0].expected_features) {
            return Fail(error, "module " + Quoted(name) + " expected features " +
                                   Hex(entry.expected_features) + " differ from module " +
                                   Quoted(registry.entries[0].canonical_name) + "'s " +
                                   Hex(registry.entries[0].expected_features));
        }
    }
    if (static_found != registry.static_count || dll_found != registry.dll_count) {
        return Fail(error, "registry counts static " + std::to_string(registry.static_count) +
                               " / dll " + std::to_string(registry.dll_count) +
                               " do not match its entries (static " +
                               std::to_string(static_found) + " / dll " +
                               std::to_string(dll_found) + ")");
    }
    return true;
}

bool ResolveStaticEntry(const SuyuRecompRegistryEntryV1& entry, ResolvedEntry* out,
                        std::string* error) {
    const std::string name = entry.canonical_name;
    const auto require = [&](bool present, const char* pointer) {
        return present ||
               Fail(error, "static module " + Quoted(name) + " has no " + pointer + " pointer");
    };
    if (!require(entry.lookup != nullptr, "lookup") ||
        !require(entry.set_base != nullptr, "set_base") ||
        !require(entry.run_slice != nullptr, "run_slice") ||
        !require(entry.image_abi_fn != nullptr, "image_abi_fn") ||
        !require(entry.guard_v2 != nullptr, "guard_v2")) {
        return false;
    }
    const u32 expected = entry.expected_features;
    if (expected & RecompImageFeature::FastmemPT1) {
        if (!require(entry.features != nullptr, "features") ||
            !require(entry.fastmem_v1 != nullptr, "fastmem_v1")) {
            return false;
        }
    }
    if ((expected & RecompImageFeature::GuardGen1) &&
        !require(entry.guard_gen_v1 != nullptr, "guard_gen_v1")) {
        return false;
    }
    if ((expected & RecompImageFeature::ExactFpX1) &&
        !require(entry.fpx_v1 != nullptr, "fpx_v1")) {
        return false;
    }
    if ((expected & RecompImageFeature::CrossModule1) &&
        !require(entry.xmod_v1 != nullptr, "xmod_v1")) {
        return false;
    }
    out->lookup = entry.lookup;
    out->set_base = entry.set_base;
    out->run_slice = entry.run_slice;
    out->image_abi_fn = entry.image_abi_fn;
    out->guard_v2 = entry.guard_v2;
    out->features = entry.features;
    out->fastmem_v1 = entry.fastmem_v1;
    out->guard_gen_v1 = entry.guard_gen_v1;
    out->fpx_v1 = entry.fpx_v1;
    out->xmod_v1 = entry.xmod_v1;
    out->index32_v1 = entry.index32_v1;
    return true;
}

#ifdef _WIN32

/// The registry's UTF-8 file name as UTF-16, via MultiByteToWideChar directly so malformed UTF-8
/// is an error and this unit needs no other common sources.
bool Utf8ToWide(const std::string& utf8, std::wstring* wide) {
    if (utf8.empty()) {
        return false;
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return false;
    }
    wide->assign(static_cast<size_t>(size), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                               static_cast<int>(utf8.size()), wide->data(), size) == size;
}

template <typename Fn>
bool ResolveExport(void* image, const std::string& module, const char* symbol, bool required,
                   Fn* out, std::string* error) {
    *out = reinterpret_cast<Fn>(GetProcAddress(static_cast<HMODULE>(image), symbol));
    if (*out == nullptr && required) {
        return Fail(error, "module " + Quoted(module) + " does not export " + symbol);
    }
    return true;
}

bool LoadAndResolveDllEntry(const SuyuRecompRegistryV1& registry,
                            const SuyuRecompRegistryEntryV1& entry,
                            const std::filesystem::path& image_dir, PendingHandles* pending,
                            ResolvedEntry* out, std::string* error) {
    const std::string name = entry.canonical_name;
    const std::string file = entry.dll_file_utf8;
    std::wstring wide_file;
    if (!Utf8ToWide(file, &wide_file)) {
        return Fail(error, "module " + Quoted(name) + ": DLL file name " + Quoted(file) +
                               " is not valid UTF-8");
    }
    std::error_code absolute_error;
    const std::filesystem::path directory = std::filesystem::absolute(image_dir, absolute_error);
    if (absolute_error) {
        return Fail(error, "module " + Quoted(name) + ": cannot resolve the image directory (" +
                               absolute_error.message() + ")");
    }
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR requires an absolute path.
    const std::filesystem::path path = directory / wide_file;
    HMODULE image = LoadLibraryExW(path.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                       LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!image) {
        return Fail(error, "module " + Quoted(name) + ": cannot load DLL " + Quoted(file) +
                               " (Win32 error " + std::to_string(GetLastError()) + ")");
    }
    pending->Add(image);

    const u32 expected = entry.expected_features;
    const bool abi6 = registry.bundle_abi == kAbiWithFeatures;
    const bool fm1 = abi6 && (expected & RecompImageFeature::FastmemPT1);
    const bool gg1 = abi6 && (expected & RecompImageFeature::GuardGen1);
    const bool fpx1 = abi6 && (expected & RecompImageFeature::ExactFpX1);
    const bool xmod1 = abi6 && (expected & RecompImageFeature::CrossModule1);
    if (!ResolveExport(image, name, RECOMP_EXPORT_LOOKUP, true, &out->lookup, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_SET_BASE, true, &out->set_base, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_RUN_SLICE, true, &out->run_slice, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_ABI, true, &out->image_abi_fn, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_GUARD_V2, true, &out->guard_v2, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_FEATURES, fm1, &out->features, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_FASTMEM_V1, fm1, &out->fastmem_v1, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_GUARD_GEN_V1, gg1, &out->guard_gen_v1, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_FPX_V1, fpx1, &out->fpx_v1, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_XMOD_V1, xmod1, &out->xmod_v1, error) ||
        !ResolveExport(image, name, RECOMP_EXPORT_INDEX32_V1, false, &out->index32_v1, error)) {
        return false;
    }

    RecompImageIdentityV1Fn identity = nullptr;
    if (!ResolveExport(image, name, RECOMP_IMAGE_IDENTITY_V1_SYMBOL, true, &identity, error)) {
        return false;
    }
    const char* image_name = nullptr;
    const uint8_t* image_bundle = nullptr;
    if (identity(&image_name, &image_bundle) != kIdentityV1Filled || !image_name ||
        !image_bundle) {
        return Fail(error, "module " + Quoted(name) + ": DLL " + Quoted(file) +
                               " did not fill its v1 identity");
    }
    if (name != image_name) {
        return Fail(error, "module " + Quoted(name) + ": DLL " + Quoted(file) +
                               " identifies itself as module " + Quoted(image_name));
    }
    if (std::memcmp(image_bundle, registry.bundle_id, RECOMP_BUNDLE_ID_BYTES) != 0) {
        return Fail(error, "module " + Quoted(name) + ": DLL " + Quoted(file) + " belongs to bundle " +
                               BundleIdHex(image_bundle) + " but the registry expects bundle " +
                               BundleIdHex(registry.bundle_id));
    }
    return true;
}

#endif // _WIN32

/// ABI, feature and handshake checks shared by static and DLL entries.
bool CheckEntry(const SuyuRecompRegistryV1& registry, const SuyuRecompRegistryEntryV1& entry,
                const ResolvedEntry& image, const RecompHostHandshake& host, RecompBundle* bundle,
                std::string* error) {
    const std::string name = entry.canonical_name;
    const unsigned abi = image.image_abi_fn();
    if (abi != entry.image_abi || abi != registry.bundle_abi) {
        return Fail(error, "module " + Quoted(name) + ": image ABI " + std::to_string(abi) +
                               " does not match the registry's ABI " +
                               std::to_string(entry.image_abi) + " (bundle ABI " +
                               std::to_string(registry.bundle_abi) + ")");
    }
    if (abi == kAbiWithoutFeatures) {
        return true;
    }

    const u32 expected = entry.expected_features;
    const u32 features = image.features();
    if (const u32 unknown = RecompImageFeature::Unsupported(features)) {
        return Fail(error, "module " + Quoted(name) + " requires image features " + Hex(unknown) +
                               " that this host does not implement");
    }
    if (features != expected) {
        return Fail(error, "module " + Quoted(name) + ": image features " + Hex(features) +
                               " do not match the registry's expected features " + Hex(expected));
    }
    bundle->features |= features;

    // GG1 images complete the FM1 handshake only after their own GG1 handshake.
    if (features & RecompImageFeature::GuardGen1) {
        RecompGuardGen::Module gg{};
        gg.word = image.guard_gen_v1(host.guard_gen_host_version, &gg.code_lo, &gg.code_end,
                                     &gg.base);
        if (!gg.word) {
            return Fail(error, "module " + Quoted(name) +
                                   " refused the generation guard (GG1) handshake");
        }
        bundle->guard_gen_modules.push_back(gg);
    }
    const RecompBundleFastmemLayout& fm = host.fastmem;
    if (image.fastmem_v1(fm.page_bits, fm.stride_log2, fm.pointer_mask, fm.off_table,
                         fm.off_limit) != kFastmemHandshakeAccepted) {
        return Fail(error, "module " + Quoted(name) +
                               " does not match this host's fastmem (FM1) layout");
    }
    if (features & RecompImageFeature::ExactFpX1) {
        const unsigned answer =
            image.fpx_v1(host.fpx.off_fpcr, host.fpx.off_fpsr, host.fpx.inhibit_bit);
        if (answer == 0) {
            return Fail(error, "module " + Quoted(name) +
                                   " does not match this host's FP layout (FPX1 handshake)");
        }
        if (bundle->fpx != 0 && bundle->fpx != answer) {
            return Fail(error, "module " + Quoted(name) + " FPX1 handshake answer " + Hex(answer) +
                                   " differs from the other modules' " + Hex(bundle->fpx));
        }
        bundle->fpx = answer;
    }
    return true;
}

} // namespace

bool GetRecompExecutableDirectory(std::filesystem::path* directory, std::string* error) {
#ifdef _WIN32
    // Long paths exceed MAX_PATH, so grow until the result is not truncated. The
    // upper bound (the 32767-character path limit) keeps a failing call finite.
    constexpr DWORD kInitialCapacityChars = MAX_PATH;
    constexpr DWORD kMaxCapacityChars = 32768;
    std::wstring buffer(kInitialCapacityChars, L'\0');
    while (true) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return Fail(error, "GetModuleFileNameW failed (Win32 error " +
                                   std::to_string(GetLastError()) + ")");
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            *directory = std::filesystem::path(buffer).parent_path();
            return true;
        }
        if (buffer.size() >= kMaxCapacityChars) {
            return Fail(error, "the executable path is longer than " +
                                   std::to_string(kMaxCapacityChars) + " characters");
        }
        buffer.assign(buffer.size() * 2, L'\0');
    }
#else
    static_cast<void>(directory);
    return Fail(error, "locating shared images beside the executable is not supported on this "
                       "platform");
#endif
}

std::optional<RecompBundle> LoadRecompBundleV1(const SuyuRecompRegistryV1& registry,
                                               const std::filesystem::path& image_dir,
                                               const RecompHostHandshake& host,
                                               std::string* error) {
    if (!ValidateHeader(registry, error)) {
        return std::nullopt;
    }

    PendingHandles pending;
    RecompBundle bundle;
    bundle.bundle_abi = registry.bundle_abi;
    std::vector<ResolvedEntry> resolved(registry.entry_count);
    for (u32 i = 0; i < registry.entry_count; ++i) {
        const SuyuRecompRegistryEntryV1& entry = registry.entries[i];
        const std::string name = entry.canonical_name;
        if (entry.binding == SUYU_RECOMP_BINDING_STATIC) {
            if (!ResolveStaticEntry(entry, &resolved[i], error)) {
                return std::nullopt;
            }
        } else {
#ifdef _WIN32
            if (!LoadAndResolveDllEntry(registry, entry, image_dir, &pending, &resolved[i],
                                        error)) {
                return std::nullopt;
            }
#else
            static_cast<void>(image_dir);
            Fail(error, "DLL-bound module " + name + " is not supported on this platform");
            return std::nullopt;
#endif
        }
        if (!CheckEntry(registry, entry, resolved[i], host, &bundle, error)) {
            return std::nullopt;
        }

        RecompBundleModule module;
        module.name = name;
        module.lookup = resolved[i].lookup;
        module.set_base = resolved[i].set_base;
        module.run_slice = resolved[i].run_slice;
        module.image_abi = entry.image_abi;
        module.guard_v2 = resolved[i].guard_v2;
        module.xmod_v1 = resolved[i].xmod_v1;
        module.index32_v1 = resolved[i].index32_v1;
        module.is_dll = entry.binding == SUYU_RECOMP_BINDING_DLL;
        if (module.is_dll) {
            module.dll_file = entry.dll_file_utf8;
        }
        bundle.modules.push_back(std::move(module));
    }

    // Guard v2: all modules must answer the query alike or the bundle is refused; agreement on 2
    // enables v2 everywhere, any other agreed answer leaves it "not negotiated".
    const unsigned query_answer = bundle.modules.front().guard_v2(0);
    for (const auto& module : bundle.modules) {
        const unsigned answer = module.guard_v2(0);
        if (answer != query_answer) {
            Fail(error, "module " + Quoted(module.name) + " answers guard version " +
                            std::to_string(answer) + " but module " +
                            Quoted(bundle.modules.front().name) + " answers " +
                            std::to_string(query_answer) + "; the images disagree on guard v2");
            return std::nullopt;
        }
    }
    bundle.guard_ready = query_answer == kGuardV2Version;
    for (const auto& module : bundle.modules) {
        module.guard_v2(bundle.guard_ready ? kGuardV2Version : 0);
    }
    bundle.long_slices = std::all_of(
        bundle.modules.cbegin(), bundle.modules.cend(), [](const RecompBundleModule& module) {
            return module.image_abi >= 4 && module.run_slice != nullptr;
        });
    // The bundle owns the images from here on and keeps them for the process lifetime.
    bundle.dll_handles = pending.Take();
    return bundle;
}

std::optional<std::vector<std::pair<size_t, u64>>> BindRecompBundleToInventory(
    const RecompBundle& bundle, const std::map<u64, std::string>& inventory, std::string* error) {
    const auto fail = [&](std::string message) {
        Fail(error, std::move(message));
        return std::nullopt;
    };
    if (inventory.empty()) {
        return fail("the loader's module inventory is empty");
    }
    std::map<std::string, u64> base_by_name;
    for (const auto& [base, name] : inventory) {
        if (!base_by_name.emplace(name, base).second) {
            return fail("the loader's module inventory lists module " + Quoted(name) +
                        " more than once");
        }
    }
    std::set<std::string> bundle_names;
    for (const auto& module : bundle.modules) {
        bundle_names.insert(module.name);
    }
    for (const auto& [name, base] : base_by_name) {
        if (!bundle_names.count(name)) {
            return fail("the loader's module inventory has module " + Quoted(name) + " (at " +
                        Hex(base) + ") that the bundle does not contain");
        }
    }
    std::vector<std::pair<size_t, u64>> bases;
    bases.reserve(bundle.modules.size());
    for (size_t i = 0; i < bundle.modules.size(); ++i) {
        const auto found = base_by_name.find(bundle.modules[i].name);
        if (found == base_by_name.end()) {
            return fail("bundle module " + Quoted(bundle.modules[i].name) +
                        " is missing from the loader's module inventory");
        }
        bases.emplace_back(i, found->second);
    }
    if (bases.size() != inventory.size()) {
        return fail("the bundle has " + std::to_string(bases.size()) +
                    " modules but the loader's inventory has " + std::to_string(inventory.size()));
    }
    return bases;
}

void ApplyRecompBundleBases(const RecompBundle& bundle,
                            const std::vector<std::pair<size_t, u64>>& bases) {
    for (const auto& [index, base] : bases) {
        bundle.modules[index].set_base(base);
    }
}

bool PublishRecompXmod(RecompBundle& bundle, const std::vector<std::pair<size_t, u64>>& bases,
                       std::string* error) {
    if (!(bundle.features & RecompImageFeature::CrossModule1)) {
        return true;
    }
    if (bundle.xmod_table) {
        return Fail(error, "the XMOD1 table is already published; unpublish it first");
    }
    if (bases.size() != bundle.modules.size() ||
        bases.size() > SUYU_RECOMP_XMOD_V1_MAX_MODULES) {
        return Fail(error, "XMOD1 needs one base for each of the " +
                               std::to_string(bundle.modules.size()) +
                               " modules (at most " +
                               std::to_string(SUYU_RECOMP_XMOD_V1_MAX_MODULES) + "), got " +
                               std::to_string(bases.size()));
    }
    std::vector<std::pair<u64, size_t>> by_base;
    std::set<size_t> seen;
    for (const auto& [index, base] : bases) {
        if (index >= bundle.modules.size() || !seen.insert(index).second) {
            return Fail(error, "XMOD1 base list has module index " + std::to_string(index) +
                                   " out of range or repeated");
        }
        const RecompBundleModule& module = bundle.modules[index];
        if (!module.xmod_v1 || !module.lookup) {
            return Fail(error, "module " + Quoted(module.name) +
                                   " has no xmod_v1/lookup pointer for the XMOD1 table");
        }
        by_base.emplace_back(base, index);
    }
    std::sort(by_base.begin(), by_base.end());
    for (size_t i = 1; i < by_base.size(); ++i) {
        if (by_base[i].first == by_base[i - 1].first) {
            return Fail(error, "modules " + Quoted(bundle.modules[by_base[i - 1].second].name) +
                                   " and " + Quoted(bundle.modules[by_base[i].second].name) +
                                   " share load base " + Hex(by_base[i].first));
        }
    }

    // Allocated for the process lifetime on purpose: see the header.
    auto* table = new SuyuRecompXmodTableV1{};
    table->version = SUYU_RECOMP_XMOD_V1_VERSION;
    table->struct_size = sizeof(SuyuRecompXmodTableV1);
    table->entry_size = sizeof(SuyuRecompXmodEntryV1);
    table->count = static_cast<u32>(by_base.size());
    for (size_t i = 0; i < by_base.size(); ++i) {
        table->entries[i].base = by_base[i].first;
        table->entries[i].lookup = bundle.modules[by_base[i].second].lookup;
    }
    for (size_t i = 0; i < bases.size(); ++i) {
        const RecompBundleModule& module = bundle.modules[bases[i].first];
        if (module.xmod_v1(table) != 1) {
            UnpublishRecompXmod(bundle);
            return Fail(error, "module " + Quoted(module.name) +
                                   " refused the XMOD1 table; every image was cleared");
        }
    }
    bundle.xmod_table = table;
    return true;
}

void UnpublishRecompXmod(RecompBundle& bundle) {
    for (const RecompBundleModule& module : bundle.modules) {
        if (module.xmod_v1) {
            module.xmod_v1(nullptr);
        }
    }
    bundle.xmod_table = nullptr;
}

std::vector<RecompIndexNote> DescribeRecompBlockIndexes(
    const RecompBundle& bundle, const std::vector<std::pair<size_t, u64>>& bases) {
    std::vector<RecompIndexNote> notes;
    for (const auto& entry : bases) {
        if (entry.first >= bundle.modules.size()) {
            notes.push_back({true, "block index report: module index " + std::to_string(entry.first) +
                                       " is outside the bundle's " +
                                       std::to_string(bundle.modules.size()) + " modules"});
            continue;
        }
        const RecompBundleModule& module = bundle.modules[entry.first];
        if (!module.index32_v1) {
            notes.push_back({false, module.name + ": no 32-bit block index export; dispatch uses "
                                                  "the image's lookup export"});
            continue;
        }
        std::string note;
        const RecompIndexView view = AcquireRecompIndexView(module.index32_v1, nullptr, &note);
        if (view.kind == RecompIndexView::Kind::Index32) {
            const u64 words = ((view.hi - view.lo) >> 2) + 1;
            notes.push_back({false, module.name + ": 32-bit block index of " +
                                        std::to_string(words) + " words (" +
                                        std::to_string(words * 4 / 1024) + " KiB)"});
        } else {
            notes.push_back({true, module.name + ": " + note});
        }
    }
    return notes;
}

} // namespace Core
