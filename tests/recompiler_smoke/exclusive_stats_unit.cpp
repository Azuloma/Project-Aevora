// smoke_exclusive_stats_unit: the SUYU_RECOMP_EXCL_STATS counters and window report
// (core/arm/recomp/recomp_exclusive_stats.h).
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/arm/recomp/recomp_exclusive_stats.h"

using Core::RecompExclusiveStats;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint64_t kWindowNs = 1000;

int FirstCallOpensWindowAndReportsNothing() {
    RecompExclusiveStats stats;
    CHECK(!stats.TakeReport(0, 0, kWindowNs));
    return 0;
}

int ReportsWindowDeltasAndConvertsTicks() {
    RecompExclusiveStats stats;
    auto& slot = stats.Acquire();
    RecompExclusiveStats::CountLoad(slot, 5);
    CHECK(!stats.TakeReport(0, 0, kWindowNs));
    // Counts from before the window opened must not be reported.
    RecompExclusiveStats::CountLoad(slot, 4000000);
    RecompExclusiveStats::CountLoad(slot, 2000000);
    RecompExclusiveStats::CountStore(slot, true, 3000000);
    RecompExclusiveStats::CountStore(slot, false, 1000000);
    // 2 ticks per ns: 6e6 load ticks = 3e6 ns = 3 ms, 4e6 store ticks = 2 ms.
    const auto early = stats.TakeReport(kWindowNs - 1, 2 * (kWindowNs - 1), kWindowNs);
    CHECK(!early);
    const auto report = stats.TakeReport(2'000'000'000ULL, 4'000'000'000ULL, kWindowNs);
    CHECK(report);
    CHECK(report->find("window_ms=2000") != std::string::npos);
    CHECK(report->find("loads=2 ") != std::string::npos);
    CHECK(report->find("stores=2 ") != std::string::npos);
    CHECK(report->find("stores_failed=1 ") != std::string::npos);
    CHECK(report->find("load_ms=3 ") != std::string::npos);
    CHECK(report->find("store_ms=2 ") != std::string::npos);
    CHECK(report->find("load_avg_ns=1500000 ") != std::string::npos);
    CHECK(report->find("store_avg_ns=1000000") != std::string::npos);
    // The next window starts empty.
    const auto next = stats.TakeReport(4'000'000'000ULL, 8'000'000'000ULL, kWindowNs);
    CHECK(next);
    CHECK(next->find("loads=0 ") != std::string::npos);
    CHECK(next->find("load_avg_ns=0 ") != std::string::npos);
    return 0;
}

int CountsFromThreadsThatAlreadyExited() {
    RecompExclusiveStats stats;
    CHECK(!stats.TakeReport(0, 1, kWindowNs));
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            auto& slot = stats.Acquire();
            for (int n = 0; n < 1000; ++n) {
                RecompExclusiveStats::CountLoad(slot, 1);
                RecompExclusiveStats::CountStore(slot, n % 2 == 0, 1);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    const auto report = stats.TakeReport(kWindowNs, 1 + kWindowNs, kWindowNs);
    CHECK(report);
    CHECK(report->find("loads=4000 ") != std::string::npos);
    CHECK(report->find("stores=4000 ") != std::string::npos);
    CHECK(report->find("stores_failed=2000 ") != std::string::npos);
    return 0;
}

} // namespace

int main() {
    if (const int rc = FirstCallOpensWindowAndReportsNothing()) {
        return rc;
    }
    if (const int rc = ReportsWindowDeltasAndConvertsTicks()) {
        return rc;
    }
    if (const int rc = CountsFromThreadsThatAlreadyExited()) {
        return rc;
    }
    std::puts("exclusive_stats_unit: ok");
    return 0;
}
