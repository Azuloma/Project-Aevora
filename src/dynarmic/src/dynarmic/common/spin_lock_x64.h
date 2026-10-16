// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include "dynarmic/backend/x64/xbyak.h"

namespace Dynarmic {

// ttas spins on a plain load between exchanges; it has no effect together with waitpkg.
void EmitSpinLockLock(Xbyak::CodeGenerator& code, Xbyak::Reg64 ptr, Xbyak::Reg32 tmp, bool waitpkg,
                      bool ttas = false);
void EmitSpinLockUnlock(Xbyak::CodeGenerator& code, Xbyak::Reg64 ptr, Xbyak::Reg32 tmp);

// True when the shared SpinLock stub was emitted with SUYU_SPINLOCK_TTAS=1.
bool SpinLockUsesTtas() noexcept;

}  // namespace Dynarmic
