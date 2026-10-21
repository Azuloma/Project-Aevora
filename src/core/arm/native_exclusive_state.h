// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

#include "common/atomic_ops.h"
#include "common/common_types.h"

namespace Core {

// Dynarmic::ExclusiveMonitor's reservation semantics without dynarmic or guest memory: a store
// clears the address on every core, and the lock is held until the store is done.
class NativeExclusiveState {
public:
    explicit NativeExclusiveState(std::size_t core_count_)
        : core_count{core_count_}, slots{std::make_unique<Slot[]>(core_count_)} {}

    /// Reserve `addr` and read through `load`; `lock_free` skips the lock, so the store side
    /// MUST keep comparing against the saved value.
    template <typename T, typename Load>
    T ReadAndMark(std::size_t core_index, VAddr addr, Load load, bool lock_free) {
        static_assert(std::is_trivially_copyable_v<T>);
        Slot& slot = slots[core_index];
        if (lock_free) {
            slot.address.store(addr, std::memory_order_seq_cst);
            return SaveValue<T>(slot, load());
        }
        LockGuard guard{*this};
        slot.address.store(addr, std::memory_order_relaxed);
        return SaveValue<T>(slot, load());
    }

    /// Runs `op(saved_value)` under the lock if the core still owns `addr`, after clearing every
    /// core's reservation of it; false without running `op` when the reservation is lost.
    template <typename T, typename Operation>
    bool DoExclusiveOperation(std::size_t core_index, VAddr addr, Operation op) {
        static_assert(std::is_trivially_copyable_v<T>);
        LockGuard guard{*this};
        Slot& own = slots[core_index];
        if (own.address.load(std::memory_order_relaxed) != addr) {
            return false;
        }
        for (std::size_t i = 0; i < core_count; ++i) {
            if (slots[i].address.load(std::memory_order_relaxed) == addr) {
                slots[i].address.store(kInvalidAddress, std::memory_order_relaxed);
            }
        }
        T saved;
        std::memcpy(&saved, own.value, sizeof(T));
        return op(saved);
    }

    void ClearCore(std::size_t core_index) {
        LockGuard guard{*this};
        slots[core_index].address.store(kInvalidAddress, std::memory_order_relaxed);
    }

private:
    static constexpr VAddr kInvalidAddress = 0xDEADDEADDEADDEADULL;

    // One cache line per core so a core marking its reservation does not bounce its neighbours'.
    struct alignas(64) Slot {
        std::atomic<VAddr> address{kInvalidAddress};
        alignas(16) u8 value[16]{};
    };

    // Test-and-test-and-set: waiters spin on a plain load, not on the cache line owner's write.
    class LockGuard {
    public:
        explicit LockGuard(NativeExclusiveState& state_) : state{state_} {
            while (state.lock_flag.exchange(true, std::memory_order_acquire)) {
                while (state.lock_flag.load(std::memory_order_relaxed)) {
#if defined(_M_X64) || defined(__x86_64__)
                    _mm_pause();
#endif
                }
            }
        }
        ~LockGuard() {
            state.lock_flag.store(false, std::memory_order_release);
        }
        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;

    private:
        NativeExclusiveState& state;
    };

    template <typename T>
    static T SaveValue(Slot& slot, const T& value) {
        std::memcpy(slot.value, &value, sizeof(T));
        return value;
    }

    std::size_t core_count;
    std::unique_ptr<Slot[]> slots;
    alignas(64) std::atomic<bool> lock_flag{false};
};

// The two halves of a guest exclusive pair for a host pointer to a plain mapped page.

template <typename T>
T LoadFromHost(const u8* host) {
    T value;
    if constexpr (std::is_same_v<T, u128>) {
        std::memcpy(&value[0], host, sizeof(u64));
        std::memcpy(&value[1], host + sizeof(u64), sizeof(u64));
    } else {
        std::memcpy(&value, host, sizeof(T));
    }
    return value;
}

template <typename T>
bool CompareAndSwapHost(u8* host, T value, T expected) {
    if constexpr (std::is_same_v<T, u128>) {
        return Common::AtomicCompareAndSwap(reinterpret_cast<u64*>(host), value, expected);
    } else {
        return Common::AtomicCompareAndSwap(reinterpret_cast<T*>(host), value, expected);
    }
}

/// A store-exclusive to a plain page. `notify_store` runs after the lock is released, and only
/// when the compare-and-swap ran, whether or not it matched.
template <typename T, typename Notify>
bool StoreExclusiveToHost(NativeExclusiveState& state, std::size_t core_index, VAddr addr,
                          u8* host, T value, Notify notify_store) {
    bool attempted = false;
    const bool stored = state.DoExclusiveOperation<T>(core_index, addr, [&](T expected) -> bool {
        attempted = true;
        return CompareAndSwapHost<T>(host, value, expected);
    });
    if (attempted) {
        notify_store();
    }
    return stored;
}

} // namespace Core
