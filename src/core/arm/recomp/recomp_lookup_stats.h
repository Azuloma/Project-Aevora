// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    // Modules beyond kMaxModules, or a pc below every module base, share the kOtherModule slot.
    static constexpr std::size_t kMaxModules = 16;
    static constexpr std::size_t kOtherModule = kMaxModules;
    static constexpr std::size_t kModuleSlots = kMaxModules + 1;
    static constexpr std::size_t kTopPairCount = 8;

    enum Counter : std::size_t {
        Dispatches,
        ChainedBlocks,
        Halted,
        Svc,
        BudgetExhausted,
        ExitCovered,
        ExitUncovered,
        kScalarCount,
        kPairBase = kScalarCount,
        kCounterCount = kPairBase + kModuleSlots * kModuleSlots,
    };
    using Counters = RecompShardedCounters<kCounterCount>;
    static constexpr std::uint64_t kReportTickMask = 0xFFF;

    // Index of the module containing pc: the highest base at or below pc in sorted_bases.
    static std::size_t ModuleIndex(const std::vector<std::uint64_t>& sorted_bases, std::uint64_t pc) {
        const auto above = std::upper_bound(sorted_bases.begin(), sorted_bases.end(), pc);
        if (above == sorted_bases.begin()) {
            return kOtherModule;
        }
        const std::size_t index = static_cast<std::size_t>(above - sorted_bases.begin()) - 1;
        return index < kMaxModules ? index : kOtherModule;
    }

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

    // from_module and to_module are ModuleIndex values of the entry pc and of the next pc.
    static void CountExit(Counters::Slot& slot, bool next_pc_covered, std::size_t from_module,
                          std::size_t to_module) {
        Counters::Increment(slot, next_pc_covered ? ExitCovered : ExitUncovered);
        Counters::Increment(slot, kPairBase + from_module * kModuleSlots + to_module);
    }

    // One line of per-window counts once window_ns has passed since the previous report;
    // module_names[i] labels module index i, valid for module_generation (see modules_changed).
    std::optional<std::string> TakeReport(std::uint64_t now_ns, std::uint64_t window_ns,
                                          const std::vector<std::string>& module_names = {},
                                          std::uint32_t module_generation = 0) {
        std::scoped_lock lock{report_lock_};
        if (!window_started_) {
            window_started_ = true;
            window_start_ns_ = now_ns;
            window_module_generation_ = module_generation;
            for (std::size_t i = 0; i < kCounterCount; ++i) {
                reported_[i] = counters_.Sum(i);
            }
            return std::nullopt;
        }
        if (now_ns < window_start_ns_ || now_ns - window_start_ns_ < window_ns) {
            return std::nullopt;
        }
        std::string line =
            "lookup-stats window_ms=" + std::to_string((now_ns - window_start_ns_) / 1000000);
        static constexpr const char* kNames[kScalarCount] = {
            "dispatches",       "chained_blocks", "halted",        "svc",
            "budget_exhausted", "exit_covered",   "exit_uncovered"};
        std::vector<std::pair<std::uint64_t, std::size_t>> pairs;
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            const std::uint64_t total = counters_.Sum(i);
            const std::uint64_t delta = total - reported_[i];
            reported_[i] = total;
            if (i < kScalarCount) {
                line += std::string(" ") + kNames[i] + "=" + std::to_string(delta);
            } else if (delta != 0) {
                pairs.emplace_back(delta, i - kPairBase);
            }
        }
        std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        // Indices from before a module-list change no longer match the names: label them by number.
        const bool modules_changed = module_generation != window_module_generation_;
        const std::vector<std::string> no_names;
        const std::vector<std::string>& labels = modules_changed ? no_names : module_names;
        for (std::size_t n = 0; n < pairs.size() && n < kTopPairCount; ++n) {
            const std::size_t from = pairs[n].second / kModuleSlots;
            const std::size_t to = pairs[n].second % kModuleSlots;
            line += " exit[" + ModuleLabel(labels, from) + ">" + ModuleLabel(labels, to) +
                    "]=" + std::to_string(pairs[n].first);
        }
        if (modules_changed) {
            line += " modules_changed=1";
        }
        window_module_generation_ = module_generation;
        window_start_ns_ = now_ns;
        return line;
    }

private:
    static std::string ModuleLabel(const std::vector<std::string>& names, std::size_t index) {
        if (index == kOtherModule) {
            return "other";
        }
        if (index < names.size() && !names[index].empty()) {
            return names[index];
        }
        return "module" + std::to_string(index);
    }

    Counters counters_;
    std::mutex report_lock_;
    bool window_started_ = false;
    std::uint64_t window_start_ns_ = 0;
    std::uint32_t window_module_generation_ = 0;
    std::uint64_t reported_[kCounterCount] = {};
};

} // namespace Core
