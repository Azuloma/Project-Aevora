// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/arm/exclusive_monitor.h"
#include "core/arm/native_exclusive_state.h"

namespace Core::Memory {
class Memory;
}

namespace Core {

/// The exclusive monitor without dynarmic, with dynarmic's semantics and fast paths (see
/// NativeExclusiveState). SUYU_EXCL_LOCKFREE_MARK=0 puts reservation marks back under the lock.
class StandaloneExclusiveMonitor final : public ExclusiveMonitor {
public:
    explicit StandaloneExclusiveMonitor(Memory::Memory& memory_, std::size_t core_count_);
    ~StandaloneExclusiveMonitor() override;

    u8 ExclusiveRead8(std::size_t core_index, VAddr addr) override;
    u16 ExclusiveRead16(std::size_t core_index, VAddr addr) override;
    u32 ExclusiveRead32(std::size_t core_index, VAddr addr) override;
    u64 ExclusiveRead64(std::size_t core_index, VAddr addr) override;
    u128 ExclusiveRead128(std::size_t core_index, VAddr addr) override;
    void ClearExclusive(std::size_t core_index) override;

    bool ExclusiveWrite8(std::size_t core_index, VAddr vaddr, u8 value) override;
    bool ExclusiveWrite16(std::size_t core_index, VAddr vaddr, u16 value) override;
    bool ExclusiveWrite32(std::size_t core_index, VAddr vaddr, u32 value) override;
    bool ExclusiveWrite64(std::size_t core_index, VAddr vaddr, u64 value) override;
    bool ExclusiveWrite128(std::size_t core_index, VAddr vaddr, u128 value) override;

private:
    template <typename T>
    T ExclusiveRead(std::size_t core_index, VAddr addr);

    template <typename T>
    bool ExclusiveWrite(std::size_t core_index, VAddr vaddr, T value);

    NativeExclusiveState state;
    Memory::Memory& memory;
};

} // namespace Core
