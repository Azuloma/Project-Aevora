// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SUYU_BOUNDARY_INVENTORY=1 diagnostic (N0): records which SVCs, IPC commands, nvdrv ioctls, audio
// renderer parameters, applets and files the guest uses. OS-free; hooks only call it when Enabled().

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>

#include "common/env_option.h"

namespace Core::BoundaryInventory {

constexpr std::size_t kMaxCores = 8;
constexpr std::size_t kSvcSlots = 256;
constexpr std::size_t kMaxKeysPerSection = 65536;
constexpr std::uint64_t kWriteIntervalNs = 10'000'000'000ULL;
constexpr std::string_view kUnknownDevice = "<unknown fd>";

enum class FsKind : std::uint32_t {
    RomFsCurrentProcess,
    DataStorageById,
    SaveData,
    Sdmc,
    Bis,
    Other,
};

constexpr std::string_view FsKindName(FsKind kind) {
    switch (kind) {
    case FsKind::RomFsCurrentProcess:
        return "romfs_current_process";
    case FsKind::DataStorageById:
        return "data_storage_by_id";
    case FsKind::SaveData:
        return "save_data";
    case FsKind::Sdmc:
        return "sdmc";
    case FsKind::Bis:
        return "bis";
    case FsKind::Other:
        break;
    }
    return "other";
}

/// Off unless SUYU_BOUNDARY_INVENTORY is exactly "1"; read once per process.
inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("SUYU_BOUNDARY_INVENTORY");
        return value && Common::EnvFlagDefaultOff(value);
    }();
    return enabled;
}

namespace Detail {

/// Mutex-guarded map with a key cap; first_seq orders keys by first appearance in the section.
template <typename Key, typename Value>
class Table {
public:
    template <typename Fn>
    void Update(const Key& key, Fn&& fn) {
        std::lock_guard lock{mutex_};
        auto it = map_.find(key);
        if (it == map_.end()) {
            if (map_.size() >= kMaxKeysPerSection) {
                ++dropped_;
                return;
            }
            it = map_.emplace(key, Value{}).first;
            it->second.first_seq = next_seq_++;
        }
        fn(it->second);
    }

    template <typename Fn>
    void ForEach(Fn&& fn) const {
        std::lock_guard lock{mutex_};
        for (const auto& [key, value] : map_) {
            fn(key, value);
        }
    }

    std::uint64_t Dropped() const {
        std::lock_guard lock{mutex_};
        return dropped_;
    }

private:
    mutable std::mutex mutex_;
    std::map<Key, Value> map_;
    std::uint64_t next_seq_ = 0;
    std::uint64_t dropped_ = 0;
};

struct CountEntry {
    std::uint64_t first_seq = 0;
    std::uint64_t count = 0;
};

struct IpcEntry : CountEntry {
    bool unimplemented = false;
};

struct AudioUpdateEntry : CountEntry {
    std::uint64_t min_input = UINT64_MAX;
    std::uint64_t max_input = 0;
    std::uint64_t max_output = 0;
    std::uint64_t max_performance = 0;
};

struct FsReadEntry : CountEntry {
    std::uint64_t total_bytes = 0;
    std::uint64_t max_end_offset = 0;
};

struct alignas(64) CoreSvcCounters {
    std::array<std::atomic<std::uint64_t>, kSvcSlots> calls{};
};

inline void AppendU64(std::string& out, std::uint64_t value) {
    out += std::to_string(value);
}

} // namespace Detail

class Recorder {
public:
    /// Single writer per core: a plain load and store. Anything outside the table is overflow.
    void RecordSvc(std::size_t core_index, std::uint32_t svc_number) {
        if (core_index >= kMaxCores || svc_number >= kSvcSlots) {
            svc_overflow_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto& slot = svc_[core_index].calls[svc_number];
        slot.store(slot.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    void RecordIpc(std::string_view service, std::uint32_t command_type, std::uint32_t command_id) {
        ipc_.Update(IpcKey{std::string{service}, command_type, command_id},
                    [](Detail::IpcEntry& entry) { ++entry.count; });
    }

    void MarkIpcUnimplemented(std::string_view service, std::uint32_t command_type,
                              std::uint32_t command_id) {
        ipc_.Update(IpcKey{std::string{service}, command_type, command_id},
                    [](Detail::IpcEntry& entry) { entry.unimplemented = true; });
    }

    void RecordNvdrvOpen(std::int32_t fd, std::string_view device_name) {
        {
            std::lock_guard lock{nvdrv_fd_mutex_};
            nvdrv_fd_names_[fd] = std::string{device_name};
        }
        nvdrv_opens_.Update(std::string{device_name}, [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordNvdrvClose(std::int32_t fd) {
        std::lock_guard lock{nvdrv_fd_mutex_};
        nvdrv_fd_names_.erase(fd);
    }

    void RecordNvdrvIoctl(std::int32_t fd, std::uint32_t variant, std::uint32_t ioctl) {
        std::string device_name{kUnknownDevice};
        {
            std::lock_guard lock{nvdrv_fd_mutex_};
            if (const auto it = nvdrv_fd_names_.find(fd); it != nvdrv_fd_names_.end()) {
                device_name = it->second;
            }
        }
        nvdrv_ioctls_.Update(NvdrvKey{std::move(device_name), variant, ioctl},
                             [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordAudioOpen(std::string_view parameters) {
        audio_opens_.Update(std::string{parameters}, [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordAudioUpdate(std::uint64_t input_bytes, std::uint64_t output_bytes,
                           std::uint64_t performance_bytes) {
        audio_updates_.Update(0, [&](Detail::AudioUpdateEntry& entry) {
            ++entry.count;
            entry.min_input = std::min(entry.min_input, input_bytes);
            entry.max_input = std::max(entry.max_input, input_bytes);
            entry.max_output = std::max(entry.max_output, output_bytes);
            entry.max_performance = std::max(entry.max_performance, performance_bytes);
        });
    }

    void RecordApplet(std::uint32_t applet_id) {
        applets_.Update(applet_id, [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordFsOpen(FsKind kind, std::string_view path, std::uint32_t mode) {
        fs_opens_.Update(FsOpenKey{static_cast<std::uint32_t>(kind), std::string{path}, mode},
                         [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordFsRead(std::string_view path, std::uint64_t offset, std::uint64_t size) {
        fs_reads_.Update(std::string{path}, [&](Detail::FsReadEntry& entry) {
            ++entry.count;
            entry.total_bytes += size;
            entry.max_end_offset = std::max(entry.max_end_offset, offset + size);
        });
    }

    /// True for the first caller after kWriteIntervalNs since the last slot; false otherwise.
    bool TryTakeWriteSlot(std::uint64_t now_ns) {
        std::uint64_t last = last_write_ns_.load(std::memory_order_relaxed);
        if (last != kNeverWritten && now_ns - last < kWriteIntervalNs) {
            return false;
        }
        return last_write_ns_.compare_exchange_strong(last, now_ns, std::memory_order_relaxed);
    }

    /// Sorted, tab-separated snapshot; the same state always formats to the same text.
    std::string FormatReport() const {
        std::string out = "# boundary inventory v1\n";
        FormatSvc(out);
        FormatIpc(out);
        FormatNvdrv(out);
        FormatAudio(out);
        FormatApplets(out);
        FormatFs(out);
        return out;
    }

    std::uint64_t DistinctKeys() const {
        std::uint64_t svcs = 0;
        for (std::size_t n = 0; n < kSvcSlots; ++n) {
            svcs += SvcTotal(n) != 0;
        }
        std::uint64_t keys = svcs;
        const auto count = [&keys](const auto&, const auto&) { ++keys; };
        ipc_.ForEach(count);
        nvdrv_opens_.ForEach(count);
        nvdrv_ioctls_.ForEach(count);
        audio_opens_.ForEach(count);
        audio_updates_.ForEach(count);
        applets_.ForEach(count);
        fs_opens_.ForEach(count);
        fs_reads_.ForEach(count);
        return keys;
    }

private:
    using IpcKey = std::tuple<std::string, std::uint32_t, std::uint32_t>;
    using NvdrvKey = std::tuple<std::string, std::uint32_t, std::uint32_t>;
    using FsOpenKey = std::tuple<std::uint32_t, std::string, std::uint32_t>;
    static constexpr std::uint64_t kNeverWritten = UINT64_MAX;

    std::uint64_t SvcTotal(std::size_t svc_number) const {
        std::uint64_t total = 0;
        for (const auto& core : svc_) {
            total += core.calls[svc_number].load(std::memory_order_relaxed);
        }
        return total;
    }

    static void AppendHex(std::string& out, std::uint32_t value) {
        char text[16];
        std::snprintf(text, sizeof text, "0x%02X", value);
        out += text;
    }

    void FormatSvc(std::string& out) const {
        out += "[svc]\n";
        for (std::size_t n = 0; n < kSvcSlots; ++n) {
            if (const auto total = SvcTotal(n); total != 0) {
                AppendHex(out, static_cast<std::uint32_t>(n));
                out += '\t';
                Detail::AppendU64(out, total);
                out += '\n';
            }
        }
        out += "overflow\t";
        Detail::AppendU64(out, svc_overflow_.load(std::memory_order_relaxed));
        out += '\n';
    }

    void FormatIpc(std::string& out) const {
        out += "[ipc]\n";
        ipc_.ForEach([&out](const IpcKey& key, const Detail::IpcEntry& entry) {
            out += std::get<0>(key) + '\t' + std::to_string(std::get<1>(key)) + '\t' +
                   std::to_string(std::get<2>(key)) + '\t' + std::to_string(entry.count) + '\t' +
                   (entry.unimplemented ? '1' : '0') + '\t' + std::to_string(entry.first_seq) +
                   '\n';
        });
        AppendDropped(out, ipc_.Dropped());
    }

    void FormatNvdrv(std::string& out) const {
        out += "[nvdrv]\n";
        nvdrv_opens_.ForEach([&out](const std::string& name, const Detail::CountEntry& entry) {
            out += "open\t" + name + "\t\t" + std::to_string(entry.count) + '\t' +
                   std::to_string(entry.first_seq) + '\n';
        });
        nvdrv_ioctls_.ForEach([&out](const NvdrvKey& key, const Detail::CountEntry& entry) {
            out += "ioctl\t" + std::get<0>(key) + '\t' + std::to_string(std::get<1>(key)) + '\t';
            AppendHex32(out, std::get<2>(key));
            out += '\t' + std::to_string(entry.count) + '\t' + std::to_string(entry.first_seq) +
                   '\n';
        });
        AppendDropped(out, nvdrv_opens_.Dropped() + nvdrv_ioctls_.Dropped());
    }

    void FormatAudio(std::string& out) const {
        out += "[audio]\n";
        audio_opens_.ForEach([&out](const std::string& parameters, const Detail::CountEntry& entry) {
            out += "open\t" + parameters + '\t' + std::to_string(entry.count) + '\n';
        });
        audio_updates_.ForEach([&out](int, const Detail::AudioUpdateEntry& entry) {
            out += "update\tcount=" + std::to_string(entry.count) +
                   "\tinput_min=" + std::to_string(entry.min_input) +
                   "\tinput_max=" + std::to_string(entry.max_input) +
                   "\toutput_max=" + std::to_string(entry.max_output) +
                   "\tperformance_max=" + std::to_string(entry.max_performance) + '\n';
        });
        AppendDropped(out, audio_opens_.Dropped());
    }

    void FormatApplets(std::string& out) const {
        out += "[applet]\n";
        applets_.ForEach([&out](std::uint32_t id, const Detail::CountEntry& entry) {
            out += std::to_string(id) + '\t' + std::to_string(entry.count) + '\t' +
                   std::to_string(entry.first_seq) + '\n';
        });
        AppendDropped(out, applets_.Dropped());
    }

    void FormatFs(std::string& out) const {
        out += "[fs-open]\n";
        fs_opens_.ForEach([&out](const FsOpenKey& key, const Detail::CountEntry& entry) {
            out += std::string{FsKindName(static_cast<FsKind>(std::get<0>(key)))} + '\t' +
                   std::get<1>(key) + '\t' + std::to_string(std::get<2>(key)) + '\t' +
                   std::to_string(entry.count) + '\t' + std::to_string(entry.first_seq) + '\n';
        });
        AppendDropped(out, fs_opens_.Dropped());
        out += "[fs-read]\n";
        fs_reads_.ForEach([&out](const std::string& path, const Detail::FsReadEntry& entry) {
            out += path + '\t' + std::to_string(entry.count) + '\t' +
                   std::to_string(entry.total_bytes) + '\t' +
                   std::to_string(entry.max_end_offset) + '\t' + std::to_string(entry.first_seq) +
                   '\n';
        });
        AppendDropped(out, fs_reads_.Dropped());
    }

    static void AppendHex32(std::string& out, std::uint32_t value) {
        char text[16];
        std::snprintf(text, sizeof text, "0x%08X", value);
        out += text;
    }

    static void AppendDropped(std::string& out, std::uint64_t dropped) {
        if (dropped != 0) {
            out += "dropped\t" + std::to_string(dropped) + '\n';
        }
    }

    std::array<Detail::CoreSvcCounters, kMaxCores> svc_{};
    std::atomic<std::uint64_t> svc_overflow_{0};
    Detail::Table<IpcKey, Detail::IpcEntry> ipc_;
    std::mutex nvdrv_fd_mutex_;
    std::map<std::int32_t, std::string> nvdrv_fd_names_;
    Detail::Table<std::string, Detail::CountEntry> nvdrv_opens_;
    Detail::Table<NvdrvKey, Detail::CountEntry> nvdrv_ioctls_;
    Detail::Table<std::string, Detail::CountEntry> audio_opens_;
    Detail::Table<int, Detail::AudioUpdateEntry> audio_updates_;
    Detail::Table<std::uint32_t, Detail::CountEntry> applets_;
    Detail::Table<FsOpenKey, Detail::CountEntry> fs_opens_;
    Detail::Table<std::string, Detail::FsReadEntry> fs_reads_;
    std::atomic<std::uint64_t> last_write_ns_{kNeverWritten};
};

inline Recorder& Instance() {
    static Recorder recorder;
    return recorder;
}

} // namespace Core::BoundaryInventory
