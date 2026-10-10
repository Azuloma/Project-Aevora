// smoke_sharded_counters_unit: the per-thread counter slots behind the AOT dispatch-path counters
// (core/arm/recomp/recomp_sharded_counters.h).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/arm/recomp/recomp_sharded_counters.h"

using Core::RecompShardedCounters;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr int kThreads = 4;
constexpr std::uint64_t kIncrementsPerThread = 200000;

// Releases the workers only once all of them are running, so their increments overlap.
template <typename Body>
void RunWorkers(Body body) {
    std::atomic<int> arrived{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    for (int i = 0; i < kThreads; ++i) {
        workers.emplace_back([&, i] {
            arrived.fetch_add(1);
            while (!go.load()) {
            }
            body(i);
        });
    }
    while (arrived.load() < kThreads) {
        std::this_thread::yield();
    }
    go.store(true);
    for (auto& worker : workers) {
        worker.join();
    }
}

int CountsEveryIncrementFromConcurrentThreads() {
    RecompShardedCounters<2> counters;
    RunWorkers([&](int) {
        auto& slot = counters.Acquire();
        for (std::uint64_t n = 0; n < kIncrementsPerThread; ++n) {
            RecompShardedCounters<2>::Increment(slot, 0);
        }
        RecompShardedCounters<2>::Increment(slot, 1);
    });
    // The workers have exited; their slots must still be counted.
    CHECK(counters.Sum(0) == kThreads * kIncrementsPerThread);
    CHECK(counters.Sum(1) == kThreads);
    return 0;
}

// Negative control: the same loop on one shared slot. The increment is load+store, so overlapping
// threads lose updates; if this ever counts everything, the test above proves nothing.
int SharedSlotLosesUpdates() {
    if (std::thread::hardware_concurrency() < 2) {
        std::puts("sharded_counters_unit: negative control skipped (single hardware thread)");
        return 0;
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        RecompShardedCounters<1> counters;
        auto& shared = counters.Acquire();
        RunWorkers([&](int) {
            for (std::uint64_t n = 0; n < kIncrementsPerThread; ++n) {
                RecompShardedCounters<1>::Increment(shared, 0);
            }
        });
        if (counters.Sum(0) < kThreads * kIncrementsPerThread) {
            return 0;
        }
    }
    std::fprintf(stderr, "FAIL: shared-slot negative control never lost an update\n");
    return 1;
}

} // namespace

int main() {
    if (CountsEveryIncrementFromConcurrentThreads() != 0) {
        return 1;
    }
    if (SharedSlotLosesUpdates() != 0) {
        return 1;
    }
    std::puts("sharded_counters_unit: OK");
    return 0;
}
