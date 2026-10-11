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
    RecompLookupStats::CountExit(slot, true, 0, 1);
    RecompLookupStats::CountReturn(slot, DispatchReturn::Exit, 7);
    RecompLookupStats::CountExit(slot, false, 1, 0);
    RecompLookupStats::CountReturn(slot, DispatchReturn::Halted, 1);
    CHECK(!stats.TakeReport(999 * kMs, 1000 * kMs).has_value());
    auto report = stats.TakeReport(1000 * kMs, 1000 * kMs);
    CHECK(report.has_value());
    CHECK(Contains(*report, "window_ms=1000 dispatches=5 chained_blocks=272 halted=1 svc=1 "
                            "budget_exhausted=1 exit_covered=1 exit_uncovered=1"));
    RecompLookupStats::CountReturn(slot, DispatchReturn::Exit, 2);
    RecompLookupStats::CountExit(slot, true, 0, 1);
    report = stats.TakeReport(2500 * kMs, 1000 * kMs);
    CHECK(report.has_value());
    CHECK(Contains(*report, "window_ms=1500 dispatches=1 chained_blocks=2 halted=0 svc=0 "
                            "budget_exhausted=0 exit_covered=1 exit_uncovered=0"));
    return 0;
}

int ResolvesModulesByHighestBaseAtOrBelowPc() {
    const std::vector<std::uint64_t> bases = {0x1000, 0x5000, 0x9000};
    CHECK(RecompLookupStats::ModuleIndex(bases, 0xFFF) == RecompLookupStats::kOtherModule);
    CHECK(RecompLookupStats::ModuleIndex(bases, 0x1000) == 0);
    CHECK(RecompLookupStats::ModuleIndex(bases, 0x4FFF) == 0);
    CHECK(RecompLookupStats::ModuleIndex(bases, 0x5000) == 1);
    CHECK(RecompLookupStats::ModuleIndex(bases, 0xFFFFFF) == 2);
    CHECK(RecompLookupStats::ModuleIndex({}, 0x5000) == RecompLookupStats::kOtherModule);
    std::vector<std::uint64_t> many;
    for (std::uint64_t i = 0; i < RecompLookupStats::kMaxModules + 2; ++i) {
        many.push_back(0x1000 * (i + 1));
    }
    CHECK(RecompLookupStats::ModuleIndex(many, many.back()) == RecompLookupStats::kOtherModule);
    CHECK(RecompLookupStats::ModuleIndex(many, many[RecompLookupStats::kMaxModules - 1]) ==
          RecompLookupStats::kMaxModules - 1);
    return 0;
}

int ReportsBusiestExitPairsWithModuleNames() {
    RecompLookupStats stats;
    auto& slot = stats.Acquire();
    stats.TakeReport(0, 1000 * kMs);
    for (int n = 0; n < 9; ++n) {
        RecompLookupStats::CountExit(slot, true, 2, 0);
    }
    for (int n = 0; n < 5; ++n) {
        RecompLookupStats::CountExit(slot, true, 0, 2);
    }
    RecompLookupStats::CountExit(slot, false, RecompLookupStats::kOtherModule, 1);
    const auto report = stats.TakeReport(1000 * kMs, 1000 * kMs, {"rtld", "main", "sdk"});
    CHECK(report.has_value());
    CHECK(Contains(*report, "exit[sdk>rtld]=9 exit[rtld>sdk]=5 exit[other>main]=1"));
    CHECK(Contains(*report, "exit_covered=14 exit_uncovered=1"));
    const auto unnamed = [&] {
        RecompLookupStats::CountExit(slot, true, 3, 3);
        return stats.TakeReport(2000 * kMs, 1000 * kMs, {"rtld"});
    }();
    CHECK(unnamed.has_value());
    CHECK(Contains(*unnamed, "exit[module3>module3]=1"));
    CHECK(!Contains(*unnamed, "exit[rtld"));
    return 0;
}

int LabelsPairsByNumberAfterAModuleListChange() {
    RecompLookupStats stats;
    auto& slot = stats.Acquire();
    stats.TakeReport(0, 1000 * kMs, {"rtld", "main", "subsdk0", "sdk"}, 7);
    RecompLookupStats::CountExit(slot, true, 2, 1);
    const auto changed = stats.TakeReport(1000 * kMs, 1000 * kMs, {"rtld", "main", "sdk"}, 8);
    CHECK(changed.has_value());
    CHECK(Contains(*changed, "exit[module2>module1]=1 modules_changed=1"));
    RecompLookupStats::CountExit(slot, true, 2, 1);
    const auto stable = stats.TakeReport(2000 * kMs, 1000 * kMs, {"rtld", "main", "sdk"}, 8);
    CHECK(stable.has_value());
    CHECK(Contains(*stable, "exit[sdk>main]=1"));
    CHECK(!Contains(*stable, "modules_changed"));
    return 0;
}

int NamesTheOtherSlotOtherEvenWithManyNames() {
    RecompLookupStats stats;
    auto& slot = stats.Acquire();
    stats.TakeReport(0, 1000 * kMs);
    RecompLookupStats::CountExit(slot, true, RecompLookupStats::kOtherModule, 0);
    std::vector<std::string> names;
    for (std::size_t i = 0; i <= RecompLookupStats::kMaxModules; ++i) {
        names.push_back("m" + std::to_string(i));
    }
    const auto report = stats.TakeReport(1000 * kMs, 1000 * kMs, names);
    CHECK(report.has_value());
    CHECK(Contains(*report, "exit[other>m0]=1"));
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
    CHECK(ResolvesModulesByHighestBaseAtOrBelowPc() == 0);
    CHECK(ReportsBusiestExitPairsWithModuleNames() == 0);
    CHECK(LabelsPairsByNumberAfterAModuleListChange() == 0);
    CHECK(NamesTheOtherSlotOtherEvenWithManyNames() == 0);
    CHECK(TicksEveryFourThousandNinetySixthReturnOfAThread() == 0);
    CHECK(SumsAllThreadsAndToleratesAClockGoingBackwards() == 0);
    std::puts("All synthetic LOOKUP STATS smoke checks passed.");
    return 0;
}
