// smoke_access_stats_unit: the SUYU_RECOMP_ACCESS_STATS classifier and window report
// (core/arm/recomp/recomp_access_stats.h).
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/arm/recomp/recomp_access_stats.h"

using Core::RecompAccessStats;
using Reason = RecompAccessStats::Reason;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint32_t kPageBits = 12;
constexpr std::uint64_t kLimit = 0x100000;
constexpr std::uint64_t kPtr = 0x7ff000000000;

// Page 0 plain, 1 unmapped, 2 debug, 3 rasterizer, 4 plain+watched, 5 plain; the rest unmapped.
RecompAccessStats::Entry EntryOf(std::uint64_t page) {
    switch (page) {
    case 0:
    case 5:
        return {kPtr | RecompAccessStats::kTypeMemory, 0};
    case 2:
        return {RecompAccessStats::kTypeDebug, 0};
    case 3:
        return {RecompAccessStats::kTypeRasterizer, 0};
    case 4:
        return {kPtr | RecompAccessStats::kTypeMemory, 1};
    default:
        return {0, 0};
    }
}

struct Case {
    std::uint64_t va;
    std::uint32_t size;
    bool is_store;
    Reason expected;
};

constexpr Case kCases[] = {
    {1ULL << 48, 4, false, Reason::Tagged},
    {0xff00000000001000ULL, 8, true, Reason::Tagged},
    {kLimit, 4, false, Reason::OutOfRange},
    {kLimit - 2, 4, true, Reason::OutOfRange},
    {0x1000, 4, false, Reason::Unmapped},
    {0x2000, 8, true, Reason::Debug},
    {0x3000, 4, false, Reason::Rasterizer},
    {0x3ffe, 4, true, Reason::Rasterizer},
    {0x0, 4, false, Reason::Other},
    {0xff8, 8, true, Reason::Other},
    {0xffc, 8, false, Reason::PageCross},
    {0xfff, 2, true, Reason::PageCross},
    {0x4000, 4, true, Reason::Watched},
    {0x4000, 4, false, Reason::Other},
    {0x4ffe, 4, true, Reason::PageCross},
    {0x1ffe, 4, false, Reason::Unmapped},
    {0x5000, 1, true, Reason::Other},
};

template <typename Classifier>
int Mismatches(Classifier&& classify) {
    int mismatches = 0;
    for (const Case& c : kCases) {
        if (classify(c) != c.expected) {
            ++mismatches;
        }
    }
    return mismatches;
}

Reason Real(const Case& c) {
    return RecompAccessStats::Classify(c.va, c.size, c.is_store, kPageBits, kLimit, EntryOf);
}

// Each weakened classifier drops one rule; the case table must notice every one.
int ClassifierMatchesTableAndWeakeningsAreCaught() {
    CHECK(Mismatches(Real) == 0);
    CHECK(Mismatches([](const Case& c) {  // no page-cross rule
        return RecompAccessStats::Classify(c.va, 1, c.is_store, kPageBits, kLimit, EntryOf);
    }) > 0);
    CHECK(Mismatches([](const Case& c) {  // stores never watched
        return RecompAccessStats::Classify(c.va, c.size, false, kPageBits, kLimit, EntryOf);
    }) > 0);
    CHECK(Mismatches([](const Case& c) {  // unlimited address space
        return RecompAccessStats::Classify(c.va, c.size, c.is_store, kPageBits, ~0ULL >> 16, EntryOf);
    }) > 0);
    CHECK(Mismatches([](const Case& c) {  // every entry treated as plain
        return RecompAccessStats::Classify(c.va, c.size, c.is_store, kPageBits, kLimit, [](std::uint64_t) {
            return RecompAccessStats::Entry{kPtr | RecompAccessStats::kTypeMemory, 0};
        });
    }) > 0);
    return 0;
}

int EntryIsNotReadForOutOfRangeAddresses() {
    int reads = 0;
    const auto counting = [&reads](std::uint64_t page) {
        ++reads;
        return EntryOf(page);
    };
    CHECK(RecompAccessStats::Classify(kLimit, 4, false, kPageBits, kLimit, counting) == Reason::OutOfRange);
    CHECK(RecompAccessStats::Classify(1ULL << 50, 4, false, kPageBits, kLimit, counting) == Reason::Tagged);
    CHECK(reads == 0);
    return 0;
}

int ReportsWindowDeltasPerReason() {
    RecompAccessStats stats;
    auto& slot = stats.Acquire();
    RecompAccessStats::Count(slot, false, Reason::Unmapped, 7);
    CHECK(!stats.TakeReport(0, 0, 1000));
    // Counts from before the window opened must not be reported.
    // 2 ticks per ns over the window: 6e6 rasterizer ticks = 3e6 ns for 3 accesses = 1e6 ns each.
    RecompAccessStats::Count(slot, false, Reason::Rasterizer, 1'000'000);
    RecompAccessStats::Count(slot, false, Reason::Rasterizer, 2'000'000);
    RecompAccessStats::Count(slot, true, Reason::Rasterizer, 3'000'000);
    RecompAccessStats::Count(slot, true, Reason::Watched, 4'000'000);
    CHECK(!stats.TakeReport(999, 1999, 1000));
    const auto report = stats.TakeReport(2'000'000'000ULL, 4'000'000'000ULL, 1000);
    CHECK(report);
    CHECK(report->find("window_ms=2000 ") != std::string::npos);
    CHECK(report->find("loads=2 ") != std::string::npos);
    CHECK(report->find("stores=2 ") != std::string::npos);
    CHECK(report->find("per_s=2 ") != std::string::npos);
    CHECK(report->find("load_ms=1 ") != std::string::npos);
    CHECK(report->find("store_ms=3 ") != std::string::npos);
    CHECK(report->find("load_rasterizer=2 ") != std::string::npos);
    CHECK(report->find("load_unmapped=0 ") != std::string::npos);
    CHECK(report->find("store_rasterizer=1 ") != std::string::npos);
    CHECK(report->find("store_watched=1 ") != std::string::npos);
    CHECK(report->find("avg_ns_rasterizer=1000000") != std::string::npos);
    CHECK(report->find("avg_ns_watched=2000000") != std::string::npos);
    CHECK(report->find("avg_ns_unmapped") == std::string::npos);
    const auto next = stats.TakeReport(4'000'000'000ULL, 8'000'000'000ULL, 1000);
    CHECK(next);
    CHECK(next->find("loads=0 stores=0 ") != std::string::npos);
    CHECK(next->find("avg_ns_") == std::string::npos);
    return 0;
}

int CountsFromThreadsThatAlreadyExited() {
    RecompAccessStats stats;
    CHECK(!stats.TakeReport(0, 1, 1000));
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            auto& slot = stats.Acquire();
            for (int n = 0; n < 1000; ++n) {
                RecompAccessStats::Count(slot, false, Reason::PageCross);
                RecompAccessStats::Count(slot, true, Reason::Debug);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    const auto report = stats.TakeReport(1000, 1001, 1000);
    CHECK(report);
    CHECK(report->find("load_page_cross=4000 ") != std::string::npos);
    CHECK(report->find("store_debug=4000") != std::string::npos);
    return 0;
}

} // namespace

int main() {
    if (const int rc = ClassifierMatchesTableAndWeakeningsAreCaught()) {
        return rc;
    }
    if (const int rc = EntryIsNotReadForOutOfRangeAddresses()) {
        return rc;
    }
    if (const int rc = ReportsWindowDeltasPerReason()) {
        return rc;
    }
    if (const int rc = CountsFromThreadsThatAlreadyExited()) {
        return rc;
    }
    std::puts("access_stats_unit: ok");
    return 0;
}
