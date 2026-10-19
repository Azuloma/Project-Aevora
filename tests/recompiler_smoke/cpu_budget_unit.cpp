// smoke_cpu_budget_unit: window deltas, per-frame math, percentiles, frame recording and the
// report line behind SUYU_CPU_BUDGET (common/cpu_budget.h).
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "common/cpu_budget.h"

using namespace Common;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint64_t kWindowStartTicks = 1000;

// Negative control: matches by id only, so a reused id inherits the old thread's baseline.
const ThreadCpuSample* MatchByIdOnly(const std::vector<ThreadCpuSample>& previous,
                                     const ThreadCpuSample& current) {
    for (const ThreadCpuSample& old : previous) {
        if (old.thread_id == current.thread_id) {
            return &old;
        }
    }
    return nullptr;
}

// The matching rules, shared by the real function and the control. False on a breach.
template <class Match>
bool MatchRulesHold(Match match) {
    const std::vector<ThreadCpuSample> previous{{7, 100, "Old", 1'000'000}};
    const ThreadCpuSample same{7, 100, "Old", 3'000'000};
    const ThreadCpuSample reused{7, 1500, "New", 8'000'000};
    return match(previous, same) == &previous[0] && match(previous, reused) == nullptr;
}

int ThreadsAreMatchedByIdAndCreation() {
    CHECK(MatchRulesHold(FindSameThread));
    CHECK(!MatchRulesHold(MatchByIdOnly));
    return 0;
}

// Negative control: an unmatched thread always counts from zero, so a missed sample charges
// a long-running thread's whole lifetime to one window.
std::optional<std::uint64_t> UnmatchedFromZero(const ThreadCpuSample* previous,
                                               const ThreadCpuSample& current, std::uint64_t) {
    return previous ? current.cpu_ns - previous->cpu_ns : current.cpu_ns;
}

template <class WindowCpu>
bool WindowCpuRulesHold(WindowCpu window_cpu) {
    const ThreadCpuSample before{1, 10, "T", 1000};
    const ThreadCpuSample after{1, 10, "T", 1500};
    const ThreadCpuSample created_in_window{2, kWindowStartTicks + 5, "N", 700};
    const ThreadCpuSample missed_earlier{3, kWindowStartTicks - 5, "M", 9'000'000};
    return window_cpu(&before, after, kWindowStartTicks) == 500u &&
           window_cpu(nullptr, created_in_window, kWindowStartTicks) == 700u &&
           !window_cpu(nullptr, missed_earlier, kWindowStartTicks);
}

int UnknownBaselinesAreNotChargedToTheWindow() {
    CHECK(WindowCpuRulesHold(ThreadWindowCpuNs));
    CHECK(!WindowCpuRulesHold(UnmatchedFromZero));
    return 0;
}

int ThreadUsesAreSortedAndCountUnknowns() {
    const std::vector<ThreadCpuSample> previous{{1, 10, "GPU", 1'000'000},
                                                {2, 10, "CPUCore_0", 500'000}};
    const std::vector<ThreadCpuSample> current{{1, 10, "GPU", 3'000'000},
                                               {2, 10, "CPUCore_0", 9'500'000},
                                               {3, kWindowStartTicks + 1, "New", 400'000},
                                               {4, 10, "Missed", 50'000'000}};
    const ThreadCpuUses collected = CollectThreadCpuUses(previous, current, kWindowStartTicks);
    CHECK(collected.uses.size() == 3);
    CHECK(collected.unknown_baseline_threads == 1);
    CHECK(collected.uses[0].thread_id == 2 && collected.uses[0].cpu_ns == 9'000'000);
    CHECK(collected.uses[1].thread_id == 1 && collected.uses[1].cpu_ns == 2'000'000);
    CHECK(collected.uses[2].thread_id == 3 && collected.uses[2].cpu_ns == 400'000);
    return 0;
}

// Negative control: the mean hides the slow frames a p99 must show.
std::uint64_t MeanInsteadOfPercentile(std::vector<std::uint64_t> values, unsigned) {
    std::uint64_t sum = 0;
    for (const std::uint64_t v : values) {
        sum += v;
    }
    return values.empty() ? 0 : sum / values.size();
}

template <class Percentile>
bool PercentileRulesHold(Percentile percentile) {
    std::vector<std::uint64_t> intervals(98, 33);
    intervals.push_back(100);
    intervals.push_back(100);
    return percentile(intervals, 99) == 100 && percentile(intervals, 50) == 33 &&
           percentile(intervals, 100) == 100 && percentile({}, 99) == 0 &&
           percentile({7}, 99) == 7;
}

int PercentileIsNearestRank() {
    CHECK(PercentileRulesHold(NearestRankPercentile));
    CHECK(!PercentileRulesHold(MeanInsteadOfPercentile));
    return 0;
}

// Negative control: reports a percentile over the kept intervals even after drops.
std::optional<double> PercentileIgnoringDrops(const FrameWindow& window, unsigned percent) {
    if (window.intervals_ns.empty()) {
        return std::nullopt;
    }
    return static_cast<double>(NearestRankPercentile(window.intervals_ns, percent)) / 1e6;
}

template <class FramePercentile>
bool TruncatedWindowRulesHold(FramePercentile frame_percentile) {
    FrameWindow complete;
    complete.frames = 3;
    complete.intervals_ns = {33'000'000, 34'000'000};
    FrameWindow truncated = complete;
    truncated.dropped_intervals = 1;
    const std::optional<double> p99 = frame_percentile(complete, 99);
    return p99 && *p99 == 34.0 && !frame_percentile(truncated, 99) &&
           !frame_percentile(FrameWindow{}, 99);
}

int TruncatedWindowsHaveNoPercentile() {
    CHECK(TruncatedWindowRulesHold(FrameIntervalPercentileMs));
    CHECK(!TruncatedWindowRulesHold(PercentileIgnoringDrops));
    return 0;
}

// Negative control: per wall second, as the HUD reports, not per frame.
std::optional<double> PerSecondNotPerFrame(std::uint64_t cpu_ns, std::uint64_t) {
    return static_cast<double>(cpu_ns) / 1e6 / 10.0;
}

template <class PerFrame>
bool PerFrameRulesHold(PerFrame per_frame) {
    // 6 s of CPU over 300 frames is 20 ms per frame; no frames means no value.
    const std::optional<double> twenty = per_frame(6'000'000'000ULL, 300);
    return twenty && *twenty == 20.0 && !per_frame(5, 0);
}

int CpuIsNormalizedPerFrame() {
    CHECK(PerFrameRulesHold(MsPerFrame));
    CHECK(!PerFrameRulesHold(PerSecondNotPerFrame));
    return 0;
}

// Negative control: measures the first frame from time zero and never caps.
class FirstFrameFromZeroRecorder {
public:
    static constexpr std::size_t kMaxIntervalsPerWindow = FrameIntervalRecorder::kMaxIntervalsPerWindow;

    void RecordFrameEnd(std::uint64_t now_ns) {
        ++window.frames;
        window.intervals_ns.push_back(now_ns - last);
        last = now_ns;
    }
    FrameWindow TakeWindow() {
        FrameWindow taken = window;
        window = {};
        return taken;
    }

private:
    FrameWindow window;
    std::uint64_t last = 0;
};

template <class Recorder>
bool RecorderRulesHold() {
    Recorder recorder;
    recorder.RecordFrameEnd(1'000'000'000);
    recorder.RecordFrameEnd(1'033'000'000);
    const FrameWindow first = recorder.TakeWindow();
    recorder.RecordFrameEnd(1'070'000'000);
    const FrameWindow second = recorder.TakeWindow();
    return first.frames == 2 && first.intervals_ns == std::vector<std::uint64_t>{33'000'000} &&
           second.frames == 1 && second.intervals_ns == std::vector<std::uint64_t>{37'000'000};
}

template <class Recorder>
bool CapRulesHold() {
    Recorder recorder;
    const std::size_t ends = Recorder::kMaxIntervalsPerWindow + 11;
    for (std::size_t i = 0; i < ends; ++i) {
        recorder.RecordFrameEnd(i * 1000);
    }
    const FrameWindow window = recorder.TakeWindow();
    return window.frames == ends && window.intervals_ns.size() == Recorder::kMaxIntervalsPerWindow &&
           window.dropped_intervals == 10;
}

int RecorderSkipsTheFirstFrameCarriesTheLastEndAndCaps() {
    CHECK(RecorderRulesHold<FrameIntervalRecorder>());
    CHECK(!RecorderRulesHold<FirstFrameFromZeroRecorder>());
    CHECK(CapRulesHold<FrameIntervalRecorder>());
    CHECK(!CapRulesHold<FirstFrameFromZeroRecorder>());
    return 0;
}

// Negative control: a synchronized recorder whose Take loses one frame of every non-empty window.
class LossyTakeRecorder {
public:
    void RecordFrameEnd(std::uint64_t now_ns) {
        std::scoped_lock lock{mutex};
        inner.RecordFrameEnd(now_ns);
    }
    FrameWindow TakeWindow() {
        std::scoped_lock lock{mutex};
        FrameWindow taken = inner.TakeWindow();
        if (taken.frames != 0) {
            --taken.frames;
        }
        return taken;
    }

private:
    std::mutex mutex;
    FrameIntervalRecorder inner;
};

// Writers and a concurrent taker: every frame lands in exactly one taken window.
template <class Recorder>
bool ConcurrentTakesKeepEveryFrame() {
    constexpr int kWriters = 4;
    constexpr int kEndsPerWriter = 2000;
    constexpr int kTakesWhileWriting = 50;
    Recorder recorder;
    std::vector<std::thread> writers;
    for (int t = 0; t < kWriters; ++t) {
        writers.emplace_back([&recorder] {
            for (int i = 0; i < kEndsPerWriter; ++i) {
                recorder.RecordFrameEnd(5);
            }
        });
    }
    std::uint64_t frames = 0;
    std::uint64_t intervals = 0;
    for (int i = 0; i < kTakesWhileWriting; ++i) {
        const FrameWindow window = recorder.TakeWindow();
        frames += window.frames;
        intervals += window.intervals_ns.size() + window.dropped_intervals;
    }
    for (std::thread& writer : writers) {
        writer.join();
    }
    const FrameWindow rest = recorder.TakeWindow();
    frames += rest.frames;
    intervals += rest.intervals_ns.size() + rest.dropped_intervals;
    const std::uint64_t total = static_cast<std::uint64_t>(kWriters) * kEndsPerWriter;
    return frames == total && intervals == total - 1;
}

int RecorderKeepsFramesAcrossConcurrentTakes() {
    CHECK(ConcurrentTakesKeepEveryFrame<FrameIntervalRecorder>());
    // At least one taken window is non-empty, so the control fails whatever the interleaving.
    CHECK(!ConcurrentTakesKeepEveryFrame<LossyTakeRecorder>());
    return 0;
}

int ReportMatchesTheLayout() {
    CpuBudgetInput input;
    input.wall_ns = 10'000'000'000ULL;
    input.window_start_ticks = kWindowStartTicks;
    input.process_cpu_ns = 9'000'000'000ULL;
    input.previous_threads = {{7, 10, "CPUCore_0", 1'000'000'000ULL}, {8, 10, "", 0}};
    input.current_threads = {{7, 10, "CPUCore_0", 7'000'000'000ULL},
                             {8, 10, "", 1'500'000'000ULL},
                             {9, 10, "Missed", 2'000'000'000ULL}};
    input.frames.frames = 300;
    input.frames.intervals_ns = {33'000'000, 34'000'000, 50'000'000};
    CHECK(FormatCpuBudgetReport(input, 1) ==
          "cpu-budget window_ms=10000 frames=300 process_ms_per_frame=30.00 "
          "threads_ms_per_frame=25.00 untracked_ms_per_frame=5.00 frame_p50_ms=34.00 "
          "frame_p99_ms=50.00 frame_max_ms=50.00 dropped_intervals=0 threads=2 "
          "unknown_baseline_threads=1 | CPUCore_0#7 ms_per_frame=20.00 core_pct=60.00");
    return 0;
}

int ReportWithoutFramesSaysNA() {
    CpuBudgetInput input;
    input.wall_ns = 1'000'000'000ULL;
    input.current_threads = {{1, 0, "", 5}};
    CHECK(FormatCpuBudgetReport(input, 4) ==
          "cpu-budget window_ms=1000 frames=0 process_ms_per_frame=N/A threads_ms_per_frame=N/A "
          "untracked_ms_per_frame=N/A frame_p50_ms=N/A frame_p99_ms=N/A frame_max_ms=N/A "
          "dropped_intervals=0 threads=1 unknown_baseline_threads=0 "
          "| unnamed#1 ms_per_frame=N/A core_pct=0.00");
    return 0;
}

// Negative control: atoi-style parsing accepts trailing text and zero.
std::optional<std::uint32_t> AtoiWindow(std::string_view text) {
    return static_cast<std::uint32_t>(std::atoi(std::string(text).c_str()));
}

template <class Parse>
bool WindowParseRulesHold(Parse parse) {
    return parse("10") == 10u && parse("60") == 60u && !parse("0") && !parse("61") &&
           !parse("10s") && !parse("") && !parse("-5") && !parse("99999");
}

int WindowParsingRejectsJunk() {
    CHECK(WindowParseRulesHold(ParseCpuBudgetWindowSec));
    CHECK(!WindowParseRulesHold(AtoiWindow));
    return 0;
}

} // namespace

int main() {
    CHECK(ThreadsAreMatchedByIdAndCreation() == 0);
    CHECK(UnknownBaselinesAreNotChargedToTheWindow() == 0);
    CHECK(ThreadUsesAreSortedAndCountUnknowns() == 0);
    CHECK(PercentileIsNearestRank() == 0);
    CHECK(TruncatedWindowsHaveNoPercentile() == 0);
    CHECK(CpuIsNormalizedPerFrame() == 0);
    CHECK(RecorderSkipsTheFirstFrameCarriesTheLastEndAndCaps() == 0);
    CHECK(RecorderKeepsFramesAcrossConcurrentTakes() == 0);
    CHECK(ReportMatchesTheLayout() == 0);
    CHECK(ReportWithoutFramesSaysNA() == 0);
    CHECK(WindowParsingRejectsJunk() == 0);
    std::puts("All synthetic CPU budget smoke checks passed.");
    return 0;
}
