// smoke_inventory_session_unit: the manual-segment state of the boundary inventory
// (suyu_cmd/overlay/inventory_session.h). Time is driven as plain nanosecond counts.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "suyu_cmd/overlay/inventory_session.h"

using SuyuCmd::FormatInventoryStateLine;
using SuyuCmd::InventorySession;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint64_t kSecondNs = 1'000'000'000ULL;

int PauseDoesNotCountTime() {
    InventorySession session{false, 0};
    CHECK(session.ElapsedNs(5 * kSecondNs) == 0);
    session.SetRecording(true, 5 * kSecondNs);
    CHECK(session.ElapsedNs(8 * kSecondNs) == 3 * kSecondNs);
    session.SetRecording(false, 8 * kSecondNs);
    CHECK(session.ElapsedNs(60 * kSecondNs) == 3 * kSecondNs);
    session.SetRecording(true, 60 * kSecondNs);
    CHECK(session.ElapsedNs(62 * kSecondNs) == 5 * kSecondNs);
    // A clock sample older than the last change never goes backwards.
    CHECK(session.ElapsedNs(59 * kSecondNs) == 3 * kSecondNs);
    return 0;
}

int MarkEndsTheSegmentAndAdvancesTheStep() {
    InventorySession session{true, 0};
    CHECK(session.Label() == "title");
    CHECK(session.NextSerial() == 1);
    const auto first = session.Mark(4 * kSecondNs);
    CHECK(first.label == "title" && first.serial == 1 && first.elapsed_ns == 4 * kSecondNs);
    CHECK(session.Label() == "village" && session.NextSerial() == 2);
    // The new segment starts at the mark.
    CHECK(session.ElapsedNs(5 * kSecondNs) == kSecondNs);
    const auto second = session.Mark(9 * kSecondNs);
    CHECK(second.label == "village" && second.serial == 2 && second.elapsed_ns == 5 * kSecondNs);
    return 0;
}

int PausedMarkKeepsThePause() {
    InventorySession session{false, 0};
    session.SetRecording(true, 0);
    session.SetRecording(false, 2 * kSecondNs);
    const auto ended = session.Mark(50 * kSecondNs);
    CHECK(ended.elapsed_ns == 2 * kSecondNs);
    CHECK(!session.Recording());
    CHECK(session.ElapsedNs(90 * kSecondNs) == 0);
    return 0;
}

int StepsRunOutIntoExtraLabels() {
    InventorySession session{true, 0};
    for (std::size_t n = 0; n < SuyuCmd::kInventoryStepLabels.size(); ++n) {
        CHECK(session.Label() == std::string{SuyuCmd::kInventoryStepLabels[n]});
        session.SkipStep();
    }
    CHECK(session.Label() == "extra1");
    session.SkipStep();
    CHECK(session.Label() == "extra2");
    // Skipping writes nothing and uses no serial.
    CHECK(session.NextSerial() == 1);
    return 0;
}

int TypedLabelLastsOneSegment() {
    InventorySession session{true, 0};
    session.SetCustomLabel("rare spawn");
    CHECK(session.Label() == "rare spawn");
    const auto ended = session.Mark(kSecondNs);
    CHECK(ended.label == "rare spawn");
    CHECK(session.Label() == "village");
    session.SetCustomLabel("x");
    session.SkipStep();
    CHECK(session.Label() == "field");
    return 0;
}

int StateLineShowsStateStepAndTenths() {
    InventorySession session{true, 0};
    session.Mark(0);
    session.Mark(0);
    session.Mark(0);
    session.Mark(0);
    const auto snapshot = session.Snapshot(12'400'000'000ULL, true, "");
    CHECK(FormatInventoryStateLine(snapshot, 12'400'000'000ULL) == "INV rec 5/8 battle 12.4 s");
    // Between publishes a recording segment keeps counting.
    CHECK(FormatInventoryStateLine(snapshot, 15'000'000'000ULL) == "INV rec 5/8 battle 15.0 s");
    session.SetRecording(false, 20 * kSecondNs);
    const auto paused = session.Snapshot(20 * kSecondNs, true, "");
    CHECK(FormatInventoryStateLine(paused, 99 * kSecondNs) == "INV paused 5/8 battle 20.0 s");
    CHECK(FormatInventoryStateLine(snapshot, 1) == "INV rec 5/8 battle 12.4 s");
    return 0;
}

// A writer that fails while `failing` is set and records every file it wrote.
struct FakeDisk {
    bool failing{};
    std::vector<std::string> written;
    bool operator()(const std::string& file_name, const std::string&, std::string& error) {
        if (failing) {
            error = "disk full";
            return false;
        }
        written.push_back(file_name);
        return true;
    }
};

int PendingFilesKeepOrderAndRetry() {
    SuyuCmd::PendingSegmentFiles pending;
    FakeDisk disk;
    disk.failing = true;
    pending.Add("a", "1");
    pending.Add("b", "2");
    auto failed = pending.Flush(disk);
    CHECK(!failed.ok && failed.error == "disk full" && failed.lost.empty());
    CHECK(pending.Size() == 2 && disk.written.empty());
    disk.failing = false;
    pending.Add("c", "3");
    const auto retried = pending.Flush(disk);
    CHECK(retried.ok && retried.last_file == "c" && pending.Size() == 0);
    CHECK((disk.written == std::vector<std::string>{"a", "b", "c"}));
    return 0;
}

int PendingFilesRetryBeforeDroppingAnything() {
    SuyuCmd::PendingSegmentFiles pending;
    FakeDisk disk;
    disk.failing = true;
    for (std::size_t n = 0; n < SuyuCmd::kMaxUnwrittenSegments; ++n) {
        pending.Add("s" + std::to_string(n), "x");
        CHECK(pending.Flush(disk).lost.empty());
    }
    // The disk comes back just before the segment that would overflow the queue.
    disk.failing = false;
    pending.Add("overflow", "x");
    const auto outcome = pending.Flush(disk);
    CHECK(outcome.ok && outcome.lost.empty());
    CHECK(disk.written.size() == SuyuCmd::kMaxUnwrittenSegments + 1);
    CHECK(disk.written.front() == "s0" && disk.written.back() == "overflow");
    return 0;
}

int PendingFilesReportWhatTheyDrop() {
    SuyuCmd::PendingSegmentFiles pending;
    FakeDisk disk;
    disk.failing = true;
    for (std::size_t n = 0; n <= SuyuCmd::kMaxUnwrittenSegments; ++n) {
        pending.Add("s" + std::to_string(n), "x");
    }
    const auto outcome = pending.Flush(disk);
    CHECK(!outcome.ok);
    CHECK(outcome.lost.size() == 1 && outcome.lost[0] == "s0");
    CHECK(pending.Size() == SuyuCmd::kMaxUnwrittenSegments);
    return 0;
}

} // namespace

int main() {
    if (const int rc = PendingFilesKeepOrderAndRetry()) return rc;
    if (const int rc = PendingFilesRetryBeforeDroppingAnything()) return rc;
    if (const int rc = PendingFilesReportWhatTheyDrop()) return rc;
    if (const int rc = PauseDoesNotCountTime()) return rc;
    if (const int rc = MarkEndsTheSegmentAndAdvancesTheStep()) return rc;
    if (const int rc = PausedMarkKeepsThePause()) return rc;
    if (const int rc = StepsRunOutIntoExtraLabels()) return rc;
    if (const int rc = TypedLabelLastsOneSegment()) return rc;
    if (const int rc = StateLineShowsStateStepAndTenths()) return rc;
    std::puts("smoke_inventory_session_unit: OK");
    return 0;
}
