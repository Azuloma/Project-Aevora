// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Mods discovery and folder handling for the overlay, free of ImGui and of the window: what a
// scan of mods/ reports, what the UI may ask for, and how a path becomes a file URL.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace SuyuCmd {

/// A scan lists at most this many mods; the rest are only counted as truncated.
inline constexpr std::size_t kMaxListedMods = 512;

/// A scan keeps at most this many error messages.
inline constexpr std::size_t kMaxScanErrors = 8;

/// A mod is a folder at `<mods>/<title id folder>/<mod name>`, or a `cheat_*` file directly in
/// the title id folder, as the patch manager loads them.
struct ModEntry {
    std::string title_id_folder;
    std::string mod_name;
    bool cheat_file{};

    bool operator==(const ModEntry&) const = default;
};

struct ModsScan {
    std::vector<ModEntry> mods; ///< Sorted by title id folder, then mod name.
    bool truncated{};
    bool directory_missing{}; ///< The mods folder does not exist (not an error).
    std::vector<std::string> errors; ///< Reads that failed; the list above may be incomplete.
};

enum class FolderKind : std::uint8_t { User, Mods, Keys };

struct ModsFoldersRequest {
    enum class Kind : std::uint8_t {
        Rescan,     ///< scan mods/ again and publish the result
        OpenFolder, ///< open `folder` in the file manager, creating it first
    };

    Kind kind{Kind::Rescan};
    FolderKind folder{FolderKind::Mods};
};

/// The three folders the window names, as the runtime resolves them.
struct FolderPaths {
    std::filesystem::path user;
    std::filesystem::path mods;
    std::filesystem::path keys;
};

/// What the Mods and folders window shows; built on the event thread.
struct ModsFoldersSnapshot {
    std::string user_folder;
    std::string mods_folder;
    std::string keys_folder;
    ModsScan scan;
    bool scanned{}; ///< False while a scan is still running.
};

inline std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

namespace detail {

inline void AddScanError(ModsScan& scan, const std::filesystem::path& path,
                         const std::error_code& ec) {
    if (scan.errors.size() < kMaxScanErrors) {
        scan.errors.push_back(Utf8(path) + ": " + ec.message());
    }
}

} // namespace detail

/// Lists the mods under `mods_dir`. Entries that cannot be read are recorded in `errors` and
/// skipped; nothing throws.
inline ModsScan ScanMods(const std::filesystem::path& mods_dir) {
    namespace fs = std::filesystem;
    ModsScan scan;
    std::error_code ec;
    const fs::file_status status = fs::status(mods_dir, ec);
    if (ec == std::errc::no_such_file_or_directory || (!ec && !fs::exists(status))) {
        scan.directory_missing = true;
        return scan;
    }
    if (ec) {
        detail::AddScanError(scan, mods_dir, ec);
        return scan;
    }
    if (!fs::is_directory(status)) {
        scan.errors.push_back(Utf8(mods_dir) + ": not a folder");
        return scan;
    }
    fs::directory_iterator title(mods_dir, ec);
    if (ec) {
        detail::AddScanError(scan, mods_dir, ec);
        return scan;
    }
    for (const fs::directory_iterator end; title != end; title.increment(ec)) {
        std::error_code entry_ec;
        if (!title->is_directory(entry_ec)) {
            if (entry_ec) {
                detail::AddScanError(scan, title->path(), entry_ec);
            }
            continue;
        }
        fs::directory_iterator mod(title->path(), entry_ec);
        if (entry_ec) {
            detail::AddScanError(scan, title->path(), entry_ec);
            continue;
        }
        for (; mod != end; mod.increment(entry_ec)) {
            std::error_code kind_ec;
            const bool is_folder = mod->is_directory(kind_ec);
            const std::string name = Utf8(mod->path().filename());
            const bool is_cheat =
                !kind_ec && !is_folder && name.starts_with("cheat_") && mod->is_regular_file(kind_ec);
            if (kind_ec) {
                detail::AddScanError(scan, mod->path(), kind_ec);
            } else if (is_folder || is_cheat) {
                scan.mods.push_back({Utf8(title->path().filename()), name, is_cheat});
            }
        }
        if (entry_ec) {
            detail::AddScanError(scan, title->path(), entry_ec);
        }
    }
    if (ec) {
        detail::AddScanError(scan, mods_dir, ec);
    }
    std::sort(scan.mods.begin(), scan.mods.end(), [](const ModEntry& a, const ModEntry& b) {
        return a.title_id_folder != b.title_id_folder ? a.title_id_folder < b.title_id_folder
                                                      : a.mod_name < b.mod_name;
    });
    if (scan.mods.size() > kMaxListedMods) {
        scan.mods.resize(kMaxListedMods);
        scan.truncated = true;
    }
    return scan;
}

/// Runs scans on its own thread so a slow folder never stalls the event loop. Requests that
/// arrive during a scan collapse into one follow-up scan with the newest folder and callback.
/// The destructor waits for the running scan; callbacks run on the scanner's thread.
class ModsScanner {
public:
    using ScanFunction = std::function<ModsScan(const std::filesystem::path&)>;
    using DoneCallback = std::function<void(ModsScan)>;

    explicit ModsScanner(ScanFunction scan_function = [](const std::filesystem::path& dir) {
        return ScanMods(dir);
    })
        : scan(std::move(scan_function)) {}

    ~ModsScanner() {
        std::thread finished;
        {
            std::lock_guard lock{mutex};
            has_pending = false;
            finished = std::move(worker);
        }
        if (finished.joinable()) {
            finished.join();
        }
    }

    ModsScanner(const ModsScanner&) = delete;
    ModsScanner& operator=(const ModsScanner&) = delete;

    /// Returns at once; `done` is called with the result unless a newer request replaced it.
    void Request(std::filesystem::path dir, DoneCallback done) {
        std::thread finished;
        {
            std::lock_guard lock{mutex};
            pending_dir = std::move(dir);
            pending_done = std::move(done);
            has_pending = true;
            if (running) {
                return;
            }
            running = true;
            finished = std::move(worker);
            worker = std::thread([this] { Run(); });
        }
        if (finished.joinable()) {
            finished.join(); // already past its last use of the lock
        }
    }

private:
    void Run() {
        for (;;) {
            std::filesystem::path dir;
            DoneCallback done;
            {
                std::lock_guard lock{mutex};
                if (!has_pending) {
                    running = false;
                    return;
                }
                dir = std::move(pending_dir);
                done = std::move(pending_done);
                has_pending = false;
            }
            ModsScan result = scan(dir);
            if (done) {
                done(std::move(result));
            }
        }
    }

    ScanFunction scan;
    std::mutex mutex;
    std::thread worker;
    bool running{};
    bool has_pending{};
    std::filesystem::path pending_dir;
    DoneCallback pending_done;
};

/// "file:///C:/a%20b" for a UTF-8 path; separators become '/', every other byte outside
/// the unreserved set is percent-encoded.
inline std::string FileUrlFromPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string url = "file://";
    if (path.empty() || path.front() != '/') {
        url += '/';
    }
    constexpr char kHex[] = "0123456789ABCDEF";
    for (const char c : path) {
        const auto byte = static_cast<unsigned char>(c);
        const bool plain = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                           (byte >= '0' && byte <= '9') || c == '-' || c == '.' || c == '_' ||
                           c == '~' || c == '/' || c == ':';
        if (plain) {
            url += c;
        } else {
            url += '%';
            url += kHex[byte >> 4];
            url += kHex[byte & 0xF];
        }
    }
    return url;
}

} // namespace SuyuCmd
