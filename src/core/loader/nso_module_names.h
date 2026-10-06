// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The NSO modules of a deconstructed ExeFS, in load order. This is the one definition of
// which files the loader treats as program modules: the loader probes exactly these names,
// and the recompiler's exporter and module topology take the expected module set and its
// order from here, so the three cannot drift apart.
//
// Header-only, standard library only: it is shared by core, the exporter and tests.

#pragma once

#include <array>

namespace Loader {

// rtld, main, subsdk0..subsdk9, sdk. The order is the load order and, for a recompiled
// export, the index order of the module registry.
inline constexpr std::array<const char*, 13> kNsoModuleNames = {
    "rtld",    "main",    "subsdk0", "subsdk1", "subsdk2", "subsdk3", "subsdk4",
    "subsdk5", "subsdk6", "subsdk7", "subsdk8", "subsdk9", "sdk"};

} // namespace Loader
