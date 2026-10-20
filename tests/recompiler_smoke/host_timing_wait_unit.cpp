// smoke_host_timing_wait_unit: wait split and wake-lateness statistics behind
// SUYU_HOST_TIMING_BLOCK / SUYU_HOST_TIMING_STATS (core/host_timing_wait.h).
#include <chrono>
#include <cstdio>
#include <string>

#include "core/host_timing_wait.h"

using namespace Core::Timing;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr nanoseconds kResolution = microseconds{500};

// Negative control: blocks the whole wait, so the OS wake lateness lands on the event.
HostTimingWaitPlan PlanWithoutMargin(nanoseconds wait_ns) {
    return {wait_ns, nanoseconds{0}};
}

// Negative control: blocks even short waits and leaves no fine tail.
HostTimingWaitPlan PlanBlockingShortWaits(nanoseconds wait_ns) {
    return {wait_ns, nanoseconds{0}};
}

bool PlanIsSound(const HostTimingWaitPlan& plan, nanoseconds wait_ns, nanoseconds resolution_ns) {
    if (plan.os_block_ns + plan.fine_wait_ns != wait_ns) {
        return false;
    }
    if (plan.os_block_ns.count() > 0 && plan.fine_wait_ns < resolution_ns * 2) {
        return false;
    }
    return plan.os_block_ns.count() >= 0 && plan.fine_wait_ns.count() >= 0;
}

} // namespace

int main() {
    // Long wait: the OS-blocked part ends two resolutions before the target.
    const auto longPlan = PlanHostTimingWait(milliseconds{10}, kResolution, true);
    CHECK(longPlan.fine_wait_ns == milliseconds{1});
    CHECK(longPlan.os_block_ns == milliseconds{9});
    CHECK(PlanIsSound(longPlan, milliseconds{10}, kResolution));
    CHECK(!PlanIsSound(PlanWithoutMargin(milliseconds{10}), milliseconds{10}, kResolution));

    // Waits at or under the margin never block in the OS.
    for (const nanoseconds wait : {nanoseconds{1}, nanoseconds{microseconds{300}}, nanoseconds{milliseconds{1}}}) {
        const auto plan = PlanHostTimingWait(wait, kResolution, true);
        CHECK(plan.os_block_ns.count() == 0);
        CHECK(plan.fine_wait_ns == wait);
    }
    CHECK(PlanHostTimingWait(microseconds{1001}, kResolution, true).os_block_ns.count() == 1000);
    CHECK(!PlanIsSound(PlanBlockingShortWaits(microseconds{300}), microseconds{300},
                       kResolution));

    // Switch off or unknown resolution: the original fine-grained wait.
    const auto off = PlanHostTimingWait(milliseconds{10}, kResolution, false);
    CHECK(off.os_block_ns.count() == 0 && off.fine_wait_ns == milliseconds{10});
    const auto unknown = PlanHostTimingWait(milliseconds{10}, nanoseconds{0}, true);
    CHECK(unknown.os_block_ns.count() == 0 && unknown.fine_wait_ns == milliseconds{10});

    // Statistics: empty window, percentiles, signals, and window reset.
    HostTimingWaitStats stats;
    CHECK(stats.TakeReport().empty());
    CHECK(stats.LatenessPercentileUs(99) == 0);
    for (int i = 0; i < 98; ++i) {
        stats.Record(milliseconds{3}, true, false, microseconds{15});
    }
    stats.Record(milliseconds{3}, true, false, microseconds{3000});
    stats.Record(microseconds{50}, false, true, nanoseconds{0});
    CHECK(stats.Waits() == 100);
    CHECK(stats.LatenessPercentileUs(50) == 20);
    CHECK(stats.LatenessPercentileUs(98) == 20);
    CHECK(stats.LatenessPercentileUs(99) == 5000);
    const std::string report = stats.TakeReport();
    CHECK(report.find("waits=100") != std::string::npos);
    CHECK(report.find("os_blocked=99") != std::string::npos);
    CHECK(report.find("signalled=1") != std::string::npos);
    CHECK(report.find("late_max_us=3000") != std::string::npos);
    CHECK(stats.Waits() == 0 && stats.TakeReport().empty());

    // A negative lateness (early timer) counts as zero, and the top bucket reports the maximum.
    stats.Record(milliseconds{1}, false, false, nanoseconds{-5000});
    CHECK(stats.LatenessPercentileUs(99) == 10);
    stats.Record(milliseconds{1}, false, false, milliseconds{40});
    CHECK(stats.LatenessPercentileUs(100) == 40000);

    std::puts("host_timing_wait_unit OK");
    return 0;
}
