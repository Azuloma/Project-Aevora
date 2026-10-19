// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

namespace Core {

class PerfStats;

/// SUYU_CPU_BUDGET=1: logs a "cpu-budget" line (common/cpu_budget.h) every window on its own
/// thread. Windows only; must be destroyed before the PerfStats it reads.
class CpuBudgetSampler {
public:
    static constexpr std::size_t kReportedThreads = 12;

    /// Reads SUYU_CPU_BUDGET and SUYU_CPU_BUDGET_WINDOW_SEC; nullptr when the diagnostic is off.
    static std::unique_ptr<CpuBudgetSampler> CreateFromEnvironment(PerfStats& perf_stats);

    CpuBudgetSampler(PerfStats& perf_stats, std::chrono::seconds window);
    ~CpuBudgetSampler();

    CpuBudgetSampler(const CpuBudgetSampler&) = delete;
    CpuBudgetSampler& operator=(const CpuBudgetSampler&) = delete;

private:
    void Run();

    PerfStats& perf_stats;
    const std::chrono::seconds window;

    std::mutex mutex;
    std::condition_variable wake;
    bool stop_requested{};
    std::thread worker;
};

} // namespace Core
