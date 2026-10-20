// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// File output for the SUYU_BOUNDARY_INVENTORY recorder: the whole report is rewritten at most
// every kWriteIntervalNs and at shutdown, because teardown may never run.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/boundary_inventory.h"

namespace Core::BoundaryInventory {

inline std::filesystem::path ReportPath() {
    const char* explicit_path = std::getenv("SUYU_BOUNDARY_INVENTORY_PATH");
    return explicit_path && *explicit_path
               ? std::filesystem::path{explicit_path}
               : Common::FS::GetSuyuPath(Common::FS::SuyuPath::LogDir) / "boundary_inventory.txt";
}

/// Serialized so the periodic and shutdown writers never truncate the same file at once.
inline void WriteReport() {
    static std::mutex write_mutex;
    const std::lock_guard lock{write_mutex};
    const Recorder& recorder = Instance();
    const std::string report = recorder.FormatReport();
    const std::filesystem::path path = ReportPath();
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << report;
    }
    LOG_INFO(Core, "boundary inventory: {} distinct keys, written={} to {}", recorder.DistinctKeys(),
             static_cast<bool>(out),
             Common::FS::PathToUTF8String(path));
}

inline void WriteReportIfDue() {
    const auto now_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    if (Instance().TryTakeWriteSlot(now_ns)) {
        WriteReport();
    }
}

} // namespace Core::BoundaryInventory
