// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// smoke_topology_unit: the module topology (core/recompiler/recomp_topology.h)
// on its own: naming rules, NSO load order, the written recomp_modules.cmake and
// every validation failure.
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "core/recompiler/pe_image_size.h"
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
    // The loader's canonical list is the source of the load order; pin its content so a
    // change to it is a deliberate one (the NSO load order is part of the registry layout).
    const std::vector<std::string> canonical(Loader::kNsoModuleNames.begin(),
                                             Loader::kNsoModuleNames.end());
    CHECK((canonical == std::vector<std::string>{"rtld", "main", "subsdk0", "subsdk1", "subsdk2",
                                                 "subsdk3", "subsdk4", "subsdk5", "subsdk6",
                                                 "subsdk7", "subsdk8", "subsdk9", "sdk"}));
    CHECK(R::OrderModulesForLoad(canonical) == canonical);
    CHECK(R::OrderModulesForLoad({canonical.rbegin(), canonical.rend()}) == canonical);
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

size_t Count(const std::string& text, const std::string& part) {
    size_t count = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
        ++count;
    }
    return count;
}

R::RecompTopology SplitTopology() {
    std::string error;
    auto topology = R::BuildRecompTopology({{"sdk", "main", "subsdk0"}, {"main"}, kBundleHex}, &error);
    return topology.value();
}

int RegistryV1Text() {
    const R::RecompTopology topology = SplitTopology();
    const R::RecompRegistryOptions all_features{6, 7, true, true, true};
    const std::string text = R::WriteRecompRegistryV1C(topology, all_features);
    CHECK(!text.empty());
    CHECK(Contains(text, "#include \"core/recompiler/recomp_registry_v1.h\"\n"));
    // Static modules (subsdk0, sdk) get externs and pointers; the DLL (main) none.
    for (const char* function : {"lookup", "set_base", "run_slice", "abi", "guard_v2", "features",
                                 "fastmem_v1", "guard_gen_v1", "fpx_v1"}) {
        for (const char* module : {"subsdk0", "sdk"}) {
            CHECK(Contains(text, std::string("recomp_image_") + function + "_" + module + "("));
        }
        CHECK(!Contains(text, std::string("recomp_image_") + function + "_main"));
    }
    // Entries in NSO load order: main (DLL), subsdk0, sdk.
    const size_t at_main = text.find(".canonical_name = \"main\"");
    const size_t at_subsdk0 = text.find(".canonical_name = \"subsdk0\"");
    const size_t at_sdk = text.find(".canonical_name = \"sdk\"");
    CHECK(at_main != std::string::npos && at_main < at_subsdk0 && at_subsdk0 < at_sdk);
    CHECK(Contains(text, ".binding = SUYU_RECOMP_BINDING_DLL"));
    CHECK(Count(text, ".binding = SUYU_RECOMP_BINDING_STATIC") == 2);
    CHECK(Contains(text, ".dll_file_utf8 = \"recompiled_image.dll\""));
    CHECK(Count(text, ".dll_file_utf8 = (const char*)0") == 2);
    CHECK(Count(text, ".image_abi = 6u") == 3);
    CHECK(Count(text, ".expected_features = 7u") == 3);
    // Counts, bundle ABI and bundle id bytes.
    CHECK(Contains(text, "    2u,\n    1u,\n    6u,\n    {" + R::BundleIdHexToByteList(kBundleHex) +
                             "},\n"));
    CHECK(Contains(text, "const SuyuRecompRegistryV1* suyu_recomp_registry_v1(void);\n"
                         "const SuyuRecompRegistryV1* suyu_recomp_registry_v1(void) {"));

    // Pointers appear only for the features the options enable.
    const std::string fm1_only = R::WriteRecompRegistryV1C(topology, {6, 1, true, false, false});
    CHECK(Contains(fm1_only, "recomp_image_fastmem_v1_sdk"));
    CHECK(!Contains(fm1_only, "guard_gen_v1") && !Contains(fm1_only, "fpx_v1") &&
          !Contains(fm1_only, "xmod_v1"));
    // XMOD1 registrations carry the handshake pointer for static modules only, and need FM1.
    const std::string xmod = R::WriteRecompRegistryV1C(topology, {6, 9, true, false, false, true});
    CHECK(Contains(xmod, "recomp_image_xmod_v1_sdk") && Contains(xmod, "recomp_image_xmod_v1_subsdk0"));
    CHECK(!Contains(xmod, "recomp_image_xmod_v1_main") && Count(xmod, ".xmod_v1 = ") == 2);
    CHECK(R::WriteRecompRegistryV1C(topology, {5, 0, false, false, false, true}).empty());
    const std::string abi5 = R::WriteRecompRegistryV1C(topology, {5, 0, false, false, false});
    CHECK(!abi5.empty());
    CHECK(Contains(abi5, "recomp_image_guard_v2_sdk"));
    CHECK(!Contains(abi5, "recomp_image_features") && !Contains(abi5, "fastmem") &&
          !Contains(abi5, "guard_gen") && !Contains(abi5, "fpx"));
    CHECK(Contains(abi5, ".image_abi = 5u") && Contains(abi5, ".expected_features = 0u"));

    // Not a v1 registry: all-static topology, and options that contradict each other.
    std::string error;
    const auto all_static = R::BuildRecompTopology({{"main", "sdk"}, {}, ""}, &error);
    CHECK(R::WriteRecompRegistryV1C(*all_static, all_features).empty());
    CHECK(R::WriteRecompRegistryV1C(topology, {7, 1, true, false, false}).empty());
    CHECK(R::WriteRecompRegistryV1C(topology, {6, 4, false, false, true}).empty());
    CHECK(R::WriteRecompRegistryV1C(topology, {5, 1, true, false, false}).empty());
    CHECK(R::WriteRecompRegistryV1C(topology, {5, 1, false, false, false}).empty());
    R::RecompTopology no_bundle = topology;
    no_bundle.bundle_id_hex.clear();
    CHECK(R::WriteRecompRegistryV1C(no_bundle, all_features).empty());
    return 0;
}

int TopologyMarker() {
    const std::string marker = R::WriteRecompTopologyV1Marker();
    CHECK(Contains(marker, "#define SUYU_RECOMP_TOPOLOGY_V1 1\n"));
    return 0;
}

// A minimal PE32+ x64 header with the given SizeOfImage, built byte by byte so the
// reader is tested against the documented layout and not against its own constants.
std::vector<std::uint8_t> MakePe(std::uint32_t size_of_image, std::uint16_t machine = 0x8664,
                                 std::uint16_t optional_magic = 0x20B,
                                 std::uint32_t e_lfanew = 0x80,
                                 std::uint16_t declared_optional_size = 0xF0) {
    std::vector<std::uint8_t> bytes(0x200, 0);
    const auto put16 = [&](std::size_t at, std::uint16_t v) {
        bytes[at] = static_cast<std::uint8_t>(v);
        bytes[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    const auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            bytes[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
        }
    };
    bytes[0] = 'M';
    bytes[1] = 'Z';
    put32(0x3C, e_lfanew);
    const std::size_t pe = 0x80;
    bytes[pe] = 'P';
    bytes[pe + 1] = 'E';
    put16(pe + 4, machine);
    // COFF SizeOfOptionalHeader (offset 16 in the COFF header, which follows "PE\0\0").
    put16(pe + 4 + 16, declared_optional_size);
    const std::size_t optional_header = pe + 4 + 20;
    put16(optional_header, optional_magic);
    put32(optional_header + 56, size_of_image);
    return bytes;
}

bool PeFailsNaming(const std::vector<std::uint8_t>& bytes, const std::string& reason) {
    std::string error;
    const auto size = R::ParsePeSizeOfImage(bytes, &error);
    if (size.has_value() || !Contains(error, reason)) {
        std::fprintf(stderr, "expected PE failure containing '%s', got '%s'\n", reason.c_str(),
                     error.c_str());
        return false;
    }
    return true;
}

int PeImageSize() {
    std::string error;
    const auto valid = R::ParsePeSizeOfImage(MakePe(0x12345000u), &error);
    CHECK(valid.has_value() && *valid == 0x12345000u);
    const auto over_limit = R::ParsePeSizeOfImage(MakePe(0x90000000u), &error);
    CHECK(over_limit.has_value() && *over_limit > R::kPeImageLimitBytes);
    CHECK(R::kPeImageLimitBytes == 0x80000000ull);

    auto truncated = MakePe(0x1000);
    truncated.resize(0x80 + 4 + 20 + 40); // ends inside the optional header
    CHECK(PeFailsNaming(truncated, "outside the file"));
    CHECK(PeFailsNaming({}, "MZ"));
    CHECK(PeFailsNaming({'M', 'Z', 0, 0}, "MZ"));
    auto bad_mz = MakePe(0x1000);
    bad_mz[0] = 'X';
    CHECK(PeFailsNaming(bad_mz, "MZ"));
    CHECK(PeFailsNaming(MakePe(0x1000, 0x8664, 0x20B, 0x7FFFFFF0u), "e_lfanew"));
    CHECK(PeFailsNaming(MakePe(0x1000, 0x8664, 0x20B, 0x10), "e_lfanew"));
    auto bad_signature = MakePe(0x1000);
    bad_signature[0x80 + 2] = 1;
    CHECK(PeFailsNaming(bad_signature, "signature"));
    CHECK(PeFailsNaming(MakePe(0x1000, 0x014C), "x64"));
    CHECK(PeFailsNaming(MakePe(0x1000, 0x8664, 0x10B), "PE32+"));
    // A declared optional header too short for SizeOfImage, even though the bytes follow.
    CHECK(PeFailsNaming(MakePe(0x1000, 0x8664, 0x20B, 0x80, 0), "too short"));
    CHECK(PeFailsNaming(MakePe(0x1000, 0x8664, 0x20B, 0x80, 0x38), "too short"));
    CHECK(R::ParsePeSizeOfImage(MakePe(0x1000, 0x8664, 0x20B, 0x80, 0x3C), &error).has_value());
    CHECK(PeFailsNaming(MakePe(0), "zero"));
    // A buffer that ends exactly after SizeOfImage while the COFF header declares a longer
    // optional header is truncated; one that holds the whole declared header is accepted.
    {
        constexpr std::size_t kThroughSizeOfImage = 0x80 + 4 + 20 + 56 + 4;
        auto cut = MakePe(0x1000, 0x8664, 0x20B, 0x80, 0xF0);
        cut.resize(kThroughSizeOfImage);
        CHECK(PeFailsNaming(cut, "extends past the end"));
        auto whole = MakePe(0x1000, 0x8664, 0x20B, 0x80, 0xF0);
        whole.resize(0x80 + 4 + 20 + 0xF0);
        CHECK(R::ParsePeSizeOfImage(whole, &error).has_value());
    }

    // The file reader reads at the validated e_lfanew, also beyond the first 4 KiB, and names
    // its supported limit instead of calling a far header "outside the file".
    const auto write_pe_file = [](const std::vector<std::uint8_t>& bytes) {
        const auto path = std::filesystem::temp_directory_path() / "aevora_pe_unit.bin";
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        return path;
    };
    {
        auto far_header = MakePe(0x5000, 0x8664, 0x20B, 0x80);
        // Move the PE header to 0x2000 and point e_lfanew at it.
        std::vector<std::uint8_t> bytes(0x2000, 0);
        bytes.insert(bytes.end(), far_header.begin() + 0x80, far_header.end());
        std::copy(far_header.begin(), far_header.begin() + 0x40, bytes.begin());
        bytes[0x3C] = 0x00;
        bytes[0x3D] = 0x20;
        const auto path = write_pe_file(bytes);
        const auto size = R::ReadPeSizeOfImage(path, &error);
        CHECK(size.has_value() && *size == 0x5000);
        auto too_far = bytes;
        too_far[0x3D] = 0x00;
        too_far[0x3E] = 0x20; // e_lfanew 0x200000, past kPeMaxHeaderOffsetBytes
        const auto far_path = write_pe_file(too_far);
        CHECK(!R::ReadPeSizeOfImage(far_path, &error).has_value());
        CHECK(Contains(error, "supported limit"));
        std::filesystem::remove(far_path);
    }
    {
        // File ending exactly after SizeOfImage with a longer declaration: refused; the same
        // file with the whole declared header present: accepted.
        auto bytes = MakePe(0x7000, 0x8664, 0x20B, 0x80, 0xF0);
        bytes.resize(0x80 + 4 + 20 + 56 + 4);
        const auto cut_path = write_pe_file(bytes);
        CHECK(!R::ReadPeSizeOfImage(cut_path, &error).has_value());
        CHECK(Contains(error, "extends past the end"));
        bytes = MakePe(0x7000, 0x8664, 0x20B, 0x80, 0xF0);
        bytes.resize(0x80 + 4 + 20 + 0xF0);
        const auto whole_path = write_pe_file(bytes);
        const auto size = R::ReadPeSizeOfImage(whole_path, &error);
        CHECK(size.has_value() && *size == 0x7000);
        // A short declaration is reported as such by the file reader too.
        bytes = MakePe(0x7000, 0x8664, 0x20B, 0x80, 0x38);
        CHECK(!R::ReadPeSizeOfImage(write_pe_file(bytes), &error).has_value());
        CHECK(Contains(error, "too short"));
        std::filesystem::remove(whole_path);
    }
    CHECK(!R::ReadPeSizeOfImage(std::filesystem::temp_directory_path() / "aevora_no_such_pe", &error)
               .has_value());
    return 0;
}

} // namespace

// Prints SizeOfImage of a PE file with the production reader, for tests that
// inspect a real linked image:
//   smoke_topology_unit --pe-size-of-image <file>
int PeSizeOfImageMode(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: --pe-size-of-image <file>\n");
        return 2;
    }
    std::string error;
    const auto size = R::ReadPeSizeOfImage(argv[2], &error);
    if (!size) {
        std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
        return 2;
    }
    std::printf("%llu\n", static_cast<unsigned long long>(*size));
    return 0;
}

// Writes recomp_registration.c (registry v1) with the production writer:
//   smoke_topology_unit --write-registry-v1 <out.c> <bundle hex> <abi> <features> <fastmem 0|1>
//       <guard_gen 0|1> <fpx 0|1> <module>... [--dll <module>]... [--cross-chain]
int WriteRegistryV1Mode(int argc, char** argv) {
    constexpr int kFixedArgs = 10; // program, mode, out, bundle, abi, features, fm, gg, fpx, 1 module
    if (argc < kFixedArgs) {
        std::fprintf(stderr, "usage: --write-registry-v1 <out.c> <bundle hex> <abi> <features> "
                             "<fastmem 0|1> <gg 0|1> <fpx 0|1> <module>... [--dll <module>]...\n");
        return 2;
    }
    R::RecompTopologyRequest request;
    request.bundle_id_hex = argv[3];
    R::RecompRegistryOptions options;
    options.image_abi = static_cast<unsigned>(std::strtoul(argv[4], nullptr, 10));
    options.expected_features = static_cast<unsigned>(std::strtoul(argv[5], nullptr, 10));
    options.fastmem = std::string(argv[6]) == "1";
    options.guard_gen = std::string(argv[7]) == "1";
    options.fpx = std::string(argv[8]) == "1";
    bool dll_next = false;
    for (int i = 9; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dll") {
            dll_next = true;
        } else if (arg == "--cross-chain") {
            options.cross_chain = true;
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
    const std::string text = R::WriteRecompRegistryV1C(*topology, options);
    if (text.empty()) {
        std::fprintf(stderr, "the topology and options do not describe a v1 registry\n");
        return 2;
    }
    std::FILE* file = std::fopen(argv[2], "wb");
    if (!file || std::fwrite(text.data(), 1, text.size(), file) != text.size()) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        if (file) std::fclose(file);
        return 2;
    }
    return std::fclose(file) == 0 ? 0 : 2;
}

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
    if (argc > 1 && std::string(argv[1]) == "--write-registry-v1") {
        return WriteRegistryV1Mode(argc, argv);
    }
    if (argc > 1 && std::string(argv[1]) == "--pe-size-of-image") {
        return PeSizeOfImageMode(argc, argv);
    }
    if (int rc = Naming()) return rc;
    if (int rc = LoadOrder()) return rc;
    if (int rc = AllStaticText()) return rc;
    if (int rc = SplitText()) return rc;
    if (int rc = Validation()) return rc;
    if (int rc = BundleBytes()) return rc;
    if (int rc = BasenameRule()) return rc;
    if (int rc = RegistryV1Text()) return rc;
    if (int rc = TopologyMarker()) return rc;
    if (int rc = PeImageSize()) return rc;
    std::puts("topology unit checks passed");
    return 0;
}
