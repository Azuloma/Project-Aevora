// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Kernel {

// SUYU_GUEST_TIME=1 diagnostic: splits one core's wall time into guest run, idle and host time.
// Single-writer, times are plain nanoseconds; the gap after EndRun minus idle is charged as host.
class GuestTimeLedger {
public:
    static constexpr std::size_t kSvcSlots = 256;

    // Guest code of thread_id starts running; closes the gap opened by the previous EndRun.
    void BeginRun(std::uint64_t thread_id, std::uint64_t now_ns) {
        CloseGap(now_ns);
        if (!window_open_) {
            window_open_ = true;
            window_start_ns_ = now_ns;
        }
        run_thread_id_ = thread_id;
        run_start_ns_ = now_ns;
        running_ = true;
    }

    // The run ended in an SVC (svc_number set) or another halt; opens the gap that follows.
    void EndRun(std::uint64_t now_ns, std::optional<std::uint32_t> svc_number) {
        if (!running_) {
            return;
        }
        running_ = false;
        const std::uint64_t run_ns = Elapsed(run_start_ns_, now_ns);
        Entry& entry = FindOrAdd(run_thread_id_);
        entry.run_ns += run_ns;
        ++entry.slices;
        run_ns_ += run_ns;
        ++slices_;
        if (svc_number) {
            ++svcs_[*svc_number % kSvcSlots].calls;
        }
        gap_open_ = true;
        gap_start_ns_ = now_ns;
        gap_idle_ns_ = 0;
        gap_thread_id_ = run_thread_id_;
        gap_svc_ = svc_number;
    }

    // Time the core spent waiting idle; counted only inside an open gap.
    void AddIdle(std::uint64_t idle_ns) {
        if (gap_open_) {
            gap_idle_ns_ += idle_ns;
        }
    }

    // After window_ns: a one-line report of the top_count threads and SVCs, then a new window.
    std::optional<std::string> TakeReport(std::uint64_t now_ns, std::uint64_t window_ns,
                                          std::size_t core, std::size_t top_count) {
        if (!window_open_ || now_ns < window_start_ns_ || now_ns - window_start_ns_ < window_ns) {
            return std::nullopt;
        }
        const std::uint64_t span_ns = now_ns - window_start_ns_;
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return a.run_ns + a.host_ns > b.run_ns + b.host_ns;
        });
        const std::uint64_t counted_ns = run_ns_ + idle_ns_ + host_ns_;
        std::ostringstream line;
        line << "guest-time core=" << core << " window_ms=" << span_ns / 1000000
             << " run_pct=" << Percent(run_ns_, span_ns) << " idle_pct=" << Percent(idle_ns_, span_ns)
             << " host_pct=" << Percent(host_ns_, span_ns)
             << " unattributed_pct=" << Percent(Elapsed(counted_ns, span_ns), span_ns)
             << " slices=" << slices_ << " threads=" << entries_.size();
        for (std::size_t i = 0; i < entries_.size() && i < top_count; ++i) {
            const Entry& e = entries_[i];
            line << " | id=" << e.thread_id << " run_pct=" << Percent(e.run_ns, span_ns)
                 << " host_pct=" << Percent(e.host_ns, span_ns) << " slices=" << e.slices;
        }
        AppendTopSvcs(line, span_ns, top_count);
        entries_.clear();
        svcs_ = {};
        run_ns_ = 0;
        idle_ns_ = 0;
        host_ns_ = 0;
        slices_ = 0;
        window_open_ = true; // reported right after BeginRun, so the new window starts now
        window_start_ns_ = now_ns;
        return line.str();
    }

private:
    struct Entry {
        std::uint64_t thread_id = 0;
        std::uint64_t run_ns = 0;
        std::uint64_t host_ns = 0;
        std::uint64_t slices = 0;
    };
    struct SvcEntry {
        std::uint64_t calls = 0;
        std::uint64_t host_ns = 0;
    };

    void CloseGap(std::uint64_t now_ns) {
        if (!gap_open_) {
            return;
        }
        gap_open_ = false;
        const std::uint64_t gap_ns = Elapsed(gap_start_ns_, now_ns);
        const std::uint64_t idle_ns = std::min(gap_idle_ns_, gap_ns);
        const std::uint64_t host_ns = gap_ns - idle_ns;
        idle_ns_ += idle_ns;
        host_ns_ += host_ns;
        FindOrAdd(gap_thread_id_).host_ns += host_ns;
        if (gap_svc_) {
            svcs_[*gap_svc_ % kSvcSlots].host_ns += host_ns;
        }
    }

    Entry& FindOrAdd(std::uint64_t thread_id) {
        for (Entry& e : entries_) {
            if (e.thread_id == thread_id) {
                return e;
            }
        }
        entries_.push_back({thread_id});
        return entries_.back();
    }

    void AppendTopSvcs(std::ostringstream& line, std::uint64_t span_ns, std::size_t top_count) {
        std::vector<std::size_t> order;
        for (std::size_t n = 0; n < kSvcSlots; ++n) {
            if (svcs_[n].calls != 0) {
                order.push_back(n);
            }
        }
        std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
            return svcs_[a].host_ns > svcs_[b].host_ns;
        });
        for (std::size_t i = 0; i < order.size() && i < top_count; ++i) {
            const SvcEntry& svc = svcs_[order[i]];
            line << " | svc=0x" << std::hex << order[i] << std::dec
                 << " host_pct=" << Percent(svc.host_ns, span_ns) << " calls=" << svc.calls;
        }
    }

    static std::uint64_t Elapsed(std::uint64_t start_ns, std::uint64_t end_ns) {
        return end_ns > start_ns ? end_ns - start_ns : 0;
    }

    static std::uint64_t Percent(std::uint64_t part_ns, std::uint64_t whole_ns) {
        return whole_ns ? part_ns * 100 / whole_ns : 0;
    }

    std::vector<Entry> entries_;
    std::array<SvcEntry, kSvcSlots> svcs_{};
    std::uint64_t run_ns_ = 0;
    std::uint64_t idle_ns_ = 0;
    std::uint64_t host_ns_ = 0;
    std::uint64_t slices_ = 0;
    std::uint64_t window_start_ns_ = 0;
    bool window_open_ = false;

    bool running_ = false;
    std::uint64_t run_thread_id_ = 0;
    std::uint64_t run_start_ns_ = 0;

    bool gap_open_ = false;
    std::uint64_t gap_start_ns_ = 0;
    std::uint64_t gap_idle_ns_ = 0;
    std::uint64_t gap_thread_id_ = 0;
    std::optional<std::uint32_t> gap_svc_;
};

} // namespace Kernel
