// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mixed-bundle host: the production bundle loader (core/arm/recomp/recomp_bundle.cpp)
// against a registry v1 written by the production writer (smoke_topology_unit
// --write-registry-v1) for the topology {smoke: static, second: DLL}, with the real
// shared image of module "second" loaded from a directory.
//
//   bundle_host <case> <image directory>
//
// Exit 0: the case passed (the "ok" cases loaded and bound the bundle). Exit 1: the
// loader or the binding refused, with "bundle error: <message>" on stderr; each
// negative case is run by run.py, which checks that the message names the broken
// item. Exit 2: a check inside this host failed.
//
// Handshake values are the ones the other smoke hosts use: FM1 (static.c), FPX1
// (run.c), GG1 (Core::RecompGuardGen::kHostVersion).

#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "core/arm/recomp/recomp_bundle.h"
#include "core/arm/recomp/recomp_image_features.h"

// bundle_exec.c: runs the memory block of both images against one host bridge.
extern "C" int bundle_exec_cross_images(void (*run_slice_first)(void*),
                                        void (*run_slice_second)(void*), uint64_t first_base,
                                        uint64_t second_base);

namespace {

constexpr int kExitRefused = 1;
constexpr int kExitHostCheckFailed = 2;

// Where module "smoke" and "second" are placed, and a block known to exist in each
// module-relative image (the smoke code starts at module offset 0x1000).
constexpr u64 kSmokeBase = 0x1000000;
constexpr u64 kSecondBase = 0x2000000;
constexpr u64 kBlockOffset = 0x1000;

const Core::RecompHostHandshake kHost{{12, 5, ~u64{3}, 872, 880},
                                      {848, 856, u64{1} << 32},
                                      Core::RecompGuardGen::kHostVersion};

// What the load handshakes are run with; a case may change one value to make the host
// disagree with every image.
Core::RecompHostHandshake g_host = kHost;

#define HOST_CHECK(x)                                                                            \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "bundle_host FAIL line %d: %s\n", __LINE__, #x);                \
            return kExitHostCheckFailed;                                                         \
        }                                                                                        \
    } while (0)

// A modifiable copy of the production registry, so a case can break one field.
struct RegistryCopy {
    SuyuRecompRegistryV1 header;
    std::vector<SuyuRecompRegistryEntryV1> entries;

    RegistryCopy() {
        header = *suyu_recomp_registry_v1();
        entries.assign(header.entries, header.entries + header.entry_count);
        header.entries = entries.data();
    }
};

int Refused(const std::string& error) {
    std::fprintf(stderr, "bundle error: %s\n", error.c_str());
    return kExitRefused;
}

#ifdef _WIN32
bool DllIsLoaded(const char* narrow_ascii_name) {
    std::wstring name;
    for (const char* c = narrow_ascii_name; *c; ++c) {
        name += static_cast<wchar_t>(*c);
    }
    return GetModuleHandleW(name.c_str()) != nullptr;
}
#endif

// Loads `registry` and reports the refusal; on success `bundle` is filled.
int LoadOrRefuse(const SuyuRecompRegistryV1& registry, const std::filesystem::path& dir,
                 std::optional<Core::RecompBundle>* bundle) {
    std::string error;
    *bundle = Core::LoadRecompBundleV1(registry, dir, g_host, &error);
    return *bundle ? 0 : Refused(error);
}

// A DLL without the optional block index export loads, with no reporter, and is described so.
int CaseDllWithoutIndexExport(const std::filesystem::path& dir) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadOrRefuse(*suyu_recomp_registry_v1(), dir, &bundle)) {
        return rc;
    }
    HOST_CHECK(bundle->modules.size() == 2);
    HOST_CHECK(bundle->modules[0].index32_v1 != nullptr);
    HOST_CHECK(bundle->modules[1].is_dll && bundle->modules[1].index32_v1 == nullptr);
    const auto notes = Core::DescribeRecompBlockIndexes(*bundle, {{0, kSmokeBase}, {1, kSecondBase}});
    HOST_CHECK(notes.size() == 2 && !notes[1].warning);
    HOST_CHECK(notes[1].text.find("second: no 32-bit block index export") != std::string::npos);
    std::printf("dll without index export ok\n");
    return 0;
}

int CaseOk(const std::filesystem::path& dir) {
    const SuyuRecompRegistryV1& registry = *suyu_recomp_registry_v1();
    {
        std::optional<Core::RecompBundle> bundle;
        if (int rc = LoadOrRefuse(registry, dir, &bundle)) {
            return rc;
        }
        HOST_CHECK(bundle->modules.size() == 2);
        HOST_CHECK(bundle->modules[0].name == "smoke" && !bundle->modules[0].is_dll);
        HOST_CHECK(bundle->modules[1].name == "second" && bundle->modules[1].is_dll);
        HOST_CHECK(bundle->modules[1].dll_file == registry.entries[1].dll_file_utf8);
        HOST_CHECK(bundle->bundle_abi == registry.bundle_abi);
        HOST_CHECK(bundle->features == registry.entries[0].expected_features);
        HOST_CHECK(bundle->guard_ready);
        HOST_CHECK(bundle->long_slices);
        HOST_CHECK(bundle->fpx != 0);
        HOST_CHECK(bundle->dll_handles.size() == 1);
        // GG1 descriptors, in full-topology order: one per module.
        HOST_CHECK(bundle->guard_gen_modules.size() == 2);
        HOST_CHECK(bundle->guard_gen_modules[0].word && bundle->guard_gen_modules[1].word);
        HOST_CHECK(bundle->guard_gen_modules[0].word != bundle->guard_gen_modules[1].word);

        // Bases by canonical name, whatever the inventory's address order.
        const std::map<u64, std::string> inventory{{kSmokeBase, "smoke"}, {kSecondBase, "second"}};
        std::string error;
        const auto bases = Core::BindRecompBundleToInventory(*bundle, inventory, &error);
        if (!bases) {
            return Refused(error);
        }
        HOST_CHECK(bases->size() == 2);
        HOST_CHECK((*bases)[0] == std::make_pair(size_t{0}, kSmokeBase));
        HOST_CHECK((*bases)[1] == std::make_pair(size_t{1}, kSecondBase));
        Core::ApplyRecompBundleBases(*bundle, *bases);

        // The launcher's block index report: the static module's reporter comes from the registry
        // and the DLL's from its export; both describe a 32-bit index.
        HOST_CHECK(bundle->modules[0].index32_v1 && bundle->modules[1].index32_v1);
        for (const auto& note : Core::DescribeRecompBlockIndexes(*bundle, *bases)) {
            HOST_CHECK(!note.warning && note.text.find("32-bit block index of ") != std::string::npos);
        }
        // An entry without the reporter still loads, and is reported as an image without it.
        {
            RegistryCopy older;
            older.entries[0].index32_v1 = nullptr;
            std::optional<Core::RecompBundle> older_bundle;
            if (int rc = LoadOrRefuse(older.header, dir, &older_bundle)) {
                return rc;
            }
            HOST_CHECK(older_bundle->modules[0].index32_v1 == nullptr);
            const auto older_notes = Core::DescribeRecompBlockIndexes(*older_bundle, *bases);
            HOST_CHECK(older_notes.size() == 2 && !older_notes[0].warning);
            HOST_CHECK(older_notes[0].text.find("no 32-bit block index export") != std::string::npos);
        }

        const auto& smoke = bundle->modules[0];
        const auto& second = bundle->modules[1];
        const u64 smoke_pc = kSmokeBase + kBlockOffset;
        const u64 second_pc = kSecondBase + kBlockOffset;
        HOST_CHECK(smoke.lookup(smoke_pc) != nullptr);
        HOST_CHECK(second.lookup(second_pc) != nullptr);
        // Each image knows only its own base.
        HOST_CHECK(smoke.lookup(second_pc) == nullptr);
        HOST_CHECK(second.lookup(smoke_pc) == nullptr);
        // The launcher's combined lookup tries modules in order; the DLL module is
        // reached only through its own addresses.
        const auto combined = [&](u64 pc) -> int {
            for (size_t i = 0; i < bundle->modules.size(); ++i) {
                if (bundle->modules[i].lookup(pc)) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        HOST_CHECK(combined(smoke_pc) == 0);
        HOST_CHECK(combined(second_pc) == 1);
        HOST_CHECK(combined(kSecondBase + 0x100000) == -1);
        std::printf("bundle ok: %s(static) %s(DLL %s) abi %u features %#x\n",
                    smoke.name.c_str(), second.name.c_str(), second.dll_file.c_str(),
                    bundle->bundle_abi, bundle->features);
    }
#ifdef _WIN32
    // The bundle is gone, but the image stays: lookup and GG1 pointers handed to the
    // host point into it.
    HOST_CHECK(DllIsLoaded(registry.entries[1].dll_file_utf8));
#endif
    return 0;
}

// Loads a registry with one broken field and requires the refusal.
int CaseRefusal(RegistryCopy& copy, const std::filesystem::path& dir) {
    std::optional<Core::RecompBundle> bundle;
    const int rc = LoadOrRefuse(copy.header, dir, &bundle);
    HOST_CHECK(!bundle);
#ifdef _WIN32
    // A refused load must not leave images behind.
    HOST_CHECK(!DllIsLoaded(suyu_recomp_registry_v1()->entries[1].dll_file_utf8));
#endif
    return rc;
}

// ---- inventory cases: a synthetic bundle, so no image is needed on any platform ----

Core::RecompBundle SyntheticBundle() {
    Core::RecompBundle bundle;
    for (const char* name : {"rtld", "main", "sdk"}) {
        Core::RecompBundleModule module;
        module.name = name;
        bundle.modules.push_back(std::move(module));
    }
    return bundle;
}

int CaseInventory(const std::string& which) {
    const Core::RecompBundle bundle = SyntheticBundle();
    std::map<u64, std::string> inventory{{0x8000000, "rtld"}, {0x8010000, "main"},
                                         {0x9000000, "sdk"}};
    std::string error;
    if (which == "inventory-ok") {
        // The loader's address order need not be the bundle order.
        inventory = {{0x100, "sdk"}, {0x200, "main"}, {0x300, "rtld"}};
        const auto bases = Core::BindRecompBundleToInventory(bundle, inventory, &error);
        HOST_CHECK(bases && bases->size() == 3);
        HOST_CHECK((*bases)[0] == std::make_pair(size_t{0}, u64{0x300}));
        HOST_CHECK((*bases)[1] == std::make_pair(size_t{1}, u64{0x200}));
        HOST_CHECK((*bases)[2] == std::make_pair(size_t{2}, u64{0x100}));
        return 0;
    }
    if (which == "inventory-missing-middle") {
        inventory.erase(0x8010000);
    } else if (which == "inventory-extra") {
        inventory[0x8800000] = "subsdk0";
    } else if (which == "inventory-duplicate-name") {
        inventory[0x8800000] = "main";
    } else if (which == "inventory-empty") {
        inventory.clear();
    } else {
        std::fprintf(stderr, "unknown inventory case %s\n", which.c_str());
        return kExitHostCheckFailed;
    }
    const auto bases = Core::BindRecompBundleToInventory(bundle, inventory, &error);
    HOST_CHECK(!bases);
    return Refused(error);
}

// Guard v2 stub for a module that does not negotiate: it answers 0 to everything
// and remembers the last version it was handed.
unsigned g_stub_guard_last_argument = ~0u;
unsigned StubGuardV2NotNegotiated(unsigned version) {
    g_stub_guard_last_argument = version;
    return 0;
}

// Every module answers the guard query alike, but not with 2: the bundle loads and
// the guard stays "not negotiated" on all of them. Built from the static module
// alone (a real image always answers 2).
int CaseGuardUniformNotNegotiated(const std::filesystem::path& dir) {
    RegistryCopy copy;
    copy.entries.resize(1);
    copy.entries[0].guard_v2 = StubGuardV2NotNegotiated;
    copy.header.entry_count = 1;
    copy.header.static_count = 1;
    copy.header.dll_count = 0;
    copy.header.entries = copy.entries.data();
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadOrRefuse(copy.header, dir, &bundle)) {
        return rc;
    }
    HOST_CHECK(!bundle->guard_ready);
    HOST_CHECK(g_stub_guard_last_argument == 0); // "not negotiated" was set, not version 2
    std::printf("guard not negotiated on every module, bundle loaded\n");
    return 0;
}

// Both images run the memory block against one shared bridge (bundle_exec.c).
int CaseExec(const std::filesystem::path& dir) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadOrRefuse(*suyu_recomp_registry_v1(), dir, &bundle)) {
        return rc;
    }
    const std::map<u64, std::string> inventory{{kSmokeBase, "smoke"}, {kSecondBase, "second"}};
    std::string error;
    const auto bases = Core::BindRecompBundleToInventory(*bundle, inventory, &error);
    if (!bases) {
        return Refused(error);
    }
    Core::ApplyRecompBundleBases(*bundle, *bases);
    HOST_CHECK(bundle_exec_cross_images(bundle->modules[0].run_slice, bundle->modules[1].run_slice,
                                        kSmokeBase, kSecondBase) == 1);
    std::printf("exec ok: static and DLL images ran through one host bridge\n");
    return 0;
}

int RunCase(const std::string& name, const std::filesystem::path& dir) {
    if (name == "dll-without-index-export") {
        return CaseDllWithoutIndexExport(dir);
    }
    if (name == "ok" || name == "dll-in-unicode-dir") {
        return CaseOk(dir);
    }
    if (name == "exec") {
        return CaseExec(dir);
    }
    if (name == "guard-uniform-not-negotiated") {
        return CaseGuardUniformNotNegotiated(dir);
    }
    if (name.rfind("inventory-", 0) == 0) {
        return CaseInventory(name);
    }
    RegistryCopy copy;
    HOST_CHECK(copy.entries.size() == 2);
    SuyuRecompRegistryEntryV1& dll = copy.entries[1];
    static const char kOtherName[] = "other";
    static const char kEscapingFile[] = "..\\recompiled_second.dll";
    if (name == "dll-missing") {
        // `dir` is an empty directory.
    } else if (name == "dll-invalid" || name == "dll-missing-export") {
        // `dir` holds a file of the DLL's name that is not a generated image: garbage
        // bytes, or a valid DLL that exports none of the image entry points.
    } else if (name == "identity-bundle-mismatch") {
        copy.header.bundle_id[0] ^= 0xff;
    } else if (name == "identity-name-mismatch") {
        dll.canonical_name = kOtherName;
    } else if (name == "abi-mismatch") {
        dll.image_abi += 1;
    } else if (name == "abi-mismatch-image") {
        // A consistent ABI 5 registry over ABI 6 images: the images disagree.
        copy.header.bundle_abi = 5;
        for (auto& entry : copy.entries) {
            entry.image_abi = 5;
            entry.expected_features = 0;
        }
    } else if (name == "feature-mismatch") {
        dll.expected_features &= ~Core::RecompImageFeature::ExactFpX1;
    } else if (name == "feature-mismatch-fm1") {
        dll.expected_features &= ~Core::RecompImageFeature::FastmemPT1;
    } else if (name == "feature-mismatch-gg1") {
        dll.expected_features &= ~Core::RecompImageFeature::GuardGen1;
    } else if (name == "host-fastmem-layout") {
        g_host.fastmem.page_bits += 1;
    } else if (name == "host-fpx-layout") {
        g_host.fpx.off_fpcr += 8;
    } else if (name == "host-gg1-version") {
        g_host.guard_gen_host_version += 1;
    } else if (name == "static-null-features") {
        copy.entries[0].features = nullptr;
    } else if (name == "static-null-fastmem") {
        copy.entries[0].fastmem_v1 = nullptr;
    } else if (name == "static-null-guard-gen") {
        copy.entries[0].guard_gen_v1 = nullptr;
    } else if (name == "static-null-fpx") {
        copy.entries[0].fpx_v1 = nullptr;
    } else if (name == "feature-mismatch-image") {
        for (auto& entry : copy.entries) {
            entry.expected_features &= ~Core::RecompImageFeature::ExactFpX1;
        }
    } else if (name == "guard-disagreement") {
        // The static module no longer negotiates guard v2 while the DLL does; this
        // is found only after the DLL has loaded, which must then be unloaded.
        copy.entries[0].guard_v2 = StubGuardV2NotNegotiated;
    } else if (name == "header-mismatch") {
        copy.header.entry_size += 1;
    } else if (name == "header-counts") {
        copy.header.dll_count += 1;
    } else if (name == "dll-file-escapes") {
        dll.dll_file_utf8 = kEscapingFile;
    } else if (name == "static-null-pointer") {
        copy.entries[0].lookup = nullptr;
    } else {
        std::fprintf(stderr, "unknown case %s\n", name.c_str());
        return kExitHostCheckFailed;
    }
    return CaseRefusal(copy, dir);
}

} // namespace

#ifdef _WIN32
// Wide entry point: the image directory reaches the loader exactly as the process
// was given it, whatever the ANSI code page.
int wmain(int argc, wchar_t** argv) {
    // An unloadable DLL must fail the case, not raise the modal Bad Image dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3) {
        std::fprintf(stderr, "usage: bundle_host <case> <image directory>\n");
        return kExitHostCheckFailed;
    }
    std::string name;
    for (const wchar_t* c = argv[1]; *c; ++c) {
        if (*c > 0x7f) {
            std::fprintf(stderr, "bundle_host: the case name must be ASCII\n");
            return kExitHostCheckFailed;
        }
        name += static_cast<char>(*c);
    }
    return RunCase(name, std::filesystem::path(argv[2]));
}
#else
int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: bundle_host <case> <image directory>\n");
        return kExitHostCheckFailed;
    }
    return RunCase(argv[1], std::filesystem::path(argv[2]));
}
#endif
