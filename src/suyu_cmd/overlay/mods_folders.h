// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Mods discovery and folder handling for the overlay, free of ImGui and of the window: what a
// scan of mods/ reports, what the UI may ask for, and how a path becomes a file URL.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace SuyuCmd {

/// A scan lists at most this many mods; the rest are only counted as truncated.
inline constexpr std::size_t kMaxListedMods = 512;

/// A mod is a folder at `<mods>/<title id folder>/<mod name>`.
struct ModEntry {
    std::string title_id_folder;
    std::string mod_name;

    bool operator==(const ModEntry&) const = default;
};

struct ModsScan {
    std::vector<ModEntry> mods; ///< Sorted by title id folder, then mod name.
    bool truncated{};
    bool directory_missing{};
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

/// What the Mods and folders window shows; built on the event thread.
struct ModsFoldersSnapshot {
    std::string user_folder;
    std::string mods_folder;
    std::string keys_folder;
    ModsScan scan;
    bool scanned{}; ///< False until the first scan finished.
};

inline std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/// Lists the mod folders under `mods_dir`. Files and unreadable entries are skipped; nothing
/// throws.
inline ModsScan ScanMods(const std::filesystem::path& mods_dir) {
    namespace fs = std::filesystem;
    ModsScan scan;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) {
        scan.directory_missing = true;
        return scan;
    }
    for (fs::directory_iterator title(mods_dir, ec), end; !ec && title != end;
         title.increment(ec)) {
        std::error_code entry_ec;
        if (!title->is_directory(entry_ec)) {
            continue;
        }
        for (fs::directory_iterator mod(title->path(), entry_ec); !entry_ec && mod != end;
             mod.increment(entry_ec)) {
            std::error_code kind_ec;
            if (!mod->is_directory(kind_ec)) {
                continue;
            }
            scan.mods.push_back(
                {Utf8(title->path().filename()), Utf8(mod->path().filename())});
        }
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
