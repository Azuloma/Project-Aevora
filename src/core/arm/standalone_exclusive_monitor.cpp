// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <string_view>

#include "common/env_option.h"
#include "core/arm/standalone_exclusive_monitor.h"
#include "core/memory.h"

namespace Core {

StandaloneExclusiveMonitor::StandaloneExclusiveMonitor(Memory::Memory& memory_,
                                                       std::size_t core_count_)
    : state{core_count_}, memory{memory_} {}

StandaloneExclusiveMonitor::~StandaloneExclusiveMonitor() = default;

namespace {

// Reservations are marked without the global lock unless SUYU_EXCL_LOCKFREE_MARK is exactly "0".
const bool kLockFreeMarks = [] {
    const char* e = std::getenv("SUYU_EXCL_LOCKFREE_MARK");
    return Common::EnvFlagDefaultOn(e ? std::string_view{e} : std::string_view{});
}();

// The translating accesses, for pages GetPlainExclusivePointer declines; they run under the lock.
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
T StandaloneExclusiveMonitor::ExclusiveRead(std::size_t core_index, VAddr addr) {
    if (const u8* const host = memory.GetPlainExclusivePointer(addr, sizeof(T))) {
        return state.ReadAndMark<T>(core_index, addr, [host] { return LoadFromHost<T>(host); },
                                    kLockFreeMarks);
    }
    return state.ReadAndMark<T>(core_index, addr,
                                [&] { return ReadThroughMemory<T>(memory, addr); }, false);
}

template <typename T>
bool StandaloneExclusiveMonitor::ExclusiveWrite(std::size_t core_index, VAddr vaddr, T value) {
    if (u8* const host = memory.GetPlainExclusivePointer(vaddr, sizeof(T))) {
        return StoreExclusiveToHost<T>(state, core_index, vaddr, host, value,
                                       [&] { memory.NoteExclusiveWrite(vaddr, sizeof(T)); });
    }
    return state.DoExclusiveOperation<T>(core_index, vaddr, [&](T expected) -> bool {
        return WriteExclusiveThroughMemory<T>(memory, vaddr, value, expected);
    });
}

u8 StandaloneExclusiveMonitor::ExclusiveRead8(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u8>(core_index, addr);
}

u16 StandaloneExclusiveMonitor::ExclusiveRead16(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u16>(core_index, addr);
}

u32 StandaloneExclusiveMonitor::ExclusiveRead32(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u32>(core_index, addr);
}

u64 StandaloneExclusiveMonitor::ExclusiveRead64(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u64>(core_index, addr);
}

u128 StandaloneExclusiveMonitor::ExclusiveRead128(std::size_t core_index, VAddr addr) {
    return ExclusiveRead<u128>(core_index, addr);
}

void StandaloneExclusiveMonitor::ClearExclusive(std::size_t core_index) {
    state.ClearCore(core_index);
}

bool StandaloneExclusiveMonitor::ExclusiveWrite8(std::size_t core_index, VAddr vaddr, u8 value) {
    return ExclusiveWrite<u8>(core_index, vaddr, value);
}

bool StandaloneExclusiveMonitor::ExclusiveWrite16(std::size_t core_index, VAddr vaddr, u16 value) {
    return ExclusiveWrite<u16>(core_index, vaddr, value);
}

bool StandaloneExclusiveMonitor::ExclusiveWrite32(std::size_t core_index, VAddr vaddr, u32 value) {
    return ExclusiveWrite<u32>(core_index, vaddr, value);
}

bool StandaloneExclusiveMonitor::ExclusiveWrite64(std::size_t core_index, VAddr vaddr, u64 value) {
    return ExclusiveWrite<u64>(core_index, vaddr, value);
}

bool StandaloneExclusiveMonitor::ExclusiveWrite128(std::size_t core_index, VAddr vaddr, u128 value) {
    return ExclusiveWrite<u128>(core_index, vaddr, value);
}

} // namespace Core
