// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// smoke_topology_unit: the module topology (core/recompiler/recomp_topology.h)
// on its own: naming rules, NSO load order, the written recomp_modules.cmake and
// every validation failure.
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "core/recompiler/recomp_topology.h"

namespace R = suyu::recomp;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

const std::string kBundleHex = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// A failing request must return nullopt and an error that names `offender`.
bool FailsNaming(const R::RecompTopologyRequest& request, const std::string& offender) {
    std::string error;
    const auto topology = R::BuildRecompTopology(request, &error);
    if (topology.has_value() || !Contains(error, offender)) {
        std::fprintf(stderr, "expected failure naming '%s', got error '%s'\n", offender.c_str(),
                     error.c_str());
        return false;
    }
    return true;
}

int Naming() {
    CHECK(R::SharedImageTargetName("main") == "recompiled_image");
    CHECK(R::SharedImageTargetName("sdk") == "recompiled_sdk");
    CHECK(R::SharedImageTargetName("subsdk3") == "recompiled_subsdk3");
    CHECK(R::SharedImageFileName("main") == "recompiled_image.dll");
    CHECK(R::SharedImageFileName("subsdk3") == "recompiled_subsdk3.dll");
    return 0;
}

int LoadOrder() {
    const std::vector<std::string> shuffled = {"sdk",     "zeta",    "subsdk2", "main", "alpha",
                                               "subsdk0", "subsdk9", "rtld"};
    const std::vector<std::string> expected = {"rtld",    "main", "subsdk0", "subsdk2",
                                               "subsdk9", "sdk",  "zeta",    "alpha"};
    CHECK(R::OrderModulesForLoad(shuffled) == expected);
    CHECK(R::OrderModulesForLoad({}).empty());
    // Modules that are absent are not invented.
    CHECK((R::OrderModulesForLoad({"sdk", "main"}) == std::vector<std::string>{"main", "sdk"}));
    return 0;
}

int AllStaticText() {
    std::string error;
    const auto topology = R::BuildRecompTopology({{"sdk", "main", "rtld"}, {}, ""}, &error);
    CHECK(topology.has_value());
    CHECK(!topology->HasDllEntries());
    CHECK(topology->version == R::kRecompTopologyVersion);
    // SUYU_RECOMP_MODULES keeps the caller's order; entries are in load order.
    CHECK((topology->selection_order == std::vector<std::string>{"sdk", "main", "rtld"}));
    CHECK(topology->entries.size() == 3);
    CHECK(topology->entries[0].name == "rtld");
    CHECK(topology->entries[1].name == "main");
    CHECK(topology->entries[2].name == "sdk");
    for (const auto& e : topology->entries) {
        CHECK(e.binding == R::RecompModuleBinding::Static);
        CHECK(e.dll_file.empty());
    }
    // The text game_export.cpp wrote before topology support existed.
    CHECK(R::WriteRecompModulesCMake(*topology) ==
          "# Generated alongside recomp_registration.c.\nset(SUYU_RECOMP_MODULES sdk main rtld)\n");
    return 0;
}

int SplitText() {
    std::string error;
    const auto topology =
        R::BuildRecompTopology({{"sdk", "main", "subsdk0"}, {"sdk", "main"}, kBundleHex}, &error);
    CHECK(topology.has_value());
    CHECK(topology->HasDllEntries());
    CHECK(topology->bundle_id_hex == kBundleHex);
    CHECK(topology->entries[0].name == "main");
    CHECK(topology->entries[0].binding == R::RecompModuleBinding::Dll);
    CHECK(topology->entries[0].dll_file == "recompiled_image.dll");
    CHECK(topology->entries[1].name == "subsdk0");
    CHECK(topology->entries[1].binding == R::RecompModuleBinding::Static);
    CHECK(topology->entries[2].name == "sdk");
    CHECK(topology->entries[2].dll_file == "recompiled_sdk.dll");

    const std::string text = R::WriteRecompModulesCMake(*topology);
    CHECK(text.rfind("# Generated alongside recomp_registration.c.\n"
                     "set(SUYU_RECOMP_MODULES sdk main subsdk0)\n",
                     0) == 0);
    CHECK(Contains(text, "set(SUYU_RECOMP_TOPOLOGY_VERSION 1)\n"));
    CHECK(Contains(text, "set(SUYU_RECOMP_BUNDLE_ID " + kBundleHex + ")\n"));
    // DLL modules in load order.
    CHECK(Contains(text, "set(SUYU_RECOMP_BUNDLE_ID_BYTES " + R::BundleIdHexToByteList(kBundleHex) +
                             ")\n"));
    CHECK(Contains(text, "set(SUYU_RECOMP_DLL_MODULES main sdk)\n"));
    CHECK(Contains(text, "set(SUYU_RECOMP_DLL_TARGET_main recompiled_image)\n"));
    CHECK(Contains(text, "set(SUYU_RECOMP_DLL_FILE_main recompiled_image.dll)\n"));
    CHECK(Contains(text, "set(SUYU_RECOMP_DLL_TARGET_sdk recompiled_sdk)\n"));
    CHECK(Contains(text, "set(SUYU_RECOMP_DLL_FILE_sdk recompiled_sdk.dll)\n"));
    CHECK(!Contains(text, "SUYU_RECOMP_DLL_TARGET_subsdk0"));
    return 0;
}

int Validation() {
    CHECK(FailsNaming({{}, {}, ""}, "empty"));
    CHECK(FailsNaming({{"main", "sdk", "main"}, {}, ""}, "'main'"));
    CHECK(FailsNaming({{"main", "Sdk"}, {}, ""}, "'Sdk'"));
    CHECK(FailsNaming({{"main", "sub-sdk"}, {}, ""}, "'sub-sdk'"));
    CHECK(FailsNaming({{"main", ""}, {}, ""}, "[a-z0-9_]+"));
    CHECK(FailsNaming({{"main"}, {"sdk"}, kBundleHex}, "'sdk'"));
    CHECK(FailsNaming({{"main", "sdk"}, {"sdk", "sdk"}, kBundleHex}, "'sdk'"));
    CHECK(FailsNaming({{"main"}, {"main"}, ""}, "bundle id"));
    CHECK(FailsNaming({{"main"}, {"main"}, "0123"}, "'0123'"));
    std::string upper = kBundleHex;
    upper[0] = 'A';
    CHECK(FailsNaming({{"main"}, {"main"}, upper}, upper));
    std::string not_hex = kBundleHex;
    not_hex[10] = 'g';
    CHECK(FailsNaming({{"main"}, {"main"}, not_hex}, not_hex));
    CHECK(FailsNaming({{"main"}, {"main"}, kBundleHex + "0"}, "bundle id"));
    // Error output is optional.
    CHECK(!R::BuildRecompTopology({{}, {}, ""}, nullptr).has_value());
    // No DLL modules: the bundle id may be empty.
    CHECK(R::BuildRecompTopology({{"main"}, {}, ""}, nullptr).has_value());
    return 0;
}

int BundleBytes() {
    CHECK(R::BundleIdHexToByteList(kBundleHex) ==
          "0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,"
          "0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef");
    // 32 items: 31 separators.
    const std::string list = R::BundleIdHexToByteList(kBundleHex);
    CHECK(std::count(list.begin(), list.end(), ',') == RECOMP_BUNDLE_ID_BYTES - 1);
    CHECK(R::BundleIdHexToByteList("").empty());
    CHECK(R::BundleIdHexToByteList("0123").empty());
    CHECK(R::BundleIdHexToByteList(std::string(64, 'A')).empty());
    return 0;
}

int BasenameRule() {
    CHECK(R::detail::IsPlainBasename("recompiled_image.dll"));
    CHECK(!R::detail::IsPlainBasename(""));
    CHECK(!R::detail::IsPlainBasename("a/b.dll"));
    CHECK(!R::detail::IsPlainBasename("a\\b.dll"));
    CHECK(!R::detail::IsPlainBasename("c:b.dll"));
    CHECK(!R::detail::IsPlainBasename("..b.dll"));
    CHECK(!R::detail::IsPlainBasename(".."));
    return 0;
}

} // namespace

// Writes recomp_modules.cmake with the production writer, so tests of the CMake
// consumers do not carry a copy of the format:
//   smoke_topology_unit --write-modules-cmake <out> <bundle|-> <module>... [--dll <module>]...
int WriteModulesCMakeMode(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: --write-modules-cmake <out> <bundle|-> <module>... "
                             "[--dll <module>]...\n");
        return 2;
    }
    R::RecompTopologyRequest request;
    request.bundle_id_hex = std::string(argv[3]) == "-" ? "" : argv[3];
    bool dll_next = false;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dll") {
            dll_next = true;
        } else {
            (dll_next ? request.dll_modules : request.modules).push_back(arg);
            dll_next = false;
        }
    }
    std::string error;
    const auto topology = R::BuildRecompTopology(request, &error);
    if (!topology) {
        std::fprintf(stderr, "invalid topology: %s\n", error.c_str());
        return 2;
    }
    const std::string text = R::WriteRecompModulesCMake(*topology);
    std::FILE* file = std::fopen(argv[2], "wb");
    if (!file || std::fwrite(text.data(), 1, text.size(), file) != text.size()) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        if (file) std::fclose(file);
        return 2;
    }
    return std::fclose(file) == 0 ? 0 : 2;
}

int main(int argc, char** argv) {
    // Prints the byte-list form of a bundle id, for tests that must pass it as a
    // compile definition without retyping the conversion.
    if (argc == 3 && std::string(argv[1]) == "--bundle-bytes") {
        const std::string list = R::BundleIdHexToByteList(argv[2]);
        std::fputs(list.c_str(), stdout);
        return list.empty() ? 2 : 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--write-modules-cmake") {
        return WriteModulesCMakeMode(argc, argv);
    }
    if (int rc = Naming()) return rc;
    if (int rc = LoadOrder()) return rc;
    if (int rc = AllStaticText()) return rc;
    if (int rc = SplitText()) return rc;
    if (int rc = Validation()) return rc;
    if (int rc = BundleBytes()) return rc;
    if (int rc = BasenameRule()) return rc;
    std::puts("topology unit checks passed");
    return 0;
}
