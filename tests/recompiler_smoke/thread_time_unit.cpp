// smoke_thread_time_unit: the per-guest-thread run-time ledger behind SUYU_RECOMP_THREAD_TIME
// (core/arm/recomp/recomp_thread_time.h). Times are driven as plain nanosecond counts.
#include <cstdint>
#include <cstdio>
#include <string>

#include "core/arm/recomp/recomp_thread_time.h"

using Core::RecompThreadTimeLedger;

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

int ReportsBusiestThreadFirstWithShareOfWindow() {
    RecompThreadTimeLedger ledger;
    ledger.Record(7, 0, 100 * kMs, 0x1000);       // thread 7: 100 ms, one call
    ledger.Record(9, 100 * kMs, 400 * kMs, 0x2000); // thread 9: 300 ms ...
    ledger.Record(9, 500 * kMs, 600 * kMs, 0x2040); // ... plus 100 ms, two calls, last pc 0x2040
    const auto report = ledger.TakeReport(1000 * kMs, 1000 * kMs, 2, 4);
    CHECK(report.has_value());
    CHECK(Contains(*report, "core=2 window_ms=1000 busy_pct=50 threads=2"));
    // Thread 9 (400 ms of 1000) is listed before thread 7 (100 ms).
    const auto first = report->find("id=9 pct=40 calls=2 pc=0x2040");
    const auto second = report->find("id=7 pct=10 calls=1 pc=0x1000");
    CHECK(first != std::string::npos);
    CHECK(second != std::string::npos);
    CHECK(first < second);
    return 0;
}

int WaitsForTheWindowAndThenStartsAgain() {
    RecompThreadTimeLedger ledger;
    ledger.Record(1, 0, 10 * kMs, 0x10);
    CHECK(!ledger.TakeReport(999 * kMs, 1000 * kMs, 0, 4).has_value());
    CHECK(ledger.TakeReport(1000 * kMs, 1000 * kMs, 0, 4).has_value());
    // Nothing is reported for an empty ledger, and the old window's data does not carry over.
    CHECK(!ledger.TakeReport(5000 * kMs, 1000 * kMs, 0, 4).has_value());
    ledger.Record(2, 6000 * kMs, 6500 * kMs, 0x20);
    const auto report = ledger.TakeReport(7000 * kMs, 1000 * kMs, 0, 4);
    CHECK(report.has_value());
    CHECK(Contains(*report, "threads=1"));
    CHECK(!Contains(*report, "id=1 "));
    CHECK(Contains(*report, "id=2 pct=50"));
    return 0;
}

int LimitsListedThreadsButCountsAll() {
    RecompThreadTimeLedger ledger;
    for (std::uint64_t id = 1; id <= 5; ++id) {
        ledger.Record(id, 0, id * kMs, id);
    }
    const auto report = ledger.TakeReport(1000 * kMs, 1000 * kMs, 1, 2);
    CHECK(report.has_value());
    CHECK(Contains(*report, "threads=5"));
    CHECK(Contains(*report, "id=5 "));
    CHECK(Contains(*report, "id=4 "));
    CHECK(!Contains(*report, "id=3 "));
    return 0;
}

int ToleratesClockGoingBackwards() {
    RecompThreadTimeLedger ledger;
    ledger.Record(3, 500 * kMs, 400 * kMs, 0x30); // end before start: counts as zero, no wrap
    ledger.Record(3, 500 * kMs, 600 * kMs, 0x30);
    const auto report = ledger.TakeReport(1500 * kMs, 1000 * kMs, 0, 4);
    CHECK(report.has_value());
    CHECK(Contains(*report, "id=3 pct=10 calls=2"));
    // A report time before the window start must not underflow into a report.
    RecompThreadTimeLedger early;
    early.Record(4, 900 * kMs, 950 * kMs, 0x40);
    CHECK(!early.TakeReport(100 * kMs, 1000 * kMs, 0, 4).has_value());
    return 0;
}

} // namespace

int main() {
    CHECK(ReportsBusiestThreadFirstWithShareOfWindow() == 0);
    CHECK(WaitsForTheWindowAndThenStartsAgain() == 0);
    CHECK(LimitsListedThreadsButCountsAll() == 0);
    CHECK(ToleratesClockGoingBackwards() == 0);
    std::puts("All synthetic THREAD TIME smoke checks passed.");
    return 0;
}
