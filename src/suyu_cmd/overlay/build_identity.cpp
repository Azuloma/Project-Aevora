// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <mutex>

#include <nlohmann/json.hpp>

#include "suyu_cmd/overlay/build_identity.h"

namespace SuyuCmd {
namespace {

std::mutex identity_mutex;
BuildIdentity current_identity;

std::string ReadString(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

// is_number_unsigned() is true for any non-negative integer that fits 64 bits, so a negative
// value or one that only parses as floating point is rejected rather than wrapped.
std::optional<std::uint64_t> ReadUnsigned(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return std::nullopt;
    }
    return it->get<std::uint64_t>();
}

std::optional<bool> ReadBool(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return std::nullopt;
    }
    return it->get<bool>();
}

} // namespace

bool ParseBuildIdentity(const nlohmann::json& manifest, BuildIdentity& out) {
    if (!manifest.is_object()) {
        return false;
    }
    BuildIdentity identity;
    identity.effective_backend = ReadString(manifest, "effective_backend");
    identity.correctness_revision = ReadString(manifest, "correctness_revision");
    identity.bundle_id = ReadString(manifest, "bundle_id");
    identity.image_abi = ReadUnsigned(manifest, "image_abi");
    identity.image_features = ReadUnsigned(manifest, "image_features");
    identity.fm_inline = ReadBool(manifest, "fm_inline");
    identity.fp_shared = ReadBool(manifest, "fp_shared");
    identity.direct_chain = ReadBool(manifest, "direct_chain");
    identity.indirect_chain = ReadBool(manifest, "indirect_chain");
    identity.fallback_enabled = ReadBool(manifest, "fallback_enabled");

    const auto bindings = manifest.find("module_bindings");
    if (bindings != manifest.end() && bindings->is_object()) {
        for (const auto& [module, binding] : bindings->items()) {
            identity.module_bindings.emplace_back(
                module, binding.is_string() ? binding.get<std::string>() : std::string{});
        }
    }
    identity.loaded = true;
    out = std::move(identity);
    return true;
}

void SetBuildIdentity(BuildIdentity identity) {
    std::scoped_lock lock{identity_mutex};
    current_identity = std::move(identity);
}

bool TryGetBuildIdentity(BuildIdentity& out) {
    std::scoped_lock lock{identity_mutex};
    if (!current_identity.loaded) {
        return false;
    }
    out = current_identity;
    return true;
}

} // namespace SuyuCmd
