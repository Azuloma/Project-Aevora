// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <map>
#include <optional>
#include <string_view>
#include <thread>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/logging/log.h"
#include "common/page_table.h"
#include "common/string_util.h"
#include "common/fs/path_util.h"
#include "core/arm/recomp/arm_recomp.h"
#include "core/arm/recomp/guest_fp_env.h"
#include "core/arm/recomp/recomp_bridge_required.h"
#include "core/arm/recomp/recomp_gap_session.h"
#include "core/arm/recomp/recomp_exclusive_stats.h"
#include "core/arm/recomp/recomp_exit_targets.h"
#include "core/arm/recomp/recomp_lookup_stats.h"
#include "core/arm/recomp/recomp_diagnostic_sampler.h"
#include "core/arm/recomp/recomp_sharded_counters.h"
#include "core/arm/recomp/recomp_thread_time.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/k_lock_trace.h"
#include "core/hle/kernel/k_process.h"
#include "core/hardware_properties.h"
#include "core/arm/debug.h"
#ifndef SUYU_NO_JIT
#include "core/arm/dynarmic/arm_dynarmic_64.h"
#include "core/arm/dynarmic/dynarmic_exclusive_monitor.h"
#endif
#include "core/arm/exclusive_monitor.h"
#include "core/memory.h"

namespace Core {

namespace {
// Prefix of the recompiler's emitted GuestContext; only fields the emulator touches are modelled.
// Later members (heap bookkeeping, save-data handles) are managed by the generated code.
struct GuestContextView {
    u64 x[32];
    u64 pc;
    u8 n, z, c, v;
    u8* mem;
    u64 mem_size;
    u64 mem_base_vaddr;
    int halted;
    u64 pending_svc;
    // SIMD/FP registers and the thread pointer follow pending_svc; a context switch must carry them
    // or every live FP/vector register is lost.
    u64 vreg[32][2];
    u64 tpidr_el0;
    const void* host_mem;
    // Kernel-published TLS region (first 0x100 bytes are the IPC buffer); kept apart from the guest's
    // tpidr_el0, see GuestContext in core/recompiler/arm64_to_c.h.
    u64 tpidrro_el0;
    // FP control/status; must survive the marshal to the fallback JIT or the rounding mode resets.
    u64 fpcr;
    u64 fpsr;
    // Must stay here: the emitted struct continues with members this view does not model.
    int chain_budget;
    // ABI 6 (FM1): page table and the page-aligned limit below which generated helpers read it directly;
    // fm_limit 0 keeps the ABI 5 path, which never reads these.
    u32 fm_reserved;
    const u8* fm_table;
    u64 fm_limit;
};

// Matches RecompHostMem in the generated runtime; recompiled code accesses guest memory through it
// so it and the HLE kernel see the same address space.
struct RecompHostMem {
    void* user;
    u64 (*load)(void* user, u64 va, u32 size);
    void (*store)(void* user, u64 va, u32 size, u64 value);
    // Exclusive access, routed at the kernel's own monitor so recompiled code
    // and the fallback JIT contend correctly against each other.
    u64 (*excl_load)(void* user, u64 va, u32 size);
    u32 (*excl_store)(void* user, u64 va, u32 size, u64 value);
    void (*clear_excl)(void* user);
    // The physical counter, read from the emulator's timing source so the
    // recompiled code and the JIT agree about time.
    u64 (*read_cntpct)(void* user);
    // Exclusive pair forms; `size` is the width of one register, 4 or 8.
    void (*excl_load_pair)(void* user, u64 va, u32 size, u64* lo, u64* hi);
    // Page table for inline address resolution; a null backing pointer (unmapped, debug, GPU-tracked)
    // falls through to load/store so rasterizer invalidation still happens.
    u32 (*excl_store_pair)(void* user, u64 va, u32 size, u64 lo, u64 hi);
    const void* page_entries;
    u64 page_entry_stride;
    u64 page_bits;
    u64 pointer_mask;
    u64 address_space_max;
    // Reserved for layout compatibility; ABI 5 never reads this slot.
    const u64* guard_generation;
};

// Duplicated by hand in the emitter (arm64_to_c.h, RuntimeH) with no shared header, so a field added at
// different positions on each side compiles cleanly but breaks at runtime; these asserts pin the layout.
static_assert(offsetof(RecompHostMem, excl_load_pair) == 56);
static_assert(offsetof(RecompHostMem, excl_store_pair) == 64);
static_assert(offsetof(RecompHostMem, page_entries) == 72);
static_assert(offsetof(RecompHostMem, page_entry_stride) == 80);
static_assert(offsetof(RecompHostMem, page_bits) == 88);
static_assert(offsetof(RecompHostMem, pointer_mask) == 96);
static_assert(offsetof(RecompHostMem, address_space_max) == 104);
static_assert(offsetof(RecompHostMem, guard_generation) == 112);
static_assert(sizeof(RecompHostMem) == 120);

// The generated runtime asserts the same offsets against its own GuestContext, so an inserted
// (not appended) field fails to compile on one side.
static_assert(offsetof(GuestContextView, pc) == 256);
static_assert(offsetof(GuestContextView, pending_svc) == 304);
static_assert(offsetof(GuestContextView, vreg) == 312);
static_assert(offsetof(GuestContextView, tpidr_el0) == 824);
static_assert(offsetof(GuestContextView, chain_budget) == 864);
static_assert(offsetof(GuestContextView, fm_table) == 872);
static_assert(offsetof(GuestContextView, fm_limit) == 880);
// FM1 helpers hard-code the page table layout (RECOMP_FM_PAGE_BITS, _STRIDE_LOG2, _PTR_MASK);
// a change must fail the build.
static_assert(sizeof(Common::PageTable::PageEntryData) == 32);
static_assert(Common::PageTable::ATTRIBUTE_BITS == 2);
static_assert(Memory::YUZU_PAGEBITS == 12);
// GG1: the generated store helpers read the watch word at this offset.
static_assert(offsetof(Common::PageTable::PageEntryData, recomp_watch) ==
              RecompGuardGen::kWatchOffset);
static_assert(sizeof(std::atomic<u64>) == sizeof(u64) && std::atomic<u64>::is_always_lock_free);

// Blocks a direct-call chain may run before returning to the dispatcher; only this side sets it.
// It bounds interrupt latency and host stack depth; 256 is mid-plateau for PLA (measurements: OLD_HISTORY.md).
constexpr int kShortSliceChainBudget = 32;
constexpr int kLongSliceChainBudget = 256;

// Refuse the JIT entirely, so reaching it is a fatal error naming the address, not just an observation.
bool StrictNoFallback() {
    const char* e = std::getenv("SUYU_RECOMP_STRICT");
    return e && *e && *e != '0';
}

const int kChainBudgetOverride = [] {
    const char* e = std::getenv("SUYU_RECOMP_CHAIN_BUDGET");
    if (!e) {
        return 0;
    }
    const int v = std::atoi(e);
    return (v >= 1 && v <= 8192) ? v : 0;
}();
std::atomic<int> g_recomp_chain_budget{kShortSliceChainBudget};

int RecompChainBudget() {
    return kChainBudgetOverride ? kChainBudgetOverride
                                : g_recomp_chain_budget.load(std::memory_order_relaxed);
}

const bool kSamplePc = [] {
    const char* e = std::getenv("SUYU_RECOMP_SAMPLE_PC");
    return e && *e && *e != '0';
}();

// Blocks a thread retires between PC samples, as a power of two. The default weights samples by executed
// blocks; lower it (with SUYU_RECOMP_SAMPLE_AFTER_SEC) to sample a stall plateau instead of the boot burst.
const unsigned kSamplePcShift = [] {
    const char* e = std::getenv("SUYU_RECOMP_SAMPLE_SHIFT");
    if (!e || !*e) {
        return 18u;
    }
    const int v = std::atoi(e);
    return (v >= 6 && v <= 30) ? static_cast<unsigned>(v) : 18u;
}();

// Seconds of run time to discard before any sample is kept. 0 keeps everything.
const double kSampleAfterSec = [] {
    const char* e = std::getenv("SUYU_RECOMP_SAMPLE_AFTER_SEC");
    return (e && *e) ? std::atof(e) : 0.0;
}();

const std::chrono::steady_clock::time_point kRecompStart = std::chrono::steady_clock::now();

// SUYU_RECOMP_THREAD_TIME=1: log how long each guest thread ran on each core. Off unless exactly
// "1"; it adds two clock reads per RunThread call, so it is a measurement switch, not a default.
const bool kThreadTime = [] {
    const char* e = std::getenv("SUYU_RECOMP_THREAD_TIME");
    return e && std::string_view{e} == "1";
}();
constexpr std::uint64_t kThreadTimeWindowNs = 10'000'000'000ULL;
constexpr std::size_t kThreadTimeTopCount = 6;

std::uint64_t SteadyNowNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// Times one RunThread call for the ledger, however the call returns.
class ThreadTimeScope {
public:
    ThreadTimeScope(RecompThreadTimeLedger& ledger, std::size_t core, std::uint64_t thread_id,
                    std::uint64_t entry_pc)
        : ledger_{ledger}, core_{core}, thread_id_{thread_id}, entry_pc_{entry_pc},
          start_ns_{SteadyNowNs()} {}
    ~ThreadTimeScope() {
        const std::uint64_t end_ns = SteadyNowNs();
        ledger_.Record(thread_id_, start_ns_, end_ns, entry_pc_);
        if (const auto report =
                ledger_.TakeReport(end_ns, kThreadTimeWindowNs, core_, kThreadTimeTopCount)) {
            LOG_INFO(Core_ARM, "{}", *report);
        }
    }
    ThreadTimeScope(const ThreadTimeScope&) = delete;
    ThreadTimeScope& operator=(const ThreadTimeScope&) = delete;

private:
    RecompThreadTimeLedger& ledger_;
    std::size_t core_;
    std::uint64_t thread_id_;
    std::uint64_t entry_pc_;
    std::uint64_t start_ns_;
};

// Lowering host_mem->address_space_max routes accesses above it through HostStore, where the storing
// block's PC (c->pc) is visible; it slows loads too, so it is gated on a block count.
const u64 kSlowPathAbove = [] {
    const char* e = std::getenv("SUYU_RECOMP_SLOWPATH_ABOVE");
    return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL;
}();
const u64 kSlowPathAfterBlocks = [] {
    const char* e = std::getenv("SUYU_RECOMP_SLOWPATH_AFTER_BLOCKS");
    return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL;
}();
const u64 kTrapStoreLo = [] {
    const char* e = std::getenv("SUYU_RECOMP_TRAP_STORE_LO");
    return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL;
}();
const u64 kTrapStoreHi = [] {
    const char* e = std::getenv("SUYU_RECOMP_TRAP_STORE_HI");
    return (e && *e) ? std::strtoull(e, nullptr, 0) : 0ULL;
}();

// SUYU_RECOMP_RELA_POLICY: rtld-only (default), zero-all, zero-rtld, restore-on-main; see ApplyAllRelocations.
// Only rtld-only works for Smash; the others' measured failures are in OLD_HISTORY.md.
const std::string kRelaPolicy = [] {
    const char* e = std::getenv("SUYU_RECOMP_RELA_POLICY");
    return std::string{(e && *e) ? e : "rtld-only"};
}();
// Symbol prefix whose unresolved JUMP_SLOT/GLOB_DAT slots are tracked to show whether anything bound them.
const std::string kTrackSlotPrefix = [] {
    const char* e = std::getenv("SUYU_RECOMP_TRACK_SLOT_PREFIX");
    return std::string{(e && *e) ? e : ""};
}();
// Original DT_RELASZ/DT_PLTRELSZ (address, size) pairs for restore-on-main; process-global because only the
// first Impl sees real sizes and a later one would restore zeros.
std::mutex g_rela_restore_lock;
std::vector<std::pair<u64, u64>> g_rela_restore;
std::atomic<bool> g_rela_restored{false};

bool SampleWindowOpen() {
    if (kSampleAfterSec <= 0.0) {
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - kRecompStart).count() >= kSampleAfterSec;
}

static_assert(offsetof(GuestContextView, host_mem) == 832);
static_assert(offsetof(GuestContextView, tpidrro_el0) == 840);
static_assert(offsetof(GuestContextView, fpcr) == 848);
static_assert(offsetof(GuestContextView, fpsr) == 856);

// The generated code signals an SVC by parking with this set. Kept in sync
// with the emitted recomp_svc contract in core/recompiler/arm64_to_c.h.
constexpr u64 kNoPendingSvc = ~0ULL;

// Mirrors RECOMP_HALT_UNHANDLED: the block parked its PC on an untranslatable instruction for the fallback.
constexpr int kHaltUnhandled = 2;
// Mirrors RECOMP_HALT_BREAKPOINT. PC already names the faulting instruction.
constexpr int kHaltBreakpoint = 3;
constexpr int kHaltIcIvau = 4;
std::atomic<bool> g_code_guard_ready{false};
// ABI 6 (FM1). Set by the loader once every module passed the handshake, before
// any guest thread runs. SUYU_RECOMP_FASTMEM=0 keeps the fast path off anyway.
std::atomic<bool> g_fastmem_ready{false};
const bool kFastmemDisabled = [] {
    const char* e = std::getenv("SUYU_RECOMP_FASTMEM");
    return e && *e == '0';
}();
// ABI 6 GG1. SUYU_RECOMP_GUARD_GEN=0 keeps every module on the per-entry check.
const bool kGuardGenDisabled = [] {
    const char* e = std::getenv("SUYU_RECOMP_GUARD_GEN");
    return e && *e == '0';
}();
// ABI 6 FPX1: set once every module passed the handshake; RunThread then puts guest-core threads in the exact
// FP mode of guest_fp_env.h and checks it each dispatch. SUYU_RECOMP_FPX=0 sets the kill-switch bit.
std::atomic<bool> g_fpx_ready{false};
const bool kFpxDisabled = [] {
    const char* e = std::getenv("SUYU_RECOMP_FPX");
    return e && *e == '0';
}();
// Bit 32 of the context's fpcr, host-owned: FPX1 code masks it from MRS/MSR FPCR and takes the exact path.
constexpr u64 kFpxInhibit = u64{1} << 32;
std::atomic<u64> g_fpx_env_repairs{0};

u64 FpxInhibitBits() {
    return kFpxDisabled && g_fpx_ready.load(std::memory_order_acquire) ? kFpxInhibit : 0;
}
bool FpxActive() {
    return !kFpxDisabled && g_fpx_ready.load(std::memory_order_acquire);
}
// Restores this thread's FP mode for FPX1 code; logged once, since something on a guest-core thread changed it.
void RepairFpEnv(const char* where) {
    if (RecompFpEnv::Ensure() &&
        g_fpx_env_repairs.fetch_add(1, std::memory_order_relaxed) == 0) {
        LOG_WARNING(Core_ARM, "recomp: host FP mode was not the one FPX1 code needs ({}); restored",
                    where);
    }
}
std::atomic<u64> g_forced_cutoff_pc{0};
std::atomic<u64> g_forced_cutoff_blocks{0};

// Sentinel patched into unresolved GOT/JUMP_SLOT entries; the dispatch loop treats a jump to it
// as an immediate return to the caller (PC = LR) instead of crashing the thread.
constexpr u64 kUnresolvedImportTrap = 0xFFFF'FFFF'0000'0000ULL;
constexpr u64 kEarlyDispatchLogLimit = 64;
} // namespace

namespace {
std::atomic<RecompLookupFn> g_recomp_lookup{nullptr};
std::atomic<RecompBaseFn> g_recomp_base_setter{nullptr};
std::mutex g_process_init_lock;
std::atomic<RecompPrepareFn> g_recomp_prepare{nullptr};
std::atomic<RecompBindFn> g_recomp_bind{nullptr};

// Bumped whenever the loaded-module list changes, so per-thread snapshots know to refresh.
std::atomic<u32> g_module_generation{0};

/// Execution coverage of the AOT path: how much of what runs stays recompiled,
/// and which missing opcodes actually execute.
struct RecompCounters {
    std::atomic<u64> fallback_from_miss{0};
    std::atomic<u64> fallback_from_unhandled{0};
    std::atomic<u64> unresolved_import_traps{0};
    std::atomic<u64> no_fallback_available{0};

    // Guarded rather than atomic: touched only on a transition, already the slow path.
    std::mutex hist_lock;
    std::map<u32, u64> unhandled_insn;  ///< guest encoding -> times it forced a fallback
    std::map<u64, u64> miss_pc;         ///< PC with no block -> times it forced a fallback
    std::map<u64, u64> sampled_pc;      ///< sparse samples for zero-transition stalls
    /// Load address -> module name, so PCs in this report resolve to module+offset.
    std::map<u64, std::string> modules;

    void RecordModules(const std::map<u64, std::string>& m) {
        std::scoped_lock lk{hist_lock};
        modules = m;
        g_module_generation.fetch_add(1, std::memory_order_release);
    }

    void RecordUnhandled(u32 insn) {
        std::scoped_lock lk{hist_lock};
        ++unhandled_insn[insn];
    }
    void RecordMiss(u64 pc) {
        std::scoped_lock lk{hist_lock};
        ++miss_pc[pc];
    }
    void RecordSample(u64 pc) {
        std::scoped_lock lk{hist_lock};
        ++sampled_pc[pc];
    }
};

RecompCounters g_counters;

// Counters bumped per dispatch or per SVC; slot layout: [svc_calls, jit_to_static, svc numbers...,
// overflow]. SVC ids at or above kSvcIdLimit share the overflow slot.
constexpr std::size_t kSvcIdLimit = 256;
constexpr std::size_t kSvcCallsCounter = 0;
constexpr std::size_t kJitToStaticCounter = 1;
constexpr std::size_t kFirstSvcIdCounter = 2;
constexpr std::size_t kSvcOverflowCounter = kFirstSvcIdCounter + kSvcIdLimit;
using HotCounters = RecompShardedCounters<kSvcOverflowCounter + 1>;
HotCounters g_hot_counters;
thread_local HotCounters::Slot& t_hot_counters = g_hot_counters.Acquire();

void CountSvc(u32 svc_id) {
    HotCounters::Increment(t_hot_counters, kSvcCallsCounter);
    HotCounters::Increment(t_hot_counters,
                           svc_id < kSvcIdLimit ? kFirstSvcIdCounter + svc_id : kSvcOverflowCounter);
}

// SUYU_RECOMP_LOOKUP_STATS=1 counts dispatcher returns by reason; off unless exactly "1".
const bool kLookupStats = [] {
    const char* e = std::getenv("SUYU_RECOMP_LOOKUP_STATS");
    return e && std::string_view{e} == "1";
}();
constexpr u64 kLookupStatsWindowNs = 10'000'000'000ULL;
// SUYU_RECOMP_LOOKUP_TARGETS=1 also reports the busiest exit target pcs; needs the stats above.
const bool kLookupTargets = kLookupStats && ([] {
    const char* e = std::getenv("SUYU_RECOMP_LOOKUP_TARGETS");
    return e && std::string_view{e} == "1";
}());
RecompExitTargets g_exit_targets;
std::atomic<bool> g_exit_targets_rebased{false};

RecompExitTargets::Slot& ExitTargetsSlot() {
    thread_local RecompExitTargets::Slot& slot = g_exit_targets.Acquire();
    return slot;
}
RecompLookupStats g_lookup_stats;

// SUYU_RECOMP_EXCL_STATS=1 times the guest exclusive callbacks; off unless exactly "1".
const bool kExclusiveStats = [] {
    const char* e = std::getenv("SUYU_RECOMP_EXCL_STATS");
    return e && std::string_view{e} == "1";
}();
constexpr u64 kExclusiveStatsWindowNs = 10'000'000'000ULL;
constexpr u64 kExclusiveStatsTickMask = 0xFFF;
RecompExclusiveStats g_exclusive_stats;

RecompExclusiveStats::Counters::Slot& ExclusiveStatsSlot() {
    thread_local RecompExclusiveStats::Counters::Slot& slot = g_exclusive_stats.Acquire();
    return slot;
}

// Per-thread copy of the module bases and names, so an exit does not take the module-map lock.
struct ModuleSnapshot {
    u32 generation = ~0U;
    std::vector<u64> bases;
    std::vector<std::string> names;
};

// "module+0xoffset" for a pc, or the bare pc when no module base is at or below it.
std::string DescribePc(const ModuleSnapshot& modules, u64 pc) {
    const std::size_t index = RecompLookupStats::ModuleIndex(modules.bases, pc);
    if (index >= modules.bases.size()) {
        return fmt::format("{:#x}", pc);
    }
    return fmt::format("{}+{:#x}", modules.names[index], pc - modules.bases[index]);
}

const ModuleSnapshot& CurrentModuleSnapshot() {
    thread_local ModuleSnapshot snapshot;
    const u32 generation = g_module_generation.load(std::memory_order_acquire);
    if (snapshot.generation != generation) {
        std::scoped_lock lk{g_counters.hist_lock};
        snapshot.bases.clear();
        snapshot.names.clear();
        for (const auto& [base, name] : g_counters.modules) {
            snapshot.bases.push_back(base);
            snapshot.names.push_back(name);
        }
        snapshot.generation = generation;
    }
    return snapshot;
}

RecompLookupStats::Counters::Slot& LookupStatsSlot() {
    thread_local RecompLookupStats::Counters::Slot& slot = g_lookup_stats.Acquire();
    return slot;
}

u64 TotalSvcCalls() {
    return g_hot_counters.Sum(kSvcCallsCounter);
}

// One cache line per core: the cores write their PC every dispatch and must not share a line.
struct alignas(64) CurrentPc {
    std::atomic<u64> pc{0};
};
std::array<CurrentPc, 4> g_current_pcs{};
std::atomic<int> g_live_instances{0};
std::mutex g_snapshot_lock;
std::map<std::pair<u64, std::size_t>, std::string> g_diagnostic_snapshots;

/// Per-thread block tally: one shared atomic was 23% of the dispatch loop's cycles; the report sums the lines.
struct alignas(64) ThreadBlocks {
    std::atomic<u64> n{0};
    char pad[64 - sizeof(std::atomic<u64>)];
};
std::mutex g_tally_lock;
std::vector<ThreadBlocks*> g_tallies;
std::atomic<u64> g_retired_blocks{0};
/// TotalStaticBlocks() when the current session began, so a session that never
/// executed a recompiled block is not counted as a run in recomp_gaps.json.
std::atomic<u64> g_session_blocks_start{0};

struct ThreadBlockSlot {
    ThreadBlocks* slot = new ThreadBlocks{};
    ThreadBlockSlot() {
        std::scoped_lock lk{g_tally_lock};
        g_tallies.push_back(slot);
    }
    // The slot outlives the thread: the reporter may be summing while a guest
    // thread exits, and the count still belongs in the total.
};
thread_local ThreadBlockSlot t_blocks;

u64 TotalStaticBlocks() {
    std::scoped_lock lk{g_tally_lock};
    u64 sum = g_retired_blocks.load(std::memory_order_relaxed);
    for (const ThreadBlocks* s : g_tallies) {
        sum += s->n.load(std::memory_order_relaxed);
    }
    return sum;
}

template <typename Map>
auto TopN(const Map& m, size_t n) {
    // Not Map::value_type: that has a const key and so is not assignable, which
    // partial_sort requires.
    using Entry = std::pair<typename Map::key_type, typename Map::mapped_type>;
    std::vector<Entry> v(m.begin(), m.end());
    std::partial_sort(v.begin(), v.begin() + std::min(n, v.size()), v.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
    if (v.size() > n) {
        v.resize(n);
    }
    return v;
}

/// Formats the coverage report as one string for both the log and the periodic
/// file, since teardown (and ~ArmRecomp) may never run.
std::string FormatRecompCoverage() {
    const u64 blocks = TotalStaticBlocks();
    const u64 miss = g_counters.fallback_from_miss.load();
    const u64 unh = g_counters.fallback_from_unhandled.load();
    const u64 transitions = miss + unh;

    if (blocks == 0 && transitions == 0) {
        return {};  // backend never ran; saying nothing is better than printing zeros
    }

    std::string o = "=== RECOMP EXECUTION COVERAGE ===\n";
    o += fmt::format("  static blocks executed : {}\n", blocks);
    o += fmt::format("  SVCs to HLE            : {}\n", TotalSvcCalls());
    o += fmt::format("  static -> JIT          : {} ({} lookup miss, {} unimplemented opcode)\n",
                     transitions, miss, unh);
    o += fmt::format("  JIT -> static          : {}\n", g_hot_counters.Sum(kJitToStaticCounter));
    o += fmt::format("  unresolved import traps: {}\n", g_counters.unresolved_import_traps.load());
    if (const u64 nofb = g_counters.no_fallback_available.load(); nofb) {
        o += fmt::format("  threads killed with no JIT fallback: {}\n", nofb);
    }

    // Each transition marshals 32 GPRs + 32 vectors both ways, so blocks per transition is the key figure.
    if (transitions) {
        o += fmt::format("  blocks per transition  : {:.1f}\n",
                         double(blocks) / double(transitions));
    } else {
        o += fmt::format("  blocks per transition  : no transitions - fully static\n");
    }

    std::scoped_lock lk{g_counters.hist_lock};

    if (!g_counters.unhandled_insn.empty()) {
        o += "  --- unimplemented opcodes by execution count ---\n";
        for (const auto& [insn, count] : TopN(g_counters.unhandled_insn, 24)) {
            o += fmt::format("    {:08X}  {:>10}  {:5.2f}%  (sig {:08X})\n", insn, count,
                             unh ? 100.0 * double(count) / double(unh) : 0.0, insn & 0xFFC00000u);
        }
        o += fmt::format("    {} distinct encodings\n", g_counters.unhandled_insn.size());
    }

    std::map<u32, u64> svc_numbers;
    for (u32 id = 0; id < kSvcIdLimit; ++id) {
        if (const u64 count = g_hot_counters.Sum(kFirstSvcIdCounter + id)) {
            svc_numbers[id] = count;
        }
    }
    if (!svc_numbers.empty()) {
        // A boot that stops making SVCs while still executing blocks is spinning; this says on what.
        o += "  --- SVCs by call count ---\n";
        for (const auto& [num, count] : TopN(svc_numbers, 40)) {
            o += fmt::format("    svc 0x{:02X}  {:>10}\n", num, count);
        }
        o += fmt::format("    {} distinct SVCs\n", svc_numbers.size());
    }
    if (const u64 overflow = g_hot_counters.Sum(kSvcOverflowCounter)) {
        o += fmt::format("    {} SVCs with id >= {:#x}\n", overflow, kSvcIdLimit);
    }

    if (!g_counters.modules.empty()) {
        o += "  --- loaded modules ---\n";
        for (const auto& [base, name] : g_counters.modules) {
            o += fmt::format("    {:#018x}  {}\n", base, name);
        }
    }

    if (!g_counters.sampled_pc.empty()) {
        const auto resolve = [](u64 pc) -> std::string {
            u64 best = 0;
            const std::string* name = nullptr;
            for (const auto& [base, module_name] : g_counters.modules) {
                if (pc >= base && base >= best) {
                    best = base;
                    name = &module_name;
                }
            }
            return name ? fmt::format("  {}+{:#x}", *name, pc - best) : std::string{};
        };
        o += "  --- sampled PCs by count ---\n";
        for (const auto& [pc, count] : TopN(g_counters.sampled_pc, 48)) {
            o += fmt::format("    {:#018x}  {:>10}{}\n", pc, count, resolve(pc));
        }
        o += fmt::format("    {} distinct sampled PCs\n", g_counters.sampled_pc.size());
    }

    if (!g_counters.miss_pc.empty()) {
        // Resolved here: a bare guest PC means nothing without this run's load addresses.
        const auto resolve = [](u64 pc) -> std::string {
            u64 best = 0;
            const std::string* name = nullptr;
            for (const auto& [base, module_name] : g_counters.modules) {
                if (pc >= base && base >= best) {
                    best = base;
                    name = &module_name;
                }
            }
            return name ? fmt::format("  {}+{:#x}", *name, pc - best) : std::string{};
        };
        o += "  --- uncovered PCs by execution count ---\n";
        for (const auto& [pc, count] : TopN(g_counters.miss_pc, 16)) {
            o += fmt::format("    {:#018x}  {:>10}{}\n", pc, count, resolve(pc));
        }
        o += fmt::format("    {} distinct PCs\n", g_counters.miss_pc.size());
    }
    {
        std::scoped_lock lock{g_snapshot_lock};
        if (!g_diagnostic_snapshots.empty()) {
            o += "  --- last execution-boundary samples (not a live thread enumeration) ---\n";
            for (const auto& [key, snapshot] : g_diagnostic_snapshots) {
                (void)key;
                o += snapshot + "\n";
            }
        }
    }
    o += "=== END RECOMP EXECUTION COVERAGE ===\n";
    return o;
}

void WriteRecompCoverageFile(const std::string& text) {
    const char* explicit_path = std::getenv("SUYU_RECOMP_COVERAGE_PATH");
    const auto path = explicit_path && *explicit_path
                          ? std::filesystem::path{explicit_path}
                          : Common::FS::GetSuyuPath(Common::FS::SuyuPath::LogDir) /
                                "recomp_coverage.txt";
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << text;
    }
}

// Diagnostics are sampled synchronously by the owning CPU interface.
// No detached worker may retain a KProcess or walk a live thread list.
void ReportRecompCoverage() {
    if (const auto gg = RecompGuardGen::GetStats(); gg.modules != 0) {
        using R = RecompGuardGen::Reason;
        const auto n = [&gg](R r) { return gg.bumps[static_cast<unsigned>(r)]; };
        LOG_INFO(Core_ARM,
                 "recomp generation guard: enabled={} active={} generation={} modules={} "
                 "sticky={} bumps: activate={} map={} unmap={} protect={} device={} "
                 "invalidate={} invalidate_all={} new_table={} code_write={} pointer={} "
                 "jit={} map_log={}{}",
                 gg.enabled, gg.active, gg.generation, gg.modules, gg.sticky, n(R::Activate),
                 n(R::Map), n(R::Unmap), n(R::Protect), n(R::DeviceMap), n(R::Invalidate),
                 n(R::InvalidateAll), n(R::PageTableSwap), n(R::CodeWrite),
                 n(R::PointerExposed), n(R::JitFallback), gg.map_log,
                 gg.map_log_overflow ? " (overflowed)" : "");
    }
    const std::string report = FormatRecompCoverage();
    if (report.empty()) {
        return;
    }
    WriteRecompCoverageFile(report);
    // Split by hand: the report is already newline-delimited and the logger
    // takes one line at a time.
    size_t pos = 0;
    while (pos < report.size()) {
        const size_t nl = report.find('\n', pos);
        const std::string_view line{report.data() + pos,
                                    (nl == std::string::npos ? report.size() : nl) - pos};
        if (!line.empty()) {
            LOG_INFO(Core_ARM, "{}", line);
        }
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
}

} // namespace

RecompExecutionStats GetRecompExecutionStats() {
    return {
        TotalStaticBlocks(),
        TotalSvcCalls(),
        g_counters.fallback_from_miss.load(std::memory_order_relaxed),
        g_counters.fallback_from_unhandled.load(std::memory_order_relaxed),
        g_counters.no_fallback_available.load(std::memory_order_relaxed),
    };
}

std::array<u64, 4> GetRecompCurrentPcs() {
    std::array<u64, 4> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = g_current_pcs[index].pc.load(std::memory_order_relaxed);
    }
    return result;
}

std::map<u64, std::string> GetRecompModules() {
    std::scoped_lock lk{g_counters.hist_lock};
    return g_counters.modules;
}

void SetRecompLookup(RecompLookupFn lookup) {
    std::scoped_lock lock{g_process_init_lock};
    g_code_guard_ready.store(false, std::memory_order_release);
    g_fastmem_ready.store(false, std::memory_order_release);
    // The previous bundle's images may be unloaded; drop them untouched.
    RecompGuardGen::Forget();
    g_fpx_ready.store(false, std::memory_order_release);
    g_recomp_lookup.store(lookup, std::memory_order_release);
    if (!lookup) {
        // Back to the normal backend: a bind registration belongs to the
        // recompiled session that is being left, and must not reach a JIT process.
        g_recomp_bind.store(nullptr, std::memory_order_release);
    }
}

void SetRecompLongSlices(bool enabled) {
    g_recomp_chain_budget.store(enabled ? kLongSliceChainBudget : kShortSliceChainBudget,
                                std::memory_order_relaxed);
}

void SetRecompCodeGuardReady(bool ready) {
    g_code_guard_ready.store(ready, std::memory_order_release);
}

bool IsRecompCodeGuardReady() {
    return g_code_guard_ready.load(std::memory_order_acquire);
}

RecompFastmemLayout GetRecompFastmemLayout() {
    return RecompFastmemLayout{
        static_cast<u32>(Memory::YUZU_PAGEBITS),
        5, // log2(sizeof(Common::PageTable::PageEntryData)), pinned above
        static_cast<u64>(~uintptr_t{0} << Common::PageTable::ATTRIBUTE_BITS),
        static_cast<u32>(offsetof(GuestContextView, fm_table)),
        static_cast<u32>(offsetof(GuestContextView, fm_limit)),
    };
}

void SetRecompFastmemReady(bool ready) {
    g_fastmem_ready.store(ready, std::memory_order_release);
}

RecompFpxLayout GetRecompFpxLayout() {
    return RecompFpxLayout{
        static_cast<u32>(offsetof(GuestContextView, fpcr)),
        static_cast<u32>(offsetof(GuestContextView, fpsr)),
        kFpxInhibit,
    };
}

void SetRecompFpxReady(bool ready) {
    g_fpx_ready.store(ready, std::memory_order_release);
}

bool IsRecompFpxReady() {
    return g_fpx_ready.load(std::memory_order_acquire);
}

bool IsRecompFastmemReady() {
    return g_fastmem_ready.load(std::memory_order_acquire);
}

namespace {

const char* PinCauseName(RecompGuardGen::PinCause cause) {
    switch (cause) {
    case RecompGuardGen::PinCause::Untracked:
        return "untracked table (its pointer was never seen by OnPageTableSwap)";
    case RecompGuardGen::PinCause::MapLogOverflow:
        return "untracked: live-mapping log overflowed before activation";
    case RecompGuardGen::PinCause::ExposuresOverflow:
        return "untracked: pre-activation raw-pointer log overflowed";
    case RecompGuardGen::PinCause::NotFirstActivation:
        return "untracked: a process already activated under this registration";
    case RecompGuardGen::PinCause::Hole:
        return "span not wholly mapped read/execute-only at activation";
    case RecompGuardGen::PinCause::Rebased:
        return "rebased since activation";
    case RecompGuardGen::PinCause::AliasAtActivation:
        return "aliased (another live mapping of its physical pages)";
    case RecompGuardGen::PinCause::ExposedBeforeActivation:
        return "raw pointer exposed before activation";
    case RecompGuardGen::PinCause::Map:
        return "mapped over (or aliased by) a new mapping";
    case RecompGuardGen::PinCause::Unmap:
        return "unmapped";
    case RecompGuardGen::PinCause::Protect:
        return "made writable";
    case RecompGuardGen::PinCause::DeviceMap:
        return "device-mapped";
    case RecompGuardGen::PinCause::PointerExposed:
        return "raw pointer exposed after activation";
    case RecompGuardGen::PinCause::JitFallback:
        return "JIT fallback created";
    }
    return "unknown";
}

// Registered once, process-wide: logs which DESIGN.md section 2 rule first pins each module (one line per module).
void EnsureGuardGenPinLogger() {
    static std::once_flag once;
    std::call_once(once, [] {
        RecompGuardGen::SetPinLogger([](std::size_t module_index, RecompGuardGen::PinCause cause,
                                        u64 addr) {
            if (addr != 0) {
                LOG_INFO(Core_ARM,
                         "recomp generation guard: module {} pinned to verify-always: {} "
                         "(addr={:#x}, page={:#x})",
                         module_index, PinCauseName(cause), addr, addr >> Memory::YUZU_PAGEBITS);
            } else {
                LOG_INFO(Core_ARM, "recomp generation guard: module {} pinned to verify-always: {}",
                         module_index, PinCauseName(cause));
            }
        });
        RecompGuardGen::SetTableSeenLogger([](const void* table, std::size_t known_tables) {
            LOG_INFO(Core_ARM,
                     "recomp generation guard: OnPageTableSwap saw table={} ({} table(s) known)",
                     table, known_tables);
        });
    });
}

} // namespace

bool SetRecompGuardGenModules(std::vector<RecompGuardGen::Module> modules) {
    EnsureGuardGenPinLogger();
    const bool enabled = !kGuardGenDisabled && !modules.empty() &&
                         g_code_guard_ready.load(std::memory_order_acquire);
    const size_t count = modules.size();
    RecompGuardGen::SetModules(std::move(modules), enabled);
    if (count != 0) {
        LOG_INFO(Core_ARM, "Recompiled generation code guard: {} ({} module(s){})",
                 enabled ? "negotiated" : "verify on every entry", count,
                 kGuardGenDisabled ? ", disabled by SUYU_RECOMP_GUARD_GEN=0" : "");
    }
    return enabled;
}

void SetRecompBaseSetter(RecompBaseFn setter) {
    g_recomp_base_setter.store(setter, std::memory_order_release);
}

RecompLiveStats GetRecompLiveStats() {
    return RecompLiveStats{
        TotalStaticBlocks(),
        g_counters.fallback_from_miss.load(std::memory_order_relaxed) +
            g_counters.fallback_from_unhandled.load(std::memory_order_relaxed),
        g_forced_cutoff_pc.load(std::memory_order_relaxed),
        g_forced_cutoff_blocks.load(std::memory_order_relaxed),
        g_live_instances.load(std::memory_order_relaxed) > 0,
#ifdef SUYU_NO_JIT
        false,
#else
        true,
#endif
        StrictNoFallback(),
    };
}

void SetRecompPrepareCallback(RecompPrepareFn callback) {
    g_recomp_prepare.store(callback, std::memory_order_release);
}

bool HasRecompPrepareCallback() {
    return g_recomp_prepare.load(std::memory_order_acquire) != nullptr;
}

void SetRecompBindCallback(RecompBindFn callback) {
    g_recomp_bind.store(callback, std::memory_order_release);
}

bool HasRecompBindCallback() {
    return g_recomp_bind.load(std::memory_order_acquire) != nullptr;
}

RecompLookupFn GetRecompLookup() {
    return g_recomp_lookup.load(std::memory_order_acquire);
}

const char* ActiveRecompMissHookName() {
    for (const char* name :
         {"SUYU_RECOMP_FORCE_MISS_AFTER", "SUYU_RECOMP_FORCE_MISS_AFTER_STATIC_BLOCKS",
          "SUYU_RECOMP_FORCE_MISS_AT_MODULE_OFFSET", "SUYU_RECOMP_FORCE_MISS_AFTER_SEC",
          "SUYU_RECOMP_MAIN_HOLE"}) {
        if (std::getenv(name)) {
            return name;
        }
    }
    return nullptr;
}

namespace {
/// Hosted generated code must never take its private exclusive/counter fallbacks (per-image state), so every
/// callback it can reach must exist. Returns false and names the first missing one.
RECOMP_DEFINE_FIRST_MISSING_BRIDGE_CALLBACK(FirstMissingBridgeCallback, RecompHostMem)

bool CheckBridgeComplete(const RecompHostMem& bridge, const char** missing_callback) {
    const char* missing = FirstMissingBridgeCallback(&bridge);
    if (missing) {
        *missing_callback = missing;
        return false;
    }
    return true;
}
} // namespace

struct ArmRecomp::Impl {
    Impl(System& system_, RecompLookupFn lookup_) : system{system_}, lookup{lookup_} {
        std::memset(&ctx, 0, sizeof(ctx));
        ctx.pending_svc = kNoPendingSvc;
        // Point the recompiled code at the emulator's address space.
        bridge.user = this;
        bridge.load = &Impl::HostLoad;
        bridge.store = &Impl::HostStore;
        bridge.excl_load = kExclusiveStats ? &Impl::HostExclusiveLoadTimed : &Impl::HostExclusiveLoad;
        bridge.excl_store = kExclusiveStats ? &Impl::HostExclusiveStoreTimed : &Impl::HostExclusiveStore;
        bridge.clear_excl = &Impl::HostClearExclusive;
        bridge.read_cntpct = &Impl::HostReadCntpct;
        bridge.excl_load_pair =
            kExclusiveStats ? &Impl::HostExclusiveLoadPairTimed : &Impl::HostExclusiveLoadPair;
        bridge.excl_store_pair =
            kExclusiveStats ? &Impl::HostExclusiveStorePairTimed : &Impl::HostExclusiveStorePair;
        // Filled in by RefreshPageTable once a process exists; until then the
        // fields stay null and every access takes the callback path.
        bridge.page_entries = nullptr;
        bridge.guard_generation = nullptr;
        ctx.host_mem = &bridge;
        // Checked here, enforced in RunThread: a null callback would silently
        // switch generated code to per-image fallbacks.
        const char* missing_callback = "";
        bridge_complete = CheckBridgeComplete(bridge, &missing_callback);
        if (!bridge_complete) {
            LOG_CRITICAL(Core_ARM, "recomp: host bridge callback '{}' is not installed; this core "
                                   "will not run guest code", missing_callback);
        }
    }

    // The monitor is per-core (core_index separates guests' reservations); without one these degrade to plain
    // accesses with STXR always succeeding, which cannot run a real title.
    static u64 HostExclusiveLoad(void* user, u64 va, u32 size) {
        auto* self = static_cast<Impl*>(user);
        if (!self->exclusive_monitor) {
            return HostLoad(user, va, size);
        }
        const auto core = self->core_index;
        switch (size) {
        case 1: return self->exclusive_monitor->ExclusiveRead8(core, va);
        case 2: return self->exclusive_monitor->ExclusiveRead16(core, va);
        case 4: return self->exclusive_monitor->ExclusiveRead32(core, va);
        case 8: return self->exclusive_monitor->ExclusiveRead64(core, va);
        default: return HostLoad(user, va, size);
        }
    }

    /// Returns 0 on success, 1 when the reservation was lost - the sense of the
    /// status register STXR writes.
    static u32 HostExclusiveStore(void* user, u64 va, u32 size, u64 value) {
        auto* self = static_cast<Impl*>(user);
        if (!self->exclusive_monitor) {
            HostStore(user, va, size, value);
            return 0;
        }
        const auto core = self->core_index;
        bool ok = false;
        switch (size) {
        case 1: ok = self->exclusive_monitor->ExclusiveWrite8(core, va, static_cast<u8>(value)); break;
        case 2: ok = self->exclusive_monitor->ExclusiveWrite16(core, va, static_cast<u16>(value)); break;
        case 4: ok = self->exclusive_monitor->ExclusiveWrite32(core, va, static_cast<u32>(value)); break;
        case 8: ok = self->exclusive_monitor->ExclusiveWrite64(core, va, value); break;
        default: HostStore(user, va, size, value); return 0;
        }
        return ok ? 0u : 1u;
    }

    /// LDXP/LDAXP: the 64-bit pair takes a 128-bit reservation, the 32-bit pair a 64-bit one, as the architecture specifies.
    static void HostExclusiveLoadPair(void* user, u64 va, u32 size, u64* lo, u64* hi) {
        auto* self = static_cast<Impl*>(user);
        if (!self->exclusive_monitor) {
            *lo = HostLoad(user, va, size);
            *hi = HostLoad(user, va + size, size);
            return;
        }
        const auto core = self->core_index;
        if (size == 8) {
            const u128 v = self->exclusive_monitor->ExclusiveRead128(core, va);
            *lo = v[0];
            *hi = v[1];
        } else {
            const u64 v = self->exclusive_monitor->ExclusiveRead64(core, va);
            *lo = static_cast<u32>(v);
            *hi = static_cast<u32>(v >> 32);
        }
    }

    /// STXP/STLXP. Returns 0 on success, matching STXR's status sense.
    static u32 HostExclusiveStorePair(void* user, u64 va, u32 size, u64 lo, u64 hi) {
        auto* self = static_cast<Impl*>(user);
        if (!self->exclusive_monitor) {
            HostStore(user, va, size, lo);
            HostStore(user, va + size, size, hi);
            return 0;
        }
        const auto core = self->core_index;
        bool ok = false;
        if (size == 8) {
            ok = self->exclusive_monitor->ExclusiveWrite128(core, va, u128{lo, hi});
        } else {
            ok = self->exclusive_monitor->ExclusiveWrite64(
                core, va, static_cast<u32>(lo) | (static_cast<u64>(static_cast<u32>(hi)) << 32));
        }
        return ok ? 0u : 1u;
    }

    static u64 HostExclusiveLoadTimed(void* user, u64 va, u32 size) {
        const u64 start = RecompExclusiveStats::ReadTicks();
        const u64 value = HostExclusiveLoad(user, va, size);
        RecompExclusiveStats::CountLoad(ExclusiveStatsSlot(), RecompExclusiveStats::ReadTicks() - start);
        return value;
    }

    static u32 HostExclusiveStoreTimed(void* user, u64 va, u32 size, u64 value) {
        const u64 start = RecompExclusiveStats::ReadTicks();
        const u32 status = HostExclusiveStore(user, va, size, value);
        RecompExclusiveStats::CountStore(ExclusiveStatsSlot(), status == 0,
                                         RecompExclusiveStats::ReadTicks() - start);
        return status;
    }

    static void HostExclusiveLoadPairTimed(void* user, u64 va, u32 size, u64* lo, u64* hi) {
        const u64 start = RecompExclusiveStats::ReadTicks();
        HostExclusiveLoadPair(user, va, size, lo, hi);
        RecompExclusiveStats::CountLoad(ExclusiveStatsSlot(), RecompExclusiveStats::ReadTicks() - start);
    }

    static u32 HostExclusiveStorePairTimed(void* user, u64 va, u32 size, u64 lo, u64 hi) {
        const u64 start = RecompExclusiveStats::ReadTicks();
        const u32 status = HostExclusiveStorePair(user, va, size, lo, hi);
        RecompExclusiveStats::CountStore(ExclusiveStatsSlot(), status == 0,
                                         RecompExclusiveStats::ReadTicks() - start);
        return status;
    }

    /// Re-reads the page table description into the bridge on each run entry; the table belongs to the process
    /// and a stale pointer would read another address space.
    void RefreshPageTable() {
        const auto view = system.ApplicationMemory().GetPageTableView();
        // ABI 5 checks the instruction bytes against this mapping on every entry.
        bridge.page_entries = view.entries;
        bridge.page_entry_stride = view.entry_stride;
        bridge.page_bits = view.page_bits;
        bridge.pointer_mask = view.pointer_mask;
        bridge.address_space_max = view.address_space_max;
        if (kSlowPathAbove != 0 && kSlowPathAbove < bridge.address_space_max &&
            TotalStaticBlocks() >= kSlowPathAfterBlocks) {
            bridge.address_space_max = kSlowPathAbove;
        }
        // ABI 6 (FM1): enable the constant-layout helpers only for a table with that layout; the limit is the ABI 5
        // walk's limit rounded down to a page, so the fast path serves a subset and defers the rest.
        ctx.fm_table = nullptr;
        ctx.fm_limit = 0;
        if (!kFastmemDisabled && g_fastmem_ready.load(std::memory_order_acquire) &&
            bridge.page_entries != nullptr && bridge.page_bits == Memory::YUZU_PAGEBITS &&
            bridge.page_entry_stride == sizeof(Common::PageTable::PageEntryData) &&
            bridge.pointer_mask == GetRecompFastmemLayout().pointer_mask &&
            bridge.address_space_max <= (u64{1} << 39)) {
            ctx.fm_table = static_cast<const u8*>(bridge.page_entries);
            ctx.fm_limit = bridge.address_space_max & ~u64{0xfff};
        }
    }

    /// ABI 6 GG1 activation, decided from what the Core::Memory hooks recorded; a kernel query would take the
    /// page-table KLightLock, whose contention reschedules and could re-enter this core's RunThread.
    void ActivateGuardGen(Kernel::KProcess& process, u64 key) {
        // Watch every page of each stable module's span: GG1 stores there go to
        // HostStore, and Core::Memory reports writes and raw pointers there.
        auto& entries = process.GetPageTable().GetImpl().entries;
        const auto watch = [&entries](u64 va, u64 size) {
            const u64 first = va >> Memory::YUZU_PAGEBITS;
            const u64 last = (va + size - 1) >> Memory::YUZU_PAGEBITS;
            for (u64 page = first; page <= last && page < entries.size(); ++page) {
                reinterpret_cast<std::atomic<u64>*>(&entries[page].recomp_watch)
                    ->store(1, std::memory_order_relaxed);
            }
        };
        const void* const activate_table = process.GetMemory().GetPageTableView().entries;
        LOG_INFO(Core_ARM, "recomp generation guard: process {} activating against table={}",
                 process.GetProcessId(), activate_table);
        RecompGuardGen::Activate(key, activate_table, watch);
        const auto stats = RecompGuardGen::GetStats();
        LOG_INFO(Core_ARM,
                 "recomp generation guard: process {} active={} generation={}; {} of {} "
                 "module(s) verify on every entry",
                 process.GetProcessId(), stats.active, stats.generation, stats.sticky,
                 stats.modules);
    }

    /// The same source DynarmicCallbacks64::GetCNTPCT uses, so a guest thread
    /// that migrates between engines sees one monotonic clock.
    static u64 HostReadCntpct(void* user) {
        return static_cast<Impl*>(user)->system.CoreTiming().GetClockTicks();
    }

    static void HostClearExclusive(void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self->exclusive_monitor) {
            self->exclusive_monitor->ClearExclusive(self->core_index);
        }
    }

    /// Reports a static-executed access to an unmapped address (mapped ones resolve inline). It means an earlier
    /// instruction produced a value real hardware would not; the PC trail leads back to it.
    static void ReportUnmapped(Impl* self, u64 va, u32 size, const char* what) {
        static std::atomic<int> logs{0};
        if (logs.fetch_add(1, std::memory_order_relaxed) >= 8) {
            return;
        }
        std::string trail;
        const size_t count = std::min<size_t>(self->trail_pos, Impl::kTrail);
        for (size_t i = 0; i < count; ++i) {
            trail += fmt::format("{:#x} ",
                                 self->trail[(self->trail_pos - count + i) & (Impl::kTrail - 1)]);
        }
        LOG_ERROR(Core_ARM, "recomp unmapped {}{} @ {:#x} from pc={:#x} after {} blocks", what,
                  size * 8, va, self->ctx.pc, TotalStaticBlocks());
        LOG_ERROR(Core_ARM, "recomp unmapped PC trail (oldest first): {}", trail);
        for (size_t r = 0; r < 32; r += 4) {
            LOG_ERROR(Core_ARM,
                      "recomp unmapped regs x{:<2}={:#018x} x{:<2}={:#018x} x{:<2}={:#018x} "
                      "x{:<2}={:#018x}",
                      r, self->ctx.x[r], r + 1, self->ctx.x[r + 1], r + 2, self->ctx.x[r + 2],
                      r + 3, self->ctx.x[r + 3]);
        }
    }

    static u64 HostLoad(void* user, u64 va, u32 size) {
        va &= 0xffffffffffffULL;
        const u32 bytes = size == 0 ? 4 : size;
        // Size zero is the negotiated guard-v2 instruction read. All other
        // widths must be architectural scalar sizes, with no 48-bit wrap.
        if ((size != 0 && size != 1 && size != 2 && size != 4 && size != 8) ||
            bytes > 0x1000000000000ULL - va) {
            return 0;
        }
        auto* self = static_cast<Impl*>(user);
        auto& memory = self->system.ApplicationMemory();
        if (size == 0) {
            // Validity is per guest page, so the word's first and last bytes
            // cover it; they are the same page unless the word straddles one.
            if (!memory.IsValidVirtualAddress(va) ||
                !memory.IsValidVirtualAddress(va + bytes - 1)) {
                return 0;
            }
            return (u64{1} << 32) | memory.Read32(va);
        }
        if (!memory.IsValidVirtualAddress(va)) {
            ReportUnmapped(self, va, size, "read");
        }
        // Memory's scalar accessors split unaligned accesses across guest
        // pages. Preserve them rather than imposing byte loads on all reads.
        switch (size) {
        case 1: return memory.Read8(va);
        case 2: return memory.Read16(va);
        case 4: return memory.Read32(va);
        default: return memory.Read64(va); // Validated size == 8.
        }
    }

    static void HostStore(void* user, u64 va, u32 size, u64 value) {
        va &= 0xffffffffffffULL;
        if ((size != 1 && size != 2 && size != 4 && size != 8) ||
            size > 0x1000000000000ULL - va) {
            return;
        }
        if (kTrapStoreLo != 0 && va >= kTrapStoreLo && va < kTrapStoreHi) {
            auto* self = static_cast<Impl*>(user);
            std::string trail;
            const size_t count = std::min<size_t>(self->trail_pos, Impl::kTrail);
            for (size_t i = 0; i < count; ++i) {
                trail += fmt::format("{:#x} ",
                                     self->trail[(self->trail_pos - count + i) & (Impl::kTrail - 1)]);
            }
            LOG_ERROR(Core_ARM,
                      "recomp TRAP store {:#x} size={} value={:#x} from block pc={:#x} blocks={} "
                      "x19={:#x} x0={:#x} x1={:#x} x8={:#x} lr={:#x} trail={}",
                      va, size, value, self->ctx.pc, TotalStaticBlocks(), self->ctx.x[19],
                      self->ctx.x[0], self->ctx.x[1], self->ctx.x[8], self->ctx.x[30], trail);
        }
        auto* self = static_cast<Impl*>(user);
        auto& memory = self->system.ApplicationMemory();
        if (!memory.IsValidVirtualAddress(va)) {
            ReportUnmapped(self, va, size, "write");
        }
        switch (size) {
        case 1: memory.Write8(va, static_cast<u8>(value)); break;
        case 2: memory.Write16(va, static_cast<u16>(value)); break;
        case 4: memory.Write32(va, static_cast<u32>(value)); break;
        default: memory.Write64(va, value); break;
        }
    }

    /// Base address of the module containing `pc`, for the module-relative offset images are keyed by;
    /// read once because the module list is fixed once the process runs.
    u64 ModuleBaseFor(Kernel::KThread* thread, u64 pc) {
        if (!modules_read) {
            modules_read = true;
            if (auto* process = thread->GetOwnerProcess()) {
                modules = FindModules(process);
                g_counters.RecordModules(modules);
                // Tell each image where its module went; a bound process never gets here (modules_read is set and
                // the launcher's bind callback set the bases by canonical name).
                if (const auto setter = g_recomp_base_setter.load(std::memory_order_acquire)) {
                    // modules is keyed by base, so iteration is load order.
                    size_t index = 0;
                    for (const auto& [module_base, name] : modules) {
                        setter(index++, name.c_str(), module_base);
                    }
                }
            }
        }
        u64 base = 0;
        for (const auto& [module_base, name] : modules) {
            if (pc >= module_base && module_base >= base) {
                base = module_base;
            }
        }
        return base;
    }

    // Is `pc` in the main module? Identified by load order (rtld, main, subsdk*, sdk; `modules` is keyed by base)
    // since no module is literally named "main"; the next base bounds it.
    bool PcInMainModule(u64 pc) const {
        if (modules.size() < 2) {
            return false;
        }
        auto it = std::next(modules.begin());
        const auto next = std::next(it);
        const u64 end = (next != modules.end()) ? next->first : it->first + 0x10000000ULL;
        return pc >= it->first && pc < end;
    }

    struct DynInfo {
        u64 mod_base = 0;
        u64 rela_va = 0, rela_sz = 0, rela_ent = 24, rela_sz_va = 0;
        u64 jmprel_va = 0, jmprel_sz = 0, jmprel_ent = 24, jmprel_sz_va = 0;
        u64 symtab_va = 0, strtab_va = 0;
        // Address of a harmless "return 0" stub inside this module's own text,
        // used as the target for every import that could not be resolved.
        u64 trap_va = 0;
        std::string name;
    };

    // Finds `mov x0, #0; ret` (else a bare `ret`) in a module's text as the target for unresolved imports:
    // mapped and executable, so both engines survive it, and nothing is written to guest memory.
    u64 FindReturnStub(u64 mod_base) {
        auto& mem = system.ApplicationMemory();
        constexpr u32 kMovX0Zero = 0xD2800000, kRet = 0xD65F03C0;
        // Text sits at the front of every NSO; a module without a matching pair
        // in its first megabyte does not have one worth scanning further for.
        constexpr u64 kScanLimit = 0x100000;
        u64 bare_ret = 0;
        for (u64 off = 0; off < kScanLimit; off += 4) {
            const u32 insn = mem.Read32(mod_base + off);
            if (insn == kRet) {
                if (!bare_ret) bare_ret = mod_base + off;
            } else if (insn == kMovX0Zero && mem.Read32(mod_base + off + 4) == kRet) {
                return mod_base + off;
            }
        }
        return bare_ret;
    }

    // Locate a module's MOD0 header and parse its .dynamic section. Returns
    // false if this module has no MOD0 (nothing to relocate).
    bool ParseDynamic(u64 mod_base, DynInfo& out) {
        auto& mem = system.ApplicationMemory();
        // Scan the first few KB for the MOD0 magic (0x30444F4D); typically at mod+0x2000 for rtld.
        u64 mod0_va = 0;
        for (u64 off = 0; off < 0x4000; off += 4) {
            if (mem.Read32(mod_base + off) == 0x30444F4Du) {
                mod0_va = mod_base + off;
                break;
            }
        }
        if (!mod0_va) return false;

        // MOD0 layout: magic(4), dyn_offset(4), bss_start(4), bss_end(4)
        // dyn_offset is relative to the MOD0 header itself.
        const u32 dyn_rel_off = mem.Read32(mod0_va + 4);
        const u64 dyn_va = mod0_va + dyn_rel_off;

        constexpr u32 DT_NULL = 0, DT_PLTRELSZ = 2, DT_STRTAB = 5, DT_SYMTAB = 6, DT_RELA = 7,
                       DT_RELASZ = 8, DT_RELAENT = 9, DT_PLTREL = 20, DT_JMPREL = 23,
                       DT_REL_TAG = 17;
        out.mod_base = mod_base;
        u64 pltrel_kind = DT_RELA; // default per AArch64 ABI (RELA, not REL)
        for (u64 p = dyn_va; ; p += 16) {
            const u64 tag = mem.Read64(p);
            const u64 val = mem.Read64(p + 8);
            if (tag == DT_NULL) break;
            if (tag == DT_RELA)     out.rela_va    = mod_base + val;
            if (tag == DT_RELASZ)   { out.rela_sz = val; out.rela_sz_va = p + 8; }
            if (tag == DT_RELAENT)  out.rela_ent   = val;
            if (tag == DT_JMPREL)   out.jmprel_va  = mod_base + val;
            if (tag == DT_PLTRELSZ) { out.jmprel_sz = val; out.jmprel_sz_va = p + 8; }
            if (tag == DT_PLTREL)   pltrel_kind    = val;
            if (tag == DT_SYMTAB)   out.symtab_va  = mod_base + val;
            if (tag == DT_STRTAB)   out.strtab_va  = mod_base + val;
            if (p - dyn_va > 0x1000) break; // safety
        }
        // DT_PLTREL selects 16-byte REL over 24-byte RELA JMPREL entries; checked after the loop as it can appear anywhere.
        out.jmprel_ent = (pltrel_kind == DT_REL_TAG) ? 16 : 24;
        return true;
    }

    // Reads a symbol's name (.dynstr) and value for GLOB_DAT/JUMP_SLOT resolution; Elf64_Sym is 24 bytes.
    struct SymInfo {
        std::string name;
        u64 value = 0;
        bool defined = false;
        bool weak = false;
    };
    SymInfo ReadSymbol(const DynInfo& d, u32 index) {
        auto& mem = system.ApplicationMemory();
        SymInfo s;
        if (!d.symtab_va) return s;
        const u64 sym_va = d.symtab_va + static_cast<u64>(index) * 24;
        const u32 name_off = mem.Read32(sym_va);
        // st_shndx is the 2-byte field at +6; reading 4 bytes spilled into st_value and corrupted the defined check.
        const u16 shndx = mem.Read16(sym_va + 6);
        s.value = mem.Read64(sym_va + 8);
        s.defined = shndx != 0; // SHN_UNDEF == 0
        // st_info is at +4 with the binding in the high nibble (STB_WEAK == 2); an undefined weak symbol must resolve to 0.
        s.weak = (static_cast<u8>(mem.Read8(sym_va + 4)) >> 4) == 2;
        if (d.strtab_va) {
            std::string name;
            for (u64 i = 0; i < 512; ++i) {
                const u8 c = static_cast<u8>(mem.Read8(d.strtab_va + name_off + i));
                if (!c) break;
                name.push_back(static_cast<char>(c));
            }
            s.name = std::move(name);
        }
        return s;
    }

    // Every module's defined symbols by name, built across all modules before any relocation writes,
    // so cross-module GLOB_DAT/JUMP_SLOT references resolve.
    void IndexExports(const DynInfo& d, std::unordered_map<std::string, u64>& out) {
        if (!d.symtab_va || !d.strtab_va) return;
        // .dynsym and .dynstr are back to back in every observed Switch module, so their gap is a reliable entry count
        // (guessing from name offsets cut exports short and made rtld abort).
        u32 max_index = 8192;
        if (d.strtab_va > d.symtab_va) {
            const u64 span = d.strtab_va - d.symtab_va;
            max_index = static_cast<u32>(std::min<u64>(span / 24, 65536));
        }
        for (u32 i = 1; i < max_index; ++i) { // index 0 is always the null symbol
            const auto sym = ReadSymbol(d, i);
            if (sym.defined && !sym.name.empty()) {
                out.emplace(sym.name, d.mod_base + sym.value);
            }
        }
    }

    void ApplyRelocTable(const DynInfo& d, u64 table_va, u64 table_sz, u64 entry_sz,
                          const std::unordered_map<std::string, u64>& exports, u32& applied,
                          u32& unresolved) {
        auto& mem = system.ApplicationMemory();
        constexpr u32 R_AARCH64_ABS64 = 0x101, R_AARCH64_RELATIVE = 0x403,
                       R_AARCH64_GLOB_DAT = 0x401, R_AARCH64_JUMP_SLOT = 0x402,
                       R_AARCH64_IRELATIVE = 0x408;
        for (u64 p = table_va; p < table_va + table_sz; p += entry_sz) {
            const u64 r_offset = mem.Read64(p);
            const u64 r_info   = mem.Read64(p + 8);
            const u64 r_addend = mem.Read64(p + 16);
            const u32 r_type = static_cast<u32>(r_info & 0xFFFFFFFF);
            const u32 r_sym  = static_cast<u32>(r_info >> 32);
            if (r_type == R_AARCH64_RELATIVE) {
                mem.Write64(d.mod_base + r_offset, d.mod_base + r_addend);
                ++applied;
            } else if (r_type == R_AARCH64_IRELATIVE) {
                // IRELATIVE: r_addend is a resolver address and calling it needs nested-call machinery this backend lacks,
                // so patch to the trap sentinel like an unresolved import instead of leaving raw file content in the slot.
                LOG_ERROR(Core_ARM,
                          "recomp: IRELATIVE relocation at module base={:#x} offset={:#x} not "
                          "invoked (resolver call unsupported); trapped instead",
                          d.mod_base, r_offset);
                mem.Write64(d.mod_base + r_offset, d.trap_va ? d.trap_va : kUnresolvedImportTrap);
            } else if (r_type == R_AARCH64_GLOB_DAT || r_type == R_AARCH64_JUMP_SLOT ||
                       r_type == R_AARCH64_ABS64) {
                const auto sym = ReadSymbol(d, r_sym);
                // ABS64 is S + A (GLOB_DAT/JUMP_SLOT are plain S) and covers every vtable/typeinfo/function-pointer slot;
                // skipping it left unmapped module-relative values (the 0xe7ff0 boot crashes).
                const u64 addend = (r_type == R_AARCH64_ABS64) ? r_addend : 0;
                // Linker section-boundary symbols (__got_start, __rela_dyn_end, ...) describe this module's own layout but are
                // often left SHN_UNDEF with a correct st_value, so a nonzero value is trusted before export lookup.
                u64 synthetic = 0;
                bool is_synthetic = true;
                // These cover this module's own relocation sections; the loader patches them from the bounds parsed above.
                if (sym.name == "__rela_dyn_start" || sym.name == "__rel_dyn_start") {
                    synthetic = d.rela_va;
                } else if (sym.name == "__rela_dyn_end" || sym.name == "__rel_dyn_end") {
                    synthetic = d.rela_va + d.rela_sz;
                } else if (sym.name == "__rela_plt_start" || sym.name == "__rel_plt_start") {
                    synthetic = d.jmprel_va;
                } else if (sym.name == "__rela_plt_end" || sym.name == "__rel_plt_end") {
                    synthetic = d.jmprel_va + d.jmprel_sz;
                } else {
                    is_synthetic = false;
                }
                if (is_synthetic && synthetic) {
                    mem.Write64(d.mod_base + r_offset, synthetic + addend);
                    ++applied;
                } else if (sym.defined || sym.value != 0) {
                    mem.Write64(d.mod_base + r_offset, d.mod_base + sym.value + addend);
                    ++applied;
                } else if (auto it = exports.find(sym.name); it != exports.end()) {
                    mem.Write64(d.mod_base + r_offset, it->second + addend);
                    ++applied;
                } else if (r_type == R_AARCH64_ABS64 && r_sym == 0) {
                    // STN_UNDEF ABS64: S is 0, so the result is the addend alone, a plain constant; never trap it.
                    mem.Write64(d.mod_base + r_offset, addend);
                    ++applied;
                } else {
                    ++unresolved;
                    if (unresolved <= 30) {
                        LOG_ERROR(Core_ARM, "recomp: unresolved GOT/PLT symbol '{}' for module base={:#x}",
                                  sym.name.empty() ? "<no name>" : sym.name, d.mod_base);
                    }
                    // An undefined weak symbol resolves to 0 by ABI; a callable stub would break `if (&weak) weak(...)` (Smash's
                    // VirtualAllocHook, 5489 sites). Strong symbols keep the trap sentinel.
                    const u64 stub =
                        sym.weak ? 0 : (d.trap_va ? d.trap_va : kUnresolvedImportTrap);
                    mem.Write64(d.mod_base + r_offset, stub);
                    // FindReturnStub always succeeds, so the trap counter never sees this case; record the slot so a later poll
                    // can tell whether anything (e.g. nn::ro binding an NRO) overwrote it.
                    if (!kTrackSlotPrefix.empty() &&
                        sym.name.rfind(kTrackSlotPrefix, 0) == 0) {
                        diagnostics.TrackSlot(d.mod_base + r_offset, stub, sym.name);
                    }
                }
            }
        }
    }

    // Pre-applies relocations for every loaded module: the recompiled rtld self-relocation loop exits early,
    // leaving RELATIVE/GLOB_DAT/JUMP_SLOT entries unapplied. All bases are known, so exports are indexed first.
    void ApplyAllRelocations(const std::map<u64, std::string>& all_modules) {
        auto& mem = system.ApplicationMemory();
        std::vector<DynInfo> dyns;
        std::unordered_map<std::string, u64> exports;
        for (const auto& [module_base, name] : all_modules) {
            DynInfo d;
            if (ParseDynamic(module_base, d)) {
                d.trap_va = FindReturnStub(module_base);
                d.name = name;
                IndexExports(d, exports);
                dyns.push_back(d);
            }
        }
        for (const auto& d : dyns) {
            u32 applied = 0, unresolved = 0;
            const bool is_rtld = d.name.find("rtld") != std::string::npos;
            if (kRelaPolicy == "rtld-only" && !is_rtld) {
                // Left for rtld: it must see this module as under dynarmic (including deferred imports) or nn::ro cannot bind them.
                LOG_INFO(Core_ARM, "recomp: leaving {} base={:#x} to rtld", d.name, d.mod_base);
                continue;
            }
            if (d.rela_va && d.rela_sz) {
                ApplyRelocTable(d, d.rela_va, d.rela_sz, d.rela_ent, exports, applied, unresolved);
            }
            if (d.jmprel_va && d.jmprel_sz) {
                ApplyRelocTable(d, d.jmprel_va, d.jmprel_sz, d.jmprel_ent, exports, applied,
                                 unresolved);
            }
            // Zero DT_RELASZ/DT_PLTRELSZ so rtld's non-idempotent bootstrap self-relocation skips both tables; finding them
            // resolved trips its consistency check and svcBreak (the boot hang). Zeroing other modules breaks nn::ro binding.
            if (kRelaPolicy == "zero-all" || kRelaPolicy == "restore-on-main" ||
                ((kRelaPolicy == "zero-rtld" || kRelaPolicy == "rtld-only") && is_rtld)) {
                if (d.rela_sz_va) mem.Write64(d.rela_sz_va, 0);
                if (d.jmprel_sz_va) mem.Write64(d.jmprel_sz_va, 0);
            }
            LOG_INFO(Core_ARM,
                     "recomp: pre-applied {} relocations ({} unresolved external symbols) for "
                     "module base={:#x}",
                     applied, unresolved, d.mod_base);
        }
        // Only the first Impl through here observes the real sizes; see
        // g_rela_restore.
        {
            std::scoped_lock lk{g_rela_restore_lock};
            if (g_rela_restore.empty()) {
                for (const auto& d : dyns) {
                    if (d.rela_sz_va && d.rela_sz) {
                        g_rela_restore.emplace_back(d.rela_sz_va, d.rela_sz);
                    }
                    if (d.jmprel_sz_va && d.jmprel_sz) {
                        g_rela_restore.emplace_back(d.jmprel_sz_va, d.jmprel_sz);
                    }
                    LOG_INFO(Core_ARM, "recomp: saved rela sizes for {} base={:#x} relasz={} pltrelsz={}",
                             d.name, d.mod_base, d.rela_sz, d.jmprel_sz);
                }
            }
        }
    }

    // Put DT_RELASZ/DT_PLTRELSZ back for every module. Only meaningful under
    // the restore-on-main policy; the trigger owns proving rtld is done.
    void RestoreRelocSizes() {
        auto& mem = system.ApplicationMemory();
        std::scoped_lock lk{g_rela_restore_lock};
        for (const auto& [va, size] : g_rela_restore) {
            mem.Write64(va, size);
        }
        LOG_INFO(Core_ARM, "recomp: restored {} rela size fields", g_rela_restore.size());
    }

    void SampleDiagnostics(Kernel::KThread* thread) {
        if (!diagnostics.Enabled()) return;
        auto* process = thread->GetOwnerProcess();
        if (!process) return;
        auto& memory = process->GetMemory();
        const u64 process_id = process->GetId();
        diagnostics.Sample(RecompDiagnosticSampler::Clock::now(), thread->GetId(),
            ctx.pc, ctx.x[30], ctx.x[19], modules,
            [&](u64 address) { return memory.Read32(address); },
            [&](u64 address) { return memory.Read64(address); },
            [&](u64 address) { return memory.IsValidVirtualAddress(address); },
            [&](const std::string& snapshot) {
                std::scoped_lock lock{g_snapshot_lock};
                g_diagnostic_snapshots[{process_id, core_index}] = snapshot;
            },
            [](const std::string& line) { LOG_INFO(Core_ARM, "{}", line); });
    }

    RecompDiagnosticSampler diagnostics;
    RecompThreadTimeLedger thread_time;
    System& system;
    RecompLookupFn lookup{};
    GuestContextView ctx{};
    RecompHostMem bridge{};
    std::atomic<bool> interrupted{false};
    Loader::AppLoader::Modules modules{};
    bool modules_read{false};
    // Whether the "pre-relocation modules" line was written; cores of a bound process share one flag
    // (BindRecompProcess), an unbound legacy Impl keeps its own.
    std::shared_ptr<std::atomic_bool> prerelocation_logged{std::make_shared<std::atomic_bool>(false)};
    bool rela_applied{false};
    bool explicitly_prepared{false};
    /// Every exclusive/counter bridge callback is installed (CheckBridgeComplete).
    bool bridge_complete{false};
    static constexpr size_t kTrail = 32;
    u64 trail[kTrail]{};
    size_t trail_pos{0};

    // Interpreter fallback for PCs the static pass never covered; built on the first miss, since most runs need none.
    Kernel::KProcess* owner_process{};
    // The base interface, not dynarmic's type: holding the concrete type forced the library into builds with no JIT.
    ExclusiveMonitor* exclusive_monitor{};
    // Last guest thread run on this core, so a reservation is voided on a real
    // context switch and left alone on an ordinary return to the host.
    Kernel::KThread* last_thread{};
    std::size_t core_index{};
    bool uses_wall_clock{};
#ifndef SUYU_NO_JIT
    std::unique_ptr<ArmDynarmic64> fallback{};
#endif
    bool in_fallback{false};
    bool fallback_unavailable{false};
};

ArmRecomp::ArmRecomp(System& system, bool uses_wall_clock, RecompLookupFn lookup,
                     Kernel::KProcess* process, ExclusiveMonitor* exclusive_monitor,
                     std::size_t core_index)
    : ArmInterface{uses_wall_clock}, impl{std::make_unique<Impl>(system, lookup)} {
    impl->owner_process = process;
    impl->exclusive_monitor = exclusive_monitor;
    impl->core_index = core_index;
    impl->uses_wall_clock = uses_wall_clock;
    RecompDiagnosticSampler::Config diagnostic_config;
    diagnostic_config.snapshots = kSamplePc;
    const char* lr_env = std::getenv("SUYU_RECOMP_WATCH_LR_OFFSET");
    const char* addr_env = std::getenv("SUYU_RECOMP_WATCH_ADDR");
    diagnostic_config.watch = (lr_env && *lr_env) || (addr_env && *addr_env);
    diagnostic_config.lr_offset = (lr_env && *lr_env) ? std::strtoull(lr_env, nullptr, 0) : 0;
    diagnostic_config.fixed_address = (addr_env && *addr_env) ? std::strtoull(addr_env, nullptr, 0) : 0;
    if (diagnostic_config.watch) {
        const char* path_env = std::getenv("SUYU_RECOMP_WATCH_PATH");
        diagnostic_config.watch_path = (path_env && *path_env)
            ? std::string{path_env}
            : (Common::FS::GetSuyuPath(Common::FS::SuyuPath::LogDir) / "recomp_watch.txt").string();
    }
    impl->diagnostics.Configure(process ? process->GetId() : 0, core_index,
                                std::move(diagnostic_config));
    // Snapshot storage contains copied text only. A new session starts fresh.
    if (g_live_instances.fetch_add(1, std::memory_order_relaxed) == 0) {
        std::scoped_lock lock{g_snapshot_lock};
        g_diagnostic_snapshots.clear();
        // Before the loader places any module: it reports each one as it does.
        g_session_blocks_start.store(TotalStaticBlocks(), std::memory_order_relaxed);
        RecompGaps::BeginSession(process ? process->GetProgramId() : 0, StrictNoFallback());
    }
}

ArmRecomp::~ArmRecomp() {
    if (impl->core_index < g_current_pcs.size()) {
        g_current_pcs[impl->core_index].pc.store(0, std::memory_order_relaxed);
    }
    // Reports copied counters only, never a finalized process; once per session.
    if (g_live_instances.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        ReportRecompCoverage();
        RecompGaps::EndSession(TotalStaticBlocks() >
                               g_session_blocks_start.load(std::memory_order_relaxed));
    }
}

bool PrepareRecompProcess(Kernel::KProcess& process, const RecompModules& modules) {
    const auto prepare = g_recomp_prepare.load(std::memory_order_acquire);
    if (!prepare) return true;
    // KProcess::InitializeInterfaces chooses ArmRecomp for every application
    // core while this lookup is installed. RTTI is disabled in core builds.
    if (!GetRecompLookup() || !process.IsApplication() || !process.Is64Bit() ||
        modules.empty() || modules.begin()->second != "rtld" ||
        modules.begin()->first != GetInteger(process.GetEntryPoint())) {
        LOG_ERROR(Core_ARM, "Static preparation requires an AArch64 rtld entry point");
        return false;
    }
    for (size_t i = 0; i < Hardware::NUM_CPU_CORES; ++i) {
        const auto* cpu = static_cast<ArmRecomp*>(process.GetArmInterface(i));
        if (!cpu || cpu->impl->explicitly_prepared) return false;
    }
    if (!prepare(modules)) return false;
    for (size_t i = 0; i < Hardware::NUM_CPU_CORES; ++i) {
        auto& state = *static_cast<ArmRecomp*>(process.GetArmInterface(i))->impl;
        state.modules = modules;
        state.modules_read = true;
        // Run real rtld relocation. The desktop host pre-relocator substitutes
        // unresolved imports and must never modify this strict session's memory.
        state.rela_applied = true;
        state.explicitly_prepared = true;
    }
    g_counters.RecordModules(modules);
    return true;
}

// "name@base" for each module in load (base address) order, for the inventory log lines.
static std::string DescribeModuleInventory(const RecompModules& modules) {
    std::string text;
    for (const auto& [base, name] : modules) {
        text += fmt::format("{}{}@{:#x}", text.empty() ? "" : " ", name, base);
    }
    return text;
}

bool BindRecompProcess(Kernel::KProcess& process, const RecompModules& inventory) {
    const auto bind = g_recomp_bind.load(std::memory_order_acquire);
    if (!bind) return true;
    // Same preconditions as PrepareRecompProcess, checked before any downcast.
    if (!GetRecompLookup()) {
        LOG_CRITICAL(Core_ARM, "recomp: binding requires an installed recompiled lookup");
        return false;
    }
    if (!process.IsApplication() || !process.Is64Bit()) {
        LOG_CRITICAL(Core_ARM, "recomp: binding requires an AArch64 application process");
        return false;
    }
    if (inventory.empty()) {
        LOG_CRITICAL(Core_ARM, "recomp: binding requires a non-empty module inventory");
        return false;
    }
    // Collect every core's interface first, so nothing is bound for a process
    // that cannot take the inventory.
    ArmRecomp* cpus[Hardware::NUM_CPU_CORES]{};
    for (size_t i = 0; i < Hardware::NUM_CPU_CORES; ++i) {
        cpus[i] = static_cast<ArmRecomp*>(process.GetArmInterface(i));
        if (!cpus[i]) {
            LOG_CRITICAL(Core_ARM, "recomp: core {} has no ArmRecomp interface to bind", i);
            return false;
        }
    }
    if (!bind(inventory)) return false;
    const std::string inventory_text = DescribeModuleInventory(inventory);
    const auto process_prerelocation_logged = std::make_shared<std::atomic_bool>(false);
    for (size_t i = 0; i < Hardware::NUM_CPU_CORES; ++i) {
        cpus[i]->impl->prerelocation_logged = process_prerelocation_logged;
        // rela_applied and explicitly_prepared stay false: host pre-relocation still runs and needs the full canonical
        // inventory (including a module whose embedded path FindModules cannot read).
        cpus[i]->impl->modules = inventory;
        cpus[i]->impl->modules_read = true;
        // Greppable by tests that check which inventory a process was bound to.
        LOG_INFO(Core_ARM, "recomp: bound inventory core {}: {}", i, inventory_text);
    }
    g_counters.RecordModules(inventory);
    return true;
}

bool ArmRecomp::EnterFallback() {
    if (impl->fallback_unavailable) {
        return false;
    }
    if (StrictNoFallback()) {
        // Latched, so the caller's own critical log naming the PC is what gets
        // read, and the second thread to arrive does not repeat this one.
        impl->fallback_unavailable = true;
        LOG_CRITICAL(Core_ARM, "recomp: strict mode - refusing to fall back to the JIT");
        return false;
    }
#ifdef SUYU_NO_JIT
    // Built without a dynamic recompiler at all. There is nothing to fall back
    // to, by construction rather than by configuration.
    impl->fallback_unavailable = true;
    LOG_CRITICAL(Core_ARM, "recomp: built without a JIT; uncovered code cannot run");
    return false;
#else
    if (!impl->fallback) {
        if (!impl->owner_process || !impl->exclusive_monitor) {
            impl->fallback_unavailable = true;
            return false;
        }
        // The JIT stores through the page table without the GG1 watch.
        RecompGuardGen::OnJitFallback();
        impl->fallback = std::make_unique<ArmDynarmic64>(
            impl->system, impl->uses_wall_clock, impl->owner_process,
            static_cast<DynarmicExclusiveMonitor&>(*impl->exclusive_monitor), impl->core_index);
        LOG_WARNING(Core_ARM, "recomp: created JIT fallback for uncovered code");
    }
    impl->in_fallback = true;
    return true;
#endif
}

HaltReason ArmRecomp::RunFallback(Kernel::KThread* thread) {
#ifdef SUYU_NO_JIT
    // Unreachable: EnterFallback never succeeds in this build. Kept so the one
    // call site needs no guard of its own.
    (void)thread;
    return HaltReason::PrefetchAbort;
#else
    // The recompiled context is the single source of truth: the JIT is loaded from it and drained back, so every
    // accessor on this interface works unchanged whichever engine ran.
    impl->ctx.pending_svc = kNoPendingSvc;
    impl->ctx.halted = 0;
    impl->interrupted.store(false, std::memory_order_relaxed);

    Kernel::Svc::ThreadContext tctx{};
    this->GetContext(tctx);
    impl->fallback->SetContext(tctx);
    impl->fallback->SetTpidrroEl0(impl->ctx.tpidrro_el0);

    const HaltReason hr = impl->fallback->RunThread(thread);

    impl->fallback->GetContext(tctx);
    this->SetContext(tctx);
    if (True(hr & HaltReason::SupervisorCall)) {
        impl->ctx.pending_svc = impl->fallback->GetSvcNumber();
    }

    if (True(hr & HaltReason::SupervisorCall)) {
        HotCounters::Increment(t_hot_counters, kSvcCallsCounter);
    }

    // Return to recompiled execution as soon as the PC is covered again, so a
    // single uncovered function costs only the time spent inside it.
    if (impl->lookup && impl->lookup(impl->ctx.pc)) {
        impl->in_fallback = false;
        HotCounters::Increment(t_hot_counters, kJitToStaticCounter);
    }
    return hr;
#endif
}

HaltReason ArmRecomp::RunThread(Kernel::KThread* thread) {
    std::optional<ThreadTimeScope> thread_time_scope;
    if (kThreadTime) {
        thread_time_scope.emplace(impl->thread_time, impl->core_index, thread->GetId(),
                                  impl->ctx.pc);
    }
    // A context switch voids any outstanding reservation, or a preempted LDXR/STXR pair could succeed against a word
    // another thread changed and lose a mutex update. Only on a real switch: this backend returns to the host constantly.
    if (impl->last_thread != thread) {
        impl->last_thread = thread;
        if (impl->exclusive_monitor) {
            impl->exclusive_monitor->ClearExclusive(impl->core_index);
        }
    }
    // Logged once so a log shows whether the backend was ever entered.
    static std::atomic_bool announced{false};
    bool first_run = !announced.exchange(true, std::memory_order_relaxed);
    if (first_run) {
        LOG_INFO(Core_ARM, "ArmRecomp::RunThread entered, pc={:#x}", impl->ctx.pc);
    }
    if (!impl->lookup) {
        LOG_ERROR(Core_ARM, "No recompiled code registered; cannot run thread");
        return HaltReason::BreakLoop;
    }
    if (!impl->bridge_complete) {
        LOG_CRITICAL(Core_ARM, "recomp: incomplete host bridge on core {}; refusing to run",
                     impl->core_index);
        return HaltReason::BreakLoop;
    }

    impl->RefreshPageTable();
    // FPX1: the guest register is 32 bits; bit 32 is this host's kill switch,
    // re-applied on every entry because every import of the context drops it.
    const bool fpx_active = FpxActive();
    impl->ctx.fpcr = (impl->ctx.fpcr & 0xffffffffULL) | FpxInhibitBits();
    if (fpx_active) {
        RepairFpEnv("entry");
    }
    if (first_run) {
        LOG_INFO(Core_ARM, "ArmRecomp FPX1 native FP: {}",
                 !g_fpx_ready.load(std::memory_order_acquire)
                     ? "not used"
                     : kFpxDisabled ? "negotiated, disabled by SUYU_RECOMP_FPX=0"
                                    : "on, host FP mode enforced");
    }
    if (first_run) {
        LOG_INFO(Core_ARM,
                 "ArmRecomp page table ready: entries={} stride={} page_bits={} max={:#x} "
                 "fastmem={} (limit {:#x}{})",
                 impl->bridge.page_entries != nullptr, impl->bridge.page_entry_stride,
                 impl->bridge.page_bits, impl->bridge.address_space_max,
                 impl->ctx.fm_limit != 0, impl->ctx.fm_limit,
                 kFastmemDisabled ? ", disabled by SUYU_RECOMP_FASTMEM=0" : "");
    }

    // Registering image bases with the host dispatcher is a side effect of this call, needed once before the first
    // lookup or every image's base stays 0 and every lookup misses.
    if (HasRecompPrepareCallback() && !impl->explicitly_prepared) {
        LOG_ERROR(Core_ARM, "Refusing unprepared static guest execution");
        return HaltReason::BreakLoop;
    }
    // Keep the legacy desktop path isolated. Explicit sessions never enter
    // this process-global lazy protocol or its heuristic relocation path.
    if (!impl->explicitly_prepared && !impl->rela_applied) {
        // Every CPU Impl needs its own module map and relocation pass (once per KProcess left later Impls incomplete);
        // the shared guest-memory writes are serialized.
        std::scoped_lock lock{g_process_init_lock};
        if (!impl->rela_applied) {
            impl->ModuleBaseFor(thread, impl->ctx.pc);
            // Cores of a bound process relocate the same memory from the same map, so the module list is logged once per process.
            if (!impl->prerelocation_logged->exchange(true)) {
                LOG_INFO(Core_ARM, "recomp: pre-relocation modules: {}",
                         DescribeModuleInventory(impl->modules));
            }
            impl->ApplyAllRelocations(impl->modules);
            impl->rela_applied = true;
        }
    }

    // ABI 6 GG1: the first run of each process decides which modules may skip the per-entry check (bases set, no block run yet).
    if (RecompGuardGen::Watching()) {
        if (auto* process = thread->GetOwnerProcess()) {
            const u64 key = process->GetProcessId() | (u64{1} << 63);
            if (!RecompGuardGen::IsActive(key)) {
                impl->ActivateGuardGen(*process, key);
            }
        }
    }

    if (kRelaPolicy == "restore-on-main" && !g_rela_restored.load(std::memory_order_acquire) &&
        impl->PcInMainModule(impl->ctx.pc)) {
        // rtld is the entrypoint and relocates every module before branching to main, so the first PC inside main is after
        // rtld is done with both tables and long before nn::ro loads any NRO.
        std::scoped_lock lock{g_process_init_lock};
        if (!g_rela_restored.exchange(true, std::memory_order_acq_rel)) {
            LOG_INFO(Core_ARM, "recomp: first execution in main at pc={:#x}; restoring rela sizes",
                     impl->ctx.pc);
            impl->RestoreRelocSizes();
        }
    }

    // A previous miss handed this thread to the JIT; keep running there until the PC is recompiled again. The trap
    // sentinel is caught before that hand-off too, since the JIT cannot execute that unmapped address.
    if (impl->ctx.pc == kUnresolvedImportTrap) {
        impl->ctx.x[0] = 0;
        impl->ctx.pc = impl->ctx.x[30];
        impl->in_fallback = false;
    }
    if (impl->in_fallback) {
        return RunFallback(thread);
    }

    impl->interrupted.store(false, std::memory_order_relaxed);
    impl->ctx.halted = 0;

    while (!impl->ctx.halted) {
        // A host callback or SVC path may have changed this thread's FP mode; one control-register read per dispatch.
        if (fpx_active && !RecompFpEnv::Conforms()) {
            RepairFpEnv("dispatch");
        }
        impl->SampleDiagnostics(thread);
        if (impl->core_index < g_current_pcs.size()) {
            g_current_pcs[impl->core_index].pc.store(impl->ctx.pc, std::memory_order_relaxed);
        }
        if (impl->interrupted.load(std::memory_order_relaxed)) {
            return HaltReason::BreakLoop;
        }

        // An SVC parked us last time round; the kernel has now serviced it and
        // resumed, so clear it before continuing.
        if (impl->ctx.pending_svc != kNoPendingSvc) {
            impl->ctx.pending_svc = kNoPendingSvc;
        }

        // The dispatcher picks the image and offset for an absolute PC; a guess by pc-base ran the wrong module's code.
        // Trail of recent PCs: a wild indirect branch reports only where it landed, not which block computed it.
        impl->trail[impl->trail_pos++ & (Impl::kTrail - 1)] = impl->ctx.pc;

        if (impl->ctx.pc == kUnresolvedImportTrap) {
            // BLR through a GOT/JUMP_SLOT patched by ApplyRelocTable with no definition: most such imports are never called in a
            // given boot, so return to the caller (x30) with x0 = 0 as if a function did nothing.
            static std::atomic<int> trap_count{0};
            if (trap_count.fetch_add(1, std::memory_order_relaxed) < 16) {
                g_counters.unresolved_import_traps.fetch_add(1, std::memory_order_relaxed);
                LOG_ERROR(Core_ARM, "recomp: called through unresolved import (returning to caller {:#x})",
                          impl->ctx.x[30]);
            }
            impl->ctx.x[0] = 0;
            impl->ctx.pc = impl->ctx.x[30];
            continue;
        }

        // Counted only until the log limit, so the shared line goes quiet afterwards.
        static std::atomic<u64> early_dispatches{0};
        u64 early_dispatch = kEarlyDispatchLogLimit;
        if (early_dispatches.load(std::memory_order_relaxed) < kEarlyDispatchLogLimit) {
            early_dispatch = early_dispatches.fetch_add(1, std::memory_order_relaxed);
        }
        if (early_dispatch < kEarlyDispatchLogLimit) {
            LOG_INFO(Core_ARM, "Early static dispatch #{}: pc={:#x}", early_dispatch,
                     impl->ctx.pc);
        }
        RecompBlockFn block = impl->lookup(impl->ctx.pc);
        if (first_run) {
            LOG_INFO(Core_ARM, "First static lookup: pc={:#x}, covered={}", impl->ctx.pc,
                     block != nullptr);
        }
        // Test hook: forces every lookup past the Nth to miss, to exercise the JIT fallback on a title with no gap.
        {
            static const char* const force_miss = std::getenv("SUYU_RECOMP_FORCE_MISS_AFTER");
            static const char* const force_miss_static_blocks =
                std::getenv("SUYU_RECOMP_FORCE_MISS_AFTER_STATIC_BLOCKS");
            static std::atomic<int> blocks_run{0};
            if (force_miss) {
                const int n = blocks_run.fetch_add(1, std::memory_order_relaxed);
                const int limit = std::atoi(force_miss);
                // Name the blocks either side of the cutoff, turning a bisect index into a guest address.
                if (n >= limit - 4 && n <= limit + 4) {
                    LOG_ERROR(Core_ARM, "recomp: block #{} pc={:#x}", n, impl->ctx.pc);
                }
                if (n >= limit) {
                    block = nullptr;
                }
            }
            // Same hook keyed on a module-relative guest PC, matched against every loaded base (bases differ per run); hands over
            // at one named instruction, turning a bracket into a bisect.
            static const char* const force_miss_at_offset =
                std::getenv("SUYU_RECOMP_FORCE_MISS_AT_MODULE_OFFSET");
            if (force_miss_at_offset) {
                static const u64 target = std::strtoull(force_miss_at_offset, nullptr, 0);
                // Snapshotted, not read under the counters' lock per block; retried while empty as the loader may not have
                // registered bases yet.
                static std::vector<u64> bases;
                if (bases.empty()) {
                    std::scoped_lock lk{g_counters.hist_lock};
                    for (const auto& [base, name] : g_counters.modules) {
                        bases.push_back(base);
                    }
                }
                for (const u64 base : bases) {
                    if (impl->ctx.pc - base == target) {
                        u64 unset = 0;
                        if (g_forced_cutoff_pc.compare_exchange_strong(
                                unset, impl->ctx.pc, std::memory_order_relaxed)) {
                            g_forced_cutoff_blocks.store(TotalStaticBlocks(),
                                                         std::memory_order_relaxed);
                            LOG_ERROR(Core_ARM,
                                      "recomp: forced offset cutoff {:#x} reached pc={:#x} "
                                      "after {} blocks",
                                      target, impl->ctx.pc,
                                      g_forced_cutoff_blocks.load(std::memory_order_relaxed));
                        }
                        block = nullptr;
                        break;
                    }
                }
            }
            // Address-range hole: guest PCs whose offset in `main` falls in [lo, hi) miss the lookup and run under the JIT,
            // narrowing a fault within a module. Read at runtime; needs fallback allowed (SUYU_RECOMP_STRICT makes a miss an abort).
            static const char* const main_hole = std::getenv("SUYU_RECOMP_MAIN_HOLE");
            if (main_hole) {
                static u64 hole_lo = 0;
                static u64 hole_hi = 0;
                static const bool hole_valid = [] {
                    char* end = nullptr;
                    hole_lo = std::strtoull(main_hole, &end, 0);
                    if (!end || *end != '-') {
                        LOG_ERROR(Core_ARM, "recomp: SUYU_RECOMP_MAIN_HOLE={} is not <lo>-<hi>",
                                  main_hole);
                        return false;
                    }
                    hole_hi = std::strtoull(end + 1, nullptr, 0);
                    if (hole_hi <= hole_lo) {
                        LOG_ERROR(Core_ARM, "recomp: SUYU_RECOMP_MAIN_HOLE={} is empty", main_hole);
                        return false;
                    }
                    return true;
                }();
                // Substring of the module name the offsets are relative to (modules use the NSO's own name, e.g. Smash's main is
                // cross2_Release.nss); unset applies the hole to every module, as the all-to-JIT control wants.
                static const char* const hole_module = std::getenv("SUYU_RECOMP_HOLE_MODULE");
                // Snapshotted, and retried while empty, because the first guest
                // blocks can run before the loader registers module bases.
                static std::vector<u64> hole_bases;
                if (hole_valid && hole_bases.empty()) {
                    std::scoped_lock lk{g_counters.hist_lock};
                    for (const auto& [base, name] : g_counters.modules) {
                        if (!hole_module || name.find(hole_module) != std::string::npos) {
                            hole_bases.push_back(base);
                        }
                    }
                    if (!hole_bases.empty()) {
                        LOG_ERROR(Core_ARM, "recomp: main hole {:#x}-{:#x} armed on {} module(s)",
                                  hole_lo, hole_hi, hole_bases.size());
                    } else if (!g_counters.modules.empty()) {
                        // Named a module that is not loaded. Silence here would
                        // look exactly like a range that never executes.
                        LOG_ERROR(Core_ARM, "recomp: SUYU_RECOMP_HOLE_MODULE={} matched no module",
                                  hole_module);
                    }
                }
                for (const u64 base : hole_bases) {
                    if (impl->ctx.pc < base) {
                        continue;
                    }
                    const u64 off = impl->ctx.pc - base;
                    if (off >= hole_lo && off < hole_hi) {
                        // Logged once so a run proves the hole was applied; a silently ignored hole looks like an uninformative bisection arm.
                        static std::atomic<bool> hole_announced{false};
                        if (!hole_announced.exchange(true, std::memory_order_relaxed)) {
                            LOG_ERROR(Core_ARM,
                                      "recomp: main hole {:#x}-{:#x} active (base {:#x}), first "
                                      "handover at pc={:#x}",
                                      hole_lo, hole_hi, base, impl->ctx.pc);
                        }
                        block = nullptr;
                        break;
                    }
                }
            }
            // Same hook keyed on wall time: a stalled title stops retiring blocks, so a block-count cutoff inside it is never reached.
            static const char* const force_miss_after_sec =
                std::getenv("SUYU_RECOMP_FORCE_MISS_AFTER_SEC");
            if (force_miss_after_sec) {
                const double limit = std::atof(force_miss_after_sec);
                const double elapsed = std::chrono::duration<double>(
                                           std::chrono::steady_clock::now() - kRecompStart)
                                           .count();
                if (elapsed >= limit) {
                    u64 unset = 0;
                    if (g_forced_cutoff_pc.compare_exchange_strong(
                            unset, impl->ctx.pc, std::memory_order_relaxed)) {
                        g_forced_cutoff_blocks.store(TotalStaticBlocks(),
                                                     std::memory_order_relaxed);
                        LOG_ERROR(Core_ARM, "recomp: forced time cutoff {}s reached pc={:#x}",
                                  limit, impl->ctx.pc);
                    }
                    block = nullptr;
                }
            }
            if (force_miss_static_blocks) {
                const u64 limit = std::strtoull(force_miss_static_blocks, nullptr, 10);
                const u64 executed = TotalStaticBlocks();
                if (executed >= limit) {
                    u64 unset = 0;
                    if (g_forced_cutoff_pc.compare_exchange_strong(
                            unset, impl->ctx.pc, std::memory_order_relaxed)) {
                        g_forced_cutoff_blocks.store(executed, std::memory_order_relaxed);
                    }
                    static std::atomic<int> static_cutoff_logs{0};
                    if (static_cutoff_logs.fetch_add(1, std::memory_order_relaxed) < 8) {
                        LOG_ERROR(Core_ARM,
                                  "recomp: forced static-block cutoff {} reached at {} pc={:#x}",
                                  limit, executed, impl->ctx.pc);
                    }
                    block = nullptr;
                }
            }
        }
        // A miss is recoverable and can be frequent; the full dump is kept for the first few only.
        static std::atomic<int> miss_count{0};
        const int miss_index = block ? 0 : miss_count.fetch_add(1, std::memory_order_relaxed);
        if (!block && miss_index < 8) {
            std::string trail;
            const size_t count = std::min<size_t>(impl->trail_pos, Impl::kTrail);
            for (size_t i = 0; i < count; ++i) {
                const u64 p = impl->trail[(impl->trail_pos - count + i) & (Impl::kTrail - 1)];
                trail += fmt::format("{:#x} ", p);
            }
            LOG_ERROR(Core_ARM, "recomp PC trail (oldest first): {}", trail);
            LOG_ERROR(Core_ARM, "recomp regs x16={:#x} x17={:#x} x30={:#x} sp={:#x}",
                      impl->ctx.x[16], impl->ctx.x[17], impl->ctx.x[30], impl->ctx.x[31]);
            LOG_ERROR(Core_ARM, "recomp regs x0={:#x} x15={:#x} x18={:#x} x19={:#x}",
                      impl->ctx.x[0], impl->ctx.x[15], impl->ctx.x[18], impl->ctx.x[19]);
            // The whole register file: a miss is usually a bad value the previous block computed, and guessing which to print costs a rebuild.
            for (size_t r = 0; r < 32; r += 4) {
                LOG_ERROR(Core_ARM, "recomp regs x{:<2}={:#018x} x{:<2}={:#018x} x{:<2}={:#018x} x{:<2}={:#018x}",
                          r, impl->ctx.x[r], r + 1, impl->ctx.x[r + 1], r + 2, impl->ctx.x[r + 2],
                          r + 3, impl->ctx.x[r + 3]);
            }
            // Dump guest memory around likely pointer registers: a bad value and wrong data at the right address look the same in registers.
            for (u32 r : {8u, 22u, 25u}) {
                const u64 p = impl->ctx.x[r];
                if (p < 0x1000 || p > 0x0000'FFFF'FFFF'FFFFULL) {
                    continue;
                }
                std::string dump;
                for (s64 d = -0x20; d < 0x30; d += 4) {
                    dump += fmt::format("{:08x} ", (u32)Impl::HostLoad(impl.get(), p + d, 4));
                }
                LOG_ERROR(Core_ARM, "recomp mem @x{} ({:#x}) [-0x20..+0x30): {}", r, p, dump);
            }
            {
                const u64 mbase = impl->modules.empty() ? 0 : impl->modules.begin()->first;
                // A wide .rodata window to diff against the exporter's extracted copy: a mismatch means correct addresses into different memory.
                for (u64 w = 0x3c00; w < 0x3d80; w += 0x40) {
                    std::string dump;
                    for (u64 i = 0; i < 0x40; i += 4) {
                        dump += fmt::format("{:08x} ", (u32)Impl::HostLoad(impl.get(), mbase + w + i, 4));
                    }
                    LOG_ERROR(Core_ARM, "recomp rodata mod+{:#x}: {}", w, dump);
                }
                for (u64 seg : {0x0ULL, 0x2000ULL, 0x3000ULL}) {
                    std::string dump;
                    for (u64 i = 0; i < 0x40; i += 4) {
                        dump += fmt::format("{:08x} ", Impl::HostLoad(impl.get(), mbase + seg + i, 4));
                    }
                    LOG_ERROR(Core_ARM, "recomp mem mod+{:#x} (base {:#x}): {}", seg, mbase, dump);
                }
            }
        }
        if (!block) {
            // No recompiled block covers this address (an indirect branch into code the static pass never reached); hand the thread to
            // the JIT, since PrefetchAbort suspends it for an absent debugger and silently hangs on a black screen.
            if (miss_index < 64) {
                LOG_ERROR(Core_ARM, "No recompiled block at PC {:#x}; falling back to JIT",
                          impl->ctx.pc);
            } else {
                LOG_DEBUG(Core_ARM, "No recompiled block at PC {:#x}; falling back to JIT",
                          impl->ctx.pc);
            }
            g_counters.fallback_from_miss.fetch_add(1, std::memory_order_relaxed);
            g_counters.RecordMiss(impl->ctx.pc);
            RecompGaps::RecordMiss(impl->ctx.pc);
            if (!EnterFallback()) {
                g_counters.no_fallback_available.fetch_add(1, std::memory_order_relaxed);
                // A strict run stops here, and may never reach teardown.
                RecompGaps::Flush(true, true);
                LOG_CRITICAL(Core_ARM,
                             "recomp: no JIT fallback available at PC {:#x}; thread cannot "
                             "continue",
                             impl->ctx.pc);
                return HaltReason::PrefetchAbort;
            }
            return RunFallback(thread);
        }

        // Dump periodically since teardown may never run; one compare per block against a power-of-two mask.
        const u64 seen = t_blocks.slot->n.load(std::memory_order_relaxed) + 1;
        t_blocks.slot->n.store(seen, std::memory_order_relaxed);
        if ((seen & 0x3FFFFULL) == 0x3FFFFULL) {
            WriteRecompCoverageFile(FormatRecompCoverage());
            // Time-limited inside; teardown is not guaranteed, as above.
            RecompGaps::Flush(true, false);
        }
        if (kExclusiveStats && (seen & kExclusiveStatsTickMask) == kExclusiveStatsTickMask) {
            if (const auto report = g_exclusive_stats.TakeReport(
                    SteadyNowNs(), RecompExclusiveStats::ReadTicks(), kExclusiveStatsWindowNs)) {
                LOG_INFO(Core_ARM, "{}", *report);
            }
        }
        // Generated code calls a direct branch's target itself, so one call can run a chain of blocks; the budget left
        // afterwards gives the number of blocks that actually ran.
        const u64 lookup_entry_pc = kLookupStats ? impl->ctx.pc : 0;
        const int chain_budget = RecompChainBudget();
        impl->ctx.chain_budget = chain_budget;
        if (first_run) {
            LOG_INFO(Core_ARM, "Calling first signed static block at pc={:#x}", impl->ctx.pc);
        }
        block(&impl->ctx);
        if (early_dispatch < kEarlyDispatchLogLimit) {
            LOG_INFO(Core_ARM, "Early static return #{}: pc={:#x}, svc={}, halted={}",
                     early_dispatch, impl->ctx.pc, impl->ctx.pending_svc, impl->ctx.halted);
        }
        if (first_run) {
            LOG_INFO(Core_ARM, "First signed static block returned: pc={:#x}, svc={}, halted={}",
                     impl->ctx.pc, impl->ctx.pending_svc, impl->ctx.halted);
            first_run = false;
        }
        {
            const int spent = chain_budget - impl->ctx.chain_budget;
            u64 after = seen;
            if (spent > 0) {
                // The first block is already in `seen`; a decrement to zero parks the next PC without entering it.
                after += static_cast<u64>(spent - (impl->ctx.chain_budget == 0 ? 1 : 0));
                t_blocks.slot->n.store(after, std::memory_order_relaxed);
            }
            if (kSamplePc && (seen >> kSamplePcShift) != (after >> kSamplePcShift) &&
                SampleWindowOpen()) {
                g_counters.RecordSample(impl->ctx.pc);
            }
            if (kLookupStats) {
                auto& stats_slot = LookupStatsSlot();
                const DispatchReturn reason =
                    ClassifyReturn(impl->ctx.halted != 0, impl->ctx.pending_svc != kNoPendingSvc,
                                   impl->ctx.chain_budget);
                if (reason == DispatchReturn::Exit) {
                    // A second, diagnostic-only lookup: the unhooked answer for the next pc.
                    const ModuleSnapshot& modules = CurrentModuleSnapshot();
                    RecompLookupStats::CountExit(
                        stats_slot, impl->lookup(impl->ctx.pc) != nullptr,
                        RecompLookupStats::ModuleIndex(modules.bases, lookup_entry_pc),
                        RecompLookupStats::ModuleIndex(modules.bases, impl->ctx.pc));
                    if (kLookupTargets) {
                        RecompExitTargets::Count(ExitTargetsSlot(), impl->ctx.pc);
                    }
                }
                if (RecompLookupStats::CountReturn(stats_slot, reason, after - seen + 1)) {
                    // Same start as the stats window, which opens on the first report call.
                    if (kLookupTargets && !g_exit_targets_rebased.exchange(true)) {
                        g_exit_targets.Rebase();
                    }
                    const ModuleSnapshot& report_modules = CurrentModuleSnapshot();
                    if (const auto report = g_lookup_stats.TakeReport(
                            SteadyNowNs(), kLookupStatsWindowNs, report_modules.names,
                            report_modules.generation)) {
                        LOG_INFO(Core_ARM, "{}", *report);
                        if (kLookupTargets) {
                            LOG_INFO(Core_ARM, "{}",
                                     g_exit_targets.TakeTop([&report_modules](u64 pc) {
                                         return DescribePc(report_modules, pc);
                                     }));
                        }
                    }
                }
            }
        }

        if (impl->ctx.halted == kHaltIcIvau) {
            if (!g_code_guard_ready.load(std::memory_order_acquire)) {
                LOG_CRITICAL(Core_ARM, "recomp: IC IVAU requires guard-v2 host and all guarded modules");
                std::abort();
            }
            const u64 address = impl->ctx.pending_svc;
            impl->ctx.pending_svc = kNoPendingSvc;
            // Forward the guest instruction-cache operation to each CPU interface.
            for (size_t core = 0; core < Hardware::NUM_CPU_CORES; ++core) {
                if (auto* cpu = thread->GetOwnerProcess()->GetArmInterface(core)) cpu->InvalidateCacheRange(address, 64);
            }
            impl->ctx.pc += 4;
            impl->ctx.halted = 0;
            continue;
        }
        if (impl->ctx.halted == kHaltBreakpoint) {
            return HaltReason::InstructionBreakpoint;
        }

        // The block parked its PC on an instruction the decoder cannot translate; the JIT keeps guest state exact, unlike the old
        // step-over-and-zero-x0 that produced plausible nulls and crashed much later.
        if (impl->ctx.halted == kHaltUnhandled) {
            if ((static_cast<u32>(Impl::HostLoad(impl.get(), impl->ctx.pc, 4)) & 0xffffffe0U) == 0xd50b7520U) {
                LOG_CRITICAL(Core_ARM, "recomp: legacy unguarded IC IVAU refused at {:#x}", impl->ctx.pc);
                std::abort();
            }
            impl->ctx.halted = 0;
            // The halt contract cannot carry the encoding, so read it from guest memory (PC is parked on it); this ranks
            // missing opcodes by execution count.
            g_counters.fallback_from_unhandled.fetch_add(1, std::memory_order_relaxed);
            const u32 unsupported_opcode =
                static_cast<u32>(Impl::HostLoad(impl.get(), impl->ctx.pc, 4));
            g_counters.RecordUnhandled(unsupported_opcode);
            RecompGaps::RecordUnimplemented(unsupported_opcode);
            static std::atomic<int> unhandled_count{0};
            if (unhandled_count.fetch_add(1, std::memory_order_relaxed) < 16) {
                LOG_WARNING(Core_ARM, "recomp: unimplemented opcode {:#010x} at {:#x}; checking fallback policy",
                            unsupported_opcode, impl->ctx.pc);
            }
            if (!EnterFallback()) {
                g_counters.no_fallback_available.fetch_add(1, std::memory_order_relaxed);
                RecompGaps::Flush(true, true);
                LOG_CRITICAL(Core_ARM, "recomp: unimplemented opcode {:#010x} at {:#x} and no JIT fallback",
                             unsupported_opcode, impl->ctx.pc);
                return HaltReason::PrefetchAbort;
            }
            return RunFallback(thread);
        }

        if (impl->ctx.pending_svc != kNoPendingSvc) {
            LOG_TRACE(Core_ARM, "recomp SVC {} at pc={:#x} x0={:#x} x1={:#x} x2={:#x} x3={:#x}",
                      impl->ctx.pending_svc, impl->ctx.pc, impl->ctx.x[0], impl->ctx.x[1],
                      impl->ctx.x[2], impl->ctx.x[3]);
            CountSvc(static_cast<u32>(impl->ctx.pending_svc));
            return HaltReason::SupervisorCall;
        }
    }

    return HaltReason::BreakLoop;
}

HaltReason ArmRecomp::StepThread(Kernel::KThread* thread) {
    impl->SampleDiagnostics(thread);
    // A context switch voids any outstanding reservation, or a preempted LDXR/STXR pair could succeed against a word
    // another thread changed and lose a mutex update. Only on a real switch: this backend returns to the host constantly.
    if (impl->last_thread != thread) {
        impl->last_thread = thread;
        if (impl->exclusive_monitor) {
            impl->exclusive_monitor->ClearExclusive(impl->core_index);
        }
    }
    // Block granularity is the finest this backend can step: recompiled blocks
    // are straight-line C with no per-instruction re-entry point.
    if (!impl->lookup) {
        return HaltReason::BreakLoop;
    }
    const RecompBlockFn block = impl->lookup(impl->ctx.pc);
    if (!block) {
        return HaltReason::PrefetchAbort;
    }
    impl->ctx.halted = 0;
    impl->ctx.pending_svc = kNoPendingSvc;
    impl->ctx.chain_budget = 1;
    block(&impl->ctx);
    if (impl->ctx.halted == kHaltIcIvau) {
        if (!g_code_guard_ready.load(std::memory_order_acquire)) {
            LOG_CRITICAL(Core_ARM, "recomp: IC IVAU requires guard-v2 host and all guarded modules");
            std::abort();
        }
        const u64 address = impl->ctx.pending_svc;
        impl->ctx.pending_svc = kNoPendingSvc;
        for (size_t core = 0; core < Hardware::NUM_CPU_CORES; ++core) {
            if (auto* cpu = thread->GetOwnerProcess()->GetArmInterface(core)) cpu->InvalidateCacheRange(address, 64);
        }
        impl->ctx.pc += 4;
        impl->ctx.halted = 0;
        return HaltReason::StepThread;
    }
    if (impl->ctx.halted == kHaltBreakpoint) {
        return HaltReason::InstructionBreakpoint;
    }
    if (impl->ctx.halted == kHaltUnhandled &&
        (static_cast<u32>(Impl::HostLoad(impl.get(), impl->ctx.pc, 4)) & 0xffffffe0U) == 0xd50b7520U) {
        LOG_CRITICAL(Core_ARM, "recomp: legacy unguarded IC IVAU refused while stepping at {:#x}", impl->ctx.pc);
        std::abort();
    }
    if (impl->ctx.pending_svc != kNoPendingSvc) {
        return HaltReason::SupervisorCall;
    }
    return HaltReason::StepThread;
}

void ArmRecomp::ClearInstructionCache() {
#ifndef SUYU_NO_JIT
    if (impl->fallback) impl->fallback->ClearInstructionCache();
#endif
    // ABI 5 blocks verify bytes on every entry; GG1 blocks do once the
    // generation moves.
    RecompGuardGen::OnInvalidateAll();
}

void ArmRecomp::InvalidateCacheRange(u64 addr, std::size_t size) {
#ifndef SUYU_NO_JIT
    if (impl->fallback) impl->fallback->InvalidateCacheRange(addr, size);
#endif
    RecompGuardGen::OnInvalidate(addr, size);
}

void ArmRecomp::GetContext(Kernel::Svc::ThreadContext& ctx) const {
    std::memset(&ctx, 0, sizeof(ctx));
    for (size_t i = 0; i < 29; ++i) {
        ctx.r[i] = impl->ctx.x[i];
    }
    ctx.fp = impl->ctx.x[29];
    ctx.lr = impl->ctx.x[30];
    ctx.sp = impl->ctx.x[31];
    ctx.pc = impl->ctx.pc;
    ctx.pstate = (static_cast<u32>(impl->ctx.n) << 31) |
                 (static_cast<u32>(impl->ctx.z) << 30) |
                 (static_cast<u32>(impl->ctx.c) << 29) |
                 (static_cast<u32>(impl->ctx.v) << 28);
    // u128 here is a pair of 64-bit halves, matching how the generated
    // context stores each vector register.
    for (size_t i = 0; i < 32; ++i) {
        ctx.v[i][0] = impl->ctx.vreg[i][0];
        ctx.v[i][1] = impl->ctx.vreg[i][1];
    }
    ctx.fpcr = static_cast<u32>(impl->ctx.fpcr);
    ctx.fpsr = static_cast<u32>(impl->ctx.fpsr);
    ctx.tpidr = impl->ctx.tpidr_el0;
}

void ArmRecomp::SetContext(const Kernel::Svc::ThreadContext& ctx) {
    for (size_t i = 0; i < 29; ++i) {
        impl->ctx.x[i] = ctx.r[i];
    }
    impl->ctx.x[29] = ctx.fp;
    impl->ctx.x[30] = ctx.lr;
    impl->ctx.x[31] = ctx.sp;
    impl->ctx.pc = ctx.pc;
    impl->ctx.n = static_cast<u8>((ctx.pstate >> 31) & 1);
    impl->ctx.z = static_cast<u8>((ctx.pstate >> 30) & 1);
    impl->ctx.c = static_cast<u8>((ctx.pstate >> 29) & 1);
    impl->ctx.v = static_cast<u8>((ctx.pstate >> 28) & 1);
    for (size_t i = 0; i < 32; ++i) {
        impl->ctx.vreg[i][0] = ctx.v[i][0];
        impl->ctx.vreg[i][1] = ctx.v[i][1];
    }
    // ThreadContext carries the 32-bit guest FPCR; keep the FPX1 kill switch.
    impl->ctx.fpcr = static_cast<u32>(ctx.fpcr) | FpxInhibitBits();
    impl->ctx.fpsr = ctx.fpsr;
    // Only the guest-owned thread pointer travels in ThreadContext; PhysicalCore::LoadContext republishes the read-only
    // one on every switch-in, so writing it here would overwrite the kernel's TLS pointer.
    impl->ctx.tpidr_el0 = ctx.tpidr;
}

void ArmRecomp::SetTpidrroEl0(u64 value) {
    // The emitted MRS handler for TPIDRRO_EL0 reads it from the guest context; writing it into tpidr_el0 destroyed the
    // guest's thread pointer on every context switch.
    impl->ctx.tpidrro_el0 = value;
}

void ArmRecomp::GetSvcArguments(std::span<uint64_t, 8> args) const {
    for (size_t i = 0; i < 8; ++i) {
        args[i] = impl->ctx.x[i];
    }
}

void ArmRecomp::SetSvcArguments(std::span<const uint64_t, 8> args) {
    for (size_t i = 0; i < 8; ++i) {
        impl->ctx.x[i] = args[i];
    }
}

u32 ArmRecomp::GetSvcNumber() const {
    return static_cast<u32>(impl->ctx.pending_svc);
}

void ArmRecomp::SignalInterrupt(Kernel::KThread* thread) {
    impl->interrupted.store(true, std::memory_order_relaxed);
    // While the JIT is running this thread it is the one that has to be woken;
    // the flag above is only read by the recompiled dispatch loop.
#ifndef SUYU_NO_JIT
    if (impl->fallback) {
        impl->fallback->SignalInterrupt(thread);
    }
#else
    (void)thread;
#endif
}

const Kernel::DebugWatchpoint* ArmRecomp::HaltedWatchpoint() const {
    return nullptr;
}

void ArmRecomp::RewindBreakpointInstruction() {
    // No breakpoint patching in statically recompiled code.
}

} // namespace Core
