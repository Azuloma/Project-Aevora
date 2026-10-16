// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Exercises Core::ExclusiveAccess against the real ExclusiveMonitor and spin lock, with buffers as
// guest memory; Memory::GetExclusivePointer needs a Core::System and is covered by the PLA run.

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

#include "core/arm/dynarmic/dynarmic_exclusive_access.h"
#include "dynarmic/backend/x64/xbyak.h"
#include "dynarmic/common/spin_lock_x64.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

bool g_lock_free = false;

template <typename T>
T MarkedRead(Dynarmic::ExclusiveMonitor& monitor, std::size_t core, VAddr addr, const std::uint8_t* host) {
    return Core::ExclusiveAccess::ReadMarked<T>(monitor, core, addr, host, g_lock_free);
}

constexpr std::size_t kCores = 3;
constexpr int kIncrementsPerCore = 200000;

// One increment as a guest ldxr/stxr retry loop.
template <typename T>
void IncrementExclusive(Dynarmic::ExclusiveMonitor& monitor, std::size_t core, VAddr addr,
                        std::uint8_t* host) {
    for (;;) {
        T value = MarkedRead<T>(monitor, core, addr, host);
        if constexpr (std::is_same_v<T, u128>) {
            value[0] += 1;
        } else {
            value += 1;
        }
        if (Core::ExclusiveAccess::WriteCompared<T>(monitor, core, addr, host, value)) {
            return;
        }
    }
}

template <typename T, typename Step>
std::uint64_t RunCounter(Step step) {
    Dynarmic::ExclusiveMonitor monitor{kCores};
    alignas(16) std::array<std::uint8_t, 64> memory{};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kCores; ++core) {
        threads.emplace_back([&, core] {
            for (int i = 0; i < kIncrementsPerCore; ++i) {
                step(monitor, core, VAddr{0x1000}, memory.data());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    T value{};
    std::memcpy(&value, memory.data(), sizeof(T));
    if constexpr (std::is_same_v<T, u128>) {
        return value[0];
    } else {
        return value;
    }
}

template <typename T>
void CounterTest(const char* name) {
    std::uint64_t expected = std::uint64_t{kCores} * kIncrementsPerCore;
    if constexpr (!std::is_same_v<T, u128> && sizeof(T) < sizeof(std::uint64_t)) {
        expected &= (std::numeric_limits<T>::max)(); // the counter wraps at its width
    }
    const std::uint64_t actual = RunCounter<T>(IncrementExclusive<T>);
    std::printf("%s counter: %llu (expected %llu)\n", name,
                static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
    Check(actual == expected, name);
}

// All cores read the same old value, then store in a fixed order (barriers keep the plain stores
// race-free). The protocol lets only the first store land; the negative control lets all succeed.
void SameOldValueRace(bool use_reservation) {
    Dynarmic::ExclusiveMonitor monitor{kCores};
    alignas(16) std::array<std::uint8_t, 64> memory{};
    std::barrier<> after_reads{kCores};
    std::atomic<int> turn{0};
    std::atomic<int> successes{0};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kCores; ++core) {
        threads.emplace_back([&, core] {
            const std::uint32_t old_value =
                MarkedRead<std::uint32_t>(monitor, core, 0x1000, memory.data());
            after_reads.arrive_and_wait();
            while (turn.load() != static_cast<int>(core)) {
                std::this_thread::yield();
            }
            const std::uint32_t new_value = old_value + 1;
            bool stored = true;
            if (use_reservation) {
                stored = Core::ExclusiveAccess::WriteCompared<std::uint32_t>(
                    monitor, core, 0x1000, memory.data(), new_value);
            } else {
                std::memcpy(memory.data(), &new_value, sizeof(new_value));
            }
            successes += stored ? 1 : 0;
            turn.store(static_cast<int>(core) + 1);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    std::uint32_t value{};
    std::memcpy(&value, memory.data(), sizeof(value));
    std::printf("same-old-value race (%s): %d stores succeeded, counter %u\n",
                use_reservation ? "protocol" : "negative control", successes.load(), value);
    if (use_reservation) {
        Check(successes == 1 && value == 1, "exactly one of three racing stores lands");
    } else {
        Check(successes == static_cast<int>(kCores) && value == 1,
              "negative control must show every store succeeding for one counter step");
    }
}

void ReservationSemantics() {
    Dynarmic::ExclusiveMonitor monitor{kCores};
    alignas(16) std::array<std::uint8_t, 64> memory{};
    std::uint8_t* const a = memory.data();
    std::uint8_t* const b = memory.data() + 16;
    constexpr VAddr kAddrA = 0x1000;
    constexpr VAddr kAddrB = 0x1010;

    using namespace Core::ExclusiveAccess;
    // A store without a reservation fails and does not write.
    Check(!WriteCompared<std::uint32_t>(monitor, 0, kAddrA, a, 7), "store without reservation fails");
    std::uint32_t value{};
    std::memcpy(&value, a, sizeof(value));
    Check(value == 0, "failed store left memory unchanged");

    // A store by another core clears this core's reservation of the same address.
    MarkedRead<std::uint32_t>(monitor, 0, kAddrA, a);
    MarkedRead<std::uint32_t>(monitor, 1, kAddrA, a);
    Check(WriteCompared<std::uint32_t>(monitor, 1, kAddrA, a, 5), "owner's store succeeds");
    Check(!WriteCompared<std::uint32_t>(monitor, 0, kAddrA, a, 9), "other core's reservation was cleared");
    std::memcpy(&value, a, sizeof(value));
    Check(value == 5, "only the winning store landed");

    // A store to a different address leaves the reservation alone.
    MarkedRead<std::uint32_t>(monitor, 0, kAddrA, a);
    MarkedRead<std::uint32_t>(monitor, 1, kAddrB, b);
    Check(WriteCompared<std::uint32_t>(monitor, 1, kAddrB, b, 3), "store to B succeeds");
    Check(WriteCompared<std::uint32_t>(monitor, 0, kAddrA, a, 6), "reservation of A survives a store to B");

    // `attempted` reports whether the compare-and-swap ran: not for a lost reservation, yes for a
    // store that held its reservation whether or not the compare matched.
    bool attempted = false;
    Check(!WriteCompared<std::uint32_t>(monitor, 2, kAddrA, a, 1, &attempted) && !attempted,
          "lost reservation does not attempt the store");
    MarkedRead<std::uint32_t>(monitor, 2, kAddrA, a);
    const std::uint32_t moved = 0x4321;
    std::memcpy(a, &moved, sizeof(moved));
    attempted = false;
    Check(!WriteCompared<std::uint32_t>(monitor, 2, kAddrA, a, 1, &attempted) && attempted,
          "held reservation with a stale value still attempts the store");

    // A reservation is consumed by its store.
    Check(!WriteCompared<std::uint32_t>(monitor, 0, kAddrA, a, 8), "reservation is single-use");

    // ClearProcessor drops it.
    MarkedRead<std::uint32_t>(monitor, 2, kAddrA, a);
    monitor.ClearProcessor(2);
    Check(!WriteCompared<std::uint32_t>(monitor, 2, kAddrA, a, 1), "cleared reservation fails");

    // A value that changed behind the monitor's back fails the compare even with the reservation.
    MarkedRead<std::uint32_t>(monitor, 0, kAddrA, a);
    const std::uint32_t changed = 0x1234;
    std::memcpy(a, &changed, sizeof(changed));
    Check(!WriteCompared<std::uint32_t>(monitor, 0, kAddrA, a, 2), "stale value fails the compare");

    // 128-bit pair.
    MarkedRead<u128>(monitor, 0, kAddrB, b);
    Check(WriteCompared<u128>(monitor, 0, kAddrB, b, u128{11, 22}), "128-bit store succeeds");
    const u128 pair = MarkedRead<u128>(monitor, 0, kAddrB, b);
    Check(pair[0] == 11 && pair[1] == 22, "128-bit value read back");

    // A change to only the upper half of a 128-bit value fails the compare.
    MarkedRead<u128>(monitor, 0, kAddrB, b);
    const std::uint64_t upper = 99;
    std::memcpy(b + sizeof(std::uint64_t), &upper, sizeof(upper));
    Check(!WriteCompared<u128>(monitor, 0, kAddrB, b, u128{1, 2}), "upper-half change fails the compare");

    // ClearProcessor drops only that core's reservation.
    MarkedRead<std::uint32_t>(monitor, 0, kAddrA, a);
    MarkedRead<std::uint32_t>(monitor, 1, kAddrA, a);
    monitor.ClearProcessor(1);
    Check(WriteCompared<std::uint32_t>(monitor, 0, kAddrA, a, 4), "other core's reservation survives ClearProcessor");
}

using LockFn = void (*)(volatile int*);

enum class LockKind { Default, Ttas, NoLock };

// The pre-TTAS lock sequence, written out here so a change to the default emission is caught.
void EmitLegacyLock(Xbyak::CodeGenerator& code, Xbyak::Reg64 ptr, Xbyak::Reg32 tmp) {
    Xbyak::Label start, loop;
    code.jmp(start, code.T_NEAR);
    code.L(loop);
    code.pause();
    code.L(start);
    code.mov(tmp, 1);
    code.xchg(code.dword[ptr], tmp);
    code.test(tmp, tmp);
    code.jnz(loop, code.T_NEAR);
}

#ifdef _WIN32
const Xbyak::Reg64 kParam = Xbyak::util::rcx;
const Xbyak::Reg32 kScratch = Xbyak::util::eax;
#else
const Xbyak::Reg64 kParam = Xbyak::util::rdi;
const Xbyak::Reg32 kScratch = Xbyak::util::eax;
#endif

struct EmittedLock {
    Xbyak::CodeGenerator code{4096};
    LockFn lock = nullptr;
    LockFn unlock = nullptr;
    std::vector<std::uint8_t> lock_bytes;

    explicit EmittedLock(LockKind kind) {
        code.align();
        lock = code.getCurr<LockFn>();
        const std::uint8_t* const lock_begin = code.getCurr();
        switch (kind) {
        case LockKind::Default:
            Dynarmic::EmitSpinLockLock(code, kParam, kScratch, false);
            break;
        case LockKind::Ttas:
            Dynarmic::EmitSpinLockLock(code, kParam, kScratch, false, true);
            break;
        case LockKind::NoLock:
            code.mov(code.dword[kParam], 1); // negative control: takes the lock without checking it
            break;
        }
        lock_bytes.assign(lock_begin, code.getCurr());
        code.ret();
        code.align();
        unlock = code.getCurr<LockFn>();
        Dynarmic::EmitSpinLockUnlock(code, kParam, kScratch);
        code.ret();
    }
};

// While one thread holds the lock, a second must not get in until the release.
bool ExcludesWhileHeld(const EmittedLock& emitted) {
    volatile int word = 0;
    std::atomic<bool> second_started{false};
    std::atomic<bool> second_entered{false};
    emitted.lock(&word);
    std::thread second([&] {
        second_started = true;
        emitted.lock(&word);
        second_entered = true;
        emitted.unlock(&word);
    });
    while (!second_started.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const bool excluded = !second_entered.load();
    emitted.unlock(&word);
    second.join();
    return excluded && second_entered.load();
}

// Plain load and store to a shared atomic: lost updates show up when the lock fails to exclude.
std::uint64_t LockedIncrements(const EmittedLock& emitted) {
    volatile int word = 0;
    std::atomic<std::uint64_t> counter{0};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kCores; ++core) {
        threads.emplace_back([&] {
            for (int i = 0; i < kIncrementsPerCore; ++i) {
                emitted.lock(&word);
                counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                emitted.unlock(&word);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    return counter.load();
}

void SpinLockEmission() {
    // Default emission equals the legacy sequence byte for byte; the TTAS one differs and re-reads
    // the lock word with `cmp dword [ptr], 0` while waiting.
    Xbyak::CodeGenerator reference{4096};
    const std::uint8_t* const reference_begin = reference.getCurr();
    EmitLegacyLock(reference, kParam, kScratch);
    const std::vector<std::uint8_t> legacy(reference_begin, reference.getCurr());
    const EmittedLock plain{LockKind::Default};
    const EmittedLock ttas{LockKind::Ttas};
    Check(plain.lock_bytes == legacy, "default emission is unchanged");
    Check(ttas.lock_bytes != legacy, "TTAS emission differs from the default");
    const std::uint8_t cmp_probe[] = {0x83, 0x39, 0x00};
    const auto contains_probe = [&](const std::vector<std::uint8_t>& bytes) {
        return std::search(bytes.begin(), bytes.end(), std::begin(cmp_probe), std::end(cmp_probe)) !=
               bytes.end();
    };
#ifdef _WIN32
    Check(contains_probe(ttas.lock_bytes), "TTAS emission waits on a plain load");
    Check(!contains_probe(plain.lock_bytes), "default emission does not wait on a plain load");
#endif
    if (g_failures != 0) {
        return; // a broken sequence could spin forever in the execution checks below
    }

    Check(ExcludesWhileHeld(plain), "default lock excludes while held");
    Check(ExcludesWhileHeld(ttas), "TTAS lock excludes while held");
    const EmittedLock no_lock{LockKind::NoLock};
    Check(!ExcludesWhileHeld(no_lock), "negative control: a lock that does not check must let the second in");

    const std::uint64_t expected = std::uint64_t{kCores} * kIncrementsPerCore;
    Check(LockedIncrements(plain) == expected, "default lock keeps every increment");
    Check(LockedIncrements(ttas) == expected, "TTAS lock keeps every increment");
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // The environment decides the shared stub; the runner passes what it set.
    const bool expect_ttas = argc > 1 && std::strcmp(argv[1], "ttas") == 0;
    Check(Dynarmic::SpinLockUsesTtas() == expect_ttas, "SUYU_SPINLOCK_TTAS selects the shared stub");
    SpinLockEmission();
    if (g_failures != 0) {
        std::printf("FAILED (%d)\n", g_failures);
        return 1;
    }
    for (const bool lock_free : {false, true}) {
        g_lock_free = lock_free;
        std::printf("-- marks %s\n", lock_free ? "lock-free" : "locked");
        // The deterministic checks come first: a monitor that never marks fails them before the
        // counter loops, which would retry forever.
        ReservationSemantics();
        SameOldValueRace(true);
        SameOldValueRace(false);
        if (g_failures != 0) {
            std::printf("FAILED (%d)\n", g_failures);
            return 1;
        }
        CounterTest<std::uint8_t>("u8");
        CounterTest<std::uint16_t>("u16");
        CounterTest<std::uint32_t>("u32");
        CounterTest<std::uint64_t>("u64");
        CounterTest<u128>("u128");
    }
    std::printf(g_failures == 0 ? "PASS\n" : "FAILED (%d)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
