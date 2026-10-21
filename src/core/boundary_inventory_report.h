// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// File output for the SUYU_BOUNDARY_INVENTORY recorder: the open segment is rewritten at most
// every kWriteIntervalNs and at shutdown, because teardown may never run.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/string_util.h"
#include "core/boundary_inventory.h"

namespace Core::BoundaryInventory {

/// A path-valued environment variable, read as UTF-16 on Windows so a Unicode folder survives.
inline std::filesystem::path EnvPath(const char* name) {
#ifdef _WIN32
    const wchar_t* value = _wgetenv(Common::UTF8ToUTF16W(name).c_str());
    return value != nullptr && *value != L'\0' ? std::filesystem::path{value}
                                                : std::filesystem::path{};
#else
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::filesystem::path{value}
                                                : std::filesystem::path{};
#endif
}

/// SUYU_BOUNDARY_INVENTORY_DIR, else <log dir>/boundary_inventory.
inline std::filesystem::path OutputDir() {
    std::filesystem::path explicit_dir = EnvPath("SUYU_BOUNDARY_INVENTORY_DIR");
    return !explicit_dir.empty()
               ? explicit_dir
               : Common::FS::GetSuyuPath(Common::FS::SuyuPath::LogDir) / "boundary_inventory";
}

/// SUYU_BOUNDARY_INVENTORY_RUN, else "run"; file names sanitize it.
inline std::string RunLabel() {
    const char* run = std::getenv("SUYU_BOUNDARY_INVENTORY_RUN");
    return run && *run ? std::string{run} : std::string{"run"};
}

/// Writes `text` to `path`, creating its folder; on failure returns false and sets `error`.
inline bool WriteTextFile(const std::filesystem::path& path, const std::string& text,
                          std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "cannot create " + Common::FS::PathToUTF8String(path.parent_path()) + ": " +
                ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out || !(out << text) || !out.flush()) {
        error = "cannot write " + Common::FS::PathToUTF8String(path);
        return false;
    }
    return true;
}

/// Serialized so the periodic and shutdown writers never truncate the same file at once.
inline bool WriteReport(std::string* error_out = nullptr) {
    static std::mutex write_mutex;
    const std::lock_guard lock{write_mutex};
    Recorder& recorder = Instance();
    const std::string report = recorder.FormatReport();
    const std::filesystem::path path = OutputDir() / OpenReportFileName(RunLabel());
    std::string error;
    if (WriteTextFile(path, report, error)) {
        LOG_INFO(Core, "boundary inventory: {} distinct keys in the open segment, written to {}",
                 recorder.DistinctKeys(), Common::FS::PathToUTF8String(path));
        return true;
    }
    LOG_ERROR(Core, "boundary inventory: {}", error);
    if (error_out != nullptr) {
        *error_out = error;
    }
    return false;
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
