// smoke_fullscreen_toggle_unit: which key chords switch fullscreen (suyu_cmd/fullscreen_toggle.h).
#include <cstdio>

#include "suyu_cmd/fullscreen_toggle.h"

using namespace SuyuCmd;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr FullscreenKeys kKeys{68, 40, 88};
constexpr std::uint32_t kOther = 4;

// Negative control: Enter toggles with or without Alt.
constexpr bool IgnoresAlt(std::uint32_t scancode, bool, const FullscreenKeys& keys) {
    return scancode == keys.toggle || scancode == keys.enter || scancode == keys.keypad_enter;
}

// The chord rules, shared by the real function and the control. False on a breach.
template <class Classify>
bool ChordRulesHold(Classify classify) {
    return classify(kKeys.toggle, false, kKeys) && classify(kKeys.toggle, true, kKeys) &&
           classify(kKeys.enter, true, kKeys) && classify(kKeys.keypad_enter, true, kKeys) &&
           // A plain Enter belongs to the game.
           !classify(kKeys.enter, false, kKeys) && !classify(kKeys.keypad_enter, false, kKeys) &&
           !classify(kOther, true, kKeys) && !classify(kOther, false, kKeys);
}

} // namespace

int main() {
    CHECK(ChordRulesHold(IsFullscreenChord));
    CHECK(!ChordRulesHold(IgnoresAlt));
    std::puts("All synthetic FULLSCREEN TOGGLE smoke checks passed.");
    return 0;
}
