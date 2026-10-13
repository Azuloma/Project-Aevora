// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "core/arm/recomp/recomp_sharded_counters.h"

namespace Core {

// SUYU_RECOMP_ACCESS_STATS=1 diagnostic: guest loads and stores that left the inline fast path and
// reached the host callbacks, classified by why, per reporting window.
class RecompAccessStats {
public:
    enum class Reason : std::size_t {
        Tagged,     // bits 48..63 set
        OutOfRange, // at or above the address-space limit
        Unmapped,
        Debug,
        Rasterizer, // GPU-tracked page
        PageCross,  // plain page, access straddles the page end
        Watched,    // store to a GG1-watched code page
        Other,      // plain page, none of the above
        kCount,
    };
    static constexpr std::size_t kReasonCount = static_cast<std::size_t>(Reason::kCount);
    static constexpr std::size_t kCounterCount = 2 * kReasonCount;
    using Counters = RecompShardedCounters<kCounterCount>;

    // Page-table attribute values and the low bits that carry them (Common::PageType, ATTRIBUTE_BITS).
    static constexpr std::uint64_t kAttributeMask = 3;
    static constexpr std::uint64_t kTypeUnmapped = 0;
    static constexpr std::uint64_t kTypeMemory = 1;
    static constexpr std::uint64_t kTypeDebug = 2;
    static constexpr std::uint64_t kTypeRasterizer = 3;
    static constexpr std::uint64_t kAddressBits = 48;

    struct Entry {
        std::uint64_t raw = 0;        // PageInfo raw word: pointer part plus attribute bits
        std::uint64_t watch_word = 0; // PageEntryData::recomp_watch
    };

    // `entry_of(page_index)` returns the page's Entry; it is called only for an in-range address.
    // The page of the first byte decides, as it does for the host accessors.
    template <typename EntryOf>
    static Reason Classify(std::uint64_t va, std::uint32_t size, bool is_store, std::uint32_t page_bits,
                           std::uint64_t address_space_max, EntryOf&& entry_of) {
        if ((va >> kAddressBits) != 0) {
            return Reason::Tagged;
        }
        if (va >= address_space_max || size > address_space_max - va || page_bits >= 63) {
            return Reason::OutOfRange;
        }
        const Entry entry = entry_of(va >> page_bits);
        switch (entry.raw & kAttributeMask) {
        case kTypeUnmapped:
            return Reason::Unmapped;
        case kTypeDebug:
            return Reason::Debug;
        case kTypeRasterizer:
            return Reason::Rasterizer;
        default:
            break;
        }
        const std::uint64_t page_size = std::uint64_t{1} << page_bits;
        if (size > page_size - (va & (page_size - 1))) {
            return Reason::PageCross;
        }
        if (is_store && entry.watch_word != 0) {
            return Reason::Watched;
        }
        return Reason::Other;
    }

    static const char* ReasonName(Reason reason) {
        static constexpr std::array<const char*, kReasonCount> kNames = {
            "tagged", "out_of_range", "unmapped", "debug", "rasterizer", "page_cross", "watched", "other"};
        return kNames[static_cast<std::size_t>(reason)];
    }

    Counters::Slot& Acquire() {
        return counters_.Acquire();
    }

    static void Count(Counters::Slot& slot, bool is_store, Reason reason) {
        Counters::Increment(slot, (is_store ? kReasonCount : 0) + static_cast<std::size_t>(reason));
    }

    // One line once window_ns has passed since the previous report; the first call opens the window.
    std::optional<std::string> TakeReport(std::uint64_t now_ns, std::uint64_t window_ns) {
        std::scoped_lock lock{report_lock_};
        if (!window_started_) {
            window_started_ = true;
            Rebase(now_ns);
            return std::nullopt;
        }
        if (now_ns < window_start_ns_ || now_ns - window_start_ns_ < window_ns) {
            return std::nullopt;
        }
        const std::uint64_t elapsed_ms = (now_ns - window_start_ns_) / 1000000;
        std::array<std::uint64_t, kCounterCount> delta{};
        std::uint64_t load_total = 0;
        std::uint64_t store_total = 0;
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            const std::uint64_t total = counters_.Sum(i);
            delta[i] = total - reported_[i];
            reported_[i] = total;
            (i < kReasonCount ? load_total : store_total) += delta[i];
        }
        std::string line = "access-stats window_ms=" + std::to_string(elapsed_ms) +
                           " loads=" + std::to_string(load_total) +
                           " stores=" + std::to_string(store_total) + " per_s=" +
                           std::to_string(elapsed_ms == 0 ? 0 : (load_total + store_total) * 1000 / elapsed_ms);
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            line += std::string(i < kReasonCount ? " load_" : " store_") +
                    ReasonName(static_cast<Reason>(i % kReasonCount)) + "=" + std::to_string(delta[i]);
        }
        window_start_ns_ = now_ns;
        return line;
    }

private:
    void Rebase(std::uint64_t now_ns) {
        window_start_ns_ = now_ns;
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            reported_[i] = counters_.Sum(i);
        }
    }

    Counters counters_;
    std::mutex report_lock_;
    bool window_started_ = false;
    std::uint64_t window_start_ns_ = 0;
    std::array<std::uint64_t, kCounterCount> reported_{};
};

} // namespace Core
