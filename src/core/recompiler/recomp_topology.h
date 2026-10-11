// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The single authoritative description of a recompiled export's module topology:
// which modules exist, in what NSO load order, and whether each is linked into
// the launcher (Static) or built as a shared image (Dll). The generator's CMake,
// the exporter's registration files and the CMake consumers (clang-cl prebuild,
// in-tree launcher, export build kit) all take names, file names and the module
// list from here, so no value is spelled out in a second place that can drift.
//
// Header-only, standard library only: it is shared by the exporter (Qt), the
// generator and tests.

#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/loader/nso_module_names.h"
#include "recomp_image_identity.h"

namespace suyu::recomp {

// Bumped when the meaning of the values written by WriteRecompModulesCMake
// changes. CMake consumers compare it and refuse a version they do not know.
inline constexpr unsigned kRecompTopologyVersion = 1;

// A bundle id is a SHA-256 digest written as lowercase hex; its byte length is
// defined once, by the identity contract.
inline constexpr std::size_t kBundleIdHexLength = RECOMP_BUNDLE_ID_BYTES * 2;

// Target name the generator gives the main module's shared image; every other
// module is "recompiled_<module>".
inline constexpr std::string_view kMainSharedImageTargetName = "recompiled_image";
inline constexpr std::string_view kSharedImageTargetPrefix = "recompiled_";
// Shared images are a Windows-only feature (the launcher loads them with
// LoadLibraryExW), so the file name always carries the .dll suffix.
inline constexpr std::string_view kSharedImageFileSuffix = ".dll";

enum class RecompModuleBinding { Static, Dll };

// CMake target the generated module CMake creates for a module's shared image.
inline std::string SharedImageTargetName(std::string_view module) {
    if (module == "main") {
        return std::string(kMainSharedImageTargetName);
    }
    return std::string(kSharedImageTargetPrefix) + std::string(module);
}

// File name of the built shared image (Windows only).
inline std::string SharedImageFileName(std::string_view module) {
    return SharedImageTargetName(module) + std::string(kSharedImageFileSuffix);
}

// NSO load order, which is also the index order Core's base setter uses: the loader's
// canonical module list (core/loader/nso_module_names.h: rtld, main, subsdk0..subsdk9,
// sdk), then any other module in input order. Modules
// absent from the input are not invented.
inline std::vector<std::string> OrderModulesForLoad(const std::vector<std::string>& modules) {
    std::vector<std::string> ordered;
    ordered.reserve(modules.size());
    const auto contains = [](const std::vector<std::string>& list, const std::string& name) {
        return std::find(list.begin(), list.end(), name) != list.end();
    };
    const auto take = [&](const std::string& name) {
        if (contains(modules, name) && !contains(ordered, name)) {
            ordered.push_back(name);
        }
    };
    // The loader's module list is the load order (rtld, main, subsdk0..9, sdk).
    for (const char* canonical : Loader::kNsoModuleNames) {
        take(canonical);
    }
    for (const auto& m : modules) {
        if (!contains(ordered, m)) {
            ordered.push_back(m);
        }
    }
    return ordered;
}

struct RecompTopologyEntry {
    std::string name;
    RecompModuleBinding binding = RecompModuleBinding::Static;
    std::string dll_file; // empty for Static
};

struct RecompTopology {
    unsigned version = kRecompTopologyVersion;
    std::string bundle_id_hex;                // empty when no module is a Dll
    std::vector<std::string> selection_order; // caller order, for SUYU_RECOMP_MODULES
    std::vector<RecompTopologyEntry> entries; // NSO load order

    bool HasDllEntries() const {
        return std::any_of(entries.begin(), entries.end(), [](const RecompTopologyEntry& e) {
            return e.binding == RecompModuleBinding::Dll;
        });
    }
};

struct RecompTopologyRequest {
    std::vector<std::string> modules;
    std::vector<std::string> dll_modules;
    std::string bundle_id_hex;
};

namespace detail {

inline bool IsModuleNameValid(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

inline bool IsBundleIdHexValid(std::string_view hex) {
    return hex.size() == kBundleIdHexLength &&
           std::all_of(hex.begin(), hex.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

// A DLL file name must be a plain basename: the launcher joins it to its own
// directory, so separators, drive prefixes and ".." would escape it.
inline bool IsPlainBasename(std::string_view file) {
    if (file.empty() || file.find("..") != std::string_view::npos) {
        return false;
    }
    return file.find_first_of("/\\:") == std::string_view::npos;
}

inline std::string LowerAscii(std::string_view text) {
    std::string out(text);
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline std::nullopt_t Fail(std::string* error, std::string message) {
    if (error) {
        *error = std::move(message);
    }
    return std::nullopt;
}

} // namespace detail

// Validates the request and builds the topology. Every failure returns nullopt
// and, when `error` is non-null, a message naming the offending item.
inline std::optional<RecompTopology> BuildRecompTopology(const RecompTopologyRequest& request,
                                                         std::string* error) {
    if (request.modules.empty()) {
        return detail::Fail(error, "the module list is empty");
    }
    std::unordered_set<std::string> seen_modules;
    for (const auto& m : request.modules) {
        if (!detail::IsModuleNameValid(m)) {
            return detail::Fail(error, "module name '" + m + "' does not match [a-z0-9_]+");
        }
        if (!seen_modules.insert(m).second) {
            return detail::Fail(error, "module '" + m + "' is listed more than once");
        }
    }
    std::unordered_set<std::string> seen_dll_modules;
    for (const auto& m : request.dll_modules) {
        if (!seen_modules.count(m)) {
            return detail::Fail(error, "DLL module '" + m + "' is not in the module list");
        }
        if (!seen_dll_modules.insert(m).second) {
            return detail::Fail(error, "DLL module '" + m + "' is listed more than once");
        }
    }
    if (!request.dll_modules.empty() && !detail::IsBundleIdHexValid(request.bundle_id_hex)) {
        return detail::Fail(error, "bundle id '" + request.bundle_id_hex +
                                       "' is not 64 lowercase hex characters");
    }

    RecompTopology topology;
    topology.bundle_id_hex = request.bundle_id_hex;
    topology.selection_order = request.modules;
    std::unordered_set<std::string> seen_files; // lowercase: Windows file names ignore case
    for (const auto& name : OrderModulesForLoad(request.modules)) {
        RecompTopologyEntry entry;
        entry.name = name;
        if (seen_dll_modules.count(name)) {
            entry.binding = RecompModuleBinding::Dll;
            entry.dll_file = SharedImageFileName(name);
            if (!detail::IsPlainBasename(entry.dll_file)) {
                return detail::Fail(error, "DLL file name '" + entry.dll_file + "' of module '" +
                                               name + "' is not a plain file name");
            }
            if (!seen_files.insert(detail::LowerAscii(entry.dll_file)).second) {
                return detail::Fail(error, "DLL file name '" + entry.dll_file + "' of module '" +
                                               name + "' collides with another module's");
            }
        }
        topology.entries.push_back(std::move(entry));
    }
    return topology;
}

// The validated bundle id as a C initializer list, "0x01,0x23,...", one item per
// digest byte, for the identity export (recomp_image_identity.h). Empty when the
// text is not a valid bundle id.
inline std::string BundleIdHexToByteList(std::string_view bundle_id_hex) {
    if (!detail::IsBundleIdHexValid(bundle_id_hex)) {
        return {};
    }
    std::string list;
    for (std::size_t i = 0; i < bundle_id_hex.size(); i += 2) {
        if (i != 0) {
            list += ",";
        }
        list += "0x";
        list += bundle_id_hex[i];
        list += bundle_id_hex[i + 1];
    }
    return list;
}

// Text of recomp_modules.cmake. An all-static topology writes exactly the
// module list that existed before topology support, so such exports are
// unchanged; DLL entries append the binding variables that CMake consumers read.
inline std::string WriteRecompModulesCMake(const RecompTopology& topology) {
    std::string text = "# Generated alongside recomp_registration.c.\nset(SUYU_RECOMP_MODULES";
    for (const auto& m : topology.selection_order) {
        text += " " + m;
    }
    text += ")\n";
    if (!topology.HasDllEntries()) {
        return text;
    }
    std::string dll_modules;
    std::string per_module;
    for (const auto& e : topology.entries) {
        if (e.binding != RecompModuleBinding::Dll) {
            continue;
        }
        dll_modules += (dll_modules.empty() ? "" : " ") + e.name;
        per_module += "set(SUYU_RECOMP_DLL_TARGET_" + e.name + " " + SharedImageTargetName(e.name) +
                      ")\n";
        per_module += "set(SUYU_RECOMP_DLL_FILE_" + e.name + " " + e.dll_file + ")\n";
    }
    text += "set(SUYU_RECOMP_TOPOLOGY_VERSION " + std::to_string(topology.version) + ")\n";
    text += "set(SUYU_RECOMP_BUNDLE_ID " + topology.bundle_id_hex + ")\n";
    text += "set(SUYU_RECOMP_BUNDLE_ID_BYTES " + BundleIdHexToByteList(topology.bundle_id_hex) +
            ")\n";
    text += "set(SUYU_RECOMP_DLL_MODULES " + dll_modules + ")\n";
    text += per_module;
    return text;
}

// What the generated registry must state about the images, as decided by the
// generator options the whole bundle was emitted with.
struct RecompRegistryOptions {
    unsigned image_abi = 0;         // expected recomp_image_abi() of every module (5 or 6)
    unsigned expected_features = 0; // expected recomp_image_features() of every module
    bool fastmem = false;           // FM1 handshake exports (ABI 6)
    bool guard_gen = false;         // GG1 handshake export
    bool fpx = false;               // FPX1 handshake export
    bool cross_chain = false;       // XMOD1 shared lookup table export (needs FM1)
};

namespace detail {

// A C string literal for a registry value. Module names and DLL file names are
// validated to be plain, so this only has to keep a stray quote from breaking out.
inline std::string CStringLiteral(std::string_view text) {
    std::string literal = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            literal += '\\';
        }
        literal += c;
    }
    return literal + "\"";
}

} // namespace detail

// Text of recomp_registration.c for a topology that binds at least one module as
// a DLL (registry v1, core/recompiler/recomp_registry_v1.h). All-static exports
// keep the v4 registry the exporter already writes, so this returns an empty
// string for them, and also for a topology with an invalid bundle id or options
// that are inconsistent (GG1/FPX1 without FM1, or ABI other than 5/6, or FM1
// features on ABI 5): the caller must treat empty as an error, never write it.
//
// Static entries name the same renamed symbols as the v4 writer
// (recomp_image_<function>_<module>); DLL entries carry no function pointers.
// Every definition is preceded by a prototype (GCC -Werror=missing-declarations).
inline std::string WriteRecompRegistryV1C(const RecompTopology& topology,
                                          const RecompRegistryOptions& options) {
    constexpr unsigned kAbiWithoutFeatures = 5;
    constexpr unsigned kAbiWithFeatures = 6;
    if (!topology.HasDllEntries() || topology.entries.empty()) {
        return {};
    }
    const std::string bundle_bytes = BundleIdHexToByteList(topology.bundle_id_hex);
    if (bundle_bytes.empty()) {
        return {};
    }
    const bool abi_ok = options.image_abi == kAbiWithoutFeatures || options.image_abi == kAbiWithFeatures;
    const bool features_ok = options.image_abi == kAbiWithFeatures
                                 ? options.fastmem
                                 : !options.fastmem && !options.guard_gen && !options.fpx &&
                                       options.expected_features == 0;
    if (!abi_ok || !features_ok ||
        (!options.fastmem && (options.guard_gen || options.fpx || options.cross_chain))) {
        return {};
    }

    unsigned static_count = 0;
    unsigned dll_count = 0;
    std::string externs;
    std::string entries;
    for (const auto& e : topology.entries) {
        const bool is_dll = e.binding == RecompModuleBinding::Dll;
        (is_dll ? dll_count : static_count) += 1;
        entries += "    {\n";
        entries += "        .canonical_name = " + detail::CStringLiteral(e.name) + ",\n";
        entries += std::string("        .binding = ") +
                   (is_dll ? "SUYU_RECOMP_BINDING_DLL" : "SUYU_RECOMP_BINDING_STATIC") + ",\n";
        entries += "        .dll_file_utf8 = " +
                   (is_dll ? detail::CStringLiteral(e.dll_file) : std::string("(const char*)0")) + ",\n";
        entries += "        .image_abi = " + std::to_string(options.image_abi) + "u,\n";
        entries += "        .expected_features = " + std::to_string(options.expected_features) +
                   "u,\n";
        if (!is_dll) {
            const std::string& m = e.name;
            externs += "extern SuyuRecompBlockFn recomp_image_lookup_" + m + "(uint64_t);\n";
            externs += "extern void recomp_image_set_base_" + m + "(uint64_t);\n";
            externs += "extern void recomp_image_run_slice_" + m + "(void*);\n";
            externs += "extern unsigned recomp_image_abi_" + m + "(void);\n";
            externs += "extern unsigned recomp_image_guard_v2_" + m + "(unsigned);\n";
            entries += "        .lookup = recomp_image_lookup_" + m + ",\n";
            entries += "        .set_base = recomp_image_set_base_" + m + ",\n";
            entries += "        .run_slice = recomp_image_run_slice_" + m + ",\n";
            entries += "        .image_abi_fn = recomp_image_abi_" + m + ",\n";
            entries += "        .guard_v2 = recomp_image_guard_v2_" + m + ",\n";
            if (options.fastmem) {
                externs += "extern unsigned recomp_image_features_" + m + "(void);\n";
                externs += "extern unsigned recomp_image_fastmem_v1_" + m +
                           "(uint32_t, uint32_t, uint64_t, uint32_t, uint32_t);\n";
                entries += "        .features = recomp_image_features_" + m + ",\n";
                entries += "        .fastmem_v1 = recomp_image_fastmem_v1_" + m + ",\n";
            }
            if (options.guard_gen) {
                externs += "extern uint32_t* recomp_image_guard_gen_v1_" + m +
                           "(uint32_t, uint64_t*, uint64_t*, const uint64_t**);\n";
                entries += "        .guard_gen_v1 = recomp_image_guard_gen_v1_" + m + ",\n";
            }
            if (options.fpx) {
                externs += "extern unsigned recomp_image_fpx_v1_" + m +
                           "(uint32_t, uint32_t, uint64_t);\n";
                entries += "        .fpx_v1 = recomp_image_fpx_v1_" + m + ",\n";
            }
            if (options.cross_chain) {
                externs += "extern unsigned recomp_image_xmod_v1_" + m +
                           "(const SuyuRecompXmodTableV1*);\n";
                entries += "        .xmod_v1 = recomp_image_xmod_v1_" + m + ",\n";
            }
        }
        entries += "    },\n";
    }

    std::string text =
        "/* auto-generated by suyu game export - DO NOT EDIT */\n"
        "/* Registry v1 of a split export: this game's modules in NSO load order, each\n"
        "   linked statically or bound to a shared image beside the launcher. The layout\n"
        "   is defined by core/recompiler/recomp_registry_v1.h. */\n"
        "#include \"core/recompiler/recomp_registry_v1.h\"\n\n";
    text += externs;
    text += "\nstatic const SuyuRecompRegistryEntryV1 s_entries[] = {\n" + entries + "};\n\n";
    text += "static const SuyuRecompRegistryV1 s_registry = {\n";
    text += "    SUYU_RECOMP_REGISTRY_V1_VERSION,\n";
    text += "    (uint32_t)sizeof(SuyuRecompRegistryV1),\n";
    text += "    (uint32_t)sizeof(SuyuRecompRegistryEntryV1),\n";
    text += "    (uint32_t)(sizeof(s_entries) / sizeof(s_entries[0])),\n";
    text += "    " + std::to_string(static_count) + "u,\n";
    text += "    " + std::to_string(dll_count) + "u,\n";
    text += "    " + std::to_string(options.image_abi) + "u,\n";
    text += "    {" + bundle_bytes + "},\n";
    text += "    s_entries,\n";
    text += "};\n\n";
    text += "const SuyuRecompRegistryV1* suyu_recomp_registry_v1(void);\n";
    text += "const SuyuRecompRegistryV1* suyu_recomp_registry_v1(void) {\n";
    text += "    return &s_registry;\n";
    text += "}\n";
    return text;
}

// Content of recomp_topology_v1.h, the marker that tells the in-tree CMake the
// export's registry is topology v1 (written next to the v1 recomp_registration.c).
inline std::string WriteRecompTopologyV1Marker() {
    return "/* This export's recomp_registration.c is a topology v1 registry\n"
           "   (suyu_recomp_registry_v1 in core/recompiler/recomp_registry_v1.h). */\n"
           "#define SUYU_RECOMP_TOPOLOGY_V1 1\n";
}

} // namespace suyu::recomp
