// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/arm/recomp/recomp_lookup_stats.h"

namespace Core {

// Per-thread copy of the module bases and names, so an exit does not take the module-map lock.
struct RecompModuleSnapshot {
    std::uint32_t generation = ~0U;
    std::vector<std::uint64_t> bases;
    std::vector<std::string> names;
};

// "module+0xoffset" for a pc, or the bare "0xpc" when no module base is at or below it.
inline std::string DescribeRecompPc(const RecompModuleSnapshot& modules, std::uint64_t pc) {
    const std::size_t index = RecompLookupStats::ModuleIndex(modules.bases, pc);
    const bool known = index < modules.bases.size() && index < modules.names.size();
    const std::uint64_t value = known ? pc - modules.bases[index] : pc;
    char hex[2 + 16 + 1];
    std::snprintf(hex, sizeof(hex), "0x%llx", static_cast<unsigned long long>(value));
    return known ? modules.names[index] + "+" + hex : std::string{hex};
}

} // namespace Core
