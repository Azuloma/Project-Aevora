// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// ImGui-, SDL- and OS-free state of the boundary inventory's manual segments (N0): which step the
// open segment is, how long it has recorded, and the one-line state text. The event thread owns it.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <deque>
#include <string_view>
#include <utility>
#include <vector>

namespace SuyuCmd {

/// The supported gameplay path, in order; each mark ends the segment of the step it is on.
inline constexpr std::array<std::string_view, 8> kInventoryStepLabels{
    "title", "village", "field", "catch", "battle", "menu", "pokedex", "save"};

inline constexpr std::size_t kInventoryLabelCapacity = 48;
inline constexpr std::uint64_t kNsPerTenthSecond = 100'000'000ULL;

/// The one clock the host and the overlay share, so published times can be extrapolated.
inline std::uint64_t InventoryNowNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

/// What the overlay shows; the event thread publishes it, times are steady-clock nanoseconds.
struct InventorySnapshot {
    bool available{};
    bool recording{};
    std::size_t step{};
    std::size_t step_count{kInventoryStepLabels.size()};
    std::string label;
    std::uint32_t next_serial{1};
    std::uint64_t elapsed_ns{};   ///< recorded time of the open segment at `published_ns`
    std::uint64_t published_ns{};
    bool last_ok{true};
    std::string last_message;
};

struct InventoryRequest {
    enum class Kind : std::uint8_t { Mark, TogglePause, SkipStep, WriteNow, SetLabel };
    Kind kind{Kind::Mark};
    std::string label; ///< SetLabel only
};

/// Ended segments whose file could not be written are kept for a retry, oldest dropped first.
inline constexpr std::size_t kMaxUnwrittenSegments = 16;

/// Files for ended segments, written oldest first. A segment leaves the recorder when it ends,
/// so this queue holds the only copy of one that failed to write.
class PendingSegmentFiles {
public:
    struct Outcome {
        bool ok{true};
        std::string last_file; ///< the last file written by this flush
        std::string error;     ///< why the first failed write failed
        std::vector<std::string> lost; ///< files dropped because the queue was full
    };

    void Add(std::string file_name, std::string text) {
        files_.push_back({std::move(file_name), std::move(text)});
    }

    /// Tries every waiting file in order and stops at the first failure; only then are the
    /// oldest files beyond the cap dropped, and their names reported.
    template <typename Writer>
    Outcome Flush(Writer&& write) {
        Outcome outcome;
        while (!files_.empty()) {
            const Entry& front = files_.front();
            if (!write(front.file_name, front.text, outcome.error)) {
                outcome.ok = false;
                break;
            }
            outcome.last_file = front.file_name;
            files_.pop_front();
        }
        while (files_.size() > kMaxUnwrittenSegments) {
            outcome.lost.push_back(files_.front().file_name);
            files_.pop_front();
        }
        return outcome;
    }

    std::size_t Size() const {
        return files_.size();
    }

private:
    struct Entry {
        std::string file_name;
        std::string text;
    };
    std::deque<Entry> files_;
};

class InventorySession {
public:
    struct EndedSegment {
        std::string label;
        std::uint32_t serial{};
        std::uint64_t elapsed_ns{};
    };

    InventorySession(bool recording, std::uint64_t now_ns)
        : recording_{recording}, since_ns_{now_ns} {}

    bool Recording() const {
        return recording_;
    }

    std::size_t Step() const {
        return step_;
    }

    std::uint32_t NextSerial() const {
        return serial_;
    }

    /// Pausing keeps what was recorded; the paused time does not count.
    void SetRecording(bool recording, std::uint64_t now_ns) {
        if (recording == recording_) {
            return;
        }
        if (recording_) {
            segment_ns_ = ElapsedNs(now_ns);
        }
        since_ns_ = now_ns;
        recording_ = recording;
    }

    std::uint64_t ElapsedNs(std::uint64_t now_ns) const {
        const bool running = recording_ && now_ns > since_ns_;
        return segment_ns_ + (running ? now_ns - since_ns_ : 0);
    }

    static std::string StepLabel(std::size_t step) {
        if (step < kInventoryStepLabels.size()) {
            return std::string{kInventoryStepLabels[step]};
        }
        return "extra" + std::to_string(step - kInventoryStepLabels.size() + 1);
    }

    /// A typed label wins over the step name until the segment ends.
    std::string Label() const {
        return custom_label_.empty() ? StepLabel(step_) : custom_label_;
    }

    void SetCustomLabel(std::string label) {
        custom_label_ = std::move(label);
    }

    void SkipStep() {
        ++step_;
        custom_label_.clear();
    }

    /// Ends the open segment and starts the next one on the next step; a pause carries over.
    EndedSegment Mark(std::uint64_t now_ns) {
        EndedSegment ended{Label(), serial_, ElapsedNs(now_ns)};
        ++serial_;
        ++step_;
        custom_label_.clear();
        segment_ns_ = 0;
        since_ns_ = now_ns;
        return ended;
    }

    InventorySnapshot Snapshot(std::uint64_t now_ns, bool last_ok,
                               const std::string& last_message) const {
        InventorySnapshot snapshot;
        snapshot.available = true;
        snapshot.recording = recording_;
        snapshot.step = step_;
        snapshot.label = Label();
        snapshot.next_serial = serial_;
        snapshot.elapsed_ns = ElapsedNs(now_ns);
        snapshot.published_ns = now_ns;
        snapshot.last_ok = last_ok;
        snapshot.last_message = last_message;
        return snapshot;
    }

private:
    bool recording_;
    std::uint64_t since_ns_;
    std::uint64_t segment_ns_{};
    std::size_t step_{};
    std::uint32_t serial_{1};
    std::string custom_label_;
};

/// One line such as `INV rec 3/8 battle 12.4 s`; a recording segment keeps counting after the
/// snapshot was published, so the line moves between publishes.
inline std::string FormatInventoryStateLine(const InventorySnapshot& snapshot,
                                            std::uint64_t now_ns) {
    std::uint64_t elapsed_ns = snapshot.elapsed_ns;
    if (snapshot.recording && now_ns > snapshot.published_ns) {
        elapsed_ns += now_ns - snapshot.published_ns;
    }
    const std::uint64_t tenths = elapsed_ns / kNsPerTenthSecond;
    char text[96];
    std::snprintf(text, sizeof text, "INV %s %zu/%zu %s %llu.%llu s",
                  snapshot.recording ? "rec" : "paused", snapshot.step + 1, snapshot.step_count,
                  snapshot.label.c_str(), static_cast<unsigned long long>(tenths / 10),
                  static_cast<unsigned long long>(tenths % 10));
    return text;
}

} // namespace SuyuCmd
