// smoke_native_exclusive_unit: the dynarmic-free exclusive monitor state (native_exclusive_state.h)
// over host pages. "bench" as the first argument prints ns per operation against a mutex.
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "core/arm/native_exclusive_state.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

constexpr std::size_t kCores = 3;
constexpr int kIncrementsPerCore = 100000;
constexpr VAddr kAddrA = 0x1000;
constexpr VAddr kAddrB = 0x1010;

template <typename T>
T MarkedRead(Core::NativeExclusiveState& state, std::size_t core, VAddr addr, const u8* host,
             bool lock_free) {
    return state.ReadAndMark<T>(core, addr, [host] { return Core::LoadFromHost<T>(host); },
                                lock_free);
}

template <typename T>
bool Store(Core::NativeExclusiveState& state, std::size_t core, VAddr addr, u8* host, T value,
           bool* attempted = nullptr) {
    return state.DoExclusiveOperation<T>(core, addr, [=](T expected) -> bool {
        if (attempted != nullptr) {
            *attempted = true;
        }
        return Core::CompareAndSwapHost<T>(host, value, expected);
    });
}

// A broken model must fail the counter checks, not retry forever.
constexpr int kMaxRetriesPerIncrement = 1 << 22;

template <typename T>
bool Increment(Core::NativeExclusiveState& state, std::size_t core, u8* host, bool lock_free) {
    for (int attempt = 0; attempt < kMaxRetriesPerIncrement; ++attempt) {
        T value = MarkedRead<T>(state, core, kAddrA, host, lock_free);
        if constexpr (std::is_same_v<T, u128>) {
            value[0] += 1;
        } else {
            value += 1;
        }
        if (Store<T>(state, core, kAddrA, host, value)) {
            return true;
        }
    }
    return false;
}

template <typename T>
std::uint64_t CounterAfterIncrements(bool lock_free) {
    Core::NativeExclusiveState state{kCores};
    alignas(16) std::array<u8, 64> memory{};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kCores; ++core) {
        threads.emplace_back([&, core] {
            for (int i = 0; i < kIncrementsPerCore; ++i) {
                if (!Increment<T>(state, core, memory.data(), lock_free)) {
                    return;
                }
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

void Counters() {
    for (const bool lock_free : {false, true}) {
        const std::uint64_t expected = std::uint64_t{kCores} * kIncrementsPerCore;
        Check(CounterAfterIncrements<u32>(lock_free) == expected, "u32 counter loses no update");
        Check(CounterAfterIncrements<u64>(lock_free) == expected, "u64 counter loses no update");
        Check(CounterAfterIncrements<u128>(lock_free) == expected, "u128 counter loses no update");
    }
}

// All cores read the same old value, then store in turn: only the first store may land.
int RacingStoresThatLanded(bool use_reservation, bool lock_free) {
    Core::NativeExclusiveState state{kCores};
    alignas(16) std::array<u8, 64> memory{};
    std::barrier<> after_reads{kCores};
    std::atomic<int> turn{0};
    std::atomic<int> successes{0};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kCores; ++core) {
        threads.emplace_back([&, core] {
            const u32 old_value = MarkedRead<u32>(state, core, kAddrA, memory.data(), lock_free);
            after_reads.arrive_and_wait();
            while (turn.load() != static_cast<int>(core)) {
                std::this_thread::yield();
            }
            const u32 new_value = old_value + 1;
            bool stored = true;
            if (use_reservation) {
                stored = Store<u32>(state, core, kAddrA, memory.data(), new_value);
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
    return successes.load();
}

void SameOldValueRace() {
    for (const bool lock_free : {false, true}) {
        Check(RacingStoresThatLanded(true, lock_free) == 1, "exactly one racing store lands");
    }
    Check(RacingStoresThatLanded(false, false) == static_cast<int>(kCores),
          "negative control: unguarded stores all land");
}

void ReservationSemantics(bool lock_free) {
    Core::NativeExclusiveState state{kCores};
    alignas(16) std::array<u8, 64> memory{};
    u8* const a = memory.data();
    u8* const b = memory.data() + 16;
    u32 value{};

    Check(!Store<u32>(state, 0, kAddrA, a, 7), "store without reservation fails");
    std::memcpy(&value, a, sizeof(value));
    Check(value == 0, "failed store left memory unchanged");

    MarkedRead<u32>(state, 0, kAddrA, a, lock_free);
    MarkedRead<u32>(state, 1, kAddrA, a, lock_free);
    Check(Store<u32>(state, 1, kAddrA, a, 5), "owner's store succeeds");
    Check(!Store<u32>(state, 0, kAddrA, a, 9), "other core's reservation was cleared");
    std::memcpy(&value, a, sizeof(value));
    Check(value == 5, "only the winning store landed");

    MarkedRead<u32>(state, 0, kAddrA, a, lock_free);
    MarkedRead<u32>(state, 1, kAddrB, b, lock_free);
    Check(Store<u32>(state, 1, kAddrB, b, 3), "store to B succeeds");
    Check(Store<u32>(state, 0, kAddrA, a, 6), "reservation of A survives a store to B");

    bool attempted = false;
    Check(!Store<u32>(state, 2, kAddrA, a, 1, &attempted) && !attempted,
          "lost reservation does not attempt the store");
    MarkedRead<u32>(state, 2, kAddrA, a, lock_free);
    const u32 moved = 0x4321;
    std::memcpy(a, &moved, sizeof(moved));
    attempted = false;
    Check(!Store<u32>(state, 2, kAddrA, a, 1, &attempted) && attempted,
          "held reservation with a stale value still attempts the store");

    Check(!Store<u32>(state, 0, kAddrA, a, 8), "reservation is single-use");

    MarkedRead<u32>(state, 2, kAddrA, a, lock_free);
    state.ClearCore(2);
    Check(!Store<u32>(state, 2, kAddrA, a, 1), "cleared reservation fails");

    MarkedRead<u32>(state, 0, kAddrA, a, lock_free);
    MarkedRead<u32>(state, 1, kAddrA, a, lock_free);
    state.ClearCore(1);
    Check(Store<u32>(state, 0, kAddrA, a, 4), "other core's reservation survives ClearCore");

    MarkedRead<u32>(state, 0, kAddrA, a, lock_free);
    const u32 changed = 0x1234;
    std::memcpy(a, &changed, sizeof(changed));
    Check(!Store<u32>(state, 0, kAddrA, a, 2), "stale value fails the compare");

    MarkedRead<u128>(state, 0, kAddrB, b, lock_free);
    Check(Store<u128>(state, 0, kAddrB, b, u128{11, 22}), "128-bit store succeeds");
    const u128 pair = MarkedRead<u128>(state, 0, kAddrB, b, lock_free);
    Check(pair[0] == 11 && pair[1] == 22, "128-bit value read back");
    const u64 upper = 99;
    std::memcpy(b + sizeof(u64), &upper, sizeof(upper));
    Check(!Store<u128>(state, 0, kAddrB, b, u128{1, 2}), "upper-half change fails the compare");

    // Storing the value already in memory leaves the compare nothing to catch, so only the
    // reservation bookkeeping can fail the second store.
    const u32 same = 0x77;
    std::memcpy(a, &same, sizeof(same));
    MarkedRead<u32>(state, 0, kAddrA, a, lock_free);
    MarkedRead<u32>(state, 1, kAddrA, a, lock_free);
    Check(Store<u32>(state, 1, kAddrA, a, same), "same-value store by the owner succeeds");
    Check(!Store<u32>(state, 0, kAddrA, a, same), "same-value store clears the other reservation");
    Check(!Store<u32>(state, 1, kAddrA, a, same), "same-value reservation is single-use");

    // A narrow reservation is not satisfied by a different address on the same core.
    MarkedRead<u8>(state, 0, kAddrA, a, lock_free);
    Check(!Store<u8>(state, 0, kAddrB, b, 1), "reservation is per address");
}

// Each core stores to its own address, so only the lock can keep two callbacks from overlapping.
int OverlappingCallbacks() {
    Core::NativeExclusiveState state{kCores};
    alignas(16) std::array<u8, 64> memory{};
    std::barrier<> reserved{kCores};
    std::atomic<int> inside{0};
    std::atomic<int> overlaps{0};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kCores; ++core) {
        threads.emplace_back([&, core] {
            const VAddr addr = kAddrA + core * 16;
            u8* const host = memory.data() + core * 16;
            for (int i = 0; i < 2000; ++i) {
                MarkedRead<u32>(state, core, addr, host, true);
                reserved.arrive_and_wait();
                (void)state.DoExclusiveOperation<u32>(core, addr, [&](u32) -> bool {
                    overlaps += inside.fetch_add(1) != 0 ? 1 : 0;
                    for (int spin = 0; spin < 200; ++spin) {
                        std::this_thread::yield();
                    }
                    inside.fetch_sub(1);
                    return true;
                });
                reserved.arrive_and_wait();
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    return overlaps.load();
}

void StoreCallbacksExcludeEachOther() {
    Check(OverlappingCallbacks() == 0, "no two store callbacks run at once");
}

// The store notification runs after a compare-and-swap was attempted, matched or not, never before.
void StoreNotification() {
    Core::NativeExclusiveState state{kCores};
    alignas(16) std::array<u8, 64> memory{};
    u8* const a = memory.data();
    int notified = 0;
    const auto store = [&](std::size_t core, u32 value) {
        return Core::StoreExclusiveToHost<u32>(state, core, kAddrA, a, value, [&] { ++notified; });
    };

    Check(!store(0, 1) && notified == 0, "lost reservation is not reported");
    MarkedRead<u32>(state, 0, kAddrA, a, true);
    Check(store(0, 2) && notified == 1, "successful store is reported once");
    MarkedRead<u32>(state, 1, kAddrA, a, true);
    const u32 moved = 9;
    std::memcpy(a, &moved, sizeof(moved));
    Check(!store(1, 3) && notified == 2, "store that failed the compare is still reported");
    u32 value{};
    std::memcpy(&value, a, sizeof(value));
    Check(value == 9, "failed compare wrote nothing");
}

// The previous implementation, reproduced as the reference for the benchmark.
class MutexReference {
public:
    explicit MutexReference(std::size_t cores) : addresses(cores, kInvalid), values(cores) {}

    u32 Read(std::size_t core, VAddr addr, const u8* host) {
        std::lock_guard guard{lock};
        addresses[core] = addr;
        const u32 value = Core::LoadFromHost<u32>(host);
        values[core] = value;
        return value;
    }

    bool Write(std::size_t core, VAddr addr, u8* host, u32 value) {
        std::lock_guard guard{lock};
        if (addresses[core] != addr) {
            return false;
        }
        for (VAddr& other : addresses) {
            if (other == addr) {
                other = kInvalid;
            }
        }
        return Core::CompareAndSwapHost<u32>(host, value, values[core]);
    }

private:
    static constexpr VAddr kInvalid = 0xDEADDEADDEADDEADULL;
    std::mutex lock;
    std::vector<VAddr> addresses;
    std::vector<u32> values;
};

template <typename Step>
double NanosecondsPerIncrement(Step step) {
    alignas(16) std::array<u8, 64> memory{};
    std::barrier<> start{kCores + 1};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kCores; ++core) {
        threads.emplace_back([&, core] {
            start.arrive_and_wait();
            for (int i = 0; i < kIncrementsPerCore * 4; ++i) {
                step(core, memory.data());
            }
        });
    }
    start.arrive_and_wait();
    const auto begin = std::chrono::steady_clock::now();
    for (auto& thread : threads) {
        thread.join();
    }
    const std::chrono::duration<double, std::nano> elapsed = std::chrono::steady_clock::now() - begin;
    return elapsed.count() / (double{kCores} * kIncrementsPerCore * 4);
}

void Bench() {
    for (int round = 0; round < 3; ++round) {
        MutexReference reference{kCores};
        const double mutex_ns = NanosecondsPerIncrement([&](std::size_t core, u8* host) {
            while (!reference.Write(core, kAddrA, host, reference.Read(core, kAddrA, host) + 1)) {
            }
        });
        double native_ns[2]{};
        for (const bool lock_free : {false, true}) {
            Core::NativeExclusiveState state{kCores};
            native_ns[lock_free] = NanosecondsPerIncrement([&](std::size_t core, u8* host) {
                Increment<u32>(state, core, host, lock_free);
            });
        }
        std::printf("bench round %d: mutex %.1f ns, native locked %.1f ns, native lock-free %.1f ns "
                    "per increment (%zu cores)\n",
                    round, mutex_ns, native_ns[0], native_ns[1], kCores);

        // A guest spin-wait loop: load-exclusive over and over with no store.
        const double mutex_read_ns = NanosecondsPerIncrement([&](std::size_t core, u8* host) {
            (void)reference.Read(core, kAddrA, host);
        });
        double read_ns[2]{};
        for (const bool lock_free : {false, true}) {
            Core::NativeExclusiveState state{kCores};
            read_ns[lock_free] = NanosecondsPerIncrement([&](std::size_t core, u8* host) {
                (void)MarkedRead<u32>(state, core, kAddrA, host, lock_free);
            });
        }
        std::printf("bench round %d: spin-wait loads: mutex %.1f ns, native locked %.1f ns, native "
                    "lock-free %.1f ns per load\n",
                    round, mutex_read_ns, read_ns[0], read_ns[1]);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        Bench();
        return 0;
    }
    // Deterministic checks first, so a broken model fails before any contended run.
    ReservationSemantics(false);
    ReservationSemantics(true);
    StoreNotification();
    if (g_failures == 0) {
        StoreCallbacksExcludeEachOther();
        SameOldValueRace();
        Counters();
    }
    if (g_failures != 0) {
        std::printf("native_exclusive_unit: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("native_exclusive_unit: ok");
    return 0;
}
