// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <mutex>
#include <string>

#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/debugger/debugger.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/physical_core.h"
#include "core/hle/kernel/svc.h"

namespace Kernel {

namespace {

// Diagnostic, enabled by SUYU_FPCR_STATS (any non-empty value other than "0"):
// tallies the guest FPCR of application threads at every context save, i.e.
// at each thread switch. It answers whether the guest runs with FPCR bits
// that close the recompiler's FPX1 native-FP gate (FZ, DN, RMode, ...).
// The value is the guest's own register, so the JIT and recompiled backends
// report the same thing. FPSR.IXC is tallied too, because the gate also needs
// it set; JIT backends may not track FPSR exactly. Counts are process-wide and
// cumulative, so compare runs in fresh suyu/suyu-cmd processes. Enabling it adds
// a lock per context save; do not use FPS from such runs as a measurement.
class FpcrStats {
public:
    static FpcrStats& Instance() {
        static FpcrStats stats;
        return stats;
    }

    bool Enabled() const {
        return m_enabled;
    }

    void Record(u64 thread_id, u32 fpcr, u32 fpsr) {
        constexpr u32 kFpsrInexactBit = 1u << 4;
        constexpr u64 kSummaryIntervalSaves = 1ull << 16;

        std::scoped_lock lk{m_lock};
        ++m_total_saves;
        Entry* entry = Find(fpcr);
        if (entry == nullptr) {
            if (m_entry_count == m_entries.size()) {
                if (m_untracked_saves++ == 0) {
                    LOG_WARNING(Core_ARM,
                                "FPCR stats: more than {} distinct FPCR values; 0x{:08X} and "
                                "later new values are counted only as untracked",
                                kMaxTrackedValues, fpcr);
                }
            } else {
                entry = &m_entries[m_entry_count++];
                entry->fpcr = fpcr;
                LOG_INFO(Core_ARM, "FPCR stats: new guest FPCR 0x{:08X} ({}) first seen on thread {}",
                         fpcr, Describe(fpcr), thread_id);
            }
        }
        if (entry != nullptr) {
            ++entry->saves;
            entry->inexact_set_saves += (fpsr & kFpsrInexactBit) != 0;
        }
        if (m_total_saves % kSummaryIntervalSaves == 0) {
            LogSummary();
        }
    }

private:
    // Guests normally switch between a handful of FPCR values; the bound keeps
    // the per-save lookup short. Overflow is reported, not silently dropped.
    static constexpr std::size_t kMaxTrackedValues = 16;

    struct Entry {
        u32 fpcr = 0;
        u64 saves = 0;
        u64 inexact_set_saves = 0;
    };

    FpcrStats() {
        const char* value = std::getenv("SUYU_FPCR_STATS");
        m_enabled = value != nullptr && *value != '\0' && !(value[0] == '0' && value[1] == '\0');
        if (m_enabled) {
            LOG_INFO(Core_ARM, "FPCR stats: enabled (SUYU_FPCR_STATS)");
        }
    }

    Entry* Find(u32 fpcr) {
        for (std::size_t i = 0; i < m_entry_count; ++i) {
            if (m_entries[i].fpcr == fpcr) {
                return &m_entries[i];
            }
        }
        return nullptr;
    }

    static std::string Describe(u32 fpcr) {
        static constexpr std::array<const char*, 4> kRoundingModes{"RN", "RP", "RM", "RZ"};
        return fmt::format("RMode={} FZ={} DN={} AHP={} FZ16={} traps=0x{:X}",
                           kRoundingModes[(fpcr >> 22) & 3], (fpcr >> 24) & 1, (fpcr >> 25) & 1,
                           (fpcr >> 26) & 1, (fpcr >> 19) & 1, fpcr & 0x9F00);
    }

    void LogSummary() const {
        LOG_INFO(Core_ARM,
                 "FPCR stats (process-wide): {} context saves, {} tracked values, {} untracked saves",
                 m_total_saves, m_entry_count, m_untracked_saves);
        for (std::size_t i = 0; i < m_entry_count; ++i) {
            const Entry& entry = m_entries[i];
            LOG_INFO(Core_ARM, "FPCR stats:   0x{:08X} ({}): {} saves ({:.1f}%), FPSR.IXC set in {}",
                     entry.fpcr, Describe(entry.fpcr), entry.saves,
                     100.0 * static_cast<double>(entry.saves) / static_cast<double>(m_total_saves),
                     entry.inexact_set_saves);
        }
    }

    bool m_enabled = false;
    std::mutex m_lock;
    std::array<Entry, kMaxTrackedValues> m_entries{};
    std::size_t m_entry_count = 0;
    u64 m_total_saves = 0;
    u64 m_untracked_saves = 0;
};

// SUYU_GUEST_TIME=1 logs each core's run/SVC/other split; off unless exactly "1" (two clock reads per slice).
const bool kGuestTime = [] {
    const char* e = std::getenv("SUYU_GUEST_TIME");
    return e && std::string_view{e} == "1";
}();
constexpr std::uint64_t kGuestTimeWindowNs = 10'000'000'000ULL;
constexpr std::size_t kGuestTimeTopCount = 6;

std::uint64_t SteadyNowNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

PhysicalCore::PhysicalCore(KernelCore& kernel, std::size_t core_index)
    : m_core_index{core_index}
{
    m_is_single_core = !kernel.IsMulticore();
}
PhysicalCore::~PhysicalCore() = default;

void PhysicalCore::RunThread(KernelCore& kernel, Kernel::KThread* thread) {
    auto* process = thread->GetOwnerProcess();
    auto& system = kernel.System();
    auto* interface = process->GetArmInterface(m_core_index);
    if (!interface) {
        LOG_CRITICAL(Kernel,
                     "Cannot run thread {} for process '{}' on core {} without a CPU interface",
                     thread->GetThreadId(), process->GetName(), m_core_index);
        return;
    }

    {
        static bool announced = false;
        if (!announced) {
            announced = true;
            LOG_INFO(Kernel, "PhysicalCore::RunThread reached, core={}", m_core_index);
        }
    }

    interface->Initialize();

    const auto EnterContext = [&]() {
        // Lock the core context.
        std::scoped_lock lk{m_guard};

        // Check if we are already interrupted. If we are, we can just stop immediately.
        if (m_is_interrupted) {
            return false;
        }

        // Mark that we are running.
        m_arm_interface = interface;
        m_current_thread = thread;

        // Acquire the lock on the thread parameters.
        // This allows us to force synchronization with Interrupt.
        interface->LockThread(thread);

        return true;
    };

    const auto ExitContext = [&]() {
        // Save the JIT context back to the thread so the debugger can
        // read the current register state. Debug halt paths (step,
        // breakpoint, watchpoint) return from RunThread without going
        // through the scheduler's Unload/SaveContext.
        if (system.DebuggerEnabled()) {
            interface->GetContext(thread->GetContext());
        }

        // Unlock the thread.
        interface->UnlockThread(thread);

        // Lock the core context.
        std::scoped_lock lk{m_guard};

        // On exit, we no longer are running.
        m_arm_interface = nullptr;
        m_current_thread = nullptr;
    };

    while (true) {
        // If the thread is scheduled for termination, exit.
        if (thread->HasDpc() && thread->IsTerminationRequested()) {
            thread->Exit(kernel);
        }

        // Notify the debugger and go to sleep if a step was performed
        // and this thread has been scheduled again.
        if (thread->GetStepState() == StepState::StepPerformed) {
            system.GetDebugger().NotifyThreadStopped(thread);
            thread->RequestSuspend(kernel, SuspendType::Debug);
            return;
        }

        // Otherwise, run the thread.
        Core::HaltReason hr{};
        bool guest_time_run = false;
        std::uint64_t run_end_ns = 0;
        {
            // If we were interrupted, exit immediately.
            if (!EnterContext()) {
                return;
            }

            if (thread->GetStepState() == StepState::StepPending) {
                hr = interface->StepThread(thread);

                if (True(hr & Core::HaltReason::StepThread)) {
                    thread->SetStepState(StepState::StepPerformed);
                }
            } else {
                if (kGuestTime) {
                    const std::uint64_t run_start_ns = SteadyNowNs();
                    m_guest_time.BeginRun(thread->GetThreadId(), run_start_ns);
                    if (const auto report = m_guest_time.TakeReport(
                            run_start_ns, kGuestTimeWindowNs, m_core_index, kGuestTimeTopCount)) {
                        LOG_INFO(Kernel, "{}", *report);
                    }
                    guest_time_run = true;
                }
                hr = interface->RunThread(thread);
                if (guest_time_run) {
                    run_end_ns = SteadyNowNs();
                }
            }

            ExitContext();
        }

        // Determine why we stopped.
        // If a step completed successfully, skip other halt reason handlers
        // the step takes priority (e.g. step may also set InstructionBreakpoint
        // if the next instruction happens to be a breakpoint).
        const bool step_completed = True(hr & Core::HaltReason::StepThread)
                                    && thread->GetStepState() == StepState::StepPerformed;
        const bool supervisor_call = !step_completed && True(hr & Core::HaltReason::SupervisorCall);
        const bool prefetch_abort = !step_completed && True(hr & Core::HaltReason::PrefetchAbort);
        const bool breakpoint = !step_completed && True(hr & Core::HaltReason::InstructionBreakpoint);
        const bool data_abort = !step_completed && True(hr & Core::HaltReason::DataAbort);
        const bool interrupt = !step_completed && True(hr & Core::HaltReason::BreakLoop);

        if (guest_time_run) {
            m_guest_time.EndRun(run_end_ns, supervisor_call
                                                ? std::optional<u32>{interface->GetSvcNumber()}
                                                : std::nullopt);
        }

        // Since scheduling may occur here, we cannot use any cached
        // state after returning from calls we make.

        // Notify the debugger and go to sleep if a breakpoint was hit,
        // or if the thread is unable to continue for any reason.
        if (breakpoint || prefetch_abort) {
            if (breakpoint) {
                interface->RewindBreakpointInstruction();
                // RewindBreakpointInstruction sets the JIT state to the
                // saved breakpoint context. Update the thread context to
                // match, since ExitContext already saved the post-execution
                // state.
                interface->GetContext(thread->GetContext());
            }
            if (system.DebuggerEnabled()) {
                system.GetDebugger().NotifyThreadStopped(thread);
            } else {
                interface->LogBacktrace(process);
            }
            thread->RequestSuspend(kernel, SuspendType::Debug);
            return;
        }

        // Notify the debugger and go to sleep on data abort.
        if (data_abort) {
            if (system.DebuggerEnabled()) {
                system.GetDebugger().NotifyThreadWatchpoint(thread, *interface->HaltedWatchpoint());
            }
            thread->RequestSuspend(kernel, SuspendType::Debug);
            return;
        }

        // Handle system calls.
        if (supervisor_call) {
            Svc::Call(system, interface->GetSvcNumber());
            return;
        }

        // Handle external interrupt sources.
        if (interrupt || m_is_single_core) {
            return;
        }
    }
}

void PhysicalCore::LoadContext(const KThread* thread) {
    auto* const process = thread->GetOwnerProcess();
    if (!process) {
        // Kernel threads do not run on emulated CPU cores.
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->SetContext(thread->GetContext());
        interface->SetTpidrroEl0(GetInteger(thread->GetTlsAddress()));
        interface->SetWatchpointArray(&process->GetWatchpoints());
    }
}

void PhysicalCore::LoadSvcArguments(const KProcess& process, std::span<const uint64_t, 8> args) {
    process.GetArmInterface(m_core_index)->SetSvcArguments(args);
}

void PhysicalCore::SaveContext(KThread* thread) const {
    auto* const process = thread->GetOwnerProcess();
    if (!process) {
        // Kernel threads do not run on emulated CPU cores.
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->GetContext(thread->GetContext());
        auto& fpcr_stats = FpcrStats::Instance();
        if (fpcr_stats.Enabled() && process->IsApplication()) {
            const auto& context = thread->GetContext();
            fpcr_stats.Record(thread->GetThreadId(), context.fpcr, context.fpsr);
        }
    }
}

void PhysicalCore::SaveSvcArguments(KProcess& process, std::span<uint64_t, 8> args) const {
    process.GetArmInterface(m_core_index)->GetSvcArguments(args);
}

void PhysicalCore::CloneFpuStatus(KThread* dst) const {
    auto* process = dst->GetOwnerProcess();

    Svc::ThreadContext ctx{};
    process->GetArmInterface(m_core_index)->GetContext(ctx);

    dst->GetContext().fpcr = ctx.fpcr;
    dst->GetContext().fpsr = ctx.fpsr;
}

void PhysicalCore::LogBacktrace(KernelCore& kernel) {
    auto* process = GetCurrentProcessPointer(kernel);
    if (!process) {
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->LogBacktrace(process);
    }
}

void PhysicalCore::Idle() {
    const std::uint64_t idle_start_ns = kGuestTime ? SteadyNowNs() : 0;
    std::unique_lock lk{m_guard};
    m_on_interrupt.wait(lk, [this] { return m_is_interrupted; });
    if (kGuestTime) {
        m_guest_time.AddIdle(SteadyNowNs() - idle_start_ns);
    }
}

bool PhysicalCore::IsInterrupted() const {
    return m_is_interrupted;
}

void PhysicalCore::Interrupt() {
    // Lock core context.
    std::scoped_lock lk{m_guard};

    // Load members.
    auto* arm_interface = m_arm_interface;
    auto* thread = m_current_thread;

    // Add interrupt flag.
    m_is_interrupted = true;

    // Interrupt ourselves.
    m_on_interrupt.notify_one();

    // If there is no thread running, we are done.
    if (arm_interface == nullptr) {
        return;
    }

    // Interrupt the CPU.
    arm_interface->SignalInterrupt(thread);
}

void PhysicalCore::ClearInterrupt() {
    std::scoped_lock lk{m_guard};
    m_is_interrupted = false;
}

} // namespace Kernel
