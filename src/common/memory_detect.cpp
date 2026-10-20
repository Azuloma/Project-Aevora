// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <sysinfoapi.h>
// clang-format on
#else
#include <sys/types.h>
#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#else
#include <unistd.h>
#endif
#endif

#include "common/memory_detect.h"

namespace Common {

// Detects the RAM and Swapfile sizes
static MemoryInfo Detect() {
    MemoryInfo mem_info{};

#ifdef _WIN32
    MEMORYSTATUSEX memorystatus;
    memorystatus.dwLength = sizeof(memorystatus);
    GlobalMemoryStatusEx(&memorystatus);
    mem_info.TotalPhysicalMemory = memorystatus.ullTotalPhys;
    mem_info.TotalSwapMemory = memorystatus.ullTotalPageFile - mem_info.TotalPhysicalMemory;
#elif defined(__FreeBSD__)
    u_long physmem, swap_total;
    std::size_t sizeof_u_long = sizeof(u_long);
    // sysctlbyname(const char *, void *, size_t *, const void *, size_t);
    sysctlbyname("hw.physmem", &physmem, &sizeof_u_long, nullptr, 0);
    sysctlbyname("vm.swap_total", &swap_total, &sizeof_u_long, nullptr, 0);
    mem_info.TotalPhysicalMemory = physmem;
    mem_info.TotalSwapMemory = swap_total;
#elif defined(__linux__)
    struct sysinfo meminfo;
    sysinfo(&meminfo);
    mem_info.TotalPhysicalMemory = meminfo.totalram;
    mem_info.TotalSwapMemory = meminfo.totalswap;
#else
    mem_info.TotalPhysicalMemory = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE);
    mem_info.TotalSwapMemory = 0;
#endif

    return mem_info;
}

const MemoryInfo& GetMemInfo() {
    static MemoryInfo mem_info = Detect();
    return mem_info;
}

} // namespace Common
