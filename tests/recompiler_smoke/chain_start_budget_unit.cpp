// smoke_chain_start_budget_unit: the size rule that refuses a chain-start export projected to pass
// the image limit (core/recompiler/chain_start_budget.h). PLA numbers are the calibration points.
#include <cstdint>
#include <cstdio>

#include "core/recompiler/chain_start_budget.h"

using namespace suyu::recomp;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint64_t kMiB = std::uint64_t{1} << 20;
constexpr std::uint64_t kPlaMainTextBytes = 54'744'700;

int EstimateGrowsWithBlockLength() {
    CHECK(EstimateStartEntryHostBytes(1) == 169);
    CHECK(EstimateStartEntryHostBytes(8) == 694);
    for (std::uint32_t insns = 1; insns < kChainStartMaxInsns; ++insns) {
        CHECK(EstimateStartEntryHostBytes(insns) < EstimateStartEntryHostBytes(insns + 1));
    }
    return 0;
}

int PlaMainWithTheDefaultThresholdFits() {
    // About 291,542 start entries of up to 8 instructions, 134 MiB measured on the PLA main image.
    const auto budget = CheckChainStartBudget(kPlaMainTextBytes, 134 * kMiB);
    CHECK(budget.fits);
    CHECK(budget.projected_image_bytes < 1800 * kMiB);
    CHECK(budget.allowed_image_bytes == kImageSizeLimitBytes - kChainStartImageMarginBytes);
    return 0;
}

int ProjectionPastTheAllowedSizeIsRefused() {
    // No room for a doubled copy of the image, and none for the margin below the limit.
    CHECK(!CheckChainStartBudget(kPlaMainTextBytes, 600 * kMiB).fits);
    const std::uint64_t base = kPlaMainTextBytes * kImageBytesPerGuestTextByte;
    const std::uint64_t room = kImageSizeLimitBytes - kChainStartImageMarginBytes - base;
    CHECK(CheckChainStartBudget(kPlaMainTextBytes, room).fits);
    CHECK(!CheckChainStartBudget(kPlaMainTextBytes, room + 1).fits);
    // A module that already needs the whole limit has no room for any start entry.
    CHECK(!CheckChainStartBudget(70'000'000, 1).fits);
    return 0;
}

int ModulesLinkedIntoOneImageAreBudgetedTogether() {
    // Each of two 35 MB modules fits alone (about 1035 MiB); linked together they do not.
    CHECK(CheckChainStartBudget(35'000'000, 0).fits);
    ChainStartImageProjection together;
    CHECK(together.Add(35'000'000, 0).fits);
    CHECK(!together.Add(35'000'000, 0).fits);
    // Images are separate: a second image starts from zero.
    ChainStartImageProjection other;
    CHECK(other.Add(35'000'000, 0).fits);
    return 0;
}

} // namespace

int main() {
    if (EstimateGrowsWithBlockLength() || PlaMainWithTheDefaultThresholdFits() ||
        ProjectionPastTheAllowedSizeIsRefused() || ModulesLinkedIntoOneImageAreBudgetedTogether()) {
        return 1;
    }
    std::puts("chain_start_budget_unit: OK");
    return 0;
}
