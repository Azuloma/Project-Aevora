// smoke_guest_time_unit: the backend-agnostic run/SVC/other ledger behind SUYU_GUEST_TIME
// (core/hle/kernel/guest_time_ledger.h). Times are driven as plain nanosecond counts.
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "core/hle/kernel/guest_time_ledger.h"

using Kernel::GuestTimeLedger;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint64_t kMs = 1000000;

bool Contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

int SplitsWindowIntoRunIdleAndHost() {
    GuestTimeLedger ledger;
    // Thread 5 runs 300 ms and enters SVC 0x19; 400 ms pass (100 ms of it idle) until thread 5
    // runs again for 100 ms and halts without an SVC; the second gap is still open at the report.
    ledger.BeginRun(5, 0);
    ledger.EndRun(300 * kMs, 0x19);
    ledger.AddIdle(100 * kMs);
    ledger.BeginRun(5, 700 * kMs);
    ledger.EndRun(800 * kMs, std::nullopt);
    const auto report = ledger.TakeReport(1000 * kMs, 1000 * kMs, 1, 4);
    CHECK(report.has_value());
    CHECK(Contains(*report, "core=1 window_ms=1000 run_pct=40 idle_pct=10 host_pct=30"));
    CHECK(Contains(*report, "unattributed_pct=20 slices=2"));
    CHECK(Contains(*report, "id=5 run_pct=40 host_pct=30 slices=2"));
    CHECK(Contains(*report, "svc=0x19 host_pct=30 calls=1"));
    return 0;
}

int ChargesTheGapToThePreviousThreadAndNeverCountsRunTwice() {
    GuestTimeLedger ledger;
    // Thread 1 enters an SVC at 100 ms and thread 2 runs 200 ms meanwhile. Thread 1's gap is the
    // 50 ms before thread 2 starts; thread 2's gap is the 150 ms before thread 1 runs again.
    ledger.BeginRun(1, 0);
    ledger.EndRun(100 * kMs, 0x0B);
    ledger.BeginRun(2, 150 * kMs);
    ledger.EndRun(350 * kMs, 0x1C);
    ledger.BeginRun(1, 500 * kMs);
    ledger.EndRun(600 * kMs, std::nullopt);
    const auto report = ledger.TakeReport(1000 * kMs, 1000 * kMs, 0, 4);
    CHECK(report.has_value());
    CHECK(Contains(*report, "run_pct=40 idle_pct=0 host_pct=20"));
    CHECK(Contains(*report, "id=1 run_pct=20 host_pct=5 slices=2"));
    CHECK(Contains(*report, "id=2 run_pct=20 host_pct=15 slices=1"));
    CHECK(Contains(*report, "svc=0x1c host_pct=15 calls=1"));
    CHECK(Contains(*report, "svc=0xb host_pct=5 calls=1"));
    return 0;
}

int IgnoresIdleOutsideAGapAndClampsItToTheGap() {
    GuestTimeLedger ledger;
    ledger.AddIdle(900 * kMs); // before any run: no gap is open
    ledger.BeginRun(1, 0);
    ledger.EndRun(100 * kMs, 0x01);
    ledger.AddIdle(500 * kMs); // longer than the 200 ms gap that follows
    ledger.BeginRun(1, 300 * kMs);
    ledger.EndRun(400 * kMs, std::nullopt);
    const auto report = ledger.TakeReport(1000 * kMs, 1000 * kMs, 0, 4);
    CHECK(report.has_value());
    CHECK(Contains(*report, "run_pct=20 idle_pct=20 host_pct=0"));
    return 0;
}

int ListsBusiestThreadsAndSvcsFirstAndStartsANewWindow() {
    GuestTimeLedger ledger;
    ledger.BeginRun(1, 0);
    ledger.EndRun(10 * kMs, 0x01);
    ledger.BeginRun(2, 20 * kMs);
    ledger.EndRun(220 * kMs, 0x34);
    ledger.BeginRun(1, 320 * kMs);
    ledger.EndRun(330 * kMs, std::nullopt);
    CHECK(!ledger.TakeReport(999 * kMs, 1000 * kMs, 0, 4).has_value());
    const auto report = ledger.TakeReport(1000 * kMs, 1000 * kMs, 0, 1);
    CHECK(report.has_value());
    CHECK(Contains(*report, "threads=2"));
    CHECK(Contains(*report, "id=2 "));
    CHECK(!Contains(*report, "id=1 "));
    CHECK(Contains(*report, "svc=0x34 "));
    CHECK(!Contains(*report, "svc=0x1 "));
    // The next window starts empty.
    CHECK(!ledger.TakeReport(5000 * kMs, 1000 * kMs, 0, 4).has_value());
    return 0;
}

int ToleratesClockGoingBackwardsAndEarlyReports() {
    GuestTimeLedger ledger;
    ledger.BeginRun(3, 500 * kMs);
    ledger.EndRun(400 * kMs, 0x02); // end before start: zero, no wrap
    ledger.BeginRun(3, 450 * kMs);  // gap start (400) is before the earlier run start: still fine
    ledger.EndRun(550 * kMs, std::nullopt);
    const auto report = ledger.TakeReport(1500 * kMs, 1000 * kMs, 0, 4);
    CHECK(report.has_value());
    CHECK(Contains(*report, "run_pct=10"));
    GuestTimeLedger early;
    early.BeginRun(4, 900 * kMs);
    early.EndRun(950 * kMs, std::nullopt);
    CHECK(!early.TakeReport(100 * kMs, 1000 * kMs, 0, 4).has_value());
    // EndRun without a BeginRun is ignored.
    GuestTimeLedger stray;
    stray.EndRun(10 * kMs, 0x01);
    CHECK(!stray.TakeReport(5000 * kMs, 1000 * kMs, 0, 4).has_value());
    return 0;
}

} // namespace

int main() {
    CHECK(SplitsWindowIntoRunIdleAndHost() == 0);
    CHECK(ChargesTheGapToThePreviousThreadAndNeverCountsRunTwice() == 0);
    CHECK(IgnoresIdleOutsideAGapAndClampsItToTheGap() == 0);
    CHECK(ListsBusiestThreadsAndSvcsFirstAndStartsANewWindow() == 0);
    CHECK(ToleratesClockGoingBackwardsAndEarlyReports() == 0);
    std::puts("All synthetic GUEST TIME smoke checks passed.");
    return 0;
}
