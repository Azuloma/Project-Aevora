// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Core {

// Diagnostic only (SUYU_RECOMP_THREAD_TIME=1): how long each guest thread ran on one host core.
//
// Owned by one ArmRecomp::Impl and written only by that core's thread, so it needs no lock. Time
// is the wall time of ArmRecomp::RunThread calls: guest code plus host callbacks made from inside
// it, but not the idle wait between calls and not SVC service time spent after RunThread returns.
// Times are plain nanosecond counts so a test can drive the ledger without a clock.
class RecompThreadTimeLedger {
public:
    // One RunThread call of guest thread thread_id, entered at entry_pc, from start_ns to end_ns.
    void Record(std::uint64_t thread_id, std::uint64_t start_ns, std::uint64_t end_ns,
                std::uint64_t entry_pc) {
        if (!window_open_) {
            window_open_ = true;
            window_start_ns_ = start_ns;
        }
        const std::uint64_t elapsed_ns = end_ns > start_ns ? end_ns - start_ns : 0;
        Entry* entry = Find(thread_id);
        if (!entry) {
            entries_.push_back({thread_id});
            entry = &entries_.back();
        }
        entry->busy_ns += elapsed_ns;
        ++entry->calls;
        entry->last_entry_pc = entry_pc;
        busy_ns_ += elapsed_ns;
    }

    // Once window_ns has passed since the first record of the window: a one-line report of the
    // busiest top_count threads, then the ledger starts a new window. Otherwise nullopt.
    std::optional<std::string> TakeReport(std::uint64_t now_ns, std::uint64_t window_ns,
                                          std::size_t core, std::size_t top_count) {
        if (!window_open_ || now_ns < window_start_ns_ || now_ns - window_start_ns_ < window_ns) {
            return std::nullopt;
        }
        const std::uint64_t span_ns = now_ns - window_start_ns_;
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) { return a.busy_ns > b.busy_ns; });
        std::ostringstream line;
        line << "recomp thread-time core=" << core << " window_ms=" << span_ns / 1000000
             << " busy_pct=" << Percent(busy_ns_, span_ns) << " threads=" << entries_.size();
        for (std::size_t i = 0; i < entries_.size() && i < top_count; ++i) {
            const Entry& e = entries_[i];
            line << " | id=" << e.thread_id << " pct=" << Percent(e.busy_ns, span_ns)
                 << " calls=" << e.calls << " pc=0x" << std::hex << e.last_entry_pc << std::dec;
        }
        entries_.clear();
        busy_ns_ = 0;
        window_open_ = false;
        return line.str();
    }

private:
    struct Entry {
        std::uint64_t thread_id = 0;
        std::uint64_t busy_ns = 0;
        std::uint64_t calls = 0;
        std::uint64_t last_entry_pc = 0;
    };

    Entry* Find(std::uint64_t thread_id) {
        for (Entry& e : entries_) {
            if (e.thread_id == thread_id) {
                return &e;
            }
        }
        return nullptr;
    }

    static std::uint64_t Percent(std::uint64_t part_ns, std::uint64_t whole_ns) {
        return whole_ns ? part_ns * 100 / whole_ns : 0;
    }

    std::vector<Entry> entries_;
    std::uint64_t busy_ns_ = 0;
    std::uint64_t window_start_ns_ = 0;
    bool window_open_ = false;
};

} // namespace Core
