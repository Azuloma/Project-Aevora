// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Core {

// Diagnostic helpers for RecompAccessSites; Windows only, elsewhere they report nothing.

// Stores `count` return addresses of the current stack in `out`, starting at the one equal to
// `anchor` (the caller's own return address); zero-fills what is missing.
void CaptureCallerFrames(std::uintptr_t anchor, std::uintptr_t* out, std::size_t count);

// "module+0xrva" for an address inside a loaded module, or the bare address.
std::string DescribeHostAddress(std::uintptr_t address);

} // namespace Core
