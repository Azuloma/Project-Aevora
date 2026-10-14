// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/arm/recomp/recomp_host_frames.h"

#include <algorithm>
#include <array>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Core {

void CaptureCallerFrames(std::uintptr_t anchor, std::uintptr_t* out, std::size_t count) {
    std::fill(out, out + count, std::uintptr_t{0});
#ifdef _WIN32
    constexpr std::size_t kMaxCaptured = 16;
    std::array<void*, kMaxCaptured> frames{};
    const USHORT captured = RtlCaptureStackBackTrace(1, kMaxCaptured, frames.data(), nullptr);
    // Start at the frame equal to the anchor; without a match, at the first captured frame.
    std::size_t first = 0;
    for (std::size_t i = 0; i < captured; ++i) {
        if (reinterpret_cast<std::uintptr_t>(frames[i]) == anchor) {
            first = i;
            break;
        }
    }
    for (std::size_t i = 0; i < count && first + i < captured; ++i) {
        out[i] = reinterpret_cast<std::uintptr_t>(frames[first + i]);
    }
#else
    (void)anchor;
#endif
}

std::string DescribeHostAddress(std::uintptr_t address) {
    char text[64];
#ifdef _WIN32
    HMODULE module = nullptr;
    if (address != 0 &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module)) {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = path;
        for (DWORD i = 0; i < length; ++i) {
            if (path[i] == L'\\' || path[i] == L'/') {
                name = path + i + 1;
            }
        }
        std::string narrow;
        for (const wchar_t* c = name; *c != 0 && c < path + length; ++c) {
            narrow.push_back(*c < 0x80 ? static_cast<char>(*c) : '?');
        }
        std::snprintf(text, sizeof text, "+0x%llx",
                      static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
        return narrow + text;
    }
#endif
    std::snprintf(text, sizeof text, "0x%llx", static_cast<unsigned long long>(address));
    return text;
}

} // namespace Core
