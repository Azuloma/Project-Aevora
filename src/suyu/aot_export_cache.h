// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// The persistent AOT export cache: where a finished Build export's launcher and manifest are
// kept between exports, how an entry is named, locked and replaced, and which producer built it.
//
// This file owns the cache's key, layout and publish rules. The exporter (game_export.cpp) and
// the tests include it and never spell an entry name, a file name or a layout path themselves.
// Only the standard library is used so the rules are tested on their own
// (tests/recompiler_smoke/aot_cache_unit.cpp).
#pragma once

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace suyu::aot_cache {

// Directory under the suyu user-data directory that holds every entry. Game-derived, so it
// never lives in the repository or an output folder.
inline constexpr char kCacheRootDirName[] = "aot_export_cache";

// Layout of one entry (relative to the entry directory). The cache stores exactly these
// artifacts; reuse copies the launcher and manifest back into staging.
inline constexpr char kLauncherDirName[] = "launcher";
// The launcher image inside kLauncherDirName; an entry without it is not usable.
inline constexpr char kLauncherExecutableName[] = "static_launcher.exe";
inline constexpr char kManifestFileName[] = "aot_manifest.json";
inline constexpr char kModuleSelectionFileName[] = "exefs/recomp_modules.cmake";
// Written by PublishEntry; records the layout version, the entry name and the producer (see
// ProducerIdentity).
inline constexpr char kEntryRecordFileName[] = "cache_entry.txt";

// Bumped when the entry layout above changes, so an older entry is rebuilt, not misread.
inline constexpr unsigned kEntryLayoutVersion = 1;

// What an entry's name is made of. One key addresses one entry; a different title, platform,
// backend or topology never shares or replaces another's entry.
struct EntryKey {
    std::string title_id_hex;  // 16 lower-case hex digits
    std::string platform;      // e.g. "windows"
    std::string backend;       // e.g. "suyu-static", "suyu-hybrid"
    bool split_main{};         // main bound as a DLL, or all-static
};

// "<title_id>_<platform>_<backend>_<split|all-static>". Parts may contain dashes but never an
// underscore, so the underscores that join them make different keys give different names.
// Returns nullopt with `error` naming the offending field when a part would not make a safe
// single directory name.
[[nodiscard]] std::optional<std::string> EntryName(const EntryKey& key, std::string* error);

// <cache_root>/<EntryName>. The same error rules as EntryName.
[[nodiscard]] std::optional<std::filesystem::path> EntryPath(const std::filesystem::path& cache_root,
                                                             const EntryKey& key,
                                                             std::string* error);

// The suyu build that wrote an entry. An entry from another producer is not reused: generated
// C and launcher code depend on the exporter and runtime that made them.
struct ProducerIdentity {
    std::string suyu_revision;  // Common::g_scm_rev
    std::string build_id;       // Common::g_build_id
    friend bool operator==(const ProducerIdentity&, const ProducerIdentity&) = default;
};

// Test seam: the producer a test-driven run reports instead of the real build's (the caller
// reads it from test options; an ordinary run passes the real identity straight through).
[[nodiscard]] ProducerIdentity EffectiveProducer(const ProducerIdentity& real,
                                                 const std::optional<ProducerIdentity>& test_override);

// Checks that `entry_dir` holds a complete entry for `key` written by `expected`: the record
// file with the current layout version, key and producer, plus the launcher directory, the
// manifest and, for a split entry, the module selection. Returns true when it is usable;
// otherwise false with `mismatch` naming the item (for example "producer.suyu_revision" or
// "launcher (missing)").
[[nodiscard]] bool ValidateEntry(const std::filesystem::path& entry_dir, const EntryKey& key,
                                 const ProducerIdentity& expected, std::string* mismatch);

// An exclusive per-entry lock, held for the lifetime of the object. The operating system drops
// it when the process ends, so a crash leaves no stale lock. Lock files sit beside the entries
// (<cache_root>/<EntryName>.lock) and are not part of an entry.
class EntryLock {
public:
    // Takes the lock without waiting. Returns nullptr when another export holds it (the caller
    // runs without the cache and does not publish) or when the lock cannot be created, with
    // `error` saying which; `contended` tells the two apart.
    [[nodiscard]] static std::unique_ptr<EntryLock> TryAcquire(
        const std::filesystem::path& cache_root, const EntryKey& key, bool* contended,
        std::string* error);

    ~EntryLock();
    EntryLock(const EntryLock&) = delete;
    EntryLock& operator=(const EntryLock&) = delete;

private:
    EntryLock() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A directory on the cache's volume where the exporter assembles the next version of an entry.
// <cache_root>/.staging-<EntryName>; any leftover from an interrupted run is cleared first. If
// an interrupted publish left the previous entry set aside with no entry in place, it is
// restored first. Requires the entry lock (the proof parameter makes that explicit).
[[nodiscard]] std::optional<std::filesystem::path> PrepareStaging(
    const EntryLock& lock, const std::filesystem::path& cache_root, const EntryKey& key,
    std::string* error);

// Publishes the finished staging directory as the entry: writes the record file, then swaps it
// in with renames so a reader sees the old complete entry or the new one, never a partial one.
// An existing entry is replaced in place (no generations). The staging directory must already
// hold the artifacts ValidateEntry requires; an incomplete one is refused and left for the
// caller to inspect.
// Requires the entry lock.
[[nodiscard]] bool PublishEntry(const EntryLock& lock, const std::filesystem::path& cache_root,
                                const EntryKey& key, const ProducerIdentity& producer,
                                std::string* error);

// Total size of the files under `entry_dir`, for the log (PLA is about 3 GB per entry).
[[nodiscard]] std::uintmax_t EntrySizeBytes(const std::filesystem::path& entry_dir);

} // namespace suyu::aot_cache
