// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <string_view>

#include "common/env_option.h"
#include "core/arm/dynarmic/dynarmic_exclusive_access.h"
#include "core/arm/dynarmic/dynarmic_exclusive_monitor.h"
#include "core/memory.h"

namespace Core {

DynarmicExclusiveMonitor::DynarmicExclusiveMonitor(Memory::Memory& memory_, std::size_t core_count_)
    : monitor{core_count_}, memory{memory_} {}

DynarmicExclusiveMonitor::~DynarmicExclusiveMonitor() = default;

namespace {

// Reservations are marked without the global lock unless SUYU_EXCL_LOCKFREE_MARK is exactly "0".
const bool kLockFreeMarks = [] {
    const char* e = std::getenv("SUYU_EXCL_LOCKFREE_MARK");
    return Common::EnvFlagDefaultOn(e ? std::string_view{e} : std::string_view{});
}();

// The translating accesses, which do their rasterizer handling and logging as they go. They run
// under the monitor lock, so they are only for pages that GetPlainExclusivePointer declines.
template <typename T>
T ReadThroughMemory(Memory::Memory& memory, VAddr addr) {
    if constexpr (std::is_same_v<T, u8>) {
        return memory.Read8(addr);
    } else if constexpr (std::is_same_v<T, u16>) {
        return memory.Read16(addr);
    } else if constexpr (std::is_same_v<T, u32>) {
        return memory.Read32(addr);
    } else if constexpr (std::is_same_v<T, u64>) {
        return memory.Read64(addr);
    } else {
        return u128{memory.Read64(addr), memory.Read64(addr + sizeof(u64))};
    }
}

template <typename T>
bool WriteExclusiveThroughMemory(Memory::Memory& memory, VAddr addr, T value, T expected) {
    if constexpr (std::is_same_v<T, u8>) {
        return memory.WriteExclusive8(addr, value, expected);
    } else if constexpr (std::is_same_v<T, u16>) {
        return memory.WriteExclusive16(addr, value, expected);
    } else if constexpr (std::is_same_v<T, u32>) {
        return memory.WriteExclusive32(addr, value, expected);
    } else if constexpr (std::is_same_v<T, u64>) {
        return memory.WriteExclusive64(addr, value, expected);
    } else {
        return memory.WriteExclusive128(addr, value, expected);
    }
}

} // namespace

template <typename T>
T DynarmicExclusiveMonitor::ExclusiveRead(std::size_t core_index, VAddr addr) {
    // For a plain page the host pointer is resolved before the global monitor lock is taken, so
    // under it there is only the reservation and one load.
    if (const u8* const host = memory.GetPlainExclusivePointer(addr, sizeof(T))) {
        return ExclusiveAccess::ReadMarked<T>(monitor, core_index, addr, host, kLockFreeMarks);
    }
    return monitor.ReadAndMark<T>(core_index, addr,
                                  [&]() -> T { return ReadThroughMemory<T>(memory, addr); });
}

template <typename T>
bool DynarmicExclusiveMonitor::ExclusiveWrite(std::size_t core_index, VAddr vaddr, T value) {
    if (u8* const host = memory.GetPlainExclusivePointer(vaddr, sizeof(T))) {
        bool attempted = false;
        const bool stored =
            ExclusiveAccess::WriteCompared<T>(monitor, core_index, vaddr, host, value, &attempted);
        if (attempted) {
            // After the store and outside the lock; a lost reservation wrote nothing to report,
            // as Memory::WriteExclusive is never reached for it.
            memory.NoteExclusiveWrite(vaddr, sizeof(T));
        }
        return stored;
    }
    // Rasterizer-cached and unmapped pages: the write handling and its GPU-cache invalidation
    // must happen only for a store whose reservation holds, so it stays behind the check.
    return monitor.DoExclusiveOperation<T>(core_index, vaddr, [&](T expected) -> bool {
        return WriteExclusiveThroughMemory<T>(memory, vaddr, value, expected);
    });
}

u8 DynarmicExclusiveMonitor::ExclusiveRead8(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u8>(core_index, addr);
}

u16 DynarmicExclusiveMonitor::ExclusiveRead16(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u16>(core_index, addr);
}

u32 DynarmicExclusiveMonitor::ExclusiveRead32(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u32>(core_index, addr);
}

u64 DynarmicExclusiveMonitor::ExclusiveRead64(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u64>(core_index, addr);
}

u128 DynarmicExclusiveMonitor::ExclusiveRead128(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u128>(core_index, addr);
}

void DynarmicExclusiveMonitor::ClearExclusive(std::size_t core_index) {
    monitor.ClearProcessor(core_index);
}

bool DynarmicExclusiveMonitor::ExclusiveWrite8(std::size_t core_index, VAddr vaddr, u8 value) {
    return ExclusiveWrite<u8>(core_index, vaddr, value);
}

bool DynarmicExclusiveMonitor::ExclusiveWrite16(std::size_t core_index, VAddr vaddr, u16 value) {
    return ExclusiveWrite<u16>(core_index, vaddr, value);
}

bool DynarmicExclusiveMonitor::ExclusiveWrite32(std::size_t core_index, VAddr vaddr, u32 value) {
    return ExclusiveWrite<u32>(core_index, vaddr, value);
}

bool DynarmicExclusiveMonitor::ExclusiveWrite64(std::size_t core_index, VAddr vaddr, u64 value) {
    return ExclusiveWrite<u64>(core_index, vaddr, value);
}

bool DynarmicExclusiveMonitor::ExclusiveWrite128(std::size_t core_index, VAddr vaddr, u128 value) {
    return ExclusiveWrite<u128>(core_index, vaddr, value);
}

} // namespace Core
