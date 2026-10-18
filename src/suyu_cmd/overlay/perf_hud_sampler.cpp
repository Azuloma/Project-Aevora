// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/logging/log.h"
#include "suyu_cmd/overlay/perf_hud_sampler.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>
#endif

namespace SuyuCmd {

namespace {

#ifdef _WIN32

// 100 ns ticks, the unit of FILETIME.
constexpr std::int64_t kNanosecondsPerTick = 100;

std::uint64_t FileTimeTicks(const FILETIME& time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

/// Process CPU time (kernel plus user) in 100 ns ticks, or nothing if it cannot be read.
std::optional<std::uint64_t> ProcessCpuTicks() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        return std::nullopt;
    }
    return FileTimeTicks(kernel) + FileTimeTicks(user);
}

/// Process GPU use as Task Manager shows it: per engine type the sum over this process's engines,
/// then the busiest type.
class GpuEngineCounter {
public:
    GpuEngineCounter() {
        const std::wstring path =
            L"\\GPU Engine(pid_" + std::to_wstring(GetCurrentProcessId()) + L"_*)\\Utilization Percentage";
        if (const PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query); status != ERROR_SUCCESS) {
            Fail("PdhOpenQuery", status);
            return;
        }
        if (const PDH_STATUS status = PdhAddEnglishCounterW(query, path.c_str(), 0, &counter);
            status != ERROR_SUCCESS) {
            Fail("PdhAddEnglishCounter", status);
            return;
        }
        // Rate counters need a first collection as their baseline.
        PdhCollectQueryData(query);
        ready = true;
    }

    ~GpuEngineCounter() {
        if (query != nullptr) {
            PdhCloseQuery(query);
        }
    }

    GpuEngineCounter(const GpuEngineCounter&) = delete;
    GpuEngineCounter& operator=(const GpuEngineCounter&) = delete;

    HudPercent Read() {
        if (!ready || PdhCollectQueryData(query) != ERROR_SUCCESS) {
            return Unavailable();
        }
        DWORD buffer_bytes = 0;
        DWORD item_count = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_bytes,
                                                         &item_count, nullptr);
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || buffer_bytes == 0) {
            return Unavailable();
        }
        std::vector<std::byte> buffer(buffer_bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_bytes, &item_count,
                                              items);
        if (status != ERROR_SUCCESS) {
            return Unavailable();
        }
        std::map<std::wstring, double> percent_by_engine_type;
        for (DWORD i = 0; i < item_count; ++i) {
            if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
                continue;
            }
            percent_by_engine_type[EngineType(items[i].szName)] += items[i].FmtValue.doubleValue;
        }
        if (percent_by_engine_type.empty()) {
            return Unavailable();
        }
        double busiest = 0.0;
        for (const auto& [type, percent] : percent_by_engine_type) {
            busiest = std::max(busiest, percent);
        }
        return MakePercent(busiest);
    }

private:
    // "pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D" -> "3D"
    static std::wstring EngineType(const wchar_t* instance_name) {
        const std::wstring name = instance_name;
        constexpr wchar_t kMarker[] = L"engtype_";
        const std::size_t at = name.rfind(kMarker);
        return at == std::wstring::npos ? name : name.substr(at + (sizeof(kMarker) / sizeof(wchar_t) - 1));
    }

    void Fail(const char* call, PDH_STATUS status) {
        LOG_WARNING(Frontend, "Performance HUD: {} failed (0x{:08X}); GPU shows N/A", call,
                    static_cast<std::uint32_t>(status));
    }

    PDH_HQUERY query{};
    PDH_HCOUNTER counter{};
    bool ready{};
};

HudPercent SystemMemoryPercent() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return Unavailable();
    }
    return RatioPercent(status.ullTotalPhys - status.ullAvailPhys, status.ullTotalPhys);
}

#endif // _WIN32

} // namespace

PerfHudSampler::PerfHudSampler(Reader gpu_memory_reader_, Reader guest_memory_reader_)
    : gpu_memory_reader{std::move(gpu_memory_reader_)},
      guest_memory_reader{std::move(guest_memory_reader_)} {}

PerfHudSampler::~PerfHudSampler() {
    SetActive(false);
}

void PerfHudSampler::SetActive(bool active) {
    if (active == worker.joinable()) {
        return;
    }
    if (active) {
        {
            std::scoped_lock lock{mutex};
            stop_requested = false;
            latest = {};
        }
        worker = std::thread([this] { Run(); });
        return;
    }
    {
        std::scoped_lock lock{mutex};
        stop_requested = true;
    }
    wake.notify_all();
    worker.join();
}

PerfHudSample PerfHudSampler::Latest() const {
    std::scoped_lock lock{mutex};
    return latest;
}

void PerfHudSampler::Run() {
#ifdef _WIN32
    GpuEngineCounter gpu_counter;
    const std::uint32_t logical_cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    std::optional<std::uint64_t> previous_cpu_ticks = ProcessCpuTicks();
#endif
    auto previous_time = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(kPerfHudSampleIntervalMs);

    std::unique_lock lock{mutex};
    while (!stop_requested) {
        if (wake.wait_for(lock, interval, [this] { return stop_requested; })) {
            break;
        }
        lock.unlock();

        PerfHudSample sample;
        const auto now = std::chrono::steady_clock::now();
#ifdef _WIN32
        const std::optional<std::uint64_t> cpu_ticks = ProcessCpuTicks();
        const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous_time);
        sample.cpu = CpuPercentBetween(
            previous_cpu_ticks, cpu_ticks,
            static_cast<std::uint64_t>(wall_ns.count() / kNanosecondsPerTick), logical_cores);
        previous_cpu_ticks = cpu_ticks;
        sample.gpu = gpu_counter.Read();
        sample.system_memory = SystemMemoryPercent();
#endif
        previous_time = now;
        sample.gpu_memory = gpu_memory_reader ? gpu_memory_reader() : Unavailable();
        sample.guest_memory = guest_memory_reader ? guest_memory_reader() : Unavailable();

        lock.lock();
        latest = sample;
    }
}

} // namespace SuyuCmd
