// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/native_memory/native_address_space.h"

// Shadow comparison of the native mapper against KPageTable (N2 stage 2); knows no kernel types.
// The caller adopts the real table before each operation, then compares results and touched blocks.
namespace Core::NativeMemory {

enum class ShadowOp : u32 {
    MapMemory,
    UnmapMemory,
    SetMemoryPermission,
    SetMemoryAttribute,
    SetHeapSize,
    MapPhysicalMemory,
    UnmapPhysicalMemory,
    Count,
};

constexpr std::string_view ShadowOpName(ShadowOp op) {
    switch (op) {
    case ShadowOp::MapMemory:
        return "MapMemory";
    case ShadowOp::UnmapMemory:
        return "UnmapMemory";
    case ShadowOp::SetMemoryPermission:
        return "SetMemoryPermission";
    case ShadowOp::SetMemoryAttribute:
        return "SetMemoryAttribute";
    case ShadowOp::SetHeapSize:
        return "SetHeapSize";
    case ShadowOp::MapPhysicalMemory:
        return "MapPhysicalMemory";
    case ShadowOp::UnmapPhysicalMemory:
        return "UnmapPhysicalMemory";
    case ShadowOp::Count:
        break;
    }
    return "Unknown";
}

struct ShadowCounts {
    u64 compared = 0;
    u64 result_mismatch = 0;
    u64 state_mismatch = 0;
    u64 boundary_only = 0;
    u64 skipped = 0;
};

class Shadow {
public:
    static constexpr size_t MaxPendingReports = 32;

    Shadow(const Layout& layout, u64 physical_capacity_bytes)
        : space_(layout, physical_capacity_bytes) {}

    std::mutex& Mutex() {
        return mutex_;
    }
    AddressSpace& Space() {
        return space_;
    }

    void Sync(const std::vector<StoredBlock>& observed, u64 physical_bytes_in_use) {
        if (!space_.ReplaceBlocks(observed, physical_bytes_in_use)) {
            ++sync_failures_;
        }
    }

    // An overlapping call changed the table under the observation, so nothing was compared.
    void CountSkipped(ShadowOp op) {
        ++counts_[static_cast<size_t>(op)].skipped;
    }

    // Counts the operation and reports whether the shadow returned the kernel's result.
    bool CompareResult(ShadowOp op, u64 address, u64 size, Result real, Result shadow) {
        ShadowCounts& counts = counts_[static_cast<size_t>(op)];
        ++counts.compared;
        if (real == shadow) {
            return true;
        }
        ++counts.result_mismatch;
        Report(op, address, size, std::string("result real=") + std::string(ResultName(real)) +
                                      " shadow=" + std::string(ResultName(shadow)));
        return false;
    }

    // Compares the shadow with the real blocks covering [address, address + size) after the operation.
    // Physical map/unmap checks only the state of pages that were already mapped before it ran.
    void CompareBlocks(ShadowOp op, u64 address, u64 size,
                       const std::vector<StoredBlock>& real_blocks,
                       const std::vector<StoredBlock>& before_blocks = {}) {
        ShadowCounts& counts = counts_[static_cast<size_t>(op)];
        const u64 range_end = address + size;
        for (const StoredBlock& real : real_blocks) {
            const u64 real_end = real.base + real.size;
            u64 cursor = real.base < address ? address : real.base;
            const u64 piece_end = real_end < range_end ? real_end : range_end;
            const u8 real_user_permission = static_cast<u8>(real.permission & PermUserMask);
            for (; cursor < piece_end;) {
                const MemoryInfo shadow = space_.QueryInfo(cursor);
                const bool state_only = IsPhysicalOp(op) && WasMapped(before_blocks, cursor) &&
                                        real.state != MemoryState::Free;
                const bool differs =
                    shadow.state != real.state ||
                    (!state_only && (shadow.permission != real_user_permission ||
                                     shadow.attribute != real.attribute));
                if (differs) {
                    ++counts.state_mismatch;
                    Report(op, cursor, size,
                           "block real=" + Describe(real.state, real_user_permission, real.attribute) +
                               " shadow=" + Describe(shadow.state, shadow.permission, shadow.attribute));
                } else if (shadow.base != real.base || shadow.size != real.size) {
                    ++counts.boundary_only;
                }
                const u64 shadow_end = shadow.base + shadow.size;
                const u64 before_end =
                    IsPhysicalOp(op) ? BeforeBlockEnd(before_blocks, cursor) : ~0ULL;
                cursor = shadow_end < before_end ? shadow_end : before_end;
            }
        }
    }

    const ShadowCounts& Counts(ShadowOp op) const {
        return counts_[static_cast<size_t>(op)];
    }
    u64 SyncFailures() const {
        return sync_failures_;
    }
    u64 TotalMismatches() const {
        u64 total = sync_failures_;
        for (const ShadowCounts& counts : counts_) {
            total += counts.result_mismatch + counts.state_mismatch;
        }
        return total;
    }

    std::vector<std::string> TakeReports() {
        std::vector<std::string> taken;
        taken.swap(pending_reports_);
        return taken;
    }

    std::string Summary() const {
        std::string text = "sync_failures=" + std::to_string(sync_failures_);
        for (size_t index = 0; index < counts_.size(); ++index) {
            const ShadowCounts& counts = counts_[index];
            text += ' ';
            text += ShadowOpName(static_cast<ShadowOp>(index));
            text += '=' + std::to_string(counts.compared) + '/' +
                    std::to_string(counts.result_mismatch) + '/' +
                    std::to_string(counts.state_mismatch) + '/' +
                    std::to_string(counts.boundary_only) + '/' +
                    std::to_string(counts.skipped);
        }
        return text;
    }

private:
    static bool IsPhysicalOp(ShadowOp op) {
        return op == ShadowOp::MapPhysicalMemory || op == ShadowOp::UnmapPhysicalMemory;
    }
    static u64 BeforeBlockEnd(const std::vector<StoredBlock>& blocks, u64 address) {
        for (const StoredBlock& block : blocks) {
            if (block.base <= address && address < block.base + block.size) {
                return block.base + block.size;
            }
        }
        return ~0ULL;
    }
    static bool WasMapped(const std::vector<StoredBlock>& blocks, u64 address) {
        for (const StoredBlock& block : blocks) {
            if (block.base <= address && address < block.base + block.size) {
                return block.state != MemoryState::Free;
            }
        }
        return false;
    }
    static std::string Hex(u64 value) {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
        return buffer;
    }
    static std::string Describe(MemoryState state, u8 permission, u8 attribute) {
        return Hex(static_cast<u32>(state)) + "/p" + Hex(permission) + "/a" + Hex(attribute);
    }

    void Report(ShadowOp op, u64 address, u64 size, const std::string& what) {
        if (pending_reports_.size() < MaxPendingReports) {
            pending_reports_.push_back(std::string(ShadowOpName(op)) + " addr=" + Hex(address) +
                                       " size=" + Hex(size) + " " + what);
        }
    }

    std::mutex mutex_;
    AddressSpace space_;
    std::array<ShadowCounts, static_cast<size_t>(ShadowOp::Count)> counts_{};
    u64 sync_failures_ = 0;
    std::vector<std::string> pending_reports_;
};

} // namespace Core::NativeMemory
