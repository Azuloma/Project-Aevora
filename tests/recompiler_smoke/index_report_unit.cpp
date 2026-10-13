// Standalone test of the launcher's block index report (DescribeRecompBlockIndexes): what it says
// for an image that serves the 32-bit index, declines it, or predates the export.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/arm/recomp/recomp_bundle.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);     \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

const std::int32_t kTable[4] = {1, 2, 3, 4};

int Serves(SuyuRecompIndex32V1* out) {
    if (out->version != SUYU_RECOMP_INDEX32_V1_VERSION || out->struct_size != sizeof(*out)) {
        return 0;
    }
    out->lo = 0x1000;
    out->hi = 0x1FFC;
    out->table = kTable;
    out->anchor = 0x10000;
    return 1;
}

int DeclinesRange(SuyuRecompIndex32V1* out) {
    out->unavailable_reason = SUYU_RECOMP_INDEX32_WHY_RANGE;
    return 0;
}

int DeclinesWithoutReason(SuyuRecompIndex32V1*) {
    return 0;
}

Core::RecompBundle MakeBundle() {
    Core::RecompBundle bundle;
    for (const char* name : {"rtld", "main", "sdk", "subsdk0"}) {
        Core::RecompBundleModule module;
        module.name = name;
        bundle.modules.push_back(module);
    }
    bundle.modules[0].index32_v1 = &Serves;
    bundle.modules[1].index32_v1 = &DeclinesRange;
    bundle.modules[2].index32_v1 = nullptr;
    bundle.modules[3].index32_v1 = &DeclinesWithoutReason;
    return bundle;
}

}  // namespace

int main() {
    const Core::RecompBundle bundle = MakeBundle();
    const std::vector<std::pair<size_t, u64>> all{{0, 0x1000}, {1, 0x2000}, {2, 0x3000}, {3, 0x4000}};
    const auto notes = Core::DescribeRecompBlockIndexes(bundle, all);
    CHECK(notes.size() == 4);
    if (notes.size() == 4) {
        // Serves: 0x1000..0x1FFC is 1024 instruction words, 4 KiB; not a warning.
        CHECK(!notes[0].warning && Contains(notes[0].text, "rtld: 32-bit block index of 1024 words (4 KiB)"));
        // Declines with a reason: a warning that carries the reason and the fallback.
        CHECK(notes[1].warning && Contains(notes[1].text, "main: ") &&
              Contains(notes[1].text, SUYU_RECOMP_INDEX32_WHY_RANGE) &&
              Contains(notes[1].text, "lookup export"));
        // No export (an image that predates it): informational, names the module and the fallback.
        CHECK(!notes[2].warning && Contains(notes[2].text, "sdk: no 32-bit block index export") &&
              Contains(notes[2].text, "lookup export"));
        // Declines silently: still a warning, and says so.
        CHECK(notes[3].warning && Contains(notes[3].text, "no reason given"));
    }

    // Only the bound modules are described, in the order given.
    const auto subset = Core::DescribeRecompBlockIndexes(bundle, {{3, 0x4000}, {0, 0x1000}});
    CHECK(subset.size() == 2);
    if (subset.size() == 2) {
        CHECK(Contains(subset[0].text, "subsdk0: ") && Contains(subset[1].text, "rtld: "));
    }
    CHECK(Core::DescribeRecompBlockIndexes(bundle, {}).empty());

    // A module index outside the bundle is reported by number, not read past the end.
    const auto stray = Core::DescribeRecompBlockIndexes(bundle, {{9, 0}, {0, 0x1000}});
    CHECK(stray.size() == 2);
    if (stray.size() == 2) {
        CHECK(stray[0].warning && Contains(stray[0].text, "module index 9") &&
              Contains(stray[0].text, "4 modules"));
        CHECK(Contains(stray[1].text, "rtld: 32-bit block index"));
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("index report checks passed\n");
    return 0;
}
