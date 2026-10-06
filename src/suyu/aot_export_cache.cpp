// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "suyu/aot_export_cache.h"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace suyu::aot_cache {

namespace fs = std::filesystem;

namespace {

constexpr char kRecordMagic[] = "aevora-aot-cache-entry";
constexpr std::size_t kTitleIdHexDigits = 16;

bool IsLowerHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// A name part must stay one portable directory component: lower-case letters, digits and
// inner dashes only. No underscore, which joins the parts of an entry name.
bool IsSafeNamePart(const std::string& part) {
    if (part.empty() || part.front() == '-' || part.back() == '-') {
        return false;
    }
    return std::all_of(part.begin(), part.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

fs::path LockPath(const fs::path& cache_root, const std::string& entry_name) {
    return cache_root / (entry_name + ".lock");
}

fs::path StagingPath(const fs::path& cache_root, const std::string& entry_name) {
    return cache_root / (".staging-" + entry_name);
}

fs::path TrashPath(const fs::path& cache_root, const std::string& entry_name) {
    return cache_root / (".trash-" + entry_name);
}

std::string RecordText(const std::string& entry_name, const ProducerIdentity& producer) {
    std::ostringstream text;
    text << kRecordMagic << '\n'
         << "layout_version=" << kEntryLayoutVersion << '\n'
         << "entry=" << entry_name << '\n'
         << "producer.suyu_revision=" << producer.suyu_revision << '\n'
         << "producer.build_id=" << producer.build_id << '\n';
    return text.str();
}

bool HasLineBreak(const std::string& value) {
    return value.find_first_of("\r\n") != std::string::npos;
}

// The artifacts every entry needs, apart from the record file. On failure `mismatch` names
// the first missing item.
bool CheckArtifacts(const fs::path& dir, const EntryKey& key, std::string* mismatch) {
    std::error_code ec;
    if (!fs::is_directory(dir / kLauncherDirName, ec)) {
        *mismatch = std::string(kLauncherDirName) + " (missing)";
        return false;
    }
    if (!fs::is_regular_file(dir / kLauncherDirName / kLauncherExecutableName, ec)) {
        *mismatch = std::string(kLauncherDirName) + "/" + kLauncherExecutableName + " (missing)";
        return false;
    }
    if (!fs::is_regular_file(dir / kManifestFileName, ec)) {
        *mismatch = std::string(kManifestFileName) + " (missing)";
        return false;
    }
    if (key.split_main && !fs::is_regular_file(dir / kModuleSelectionFileName, ec)) {
        *mismatch = std::string(kModuleSelectionFileName) + " (missing)";
        return false;
    }
    return true;
}

} // namespace

std::optional<std::string> EntryName(const EntryKey& key, std::string* error) {
    const bool title_ok =
        key.title_id_hex.size() == kTitleIdHexDigits &&
        std::all_of(key.title_id_hex.begin(), key.title_id_hex.end(), IsLowerHex);
    if (!title_ok) {
        *error = "title_id '" + key.title_id_hex + "' is not 16 lower-case hex digits";
        return std::nullopt;
    }
    if (!IsSafeNamePart(key.platform)) {
        *error = "platform '" + key.platform + "' is not a safe cache name part";
        return std::nullopt;
    }
    if (!IsSafeNamePart(key.backend)) {
        *error = "backend '" + key.backend + "' is not a safe cache name part";
        return std::nullopt;
    }
    return key.title_id_hex + '_' + key.platform + '_' + key.backend + '_' +
           (key.split_main ? "split" : "all-static");
}

std::optional<fs::path> EntryPath(const fs::path& cache_root, const EntryKey& key,
                                  std::string* error) {
    const auto name = EntryName(key, error);
    if (!name) {
        return std::nullopt;
    }
    return cache_root / *name;
}

ProducerIdentity EffectiveProducer(const ProducerIdentity& real,
                                   const std::optional<ProducerIdentity>& test_override) {
    return test_override ? *test_override : real;
}

bool ValidateEntry(const fs::path& entry_dir, const EntryKey& key,
                   const ProducerIdentity& expected, std::string* mismatch) {
    std::string name_error;
    const auto name = EntryName(key, &name_error);
    if (!name) {
        *mismatch = "key (" + name_error + ")";
        return false;
    }
    std::ifstream record(entry_dir / kEntryRecordFileName, std::ios::binary);
    if (!record) {
        *mismatch = std::string(kEntryRecordFileName) + " (missing or unreadable)";
        return false;
    }
    std::string line;
    if (!std::getline(record, line) || line != kRecordMagic) {
        *mismatch = std::string(kEntryRecordFileName) + " (not a cache entry record)";
        return false;
    }
    std::map<std::string, std::string> fields;
    while (std::getline(record, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            *mismatch = std::string(kEntryRecordFileName) + " (malformed line)";
            return false;
        }
        fields[line.substr(0, equals)] = line.substr(equals + 1);
    }
    const auto field = [&fields](const char* field_name) {
        const auto found = fields.find(field_name);
        return found == fields.end() ? std::string{} : found->second;
    };
    if (field("layout_version") != std::to_string(kEntryLayoutVersion)) {
        *mismatch = "layout_version";
        return false;
    }
    if (field("entry") != *name) {
        *mismatch = "entry";
        return false;
    }
    if (field("producer.suyu_revision") != expected.suyu_revision) {
        *mismatch = "producer.suyu_revision";
        return false;
    }
    if (field("producer.build_id") != expected.build_id) {
        *mismatch = "producer.build_id";
        return false;
    }
    return CheckArtifacts(entry_dir, key, mismatch);
}

struct EntryLock::Impl {
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~Impl() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
#else
    int fd{-1};
    ~Impl() {
        if (fd >= 0) {
            flock(fd, LOCK_UN);
            close(fd);
        }
    }
#endif
};

EntryLock::~EntryLock() = default;

std::unique_ptr<EntryLock> EntryLock::TryAcquire(const fs::path& cache_root, const EntryKey& key,
                                                 bool* contended, std::string* error) {
    *contended = false;
    const auto name = EntryName(key, error);
    if (!name) {
        return nullptr;
    }
    std::error_code ec;
    fs::create_directories(cache_root, ec);
    if (ec) {
        *error = "cannot create the cache root " + cache_root.string() + ": " + ec.message();
        return nullptr;
    }
    const fs::path path = LockPath(cache_root, *name);
    auto impl = std::make_unique<Impl>();
#ifdef _WIN32
    // Share mode 0: a second open of the same file, here or in another process, fails.
    impl->handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (impl->handle == INVALID_HANDLE_VALUE) {
        const DWORD last_error = GetLastError();
        if (last_error == ERROR_SHARING_VIOLATION || last_error == ERROR_LOCK_VIOLATION) {
            *contended = true;
            *error = "another export holds the lock " + path.string();
        } else {
            *error = "cannot open the lock " + path.string() + " (Windows error " +
                     std::to_string(last_error) + ")";
        }
        return nullptr;
    }
#else
    impl->fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (impl->fd < 0) {
        *error = "cannot open the lock " + path.string() + " (errno " + std::to_string(errno) + ")";
        return nullptr;
    }
    if (flock(impl->fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            *contended = true;
            *error = "another export holds the lock " + path.string();
        } else {
            *error = "cannot lock " + path.string() + " (errno " + std::to_string(errno) + ")";
        }
        return nullptr;
    }
#endif
    std::unique_ptr<EntryLock> lock(new EntryLock());
    lock->impl_ = std::move(impl);
    return lock;
}

std::optional<fs::path> PrepareStaging(const EntryLock&, const fs::path& cache_root,
                                       const EntryKey& key, std::string* error) {
    const auto name = EntryName(key, error);
    if (!name) {
        return std::nullopt;
    }
    // An interrupted publish can leave the previous entry set aside with nothing in its place;
    // it is the only complete copy, so restore it before clearing anything.
    const fs::path entry = cache_root / *name;
    const fs::path trash = TrashPath(cache_root, *name);
    std::error_code restore_ec;
    if (!fs::exists(entry, restore_ec) && fs::exists(trash, restore_ec)) {
        fs::rename(trash, entry, restore_ec);
        if (restore_ec) {
            *error = "cannot restore the set-aside entry " + trash.string() + ": " +
                     restore_ec.message();
            return std::nullopt;
        }
    }
    // Other leftovers of an interrupted run: a half-built staging tree, or the replaced entry
    // that was not yet removed. Neither is an entry, so both go.
    for (const fs::path& leftover : {StagingPath(cache_root, *name), trash}) {
        std::error_code ec;
        fs::remove_all(leftover, ec);
        if (ec) {
            *error = "cannot clear " + leftover.string() + ": " + ec.message();
            return std::nullopt;
        }
    }
    const fs::path staging = StagingPath(cache_root, *name);
    std::error_code ec;
    fs::create_directories(staging, ec);
    if (ec) {
        *error = "cannot create " + staging.string() + ": " + ec.message();
        return std::nullopt;
    }
    return staging;
}

bool PublishEntry(const EntryLock&, const fs::path& cache_root, const EntryKey& key,
                  const ProducerIdentity& producer, std::string* error) {
    const auto name = EntryName(key, error);
    if (!name) {
        return false;
    }
    if (HasLineBreak(producer.suyu_revision) || HasLineBreak(producer.build_id)) {
        *error = "the producer identity contains a line break";
        return false;
    }
    const fs::path staging = StagingPath(cache_root, *name);
    const fs::path entry = cache_root / *name;
    const fs::path trash = TrashPath(cache_root, *name);

    std::string missing;
    if (!CheckArtifacts(staging, key, &missing)) {
        *error = "the staged entry is incomplete: " + missing;
        return false;
    }
    {
        std::ofstream record(staging / kEntryRecordFileName, std::ios::binary | std::ios::trunc);
        record << RecordText(*name, producer);
        record.flush();
        if (!record) {
            *error = "cannot write " + (staging / kEntryRecordFileName).string();
            return false;
        }
    }

    std::error_code ec;
    fs::remove_all(trash, ec);
    if (ec) {
        *error = "cannot clear " + trash.string() + ": " + ec.message();
        return false;
    }
    const bool had_entry = fs::exists(entry, ec);
    if (had_entry) {
        fs::rename(entry, trash, ec);
        if (ec) {
            *error = "cannot move the old entry aside (" + entry.string() + "): " + ec.message();
            return false;
        }
    }
    fs::rename(staging, entry, ec);
    if (ec) {
        *error = "cannot publish " + entry.string() + ": " + ec.message();
        if (had_entry) {
            std::error_code restore_ec;
            fs::rename(trash, entry, restore_ec);
            if (restore_ec) {
                *error += "; the old entry could not be restored: " + restore_ec.message();
            }
        }
        return false;
    }
    // The new entry is in place; a failure to delete the old one only wastes space until the
    // next PrepareStaging.
    fs::remove_all(trash, ec);
    return true;
}

std::uintmax_t EntrySizeBytes(const fs::path& entry_dir) {
    std::uintmax_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(entry_dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code file_ec;
        if (it->is_regular_file(file_ec)) {
            const auto size = it->file_size(file_ec);
            if (!file_ec) {
                total += size;
            }
        }
    }
    return total;
}

} // namespace suyu::aot_cache
