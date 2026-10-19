// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/cpu_budget.h"
#include "common/env_option.h"
#include "common/logging.h"
#include "common/thread.h"
#include "core/cpu_budget_sampler.h"
#include "core/perf_stats.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>

#include "common/string_util.h"
#endif

namespace Core {

namespace {

#ifdef _WIN32

// FILETIME counts 100 ns ticks.
constexpr std::uint64_t kNanosecondsPerFileTimeTick = 100;

std::uint64_t FileTimeTicks(const FILETIME& time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

std::uint64_t FileTimeNs(const FILETIME& time) {
    return FileTimeTicks(time) * kNanosecondsPerFileTimeTick;
}

// The clock of GetThreadTimes' creation time, so window starts compare with it.
std::uint64_t SystemTimeTicks() {
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    return FileTimeTicks(now);
}

std::optional<std::uint64_t> ProcessCpuNs() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        return std::nullopt;
    }
    return FileTimeNs(kernel) + FileTimeNs(user);
}

// Looked up like SetThreadDescription in common/thread.cpp, so older systems just get no names.
std::string ThreadName(HANDLE thread) {
    using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto get_description = reinterpret_cast<GetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"KernelBase.dll"), "GetThreadDescription")));
    PWSTR description = nullptr;
    if (get_description == nullptr || FAILED(get_description(thread, &description)) ||
        description == nullptr) {
        return {};
    }
    std::string name = Common::UTF16ToUTF8(std::wstring_view{description});
    LocalFree(description);
    return name;
}

// Threads that cannot be opened or read are left out; their CPU shows up as untracked.
std::vector<Common::ThreadCpuSample> SnapshotProcessThreads() {
    std::vector<Common::ThreadCpuSample> samples;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return samples;
    }
    const DWORD process_id = GetCurrentProcessId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL found = Thread32First(snapshot, &entry); found; found = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process_id) {
            continue;
        }
        const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (thread == nullptr) {
            continue;
        }
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (GetThreadTimes(thread, &creation, &exit, &kernel, &user)) {
            samples.push_back({entry.th32ThreadID, FileTimeTicks(creation), ThreadName(thread),
                               FileTimeNs(kernel) + FileTimeNs(user)});
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return samples;
}

std::chrono::seconds WindowFromEnvironment() {
    const char* text = std::getenv("SUYU_CPU_BUDGET_WINDOW_SEC");
    if (text == nullptr) {
        return std::chrono::seconds{Common::kCpuBudgetDefaultWindowSec};
    }
    if (const auto seconds = Common::ParseCpuBudgetWindowSec(text)) {
        return std::chrono::seconds{*seconds};
    }
    LOG_WARNING(Core, "SUYU_CPU_BUDGET_WINDOW_SEC=\"{}\" is not 1..{}; using {} s", text,
                Common::kCpuBudgetMaxWindowSec, Common::kCpuBudgetDefaultWindowSec);
    return std::chrono::seconds{Common::kCpuBudgetDefaultWindowSec};
}

#endif // _WIN32

} // namespace

std::unique_ptr<CpuBudgetSampler> CpuBudgetSampler::CreateFromEnvironment(PerfStats& perf_stats) {
    const char* flag = std::getenv("SUYU_CPU_BUDGET");
    if (!Common::EnvFlagDefaultOff(flag ? flag : "")) {
        return nullptr;
    }
#ifdef _WIN32
    return std::make_unique<CpuBudgetSampler>(perf_stats, WindowFromEnvironment());
#else
    LOG_WARNING(Core, "SUYU_CPU_BUDGET=1 is only implemented on Windows; ignored");
    return nullptr;
#endif
}

CpuBudgetSampler::CpuBudgetSampler(PerfStats& perf_stats_, std::chrono::seconds window_)
    : perf_stats{perf_stats_}, window{window_} {
    perf_stats.EnableGameFrameRecording();
    LOG_INFO(Core, "cpu-budget sampling on: window {} s, top {} threads", window.count(),
             kReportedThreads);
    worker = std::thread([this] { Run(); });
}

CpuBudgetSampler::~CpuBudgetSampler() {
    {
        std::scoped_lock lock{mutex};
        stop_requested = true;
    }
    wake.notify_all();
    worker.join();
}

void CpuBudgetSampler::Run() {
    Common::SetCurrentThreadName("CpuBudget");
#ifdef _WIN32
    // Frames ended before this first snapshot belong to no window.
    void(perf_stats.TakeGameFrameWindow());
    std::uint64_t window_start_ticks = SystemTimeTicks();
    std::vector<Common::ThreadCpuSample> previous_threads = SnapshotProcessThreads();
    std::optional<std::uint64_t> previous_process_ns = ProcessCpuNs();
    auto previous_time = std::chrono::steady_clock::now();

    std::unique_lock lock{mutex};
    while (!wake.wait_for(lock, window, [this] { return stop_requested; })) {
        lock.unlock();

        Common::CpuBudgetInput input;
        input.frames = perf_stats.TakeGameFrameWindow();
        input.window_start_ticks = window_start_ticks;
        const std::uint64_t snapshot_start_ticks = SystemTimeTicks();
        input.current_threads = SnapshotProcessThreads();
        const std::optional<std::uint64_t> process_ns = ProcessCpuNs();
        const auto now = std::chrono::steady_clock::now();
        input.wall_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous_time).count());
        if (previous_process_ns && process_ns && *process_ns >= *previous_process_ns) {
            input.process_cpu_ns = *process_ns - *previous_process_ns;
        }
        input.previous_threads = std::move(previous_threads);
        LOG_INFO(Core, "{}", Common::FormatCpuBudgetReport(input, kReportedThreads));

        previous_threads = std::move(input.current_threads);
        window_start_ticks = snapshot_start_ticks;
        previous_process_ns = process_ns;
        previous_time = now;
        lock.lock();
    }
#endif
}

} // namespace Core
