// smoke_exit_targets_unit: per-thread exit-target tables behind SUYU_RECOMP_LOOKUP_TARGETS
// (core/arm/recomp/recomp_exit_targets.h).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/arm/recomp/recomp_exit_targets.h"

using Core::RecompExitTargets;

#define CHECK(x)                                                                                     \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

std::string Describe(std::uint64_t pc) {
    char text[32];
    std::snprintf(text, sizeof text, "pc%llx", static_cast<unsigned long long>(pc));
    return text;
}

bool Contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int CountsPerPcAndReportsBusiestFirstAsDeltas() {
    RecompExitTargets targets;
    auto& slot = targets.Acquire();
    for (int n = 0; n < 5; ++n) {
        RecompExitTargets::Count(slot, 0x1000);
    }
    for (int n = 0; n < 9; ++n) {
        RecompExitTargets::Count(slot, 0x2000);
    }
    RecompExitTargets::Count(slot, 0); // pc 0 is a valid target, not an empty cell
    const std::string first = targets.TakeTop(Describe);
    CHECK(Contains(first, "distinct=3 counted=15 overflow=0 pc2000=9 pc1000=5 pc0=1"));
    RecompExitTargets::Count(slot, 0x1000);
    const std::string second = targets.TakeTop(Describe);
    CHECK(Contains(second, "distinct=1 counted=1 overflow=0 pc1000=1"));
    CHECK(!Contains(second, "pc2000"));
    return 0;
}

int LimitsTheListAndCountsAFullProbeRunAsOverflow() {
    RecompExitTargets targets;
    auto& slot = targets.Acquire();
    for (std::uint64_t i = 0; i < RecompExitTargets::kTopCount + 4; ++i) {
        for (std::uint64_t n = 0; n <= i; ++n) {
            RecompExitTargets::Count(slot, 0x10000 + i * 4);
        }
    }
    std::string line = targets.TakeTop(Describe);
    std::size_t entries = 0;
    for (std::size_t at = line.find(" pc"); at != std::string::npos; at = line.find(" pc", at + 1)) {
        ++entries;
    }
    CHECK(entries == RecompExitTargets::kTopCount);
    // Far more distinct pcs than the table holds: the excess must be counted, not lost.
    RecompExitTargets crowded;
    auto& crowded_slot = crowded.Acquire();
    const std::uint64_t distinct = RecompExitTargets::kTableSize * 2;
    for (std::uint64_t i = 0; i < distinct; ++i) {
        RecompExitTargets::Count(crowded_slot, 0x400000 + i * 4);
    }
    line = crowded.TakeTop(Describe);
    unsigned long long counted = 0, overflow = 0;
    CHECK(std::sscanf(line.c_str(), "lookup-targets distinct=%*u counted=%llu overflow=%llu",
                      &counted, &overflow) == 2);
    CHECK(overflow > 0);
    CHECK(counted + overflow == distinct);
    return 0;
}

int ReportsOverflowAsPerWindowDeltas() {
    RecompExitTargets targets;
    auto& slot = targets.Acquire();
    RecompExitTargets::Count(slot, UINT64_MAX); // the key would wrap to the empty marker
    CHECK(Contains(targets.TakeTop(Describe), "distinct=0 counted=0 overflow=1"));
    CHECK(Contains(targets.TakeTop(Describe), "distinct=0 counted=0 overflow=0"));
    RecompExitTargets::Count(slot, UINT64_MAX);
    RecompExitTargets::Count(slot, UINT64_MAX);
    CHECK(Contains(targets.TakeTop(Describe), "overflow=2"));
    return 0;
}

int RebaseDropsEarlierCounts() {
    RecompExitTargets targets;
    auto& slot = targets.Acquire();
    RecompExitTargets::Count(slot, 0x1000);
    targets.Rebase();
    RecompExitTargets::Count(slot, 0x2000);
    const std::string line = targets.TakeTop(Describe);
    CHECK(Contains(line, "distinct=1 counted=1"));
    CHECK(!Contains(line, "pc1000"));
    return 0;
}

int KeepsEveryCountWhileWritersAndTheReporterRunTogether() {
    RecompExitTargets targets;
    constexpr int kThreads = 3;
    constexpr std::uint64_t kPerThread = 200000;
    std::atomic<int> running{kThreads};
    std::vector<std::thread> workers;
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&] {
            auto& slot = targets.Acquire();
            for (std::uint64_t n = 0; n < kPerThread; ++n) {
                RecompExitTargets::Count(slot, 0x8000 + (n & 0x3F) * 4);
            }
            running.fetch_sub(1);
        });
    }
    unsigned long long total = 0;
    const auto add_report = [&] {
        unsigned long long counted = 0, overflow = 0;
        const std::string line = targets.TakeTop(Describe);
        CHECK(std::sscanf(line.c_str(), "lookup-targets distinct=%*u counted=%llu overflow=%llu",
                          &counted, &overflow) == 2);
        total += counted + overflow;
        return 0;
    };
    while (running.load() > 0) {
        if (add_report() != 0) {
            return 1;
        }
    }
    for (auto& worker : workers) {
        worker.join();
    }
    if (add_report() != 0) {
        return 1;
    }
    CHECK(total == kThreads * kPerThread);
    return 0;
}

int SumsAllThreads() {
    RecompExitTargets targets;
    constexpr int kThreads = 4;
    constexpr std::uint64_t kPerThread = 50000;
    std::vector<std::thread> workers;
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&] {
            auto& slot = targets.Acquire();
            for (std::uint64_t n = 0; n < kPerThread; ++n) {
                RecompExitTargets::Count(slot, 0x7000);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    CHECK(Contains(targets.TakeTop(Describe), "distinct=1 counted=200000 overflow=0 pc7000=200000"));
    return 0;
}

} // namespace

int main() {
    CHECK(CountsPerPcAndReportsBusiestFirstAsDeltas() == 0);
    CHECK(LimitsTheListAndCountsAFullProbeRunAsOverflow() == 0);
    CHECK(ReportsOverflowAsPerWindowDeltas() == 0);
    CHECK(RebaseDropsEarlierCounts() == 0);
    CHECK(KeepsEveryCountWhileWritersAndTheReporterRunTogether() == 0);
    CHECK(SumsAllThreads() == 0);
    std::puts("All synthetic EXIT TARGETS smoke checks passed.");
    return 0;
}
