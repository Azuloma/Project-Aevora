// smoke_lookup_stats_unit: dispatcher-return classification and per-window counts behind
// SUYU_RECOMP_LOOKUP_STATS (core/arm/recomp/recomp_lookup_stats.h).
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/arm/recomp/recomp_lookup_stats.h"

using Core::ClassifyReturn;
using Core::DispatchReturn;
using Core::RecompLookupStats;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint64_t kMs = 1000000;

bool Contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int ClassifiesEveryReturnReason() {
    CHECK(ClassifyReturn(false, true, 100) == DispatchReturn::Svc);
    CHECK(ClassifyReturn(false, true, 0) == DispatchReturn::Svc);
    CHECK(ClassifyReturn(true, true, 100) == DispatchReturn::Halted); // IC IVAU stores an address
    CHECK(ClassifyReturn(true, false, 0) == DispatchReturn::Halted);
    CHECK(ClassifyReturn(false, false, 0) == DispatchReturn::BudgetExhausted);
    CHECK(ClassifyReturn(false, false, -3) == DispatchReturn::BudgetExhausted);
    CHECK(ClassifyReturn(false, false, 1) == DispatchReturn::Exit);
    return 0;
}

int ReportsPerWindowDeltasAndStartsANewWindow() {
    RecompLookupStats stats;
    auto& slot = stats.Acquire();
    RecompLookupStats::CountReturn(slot, DispatchReturn::Svc, 90); // before the window opens
    CHECK(!stats.TakeReport(0, 1000 * kMs).has_value());
    RecompLookupStats::CountReturn(slot, DispatchReturn::Svc, 3);
    RecompLookupStats::CountReturn(slot, DispatchReturn::BudgetExhausted, 256);
    RecompLookupStats::CountReturn(slot, DispatchReturn::Exit, 5);
    RecompLookupStats::CountExit(slot, true);
    RecompLookupStats::CountReturn(slot, DispatchReturn::Exit, 7);
    RecompLookupStats::CountExit(slot, false);
    RecompLookupStats::CountReturn(slot, DispatchReturn::Halted, 1);
    CHECK(!stats.TakeReport(999 * kMs, 1000 * kMs).has_value());
    auto report = stats.TakeReport(1000 * kMs, 1000 * kMs);
    CHECK(report.has_value());
    CHECK(Contains(*report, "window_ms=1000 dispatches=5 chained_blocks=272 halted=1 svc=1 "
                            "budget_exhausted=1 exit_covered=1 exit_uncovered=1"));
    RecompLookupStats::CountReturn(slot, DispatchReturn::Exit, 2);
    RecompLookupStats::CountExit(slot, true);
    report = stats.TakeReport(2500 * kMs, 1000 * kMs);
    CHECK(report.has_value());
    CHECK(Contains(*report, "window_ms=1500 dispatches=1 chained_blocks=2 halted=0 svc=0 "
                            "budget_exhausted=0 exit_covered=1 exit_uncovered=0"));
    return 0;
}

int TicksEveryFourThousandNinetySixthReturnOfAThread() {
    RecompLookupStats stats;
    auto& slot = stats.Acquire();
    int ticks = 0;
    for (int n = 0; n < 3 * 4096; ++n) {
        ticks += RecompLookupStats::CountReturn(slot, DispatchReturn::Exit, 1) ? 1 : 0;
    }
    CHECK(ticks == 3);
    return 0;
}

int SumsAllThreadsAndToleratesAClockGoingBackwards() {
    RecompLookupStats stats;
    constexpr int kThreads = 4;
    constexpr std::uint64_t kPerThread = 50000;
    stats.TakeReport(0, 1000 * kMs);
    std::vector<std::thread> workers;
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&] {
            auto& slot = stats.Acquire();
            for (std::uint64_t n = 0; n < kPerThread; ++n) {
                RecompLookupStats::CountReturn(slot, DispatchReturn::Exit, 1);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    CHECK(!stats.TakeReport(0, 1000 * kMs).has_value());
    const auto report = stats.TakeReport(1000 * kMs, 1000 * kMs);
    CHECK(report.has_value());
    CHECK(Contains(*report, "dispatches=200000 chained_blocks=200000"));
    return 0;
}

} // namespace

int main() {
    CHECK(ClassifiesEveryReturnReason() == 0);
    CHECK(ReportsPerWindowDeltasAndStartsANewWindow() == 0);
    CHECK(TicksEveryFourThousandNinetySixthReturnOfAThread() == 0);
    CHECK(SumsAllThreadsAndToleratesAClockGoingBackwards() == 0);
    std::puts("All synthetic LOOKUP STATS smoke checks passed.");
    return 0;
}
