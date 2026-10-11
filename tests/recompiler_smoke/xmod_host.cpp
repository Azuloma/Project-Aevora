// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Three-module split fixture host: the production bundle loader
// (core/arm/recomp/recomp_bundle.cpp) against a registry v1 written by the production
// writer (smoke_topology_unit --write-registry-v1) for the topology
// {smoke: static, second: DLL, third: static}, so the shared image sits between two
// static ones. The real image of module "second" is loaded from a directory.
//
//   xmod_host <case> <image directory>
//
//   ok           loads and binds the bundle; checks order, GG1 descriptors at full
//                indices, bases and lookups.
//   exec         runs exclusive, counter and FP blocks of all three images through
//                one host bridge (xmod_exec.c).
//   guard-N      (N = 0, 1, 2) a changed code word in image N must abort the probe
//                block before its effect; run with SUYU_RECOMP_STRICT=1, the
//                expected exit code is 86.
//
// Exit 0: the case passed. Exit 1: the loader or the binding refused, with
// "bundle error: <message>" on stderr. Exit 2: a check inside this host failed.
//
// The handshake values are the ones bundle_host.cpp uses (FM1, FPX1, GG1).

#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "core/arm/recomp/recomp_bundle.h"
#include "core/arm/recomp/recomp_image_features.h"
#include "xmod_code.h"

// xmod_exec.c: the cross-image blocks, against the run_slice pointers of the bundle.
extern "C" int xmod_exec_scenarios(void (*run[3])(void*), void (*(*lookup[3])(uint64_t))(void*),
                                   const uint64_t base[3]);
extern "C" int xmod_exec_cross(void (*run[3])(void*), const uint64_t base[3], int expect_cross);
extern "C" int xmod_exec_cross_guard(void (*run[3])(void*), const uint64_t base[3]);
extern "C" int xmod_exec_guard_probe(void (*run[3])(void*), const uint64_t base[3],
                                     unsigned image);

namespace {

constexpr int kExitRefused = 1;
constexpr int kExitHostCheckFailed = 2;
constexpr size_t kModuleCount = 3;
constexpr size_t kDllModuleIndex = 1;

// Module bases: ascending in topology order, XMOD_BASE_SPACING apart (xmod_code.h, which
// the hop block's branch is encoded from), and a block offset known to exist in each
// module-relative image (the code starts at module offset 0x1000).
constexpr u64 kBases[kModuleCount] = {XMOD_FIRST_BASE, XMOD_FIRST_BASE + XMOD_BASE_SPACING,
                                      XMOD_FIRST_BASE + 2 * XMOD_BASE_SPACING};
constexpr u64 kBlockOffset = 0x1000;
const char* const kNames[kModuleCount] = {"smoke", "second", "third"};

const Core::RecompHostHandshake kHost{{12, 5, ~u64{3}, 872, 880},
                                      {848, 856, u64{1} << 32},
                                      Core::RecompGuardGen::kHostVersion};

#define HOST_CHECK(x)                                                                            \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "xmod_host FAIL line %d: %s\n", __LINE__, #x);                  \
            return kExitHostCheckFailed;                                                         \
        }                                                                                        \
    } while (0)

int Refused(const std::string& error) {
    std::fprintf(stderr, "bundle error: %s\n", error.c_str());
    return kExitRefused;
}

// Loads the registry, binds the three bases and, as the launcher does, publishes an XMOD1
// table unless `publish` is false (the control for what images do without it).
int LoadAndBind(const std::filesystem::path& dir, std::optional<Core::RecompBundle>* bundle,
                bool publish = true) {
    std::string error;
    *bundle = Core::LoadRecompBundleV1(*suyu_recomp_registry_v1(), dir, kHost, &error);
    if (!*bundle) {
        return Refused(error);
    }
    std::map<u64, std::string> inventory;
    for (size_t i = 0; i < kModuleCount; ++i) {
        inventory[kBases[i]] = kNames[i];
    }
    const auto bases = Core::BindRecompBundleToInventory(**bundle, inventory, &error);
    if (!bases) {
        return Refused(error);
    }
    Core::ApplyRecompBundleBases(**bundle, *bases);
    if (publish && !Core::PublishRecompXmod(**bundle, *bases, &error)) {
        return Refused(error);
    }
    return 0;
}

int CaseOk(const std::filesystem::path& dir) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadAndBind(dir, &bundle)) {
        return rc;
    }
    HOST_CHECK(bundle->modules.size() == kModuleCount);
    for (size_t i = 0; i < kModuleCount; ++i) {
        HOST_CHECK(bundle->modules[i].name == kNames[i]);
        HOST_CHECK(bundle->modules[i].is_dll == (i == kDllModuleIndex));
    }
    HOST_CHECK(bundle->dll_handles.size() == 1);
    HOST_CHECK(bundle->guard_ready && bundle->long_slices && bundle->fpx != 0);
    // GG1: one descriptor per module at its full-topology index, every word its own.
    HOST_CHECK(bundle->guard_gen_modules.size() == kModuleCount);
    for (size_t i = 0; i < kModuleCount; ++i) {
        HOST_CHECK(bundle->guard_gen_modules[i].word);
        for (size_t j = 0; j < i; ++j) {
            HOST_CHECK(bundle->guard_gen_modules[i].word != bundle->guard_gen_modules[j].word);
        }
    }
    // Each descriptor is its module's: it points at that module's own base (set by the
    // inventory binding) and its guarded span covers the module's code blocks.
    for (size_t i = 0; i < kModuleCount; ++i) {
        const auto& gg = bundle->guard_gen_modules[i];
        HOST_CHECK(gg.base && *gg.base == kBases[i]);
        HOST_CHECK(gg.code_lo <= kBlockOffset && gg.code_end > kBlockOffset);
    }
    // Each image knows only its own base: a block address resolves in exactly one.
    for (size_t owner = 0; owner < kModuleCount; ++owner) {
        const u64 pc = kBases[owner] + kBlockOffset;
        for (size_t i = 0; i < kModuleCount; ++i) {
            HOST_CHECK((bundle->modules[i].lookup(pc) != nullptr) == (i == owner));
        }
    }
    HOST_CHECK(bundle->modules[0].lookup(kBases[kDllModuleIndex] + 0x100000) == nullptr);
    std::printf("bundle ok: %s(static) %s(DLL) %s(static)\n", kNames[0], kNames[1], kNames[2]);
    return 0;
}

int CaseExec(const std::filesystem::path& dir) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadAndBind(dir, &bundle)) {
        return rc;
    }
    void (*run[kModuleCount])(void*);
    Core::RecompBundleBlockFn (*lookup[kModuleCount])(u64);
    for (size_t i = 0; i < kModuleCount; ++i) {
        run[i] = bundle->modules[i].run_slice;
        lookup[i] = bundle->modules[i].lookup;
    }
    HOST_CHECK(xmod_exec_scenarios(run, lookup, kBases) == 1);
    std::printf("exec ok: static, DLL and static images shared one host bridge\n");
    return 0;
}

// Computed branches across the three images. `published` selects the XMOD1 behaviour; the
// unpublished control must behave like an export without the feature.
int CaseCross(const std::filesystem::path& dir, bool published) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadAndBind(dir, &bundle, published)) {
        return rc;
    }
    HOST_CHECK((bundle->xmod_table != nullptr) == published);
    if (published) {
        HOST_CHECK(bundle->xmod_table->count == kModuleCount);
    }
    void (*run[kModuleCount])(void*);
    for (size_t i = 0; i < kModuleCount; ++i) {
        run[i] = bundle->modules[i].run_slice;
    }
    HOST_CHECK(xmod_exec_cross(run, kBases, published ? 1 : 0) == 1);
    std::printf("cross ok: published=%d\n", published ? 1 : 0);
    return 0;
}

// Returns only when the guard did not stop the block (that is a failure); a working guard
// ends the process with exit code 86 inside xmod_exec.c.
int CaseGuard(const std::filesystem::path& dir, unsigned image) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadAndBind(dir, &bundle)) {
        return rc;
    }
    void (*run[kModuleCount])(void*);
    for (size_t i = 0; i < kModuleCount; ++i) {
        run[i] = bundle->modules[i].run_slice;
    }
    xmod_exec_guard_probe(run, kBases, image);
    return kExitHostCheckFailed;
}

// Each image refuses a table that differs from the layout its blocks were built against, and
// still accepts the valid one afterwards.
int CaseCrossRefuse(const std::filesystem::path& dir) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadAndBind(dir, &bundle)) {
        return rc;
    }
    HOST_CHECK(bundle->xmod_table != nullptr);
    const SuyuRecompXmodTableV1 good = *bundle->xmod_table;
    const auto refused_by_all = [&](const SuyuRecompXmodTableV1& table) {
        for (const auto& module : bundle->modules) {
            if (module.xmod_v1(&table) != 0) {
                return false;
            }
        }
        return true;
    };
    SuyuRecompXmodTableV1 bad = good;
    bad.version += 1;
    HOST_CHECK(refused_by_all(bad));
    bad = good;
    bad.struct_size -= 4;
    HOST_CHECK(refused_by_all(bad));
    bad = good;
    bad.entry_size += 8;
    HOST_CHECK(refused_by_all(bad));
    bad = good;
    bad.count = 0;
    HOST_CHECK(refused_by_all(bad));
    bad = good;
    bad.count = SUYU_RECOMP_XMOD_V1_MAX_MODULES + 1;
    HOST_CHECK(refused_by_all(bad));
    bad = good;
    std::swap(bad.entries[0], bad.entries[1]);
    HOST_CHECK(refused_by_all(bad));
    bad = good;
    bad.entries[1].base = bad.entries[0].base;
    HOST_CHECK(refused_by_all(bad));
    bad = good;
    bad.entries[2].lookup = nullptr;
    HOST_CHECK(refused_by_all(bad));
    for (const auto& module : bundle->modules) {
        HOST_CHECK(module.xmod_v1(&good) == 1);
    }
    std::printf("cross-refuse ok\n");
    return 0;
}

// Exits with the guard's abort code from inside xmod_exec.c when the chained branch reaches a
// changed block of another image.
int CaseCrossGuard(const std::filesystem::path& dir) {
    std::optional<Core::RecompBundle> bundle;
    if (int rc = LoadAndBind(dir, &bundle)) {
        return rc;
    }
    void (*run[kModuleCount])(void*);
    for (size_t i = 0; i < kModuleCount; ++i) {
        run[i] = bundle->modules[i].run_slice;
    }
    xmod_exec_cross_guard(run, kBases);
    return kExitHostCheckFailed;
}

int RunCase(const std::string& name, const std::filesystem::path& dir) {
    if (name == "cross") {
        return CaseCross(dir, true);
    }
    if (name == "cross-off") {
        return CaseCross(dir, false);
    }
    if (name == "cross-refuse") {
        return CaseCrossRefuse(dir);
    }
    if (name == "cross-guard") {
        return CaseCrossGuard(dir);
    }
    if (name == "ok") {
        return CaseOk(dir);
    }
    if (name == "exec") {
        return CaseExec(dir);
    }
    if (name.size() == 7 && name.rfind("guard-", 0) == 0 && name[6] >= '0' &&
        name[6] < static_cast<char>('0' + kModuleCount)) {
        return CaseGuard(dir, static_cast<unsigned>(name[6] - '0'));
    }
    std::fprintf(stderr, "unknown case %s\n", name.c_str());
    return kExitHostCheckFailed;
}

} // namespace

#ifdef _WIN32
// Wide entry point: the image directory reaches the loader exactly as the process
// was given it, whatever the ANSI code page.
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: xmod_host <case> <image directory>\n");
        return kExitHostCheckFailed;
    }
    std::string name;
    for (const wchar_t* c = argv[1]; *c; ++c) {
        if (*c > 0x7f) {
            std::fprintf(stderr, "xmod_host: the case name must be ASCII\n");
            return kExitHostCheckFailed;
        }
        name += static_cast<char>(*c);
    }
    return RunCase(name, std::filesystem::path(argv[2]));
}
#else
int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: xmod_host <case> <image directory>\n");
        return kExitHostCheckFailed;
    }
    return RunCase(argv[1], std::filesystem::path(argv[2]));
}
#endif
