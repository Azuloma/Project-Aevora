// smoke_env_option_unit: default-on flags, the three-way choice and bounded integer options of
// common/env_option.h.
#include <cstdio>
#include <optional>
#include <string_view>

#include "common/env_option.h"

using Common::EnvChoice;
using Common::EnvFlagDefaultOff;
using Common::EnvFlagDefaultOn;
using Common::ParseEnvChoice;
using Common::ParseEnvIntInRange;

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

int FlagIsOffUnlessOne() {
    CHECK(!EnvFlagDefaultOff(""));
    CHECK(EnvFlagDefaultOff("1"));
    CHECK(!EnvFlagDefaultOff("0"));
    CHECK(!EnvFlagDefaultOff("true"));
    CHECK(!EnvFlagDefaultOff("11"));
    CHECK(!EnvFlagDefaultOff(" 1"));
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

// Negative control: accepts any leading digits, so "4ms" or "-1" would pass as numbers.
std::optional<int> LooseInt(std::string_view value) {
    return value.empty() ? std::nullopt : std::optional<int>{value[0] - '0'};
}

int IntInRangeRejectsOddText() {
    CHECK(ParseEnvIntInRange("1", 1, 8) == 1);
    CHECK(ParseEnvIntInRange("8", 1, 8) == 8);
    CHECK(ParseEnvIntInRange("4", 1, 8) == 4);
    CHECK(!ParseEnvIntInRange("", 1, 8));
    CHECK(!ParseEnvIntInRange("0", 1, 8));
    CHECK(!ParseEnvIntInRange("9", 1, 8));
    CHECK(!ParseEnvIntInRange("-1", 1, 8));
    CHECK(!ParseEnvIntInRange("+4", 1, 8));
    CHECK(!ParseEnvIntInRange("4ms", 1, 8));
    CHECK(!ParseEnvIntInRange(" 4", 1, 8));
    CHECK(!ParseEnvIntInRange("1234567890", 1, 8));
    CHECK(LooseInt("4ms") == 4 && !ParseEnvIntInRange("4ms", 1, 8));
    return 0;
}

} // namespace

int main() {
    CHECK(IntInRangeRejectsOddText() == 0);
    CHECK(FlagIsOnUnlessZero() == 0);
    CHECK(FlagIsOffUnlessOne() == 0);
    CHECK(ChoiceDistinguishesDefaultFromRequest() == 0);
    std::puts("All synthetic ENV OPTION smoke checks passed.");
    return 0;
}
