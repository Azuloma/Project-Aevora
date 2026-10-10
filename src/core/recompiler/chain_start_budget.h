// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Size rules for chain start entries (SUYU_AOT_CHAIN_START), which duplicate small block bodies.
// Calibrated on the PLA main image: 1603 MiB, 54.7 MB guest text, 169 B/block at 1 insn, 696 at 8.

#pragma once

#include <cstdint>

namespace suyu::recomp {

/// Blocks with at most this many instructions get a start entry.
inline constexpr std::uint32_t kChainStartMaxInsns = 8;

inline constexpr std::uint64_t kImageSizeLimitBytes = std::uint64_t{2} << 30;
/// An export projected closer than this to the image limit is refused.
inline constexpr std::uint64_t kChainStartImageMarginBytes = std::uint64_t{128} << 20;
/// Linked image bytes per guest .text byte of the module (PLA main: 30.7, rounded up).
inline constexpr std::uint64_t kImageBytesPerGuestTextByte = 31;
inline constexpr std::uint64_t kStartEntryBaseHostBytes = 94;
inline constexpr std::uint64_t kStartEntryHostBytesPerInsn = 75;

/// Host bytes one start entry adds; the measured spacing, so an upper bound (no switch needed).
constexpr std::uint64_t EstimateStartEntryHostBytes(std::uint32_t insns) {
    return kStartEntryBaseHostBytes + kStartEntryHostBytesPerInsn * insns;
}

struct ChainStartBudget {
    bool fits{};
    std::uint64_t projected_image_bytes{};
    std::uint64_t allowed_image_bytes{};
};

/// Projects the module's linked image with the start entries added and compares it with the limit.
constexpr ChainStartBudget CheckChainStartBudget(std::uint64_t guest_text_bytes,
                                                 std::uint64_t start_entry_bytes) {
    ChainStartBudget budget;
    budget.projected_image_bytes = guest_text_bytes * kImageBytesPerGuestTextByte + start_entry_bytes;
    budget.allowed_image_bytes = kImageSizeLimitBytes - kChainStartImageMarginBytes;
    budget.fits = budget.projected_image_bytes <= budget.allowed_image_bytes;
    return budget;
}

/// The modules linked into one image, added one at a time; each Add checks the running total.
struct ChainStartImageProjection {
    std::uint64_t guest_text_bytes{};
    std::uint64_t start_entry_bytes{};

    ChainStartBudget Add(std::uint64_t module_text_bytes, std::uint64_t module_start_entry_bytes) {
        guest_text_bytes += module_text_bytes;
        start_entry_bytes += module_start_entry_bytes;
        return CheckChainStartBudget(guest_text_bytes, start_entry_bytes);
    }
};

} // namespace suyu::recomp
