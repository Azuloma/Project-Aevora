// smoke_env_option_unit: how the on-by-default environment variables (SUYU_AOT_FASTMEM,
// SUYU_AOT_FM_INLINE, SUYU_AOT_DIRECT_CHAIN, SUYU_AOT_INDIRECT_CHAIN, SUYU_DEBUG_OVERLAY) and the
// three-way SUYU_AOT_SPLIT_MAIN are read (common/env_option.h).
#include <cstdio>

#include "common/env_option.h"

using Common::EnvChoice;
using Common::EnvFlagDefaultOn;
using Common::ParseEnvChoice;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

int FlagIsOnUnlessZero() {
    // Unset is read as an empty string.
    CHECK(EnvFlagDefaultOn(""));
    CHECK(EnvFlagDefaultOn("1"));
    CHECK(!EnvFlagDefaultOn("0"));
    // Only the exact text "0" switches an option off: a stray value must not do it silently.
    CHECK(EnvFlagDefaultOn("00"));
    CHECK(EnvFlagDefaultOn("false"));
    CHECK(EnvFlagDefaultOn("off"));
    CHECK(EnvFlagDefaultOn(" 0"));
    CHECK(EnvFlagDefaultOn("0 "));
    CHECK(EnvFlagDefaultOn("yes"));
    return 0;
}

int ChoiceDistinguishesDefaultFromRequest() {
    CHECK(ParseEnvChoice("") == EnvChoice::Default);
    CHECK(ParseEnvChoice("0") == EnvChoice::Off);
    CHECK(ParseEnvChoice("1") == EnvChoice::On);
    // Anything else is an error for the caller to report, not a quiet default.
    CHECK(ParseEnvChoice("2") == EnvChoice::Invalid);
    CHECK(ParseEnvChoice("true") == EnvChoice::Invalid);
    CHECK(ParseEnvChoice("bogus") == EnvChoice::Invalid);
    CHECK(ParseEnvChoice("01") == EnvChoice::Invalid);
    CHECK(ParseEnvChoice(" 1") == EnvChoice::Invalid);
    return 0;
}

} // namespace

int main() {
    CHECK(FlagIsOnUnlessZero() == 0);
    CHECK(ChoiceDistinguishesDefaultFromRequest() == 0);
    std::puts("All synthetic ENV OPTION smoke checks passed.");
    return 0;
}
