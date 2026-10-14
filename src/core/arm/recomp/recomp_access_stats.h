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
    // Counter layout: [load counts, store counts, load ticks, store ticks], kReasonCount each.
    static constexpr std::size_t kCountOffset = 0;
    static constexpr std::size_t kTickOffset = 2 * kReasonCount;
    static constexpr std::size_t kCounterCount = 4 * kReasonCount;
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

    // `ticks` is the time the host callback took, in the caller's tick unit.
    static void Count(Counters::Slot& slot, bool is_store, Reason reason, std::uint64_t ticks = 0) {
        const std::size_t index = (is_store ? kReasonCount : 0) + static_cast<std::size_t>(reason);
        Counters::Increment(slot, kCountOffset + index);
        Counters::Add(slot, kTickOffset + index, ticks);
    }

    // One line once window_ns has passed since the previous report; the first call opens the window.
    // The tick rate is measured from the window itself, so no TSC frequency is assumed.
    std::optional<std::string> TakeReport(std::uint64_t now_ns, std::uint64_t now_ticks,
                                          std::uint64_t window_ns) {
        std::scoped_lock lock{report_lock_};
        if (!window_started_) {
            window_started_ = true;
            Rebase(now_ns, now_ticks);
            return std::nullopt;
        }
        if (now_ns < window_start_ns_ || now_ns - window_start_ns_ < window_ns ||
            now_ticks <= window_start_ticks_) {
            return std::nullopt;
        }
        const std::uint64_t elapsed_ns = now_ns - window_start_ns_;
        const std::uint64_t elapsed_ms = elapsed_ns / 1000000;
        const double ticks_per_ns =
            static_cast<double>(now_ticks - window_start_ticks_) / static_cast<double>(elapsed_ns);
        std::array<std::uint64_t, kCounterCount> delta{};
        std::uint64_t load_total = 0;
        std::uint64_t store_total = 0;
        std::uint64_t load_ticks = 0;
        std::uint64_t store_ticks = 0;
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            const std::uint64_t total = counters_.Sum(i);
            delta[i] = total - reported_[i];
            reported_[i] = total;
        }
        for (std::size_t i = 0; i < 2 * kReasonCount; ++i) {
            const bool is_store = i >= kReasonCount;
            (is_store ? store_total : load_total) += delta[kCountOffset + i];
            (is_store ? store_ticks : load_ticks) += delta[kTickOffset + i];
        }
        const auto to_ns = [ticks_per_ns](std::uint64_t ticks) {
            return static_cast<std::uint64_t>(static_cast<double>(ticks) / ticks_per_ns);
        };
        std::string line = "access-stats window_ms=" + std::to_string(elapsed_ms) +
                           " loads=" + std::to_string(load_total) +
                           " stores=" + std::to_string(store_total) + " per_s=" +
                           std::to_string(elapsed_ms == 0 ? 0 : (load_total + store_total) * 1000 / elapsed_ms) +
                           " load_ms=" + std::to_string(to_ns(load_ticks) / 1000000) +
                           " store_ms=" + std::to_string(to_ns(store_ticks) / 1000000);
        for (std::size_t i = 0; i < 2 * kReasonCount; ++i) {
            line += std::string(i < kReasonCount ? " load_" : " store_") +
                    ReasonName(static_cast<Reason>(i % kReasonCount)) + "=" +
                    std::to_string(delta[kCountOffset + i]);
        }
        // Mean callback time per reason, loads and stores together; reasons that did not occur are skipped.
        for (std::size_t r = 0; r < kReasonCount; ++r) {
            const std::uint64_t count = delta[kCountOffset + r] + delta[kCountOffset + kReasonCount + r];
            if (count != 0) {
                const std::uint64_t ticks = delta[kTickOffset + r] + delta[kTickOffset + kReasonCount + r];
                line += std::string(" avg_ns_") + ReasonName(static_cast<Reason>(r)) + "=" +
                        std::to_string(to_ns(ticks) / count);
            }
        }
        Rebase(now_ns, now_ticks);
        return line;
    }

private:
    void Rebase(std::uint64_t now_ns, std::uint64_t now_ticks) {
        window_start_ns_ = now_ns;
        window_start_ticks_ = now_ticks;
        for (std::size_t i = 0; i < kCounterCount; ++i) {
            reported_[i] = counters_.Sum(i);
        }
    }

    Counters counters_;
    std::mutex report_lock_;
    bool window_started_ = false;
    std::uint64_t window_start_ns_ = 0;
    std::uint64_t window_start_ticks_ = 0;
    std::array<std::uint64_t, kCounterCount> reported_{};
};

} // namespace Core
