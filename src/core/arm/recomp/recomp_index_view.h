// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

#include "core/recompiler/recomp_image_index.h"

namespace Core {

using RecompIndexBlock = void (*)(void*);
using RecompIndex64Fn = int (*)(std::uint64_t*, std::uint64_t*, RecompIndexBlock**);

// The host's direct view of one image's block index: the 32-bit table when the image publishes
// it, else the legacy 64-bit table, else nothing (callers then use the image's lookup export).
struct RecompIndexView {
    enum class Kind { None, Index32, Index64 };

    Kind kind = Kind::None;
    // Inclusive absolute range; the default is empty, so Covers() is false until a table is bound.
    std::uint64_t lo = 1;
    std::uint64_t hi = 0;
    const std::int32_t* table32 = nullptr;
    std::uint64_t anchor = 0;
    RecompIndexBlock* table64 = nullptr;

    bool Covers(std::uint64_t pc) const {
        return pc >= lo && pc <= hi && ((pc - lo) & 3) == 0;
    }

    // The block at a covered pc, or null where no block starts.
    RecompIndexBlock BlockAt(std::uint64_t pc) const {
        const std::size_t slot = static_cast<std::size_t>((pc - lo) >> 2);
        if (kind == Kind::Index32) {
            return reinterpret_cast<RecompIndexBlock>(
                static_cast<std::uintptr_t>(suyu_recomp_index32_target(anchor, table32[slot])));
        }
        return table64[slot];
    }
};

// Prefers the 32-bit export, then the 64-bit one; a null export is an older image. When an image
// has the 32-bit export but declines it, note says why and what is used instead.
inline RecompIndexView AcquireRecompIndexView(SuyuRecompIndex32V1Fn get_index32,
                                              RecompIndex64Fn get_index64, std::string* note) {
    RecompIndexView view;
    std::string declined;
    if (get_index32) {
        SuyuRecompIndex32V1 out{};
        out.version = SUYU_RECOMP_INDEX32_V1_VERSION;
        out.struct_size = sizeof(out);
        if (get_index32(&out) && out.table && out.lo <= out.hi) {
            view.kind = RecompIndexView::Kind::Index32;
            view.lo = out.lo;
            view.hi = out.hi;
            view.table32 = out.table;
            view.anchor = out.anchor;
            return view;
        }
        declined = std::string("32-bit block index unavailable: ") +
                   (out.unavailable_reason ? out.unavailable_reason : "no reason given");
    }
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    RecompIndexBlock* table = nullptr;
    if (get_index64 && get_index64(&lo, &hi, &table) && table && lo <= hi) {
        view.kind = RecompIndexView::Kind::Index64;
        view.lo = lo;
        view.hi = hi;
        view.table64 = table;
    }
    if (note && !declined.empty()) {
        *note = declined + (view.kind == RecompIndexView::Kind::Index64
                                ? "; using the legacy 64-bit table"
                                : "; dispatch uses the image's lookup export");
    }
    return view;
}

} // namespace Core
