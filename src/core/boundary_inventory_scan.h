// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// N0 stage 5c: finds NVN and SDK names in loaded modules. Output stays in a local file; the
// repository never holds game-derived names. OS-free; the caller reads guest memory.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/boundary_inventory.h"

namespace Core::BoundaryInventory {

constexpr std::size_t kMinScanStringLength = 4;
constexpr std::size_t kMaxScanStringLength = 128;
constexpr std::size_t kMaxScanHits = 20000;
constexpr std::string_view kNvnPrefix = "nvn";

struct ScanHit {
    std::uint64_t address{};
    std::string text;
};

/// True for a dynsym name containing "nvn", "nn::" or the Itanium-mangled "_ZN2nn" namespace.
inline bool IsInterestingSymbolName(std::string_view name) {
    return name.find("nvn") != std::string_view::npos ||
           name.find("nn::") != std::string_view::npos ||
           name.find("_ZN2nn") != std::string_view::npos;
}

/// Appends the printable NUL-terminated "nvn" strings starting at window indexes [first, last),
/// each after a NUL or at index 0; the window holds the byte before `first` and a tail past `last`.
inline void FindNvnStrings(std::span<const std::uint8_t> window, std::size_t first,
                           std::size_t last, std::uint64_t window_base,
                           std::vector<ScanHit>& hits) {
    const std::size_t end = last < window.size() ? last : window.size();
    for (std::size_t i = first; i < end && hits.size() < kMaxScanHits; ++i) {
        if (window[i] != static_cast<std::uint8_t>(kNvnPrefix[0]) ||
            (i != 0 && window[i - 1] != 0)) {
            continue;
        }
        std::size_t length = 0;
        bool terminated = false;
        bool printable = true;
        while (i + length < window.size() && length <= kMaxScanStringLength) {
            const std::uint8_t c = window[i + length];
            if (c == 0) {
                terminated = true;
                break;
            }
            printable = printable && c >= 0x20 && c < 0x7F;
            ++length;
        }
        if (!terminated || !printable || length < kMinScanStringLength) {
            continue;
        }
        const std::string_view text{reinterpret_cast<const char*>(window.data() + i), length};
        if (text.substr(0, kNvnPrefix.size()) == kNvnPrefix) {
            hits.push_back({window_base + i, std::string{text}});
        }
    }
}

constexpr std::size_t kMaxDynamicEntries = 1024;
constexpr std::uint64_t kMaxSymtabBytes = 64ULL << 20;
constexpr std::uint64_t kMaxStrtabBytes = 64ULL << 20;
constexpr std::size_t kMaxSymbolNameLength = 512;
constexpr std::uint64_t kElf64SymbolBytes = 24;
constexpr std::uint32_t kModMagic = 0x30444F4D; // "MOD0"

/// Where a module's dynsym and dynstr tables lie, as offsets from the module base.
struct DynsymLocation {
    std::uint64_t symtab_offset{};
    std::uint64_t strtab_offset{};
    std::uint64_t strtab_size{};
    std::uint64_t symbol_bytes{};
};

/// Reads the MOD0 header and dynamic array through `read(offset, destination, size)` (false if
/// unreadable); entry count and table sizes are bounded, so corrupt metadata fails, never loops.
template <typename Read>
std::optional<DynsymLocation> FindDynsym(Read&& read, std::uint64_t module_size) {
    std::uint32_t header_offset = 0;
    std::uint32_t signature = 0;
    std::uint32_t dynamic_offset = 0;
    if (!read(4, &header_offset, sizeof header_offset) ||
        !read(header_offset, &signature, sizeof signature) || signature != kModMagic ||
        !read(std::uint64_t{header_offset} + 4, &dynamic_offset, sizeof dynamic_offset)) {
        return std::nullopt;
    }
    constexpr std::int64_t kTagNull = 0, kTagStrtab = 5, kTagSymtab = 6, kTagStrsz = 10,
                           kTagSyment = 11;
    DynsymLocation location;
    std::uint64_t symbol_bytes = 0;
    bool ended = false;
    for (std::size_t n = 0; n < kMaxDynamicEntries && !ended; ++n) {
        std::int64_t tag = 0;
        std::uint64_t value = 0;
        const std::uint64_t entry = std::uint64_t{header_offset} + dynamic_offset + n * 16;
        if (!read(entry, &tag, sizeof tag) || !read(entry + 8, &value, sizeof value)) {
            return std::nullopt;
        }
        switch (tag) {
        case kTagNull: ended = true; break;
        case kTagStrtab: location.strtab_offset = value; break;
        case kTagSymtab: location.symtab_offset = value; break;
        case kTagStrsz: location.strtab_size = value; break;
        case kTagSyment: symbol_bytes = value; break;
        default: break;
        }
    }
    if (!ended || (symbol_bytes != 0 && symbol_bytes != kElf64SymbolBytes)) {
        return std::nullopt;
    }
    location.symbol_bytes = kElf64SymbolBytes;
    const std::uint64_t symtab_size = location.strtab_offset - location.symtab_offset;
    if (location.symtab_offset == 0 || location.strtab_offset <= location.symtab_offset ||
        symtab_size > kMaxSymtabBytes || location.strtab_size == 0 ||
        location.strtab_size > kMaxStrtabBytes ||
        location.strtab_offset > module_size ||
        location.strtab_size > module_size - location.strtab_offset) {
        return std::nullopt;
    }
    return location;
}

/// Appends symbols whose name IsInterestingSymbolName, at base + st_value (0 if undefined);
/// entries with an unterminated, over-long or out-of-table name are skipped.
inline void ParseDynsym(std::span<const std::uint8_t> symtab, std::span<const std::uint8_t> strtab,
                        std::uint64_t base, std::vector<ScanHit>& hits) {
    for (std::size_t at = 0; at + kElf64SymbolBytes <= symtab.size() && hits.size() < kMaxScanHits;
         at += kElf64SymbolBytes) {
        std::uint32_t name_offset = 0;
        std::uint64_t value = 0;
        std::memcpy(&name_offset, symtab.data() + at, sizeof name_offset);
        std::memcpy(&value, symtab.data() + at + 8, sizeof value);
        if (name_offset >= strtab.size()) {
            continue;
        }
        const std::size_t room = strtab.size() - name_offset;
        const auto* name_start = strtab.data() + name_offset;
        const void* terminator = std::memchr(name_start, 0, room);
        if (terminator == nullptr) {
            continue;
        }
        const std::size_t length = static_cast<const std::uint8_t*>(terminator) - name_start;
        const std::string_view name{reinterpret_cast<const char*>(name_start), length};
        if (length <= kMaxSymbolNameLength && IsInterestingSymbolName(name)) {
            hits.push_back({value != 0 ? base + value : 0, std::string{name}});
        }
    }
}

/// inventory_<run>_scan.txt, the local name list of the loaded modules.
inline std::string ScanFileName(std::string_view run) {
    return "inventory_" + SanitizeFileComponent(run, "run") + "_scan.txt";
}

/// Tab-separated text: counts first, then `string` and `symbol` rows as name+0xRVA. Local only.
inline std::string FormatScan(const Recorder& recorder, const std::vector<ScanHit>& strings,
                              const std::vector<ScanHit>& symbols) {
    std::string out = "# boundary inventory scan v1 (local only, do not commit)\n";
    out += "# strings=" + std::to_string(strings.size()) +
           " symbols=" + std::to_string(symbols.size()) + '\n';
    for (const ScanHit& hit : strings) {
        out += "string\t" + recorder.Describe(hit.address) + '\t' + hit.text + '\n';
    }
    for (const ScanHit& hit : symbols) {
        out += "symbol\t" + recorder.Describe(hit.address) + '\t' + hit.text + '\n';
    }
    return out;
}

} // namespace Core::BoundaryInventory
