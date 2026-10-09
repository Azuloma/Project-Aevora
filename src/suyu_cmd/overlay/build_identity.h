// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace SuyuCmd {

/// What the packaged `aot_manifest.json` says about how this export was produced, for display.
/// The manifest is the single source: nothing here is recomputed or guessed. A field the manifest
/// does not carry (an older export) stays empty so the display can say so instead of showing a
/// default.
struct BuildIdentity {
    bool loaded{};
    std::string effective_backend;
    std::string correctness_revision;
    std::string bundle_id;
    /// Non-negative integers only; a negative or non-integer value is treated as not recorded.
    std::optional<std::uint64_t> image_abi;
    std::optional<std::uint64_t> image_features;
    std::optional<bool> fm_inline;
    std::optional<bool> fp_shared;
    std::optional<bool> direct_chain;
    std::optional<bool> indirect_chain;
    std::optional<bool> fallback_enabled;
    /// Module name and how it is bound ("static" or "dll"), sorted by module name.
    std::vector<std::pair<std::string, std::string>> module_bindings;
};

/// Reads the display fields out of a parsed manifest. Returns false if `manifest` is not an
/// object; missing fields are left empty.
bool ParseBuildIdentity(const nlohmann::json& manifest, BuildIdentity& out);

/// Process-wide copy, set once by the launcher when it reads the manifest and read by the
/// overlay. Thread-safe.
void SetBuildIdentity(BuildIdentity identity);
bool TryGetBuildIdentity(BuildIdentity& out);

} // namespace SuyuCmd
