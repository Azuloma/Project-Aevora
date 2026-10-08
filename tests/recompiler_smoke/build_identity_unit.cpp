// smoke_build_identity_unit: the display fields the debug overlay reads out of a packaged
// aot_manifest.json (suyu_cmd/overlay/build_identity.cpp): what is taken, what is refused, and
// that a missing or malformed field reads as "not recorded" rather than a default.
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "suyu_cmd/overlay/build_identity.h"

using SuyuCmd::BuildIdentity;
using json = nlohmann::json;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

BuildIdentity Parse(const json& manifest, bool* ok = nullptr) {
    BuildIdentity identity;
    const bool parsed = SuyuCmd::ParseBuildIdentity(manifest, identity);
    if (ok != nullptr) {
        *ok = parsed;
    }
    return identity;
}

int NothingIsStoredBeforeASet() {
    // First on purpose: the store is process-wide.
    BuildIdentity out;
    CHECK(!SuyuCmd::TryGetBuildIdentity(out));
    return 0;
}

int FullManifestIsRead() {
    const json manifest = json::parse(R"({
        "effective_backend": "suyu-static",
        "correctness_revision": "20260927-hybrid-guard-control-units-v3",
        "bundle_id": "28da18c4a6c2",
        "image_abi": 6,
        "image_features": 7,
        "fm_inline": true,
        "direct_chain": false,
        "indirect_chain": true,
        "fallback_enabled": false,
        "module_bindings": {"rtld": "static", "main": "dll", "sdk": "static"}
    })");
    bool ok = false;
    const BuildIdentity identity = Parse(manifest, &ok);
    CHECK(ok && identity.loaded);
    CHECK(identity.effective_backend == "suyu-static");
    CHECK(identity.correctness_revision == "20260927-hybrid-guard-control-units-v3");
    CHECK(identity.bundle_id == "28da18c4a6c2");
    CHECK(identity.image_abi && *identity.image_abi == 6);
    CHECK(identity.image_features && *identity.image_features == 7);
    CHECK(identity.fm_inline && *identity.fm_inline);
    CHECK(identity.direct_chain && !*identity.direct_chain);
    CHECK(identity.indirect_chain && *identity.indirect_chain);
    CHECK(identity.fallback_enabled && !*identity.fallback_enabled);
    // Sorted by module name, whatever order the file used.
    CHECK(identity.module_bindings.size() == 3);
    CHECK(identity.module_bindings[0].first == "main" && identity.module_bindings[0].second == "dll");
    CHECK(identity.module_bindings[1].first == "rtld");
    CHECK(identity.module_bindings[2].first == "sdk");
    return 0;
}

int MissingFieldsStayUnrecorded() {
    bool ok = false;
    const BuildIdentity identity = Parse(json::parse("{}"), &ok);
    CHECK(ok && identity.loaded);
    CHECK(identity.effective_backend.empty() && identity.correctness_revision.empty() &&
          identity.bundle_id.empty());
    CHECK(!identity.image_abi && !identity.image_features);
    CHECK(!identity.fm_inline && !identity.direct_chain && !identity.indirect_chain &&
          !identity.fallback_enabled);
    CHECK(identity.module_bindings.empty());

    // An older export that predates one flag: that flag alone is unrecorded, not "off".
    const BuildIdentity partial =
        Parse(json::parse(R"({"direct_chain": false, "image_abi": 6})"));
    CHECK(partial.direct_chain && !*partial.direct_chain);
    CHECK(!partial.indirect_chain);
    return 0;
}

int WrongTypesAreRefused() {
    const BuildIdentity identity = Parse(json::parse(R"({
        "effective_backend": 5,
        "bundle_id": ["x"],
        "image_abi": "6",
        "image_features": 1.5,
        "fm_inline": "true",
        "direct_chain": 1,
        "fallback_enabled": null,
        "module_bindings": ["main"]
    })"));
    CHECK(identity.loaded);
    CHECK(identity.effective_backend.empty() && identity.bundle_id.empty());
    CHECK(!identity.image_abi);
    CHECK(!identity.image_features);
    CHECK(!identity.fm_inline && !identity.direct_chain && !identity.fallback_enabled);
    CHECK(identity.module_bindings.empty());

    // A binding that is not a string keeps its module but records nothing for the binding.
    const BuildIdentity bindings =
        Parse(json::parse(R"({"module_bindings": {"main": null, "sdk": "static"}})"));
    CHECK(bindings.module_bindings.size() == 2);
    CHECK(bindings.module_bindings[0].first == "main" && bindings.module_bindings[0].second.empty());
    CHECK(bindings.module_bindings[1].second == "static");
    return 0;
}

int IntegersAreNotWrapped() {
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    BuildIdentity identity = Parse(json::parse("{\"image_abi\": 18446744073709551615, "
                                               "\"image_features\": 9223372036854775807}"));
    CHECK(identity.image_abi && *identity.image_abi == kMax);
    CHECK(identity.image_features && *identity.image_features == 9223372036854775807ull);

    // Negative values and ones too large for 64 bits are not recorded rather than wrapped.
    identity = Parse(json::parse("{\"image_abi\": -1, \"image_features\": -9223372036854775808}"));
    CHECK(!identity.image_abi && !identity.image_features);
    identity = Parse(json::parse("{\"image_abi\": 18446744073709551616}"));
    CHECK(!identity.image_abi);
    return 0;
}

int TextIsKeptVerbatim() {
    // Display text goes through a format argument, so '%' must survive as data, and UTF-8 is
    // carried byte for byte.
    const BuildIdentity identity = Parse(json::parse(
        "{\"bundle_id\": \"100%s%n\", \"effective_backend\": \"\xE3\x83\x9D\xE3\x82\xB1\"}"));
    CHECK(identity.bundle_id == "100%s%n");
    CHECK(identity.effective_backend == "\xE3\x83\x9D\xE3\x82\xB1");
    return 0;
}

int OnlyAnObjectIsAManifest() {
    BuildIdentity identity;
    identity.effective_backend = "untouched";
    CHECK(!SuyuCmd::ParseBuildIdentity(json::array(), identity));
    CHECK(!SuyuCmd::ParseBuildIdentity(json(nullptr), identity));
    CHECK(!SuyuCmd::ParseBuildIdentity(json("text"), identity));
    // A refusal leaves the caller's value alone.
    CHECK(identity.effective_backend == "untouched" && !identity.loaded);
    return 0;
}

int StoreRoundTrips() {
    SuyuCmd::SetBuildIdentity(Parse(json::parse(R"({"bundle_id": "abc", "image_abi": 6})")));
    BuildIdentity out;
    CHECK(SuyuCmd::TryGetBuildIdentity(out));
    CHECK(out.bundle_id == "abc" && out.image_abi && *out.image_abi == 6);

    // A copy that was never parsed is not "loaded" and is not served.
    SuyuCmd::SetBuildIdentity(BuildIdentity{});
    CHECK(!SuyuCmd::TryGetBuildIdentity(out));
    return 0;
}

} // namespace

int main() {
    CHECK(NothingIsStoredBeforeASet() == 0);
    CHECK(FullManifestIsRead() == 0);
    CHECK(MissingFieldsStayUnrecorded() == 0);
    CHECK(WrongTypesAreRefused() == 0);
    CHECK(IntegersAreNotWrapped() == 0);
    CHECK(TextIsKeptVerbatim() == 0);
    CHECK(OnlyAnObjectIsAManifest() == 0);
    CHECK(StoreRoundTrips() == 0);
    std::puts("All synthetic BUILD IDENTITY smoke checks passed.");
    return 0;
}
