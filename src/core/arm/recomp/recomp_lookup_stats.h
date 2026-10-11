// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "core/arm/recomp/recomp_sharded_counters.h"

namespace Core {

// Why one call into generated code returned to the host dispatcher.
enum class DispatchReturn {
    Halted, // parked on a halt such as IC IVAU, not a branch or an SVC
    Svc,
    BudgetExhausted,
    Exit, // budget left: an indirect or cross-module target the image could not chain
};

constexpr DispatchReturn ClassifyReturn(bool halted, bool has_pending_svc, int chain_budget_left) {
    if (halted) {
        return DispatchReturn::Halted;
    }
    if (has_pending_svc) {
        return DispatchReturn::Svc;
    }
    return chain_budget_left <= 0 ? DispatchReturn::BudgetExhausted : DispatchReturn::Exit;
}

// SUYU_RECOMP_LOOKUP_STATS=1 diagnostic: counts dispatcher returns by reason, per reporting window.
class RecompLookupStats {
public:
    enum Counter : std::size_t {
        Dispatches,
        ChainedBlocks,
        Halted,
        Svc,
        BudgetExhausted,
        ExitCovered,
        ExitUncovered,
        kCounterCount,
    };
    using Counters = RecompShardedCounters<kCounterCount>;
    static constexpr std::uint64_t kReportTickMask = 0xFFF;

    Counters::Slot& Acquire() {
        return counters_.Acquire();
    }

    // Counts one return; true on every kReportTickMask+1'th return of this thread's slot.
    static bool CountReturn(Counters::Slot& slot, DispatchReturn reason, std::uint64_t blocks_run) {
        Counters::Increment(slot, Dispatches);
        Counters::Add(slot, ChainedBlocks, blocks_run);
        if (reason == DispatchReturn::Halted) {
            Counters::Increment(slot, Halted);
        } else if (reason == DispatchReturn::Svc) {
            Counters::Increment(slot, Svc);
        } else if (reason == DispatchReturn::BudgetExhausted) {
            Counters::Increment(slot, BudgetExhausted);
        }
        return (slot.value[Dispatches].load(std::memory_order_relaxed) & kReportTickMask) == 0;
    }

    static void CountExit(Counters::Slot& slot, bool next_pc_covered) {
        Counters::Increment(slot, next_pc_covered ? ExitCovered : ExitUncovered);
    }

    // One line of per-window counts once window_ns has passed since the previous report.
    std::optional<std::string> TakeReport(std::uint64_t now_ns, std::uint64_t window_ns) {
        std::scoped_lock lock{report_lock_};
        if (!window_started_) {
            window_started_ = true;
            window_start_ns_ = now_ns;
            for (std::size_t i = 0; i < kCounterCount; ++i) {
                reported_[i] = counters_.Sum(i);
            }
            return std::nullopt;
        }
        if (now_ns < window_start_ns_ || now_ns - window_start_ns_ < window_ns) {
            return std::nullopt;
        }
        std::string line = "lookup-stats window_ms=" + std::to_string((now_ns - window_start_ns_) / 1000000);
        static constexpr const char* kNames[kCounterCount] = {
            "dispatches", "chained_blocks", "halted", "svc", "budget_exhausted", "exit_covered", "exit_uncovered"};
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            const std::uint64_t total = counters_.Sum(i);
            line += std::string(" ") + kNames[i] + "=" + std::to_string(total - reported_[i]);
            reported_[i] = total;
        }
        window_start_ns_ = now_ns;
        return line;
    }

private:
    Counters counters_;
    std::mutex report_lock_;
    bool window_started_ = false;
    std::uint64_t window_start_ns_ = 0;
    std::uint64_t reported_[kCounterCount] = {};
};

} // namespace Core
