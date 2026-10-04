// SPDX-FileCopyrightText: Copyright 2023 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The VMA implementation TU. It is compiled into each executable, not into video_core,
// because a static Qt already contains a VMA implementation.
#define VMA_IMPLEMENTATION

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wconversion"
#endif

#include "video_core/vulkan_common/vma.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
