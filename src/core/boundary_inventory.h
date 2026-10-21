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
#include <vector>

#include "common/env_option.h"

namespace Core::BoundaryInventory {

constexpr std::size_t kMaxCores = 8;
constexpr std::size_t kSvcSlots = 256;
constexpr std::size_t kMaxKeysPerSection = 65536;
constexpr std::uint64_t kWriteIntervalNs = 10'000'000'000ULL;
constexpr std::uint64_t kNsPerMs = 1'000'000ULL;
constexpr std::size_t kMaxFileComponentLength = 48;
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

/// Keeps ASCII letters, digits, '-' and '_' (anything else becomes '_'), at most
/// kMaxFileComponentLength characters; empty input gives `fallback`.
inline std::string SanitizeFileComponent(std::string_view text, std::string_view fallback) {
    std::string out;
    for (const char c : text) {
        if (out.size() >= kMaxFileComponentLength) {
            break;
        }
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_';
        out += plain ? c : '_';
    }
    return out.empty() ? std::string{fallback} : out;
}

/// inventory_<run>_<serial, two digits>_<label>.txt, one file per ended segment.
inline std::string SegmentFileName(std::string_view run, std::uint32_t serial,
                                   std::string_view label) {
    char number[16];
    std::snprintf(number, sizeof number, "%02u", serial);
    return "inventory_" + SanitizeFileComponent(run, "run") + '_' + number + '_' +
           SanitizeFileComponent(label, "segment") + ".txt";
}

/// inventory_<run>_open.txt, the auto-saved open segment.
inline std::string OpenReportFileName(std::string_view run) {
    return "inventory_" + SanitizeFileComponent(run, "run") + "_open.txt";
}

constexpr std::size_t kCallerFrames = 4;   ///< the SVC site plus three callers up the frame chain
constexpr std::size_t kCallersPerKey = 8;
constexpr std::size_t kMaxIpcStashThreads = 1024;

using CallerFrames = std::array<std::uint64_t, kCallerFrames>;

/// True for `svc #svc_number` (A64 SVC encoding).
constexpr bool IsSvcInstruction(std::uint32_t instruction, std::uint32_t svc_number) {
    return (instruction & 0xFFE0001Fu) == 0xD4000001u && ((instruction >> 5) & 0xFFFFu) == svc_number;
}

/// SendSyncRequest (0x21) and SendSyncRequestWithUserBuffer (0x22) start a blocking IPC request.
constexpr bool IsIpcSendSvc(std::uint32_t svc_number) {
    return svc_number == 0x21 || svc_number == 0x22;
}

/// [callers] key of one IPC command, for example "ipc:hid:4:100".
inline std::string IpcCallerKey(std::string_view service, std::uint32_t command_type,
                                std::uint32_t command_id) {
    return "ipc:" + std::string{service} + ':' + std::to_string(command_type) + ':' +
           std::to_string(command_id);
}

/// The SVC site, LR, then return addresses up the x29 chain; `read(address, value)` reads guest
/// memory. The walk stops at an invalid, null, misaligned, or non-ascending frame.
template <typename Read>
CallerFrames CollectCallerFrames(std::uint64_t site, std::uint64_t link_register,
                                 std::uint64_t frame_pointer, Read&& read) {
    CallerFrames frames{};
    frames[0] = site;
    frames[1] = link_register;
    std::uint64_t fp = frame_pointer;
    for (std::size_t i = 2; i < kCallerFrames; ++i) {
        std::uint64_t next_fp = 0;
        std::uint64_t return_address = 0;
        if (fp == 0 || (fp & 0xF) != 0 || !read(fp, next_fp) || !read(fp + 8, return_address) ||
            return_address == 0) {
            break;
        }
        frames[i] = return_address;
        if (next_fp <= fp) {
            break;
        }
        fp = next_fp;
    }
    return frames;
}

constexpr std::uint64_t kMemoryPageBytes = 0x1000;

/// Smallest power-of-two page count that holds `bytes`: "0", "<=1p", "<=2p", "<=4p", ...
inline std::string SizeClass(std::uint64_t bytes) {
    if (bytes == 0) {
        return "0";
    }
    std::uint64_t pages = (bytes + kMemoryPageBytes - 1) / kMemoryPageBytes;
    std::uint64_t bound = 1;
    while (bound < pages) {
        bound <<= 1;
    }
    return "<=" + std::to_string(bound) + 'p';
}

/// A named guest address range, for classifying the arguments of memory SVCs.
struct RegionRange {
    std::string_view name;
    std::uint64_t start{};
    std::uint64_t size{};
};

/// Name of the first range holding `address`, else "other".
inline std::string_view RegionName(std::uint64_t address, const std::vector<RegionRange>& ranges) {
    for (const RegionRange& range : ranges) {
        if (address >= range.start && address - range.start < range.size) {
            return range.name;
        }
    }
    return "other";
}

/// [memory] key of one memory SVC, for example "svc:0x04 dst=alias src=heap size=<=16p".
inline std::string MemorySvcKey(std::uint32_t svc_number, std::string_view detail) {
    char head[16];
    std::snprintf(head, sizeof head, "svc:0x%02X ", svc_number);
    return head + std::string{detail};
}

/// One loaded module: [base, end) in guest addresses.
struct ModuleRange {
    std::string name;
    std::uint64_t base{};
    std::uint64_t end{};
};

/// Off unless SUYU_BOUNDARY_INVENTORY is exactly "1"; read once at process start.
inline const bool kEnabled = [] {
    const char* value = std::getenv("SUYU_BOUNDARY_INVENTORY");
    return value && Common::EnvFlagDefaultOff(value);
}();

inline bool Enabled() {
    return kEnabled;
}

namespace Detail {

/// Mutex-guarded map with a key cap; first_seq orders keys by first appearance in the section.
template <typename Key, typename Value>
class Table {
public:
    struct Contents {
        std::map<Key, Value> entries;
        std::uint64_t dropped = 0;
    };

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

    Contents Copy() const {
        std::lock_guard lock{mutex_};
        return Contents{map_, dropped_};
    }

    /// Moves the rows out and starts a new segment: rows, drop count, and ordering are reset.
    Contents Take() {
        std::lock_guard lock{mutex_};
        Contents taken{std::move(map_), dropped_};
        map_.clear();
        next_seq_ = 0;
        dropped_ = 0;
        return taken;
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

struct MemoryEntry : CountEntry {
    std::uint64_t min_bytes = UINT64_MAX;
    std::uint64_t max_bytes = 0;
};

struct CallerEntry {
    CallerFrames frames{};
    std::uint64_t count = 0;
};

/// The callers of one SVC or IPC command: the most frequent few, and what was evicted.
struct CallerGroup : CountEntry {
    std::vector<CallerEntry> entries;
    std::uint64_t other = 0; ///< calls of callers that were evicted or never kept
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
    /// While paused the counters stand still; the nvdrv fd-to-device map keeps following the guest.
    void SetRecording(bool recording) {
        recording_.store(recording, std::memory_order_relaxed);
    }

    bool IsRecording() const {
        return recording_.load(std::memory_order_relaxed);
    }

    /// Single writer per core: a plain load and store. Anything outside the table is overflow.
    void RecordSvc(std::size_t core_index, std::uint32_t svc_number) {
        if (!IsRecording()) {
            return;
        }
        if (core_index >= kMaxCores || svc_number >= kSvcSlots) {
            svc_overflow_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto& slot = svc_[core_index].calls[svc_number];
        slot.store(slot.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    /// One request: the count and the unimplemented flag land in the same row and segment.
    void RecordIpc(std::string_view service, std::uint32_t command_type, std::uint32_t command_id,
                   bool unimplemented = false) {
        if (!IsRecording()) {
            return;
        }
        ipc_.Update(IpcKey{std::string{service}, command_type, command_id},
                    [unimplemented](Detail::IpcEntry& entry) {
                        ++entry.count;
                        entry.unimplemented = entry.unimplemented || unimplemented;
                    });
    }

    void RecordNvdrvOpen(std::int32_t fd, std::string_view device_name) {
        {
            std::lock_guard lock{nvdrv_fd_mutex_};
            nvdrv_fd_names_[fd] = std::string{device_name};
        }
        if (!IsRecording()) {
            return;
        }
        nvdrv_opens_.Update(std::string{device_name}, [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordNvdrvClose(std::int32_t fd) {
        std::lock_guard lock{nvdrv_fd_mutex_};
        nvdrv_fd_names_.erase(fd);
    }

    void RecordNvdrvIoctl(std::int32_t fd, std::uint32_t variant, std::uint32_t ioctl) {
        if (!IsRecording()) {
            return;
        }
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
        if (!IsRecording()) {
            return;
        }
        audio_opens_.Update(std::string{parameters}, [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordAudioUpdate(std::uint64_t input_bytes, std::uint64_t output_bytes,
                           std::uint64_t performance_bytes) {
        if (!IsRecording()) {
            return;
        }
        audio_updates_.Update(0, [&](Detail::AudioUpdateEntry& entry) {
            ++entry.count;
            entry.min_input = std::min(entry.min_input, input_bytes);
            entry.max_input = std::max(entry.max_input, input_bytes);
            entry.max_output = std::max(entry.max_output, output_bytes);
            entry.max_performance = std::max(entry.max_performance, performance_bytes);
        });
    }

    void RecordApplet(std::uint32_t applet_id) {
        if (!IsRecording()) {
            return;
        }
        applets_.Update(applet_id, [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordFsOpen(FsKind kind, std::string_view path, std::uint32_t mode) {
        if (!IsRecording()) {
            return;
        }
        fs_opens_.Update(FsOpenKey{static_cast<std::uint32_t>(kind), std::string{path}, mode},
                         [](Detail::CountEntry& entry) { ++entry.count; });
    }

    void RecordFsRead(std::string_view path, std::uint64_t offset, std::uint64_t size) {
        if (!IsRecording()) {
            return;
        }
        fs_reads_.Update(std::string{path}, [&](Detail::FsReadEntry& entry) {
            ++entry.count;
            entry.total_bytes += size;
            entry.max_end_offset = std::max(entry.max_end_offset, offset + size);
        });
    }

    /// One guest memory operation: `key` describes its shape, `bytes` the size (0 when it has none).
    void RecordMemory(std::string_view key, std::uint64_t bytes) {
        if (!IsRecording()) {
            return;
        }
        memory_.Update(std::string{key}, [bytes](Detail::MemoryEntry& entry) {
            ++entry.count;
            entry.min_bytes = std::min(entry.min_bytes, bytes);
            entry.max_bytes = std::max(entry.max_bytes, bytes);
        });
    }

    /// The loaded modules, so caller addresses print as name+0xRVA; replaces any earlier list.
    void SetModules(std::vector<ModuleRange> modules) {
        std::lock_guard lock{modules_mutex_};
        modules_ = std::move(modules);
    }

    /// True for the first caller only: whoever gets it fills in the module list.
    bool ClaimModuleScan() {
        return !module_scan_claimed_.exchange(true, std::memory_order_relaxed);
    }

    /// `name+0xRVA` inside a module, `?0xADDRESS` outside every module, `-` for 0.
    std::string Describe(std::uint64_t address) const {
        if (address == 0) {
            return "-";
        }
        char offset[32];
        {
            std::lock_guard lock{modules_mutex_};
            for (const ModuleRange& module : modules_) {
                if (address >= module.base && address < module.end) {
                    std::snprintf(offset, sizeof offset, "+0x%llX",
                                  static_cast<unsigned long long>(address - module.base));
                    return module.name + offset;
                }
            }
        }
        std::snprintf(offset, sizeof offset, "?0x%llX", static_cast<unsigned long long>(address));
        return offset;
    }

    /// One call of `key` (for example "svc:0x21") made from `frames`. Each key keeps its
    /// kCallersPerKey most frequent callers; the least frequent is evicted into `other`.
    void RecordCaller(std::string_view key, const CallerFrames& frames) {
        if (!IsRecording()) {
            return;
        }
        callers_.Update(std::string{key}, [&frames](Detail::CallerGroup& group) {
            ++group.count;
            for (Detail::CallerEntry& entry : group.entries) {
                if (entry.frames == frames) {
                    ++entry.count;
                    return;
                }
            }
            if (group.entries.size() < kCallersPerKey) {
                group.entries.push_back({frames, 1});
                return;
            }
            auto fewest = std::min_element(
                group.entries.begin(), group.entries.end(),
                [](const Detail::CallerEntry& a, const Detail::CallerEntry& b) {
                    return a.count < b.count;
                });
            group.other += fewest->count;
            *fewest = {frames, 1};
        });
    }

    /// Remembers the guest caller of the SVC that sends an IPC request; the service thread
    /// that handles it reads it by guest thread id. A newer stash replaces an unread one.
    void StashIpcCaller(std::uint64_t thread_id, const CallerFrames& frames) {
        if (!IsRecording()) {
            return;
        }
        std::lock_guard lock{ipc_stash_mutex_};
        if (ipc_stash_.size() >= kMaxIpcStashThreads && ipc_stash_.find(thread_id) == ipc_stash_.end()) {
            ipc_stash_.clear();
        }
        ipc_stash_[thread_id] = frames;
    }

    /// Records the stashed caller of `thread_id` under IpcCallerKey and consumes the stash;
    /// a request with no stash is counted in no row.
    void RecordIpcCaller(std::uint64_t thread_id, std::string_view service,
                         std::uint32_t command_type, std::uint32_t command_id) {
        CallerFrames frames{};
        {
            std::lock_guard lock{ipc_stash_mutex_};
            const auto it = ipc_stash_.find(thread_id);
            if (it == ipc_stash_.end()) {
                return;
            }
            frames = it->second;
            ipc_stash_.erase(it);
        }
        RecordCaller(IpcCallerKey(service, command_type, command_id), frames);
    }

    /// True for the first caller after kWriteIntervalNs since the last slot; false otherwise.
    bool TryTakeWriteSlot(std::uint64_t now_ns) {
        std::uint64_t last = last_write_ns_.load(std::memory_order_relaxed);
        // A sample older than the published slot is stale, not a full interval ahead.
        if (last != kNeverWritten && (now_ns < last || now_ns - last < kWriteIntervalNs)) {
            return false;
        }
        return last_write_ns_.compare_exchange_strong(last, now_ns, std::memory_order_relaxed);
    }

    /// The open segment (everything since the last EndSegment) as sorted, tab-separated text.
    std::string FormatReport() {
        return FormatSnapshot(Capture(false), {});
    }

    /// Closes the open segment: returns its text and starts an empty one. The label and
    /// elapsed time only describe the segment; the caller owns the clock and the file.
    std::string EndSegment(std::string_view label, std::uint64_t elapsed_ns) {
        std::string header = "# segment label=";
        header += label;
        header += " elapsed_ms=" + std::to_string(elapsed_ns / kNsPerMs) + '\n';
        return FormatSnapshot(Capture(true), header);
    }

    std::uint64_t DistinctKeys() {
        const Snapshot snapshot = Capture(false);
        std::uint64_t keys = 0;
        for (const auto count : snapshot.svc) {
            keys += count != 0;
        }
        keys += snapshot.ipc.entries.size() + snapshot.nvdrv_opens.entries.size() +
                snapshot.nvdrv_ioctls.entries.size() + snapshot.audio_opens.entries.size() +
                snapshot.audio_updates.entries.size() + snapshot.applets.entries.size() +
                snapshot.fs_opens.entries.size() + snapshot.fs_reads.entries.size() +
                snapshot.memory.entries.size();
        return keys;
    }

private:
    using IpcKey = std::tuple<std::string, std::uint32_t, std::uint32_t>;
    using NvdrvKey = std::tuple<std::string, std::uint32_t, std::uint32_t>;
    using FsOpenKey = std::tuple<std::uint32_t, std::string, std::uint32_t>;
    static constexpr std::uint64_t kNeverWritten = UINT64_MAX;

    struct Snapshot {
        std::array<std::uint64_t, kSvcSlots> svc{};
        std::uint64_t svc_overflow = 0;
        Detail::Table<IpcKey, Detail::IpcEntry>::Contents ipc;
        Detail::Table<std::string, Detail::CountEntry>::Contents nvdrv_opens;
        Detail::Table<NvdrvKey, Detail::CountEntry>::Contents nvdrv_ioctls;
        Detail::Table<std::string, Detail::CountEntry>::Contents audio_opens;
        Detail::Table<int, Detail::AudioUpdateEntry>::Contents audio_updates;
        Detail::Table<std::uint32_t, Detail::CountEntry>::Contents applets;
        Detail::Table<FsOpenKey, Detail::CountEntry>::Contents fs_opens;
        Detail::Table<std::string, Detail::FsReadEntry>::Contents fs_reads;
        Detail::Table<std::string, Detail::CallerGroup>::Contents callers;
        Detail::Table<std::string, Detail::MemoryEntry>::Contents memory;
    };

    std::uint64_t SvcTotal(std::size_t svc_number) const {
        std::uint64_t total = 0;
        for (const auto& core : svc_) {
            total += core.calls[svc_number].load(std::memory_order_relaxed);
        }
        return total;
    }

    /// SVC counts are the totals minus the baseline, each total read once so no call is lost or
    /// counted twice across segments; tables are copied, or moved out when the segment ends.
    Snapshot Capture(bool end_segment) {
        Snapshot snapshot;
        std::lock_guard lock{segment_mutex_};
        std::array<std::uint64_t, kSvcSlots> totals{};
        for (std::size_t n = 0; n < kSvcSlots; ++n) {
            totals[n] = SvcTotal(n);
            snapshot.svc[n] = totals[n] - svc_baseline_[n];
        }
        const std::uint64_t overflow_total = svc_overflow_.load(std::memory_order_relaxed);
        snapshot.svc_overflow = overflow_total - svc_overflow_baseline_;
        if (end_segment) {
            svc_baseline_ = totals;
            svc_overflow_baseline_ = overflow_total;
        }
        const auto grab = [end_segment](auto& table) {
            return end_segment ? table.Take() : table.Copy();
        };
        snapshot.ipc = grab(ipc_);
        snapshot.nvdrv_opens = grab(nvdrv_opens_);
        snapshot.nvdrv_ioctls = grab(nvdrv_ioctls_);
        snapshot.audio_opens = grab(audio_opens_);
        snapshot.audio_updates = grab(audio_updates_);
        snapshot.applets = grab(applets_);
        snapshot.fs_opens = grab(fs_opens_);
        snapshot.fs_reads = grab(fs_reads_);
        snapshot.callers = grab(callers_);
        snapshot.memory = grab(memory_);
        return snapshot;
    }

    static void AppendHex(std::string& out, std::uint32_t value) {
        char text[16];
        std::snprintf(text, sizeof text, "0x%02X", value);
        out += text;
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

    std::string FormatSnapshot(const Snapshot& snapshot, std::string_view header_extra) const {
        std::string out = "# boundary inventory v1\n";
        out += header_extra;
        FormatSvc(out, snapshot);
        FormatIpc(out, snapshot);
        FormatNvdrv(out, snapshot);
        FormatAudio(out, snapshot);
        FormatApplets(out, snapshot);
        FormatFs(out, snapshot);
        FormatCallers(out, snapshot);
        FormatMemory(out, snapshot);
        return out;
    }

    /// A row per key: count, smallest and largest size in bytes, first-seen order.
    static void FormatMemory(std::string& out, const Snapshot& snapshot) {
        out += "[memory]\n";
        for (const auto& [key, entry] : snapshot.memory.entries) {
            out += key + '\t' + std::to_string(entry.count) + '\t' +
                   std::to_string(entry.min_bytes) + '\t' + std::to_string(entry.max_bytes) +
                   '\t' + std::to_string(entry.first_seq) + '\n';
        }
        AppendDropped(out, snapshot.memory.dropped);
    }

    /// A line per key (total, evicted calls), then a line per kept caller: count and the frames.
    void FormatCallers(std::string& out, const Snapshot& snapshot) const {
        out += "[callers]\n";
        for (const auto& [key, group] : snapshot.callers.entries) {
            out += key + '\t' + std::to_string(group.count) + "\tother=" +
                   std::to_string(group.other) + '\n';
            std::vector<const Detail::CallerEntry*> ordered;
            for (const Detail::CallerEntry& entry : group.entries) {
                ordered.push_back(&entry);
            }
            std::sort(ordered.begin(), ordered.end(),
                      [](const Detail::CallerEntry* a, const Detail::CallerEntry* b) {
                          return a->count != b->count ? a->count > b->count
                                                      : a->frames < b->frames;
                      });
            for (const Detail::CallerEntry* entry : ordered) {
                out += '\t' + std::to_string(entry->count);
                for (const std::uint64_t frame : entry->frames) {
                    out += '\t' + Describe(frame);
                }
                out += '\n';
            }
        }
        AppendDropped(out, snapshot.callers.dropped);
    }

    static void FormatSvc(std::string& out, const Snapshot& snapshot) {
        out += "[svc]\n";
        for (std::size_t n = 0; n < kSvcSlots; ++n) {
            if (snapshot.svc[n] != 0) {
                AppendHex(out, static_cast<std::uint32_t>(n));
                out += '\t';
                Detail::AppendU64(out, snapshot.svc[n]);
                out += '\n';
            }
        }
        out += "overflow\t";
        Detail::AppendU64(out, snapshot.svc_overflow);
        out += '\n';
    }

    static void FormatIpc(std::string& out, const Snapshot& snapshot) {
        out += "[ipc]\n";
        for (const auto& [key, entry] : snapshot.ipc.entries) {
            out += std::get<0>(key) + '\t' + std::to_string(std::get<1>(key)) + '\t' +
                   std::to_string(std::get<2>(key)) + '\t' + std::to_string(entry.count) + '\t' +
                   (entry.unimplemented ? '1' : '0') + '\t' + std::to_string(entry.first_seq) +
                   '\n';
        }
        AppendDropped(out, snapshot.ipc.dropped);
    }

    static void FormatNvdrv(std::string& out, const Snapshot& snapshot) {
        out += "[nvdrv]\n";
        for (const auto& [name, entry] : snapshot.nvdrv_opens.entries) {
            out += "open\t" + name + "\t\t" + std::to_string(entry.count) + '\t' +
                   std::to_string(entry.first_seq) + '\n';
        }
        for (const auto& [key, entry] : snapshot.nvdrv_ioctls.entries) {
            out += "ioctl\t" + std::get<0>(key) + '\t' + std::to_string(std::get<1>(key)) + '\t';
            AppendHex32(out, std::get<2>(key));
            out += '\t' + std::to_string(entry.count) + '\t' + std::to_string(entry.first_seq) +
                   '\n';
        }
        AppendDropped(out, snapshot.nvdrv_opens.dropped + snapshot.nvdrv_ioctls.dropped);
    }

    static void FormatAudio(std::string& out, const Snapshot& snapshot) {
        out += "[audio]\n";
        for (const auto& [parameters, entry] : snapshot.audio_opens.entries) {
            out += "open\t" + parameters + '\t' + std::to_string(entry.count) + '\n';
        }
        for (const auto& [unused_key, entry] : snapshot.audio_updates.entries) {
            out += "update\tcount=" + std::to_string(entry.count) +
                   "\tinput_min=" + std::to_string(entry.min_input) +
                   "\tinput_max=" + std::to_string(entry.max_input) +
                   "\toutput_max=" + std::to_string(entry.max_output) +
                   "\tperformance_max=" + std::to_string(entry.max_performance) + '\n';
        }
        AppendDropped(out, snapshot.audio_opens.dropped);
    }

    static void FormatApplets(std::string& out, const Snapshot& snapshot) {
        out += "[applet]\n";
        for (const auto& [id, entry] : snapshot.applets.entries) {
            out += std::to_string(id) + '\t' + std::to_string(entry.count) + '\t' +
                   std::to_string(entry.first_seq) + '\n';
        }
        AppendDropped(out, snapshot.applets.dropped);
    }

    static void FormatFs(std::string& out, const Snapshot& snapshot) {
        out += "[fs-open]\n";
        for (const auto& [key, entry] : snapshot.fs_opens.entries) {
            out += std::string{FsKindName(static_cast<FsKind>(std::get<0>(key)))} + '\t' +
                   std::get<1>(key) + '\t' + std::to_string(std::get<2>(key)) + '\t' +
                   std::to_string(entry.count) + '\t' + std::to_string(entry.first_seq) + '\n';
        }
        AppendDropped(out, snapshot.fs_opens.dropped);
        out += "[fs-read]\n";
        for (const auto& [path, entry] : snapshot.fs_reads.entries) {
            out += path + '\t' + std::to_string(entry.count) + '\t' +
                   std::to_string(entry.total_bytes) + '\t' +
                   std::to_string(entry.max_end_offset) + '\t' + std::to_string(entry.first_seq) +
                   '\n';
        }
        AppendDropped(out, snapshot.fs_reads.dropped);
    }

    std::atomic<bool> recording_{true};
    std::array<Detail::CoreSvcCounters, kMaxCores> svc_{};
    std::atomic<std::uint64_t> svc_overflow_{0};
    std::mutex segment_mutex_;
    std::array<std::uint64_t, kSvcSlots> svc_baseline_{};
    std::uint64_t svc_overflow_baseline_ = 0;
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
    Detail::Table<std::string, Detail::CallerGroup> callers_;
    Detail::Table<std::string, Detail::MemoryEntry> memory_;
    mutable std::mutex modules_mutex_;
    std::vector<ModuleRange> modules_;
    std::atomic<bool> module_scan_claimed_{false};
    std::mutex ipc_stash_mutex_;
    std::map<std::uint64_t, CallerFrames> ipc_stash_;
    std::atomic<std::uint64_t> last_write_ns_{kNeverWritten};
};

inline Recorder& Instance() {
    static Recorder recorder;
    return recorder;
}

} // namespace Core::BoundaryInventory
