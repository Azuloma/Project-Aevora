// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The one list of host-bridge callbacks (RecompHostMem fields) that hosted generated
// code must find installed. Hosted code that finds one null silently switches to a
// private per-image fallback (its own exclusive monitor, its own clock), and two images
// would then disagree about the same reservation or time, so the host refuses to run
// instead (ArmRecomp::RunThread). Plain C so the generated-layout test
// (tests/recompiler_smoke/xmod_exec.c) checks the same list against the generated struct.

#define RECOMP_REQUIRED_BRIDGE_CALLBACKS(X)                                                      \
    X(excl_load) X(excl_store) X(clear_excl) X(read_cntpct) X(excl_load_pair) X(excl_store_pair)

#define RECOMP_CHECK_BRIDGE_CALLBACK_(name)                                                      \
    if (!bridge->name) {                                                                         \
        return #name;                                                                            \
    }

/// Defines `static const char* function_name(const BridgeType* bridge)`: the name of the
/// first required callback that is null, or a null pointer when the bridge is complete.
/// Each field name is looked up in BridgeType, so a list entry that the struct does not
/// have fails to compile instead of being skipped.
#define RECOMP_DEFINE_FIRST_MISSING_BRIDGE_CALLBACK(function_name, BridgeType)                   \
    static const char* function_name(const BridgeType* bridge) {                                 \
        RECOMP_REQUIRED_BRIDGE_CALLBACKS(RECOMP_CHECK_BRIDGE_CALLBACK_)                          \
        return (const char*)0;                                                                   \
    }
