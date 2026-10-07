// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>

#include <dynarmic/interface/exclusive_monitor.h>

#include "common/atomic_ops.h"
#include "common/common_types.h"

namespace Core::ExclusiveAccess {

// The two halves of a guest exclusive pair, for a host pointer to a plain mapped page that the
// caller resolved before the call (Memory::GetPlainExclusivePointer). Address translation can be
// long and the monitor lock is global, so under the lock there is only the reservation
// bookkeeping and one load or compare-and-swap. The pointer is never null.

template <typename T>
T ReadMarked(Dynarmic::ExclusiveMonitor& monitor, std::size_t core_index, VAddr addr,
             const u8* host) {
    return monitor.ReadAndMark<T>(core_index, addr, [host]() -> T {
        T value;
        if constexpr (std::is_same_v<T, u128>) {
            // Two 64-bit loads, as Memory::Read64 twice: no 16-byte atomic load is implied.
            std::memcpy(&value[0], host, sizeof(u64));
            std::memcpy(&value[1], host + sizeof(u64), sizeof(u64));
        } else {
            std::memcpy(&value, host, sizeof(T));
        }
        return value;
    });
}

/// `attempted` (optional) is set when the reservation held and the compare-and-swap ran, whether
/// or not it matched, which is exactly when the store has to be reported to the generation guard.
template <typename T>
bool WriteCompared(Dynarmic::ExclusiveMonitor& monitor, std::size_t core_index, VAddr addr,
                   u8* host, T value, bool* attempted = nullptr) {
    return monitor.DoExclusiveOperation<T>(core_index, addr, [=](T expected) -> bool {
        if (attempted != nullptr) {
            *attempted = true;
        }
        if constexpr (std::is_same_v<T, u128>) {
            return Common::AtomicCompareAndSwap(reinterpret_cast<u64*>(host), value, expected);
        } else {
            return Common::AtomicCompareAndSwap(reinterpret_cast<T*>(host), value, expected);
        }
    });
}

} // namespace Core::ExclusiveAccess
