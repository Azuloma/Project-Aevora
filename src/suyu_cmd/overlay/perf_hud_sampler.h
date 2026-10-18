// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "suyu_cmd/overlay/perf_hud.h"

namespace SuyuCmd {

/// Samples the HUD values every kPerfHudSampleIntervalMs on its own thread, only while active.
/// CPU, GPU and SysMem come from the OS (Windows only); GMem and MMem from the owner's readers.
class PerfHudSampler {
public:
    using Reader = std::function<HudPercent()>;

    PerfHudSampler(Reader gpu_memory_reader, Reader guest_memory_reader);
    ~PerfHudSampler();

    PerfHudSampler(const PerfHudSampler&) = delete;
    PerfHudSampler& operator=(const PerfHudSampler&) = delete;

    /// Starts or stops the sampling thread; stopping joins it. Call from one thread only.
    void SetActive(bool active);

    /// The newest sample; every value is N/A until the first interval has passed.
    PerfHudSample Latest() const;

private:
    void Run();

    Reader gpu_memory_reader;
    Reader guest_memory_reader;

    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stop_requested{};
    PerfHudSample latest;
};

} // namespace SuyuCmd
