// smoke_fullscreen_toggle_unit: which key chords switch fullscreen and who owns each key press
// (suyu_cmd/fullscreen_toggle.h).
#include <cstdio>

#include "suyu_cmd/fullscreen_toggle.h"

using namespace SuyuCmd;
using Route = HotkeyKeyTracker::Route;

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

// Negative control: judges every event by its own modifiers and never remembers an owner.
class StatelessTracker {
public:
    Route OnKeyDown(std::uint32_t scancode, bool alt_down, bool repeat, const FullscreenKeys& keys) {
        if (!IsFullscreenChord(scancode, alt_down, keys)) {
            return Route::Game;
        }
        return repeat ? Route::Consume : Route::Toggle;
    }
    bool OnKeyUp(std::uint32_t) {
        return false;
    }
};

// The ownership rules, shared by the real tracker and the control. False on a breach.
template <class Tracker>
bool OwnershipRulesHold(Tracker tracker) {
    // Alt+Enter toggles once; its repeats stay consumed even after Alt is let go first, and the
    // game never sees the release.
    if (tracker.OnKeyDown(kKeys.enter, true, false, kKeys) != Route::Toggle ||
        tracker.OnKeyDown(kKeys.enter, true, true, kKeys) != Route::Consume ||
        tracker.OnKeyDown(kKeys.enter, false, true, kKeys) != Route::Consume ||
        !tracker.OnKeyUp(kKeys.enter)) {
        return false;
    }
    // A plain Enter the game took stays the game's when Alt is pressed during the hold.
    if (tracker.OnKeyDown(kKeys.enter, false, false, kKeys) != Route::Game ||
        tracker.OnKeyDown(kKeys.enter, true, true, kKeys) != Route::Game ||
        tracker.OnKeyUp(kKeys.enter)) {
        return false;
    }
    // F11 toggles on its own and its release is consumed.
    return tracker.OnKeyDown(kKeys.toggle, false, false, kKeys) == Route::Toggle &&
           tracker.OnKeyUp(kKeys.toggle) && !tracker.OnKeyUp(kKeys.toggle);
}

int ChordNeedsAltForEnter() {
    CHECK(ChordRulesHold(IsFullscreenChord));
    CHECK(!ChordRulesHold(IgnoresAlt));
    return 0;
}

int OwnershipFollowsTheFirstPress() {
    CHECK(OwnershipRulesHold(HotkeyKeyTracker{}));
    CHECK(!OwnershipRulesHold(StatelessTracker{}));
    return 0;
}

int LostReleasesAndResetsNeverStickOrToggleTwice() {
    HotkeyKeyTracker tracker;
    // The release was lost (focus change): the next fresh press toggles again.
    CHECK(tracker.OnKeyDown(kKeys.toggle, false, false, kKeys) == Route::Toggle);
    CHECK(tracker.OnKeyDown(kKeys.toggle, false, false, kKeys) == Route::Toggle);
    // After a reset the owner of a repeat is unknown: a hotkey repeat never toggles.
    tracker.Reset();
    CHECK(tracker.OnKeyDown(kKeys.toggle, false, true, kKeys) == Route::Consume);
    CHECK(tracker.OnKeyDown(kOther, false, true, kKeys) == Route::Game);
    // Scancodes beyond the table are judged per event.
    CHECK(tracker.OnKeyDown(100000, false, false, kKeys) == Route::Game);
    CHECK(!tracker.OnKeyUp(100000));
    return 0;
}

} // namespace

int main() {
    CHECK(ChordNeedsAltForEnter() == 0);
    CHECK(OwnershipFollowsTheFirstPress() == 0);
    CHECK(LostReleasesAndResetsNeverStickOrToggleTwice() == 0);
    std::puts("All synthetic FULLSCREEN TOGGLE smoke checks passed.");
    return 0;
}
