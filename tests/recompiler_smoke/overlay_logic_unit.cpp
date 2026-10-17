// smoke_overlay_logic_unit: the debug overlay's command-bar visibility rule and the mouse input
// router (suyu_cmd/overlay/overlay_logic.h), which need no GPU or ImGui.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "suyu_cmd/overlay/overlay_logic.h"
#include "suyu_cmd/overlay/overlay_stats.h"

using namespace SuyuCmd;
using Kind = OverlayInputEvent::Kind;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

// The overlay's UI as the tests lay it out: a bar across the top and a window below it.
const OverlayRect kBar{0.0f, 0.0f, 1920.0f, 24.0f};
const OverlayRect kWindow{100.0f, 100.0f, 300.0f, 200.0f};
constexpr float kInsideX = 150.0f;
constexpr float kInsideY = 150.0f;
constexpr float kOutsideX = 800.0f;
constexpr float kOutsideY = 600.0f;

OverlayInputEvent Move(float x, float y) {
    return {Kind::MouseMove, x, y, 0, false};
}

OverlayInputEvent Button(std::uint8_t button, bool down, float x, float y) {
    return {Kind::MouseButton, x, y, button, down};
}

OverlayInputEvent Wheel(float dx, float dy) {
    return {Kind::MouseWheel, dx, dy, 0, false};
}

OverlayInputEvent Simple(Kind kind) {
    return {kind, 0.0f, 0.0f, 0, false};
}

std::vector<OverlayInputEvent> Drained(OverlayInputRouter& router) {
    std::vector<OverlayInputEvent> events;
    router.Drain(events);
    return events;
}

CommandBarInput Hidden(float mouse_y) {
    return {true, mouse_y, false, 0.0f, false};
}

int HoverZone() {
    CHECK(kCommandBarHoverZoneHeightPx == 20.0f);
    CHECK(ShouldShowCommandBar(Hidden(0.0f)));
    CHECK(ShouldShowCommandBar(Hidden(19.9f)));
    // The zone is [0, 20): the 20th pixel row no longer reveals the bar.
    CHECK(!ShouldShowCommandBar(Hidden(20.0f)));
    CHECK(!ShouldShowCommandBar(Hidden(400.0f)));
    CHECK(!ShouldShowCommandBar(Hidden(-1.0f)));

    CommandBarInput outside = Hidden(5.0f);
    outside.mouse_in_window = false;
    CHECK(!ShouldShowCommandBar(outside));
    return 0;
}

int StaysWhileOverBar() {
    // Once shown, the bar's own height keeps it up even below the reveal zone.
    CommandBarInput input{true, 23.0f, true, 24.0f, false};
    CHECK(ShouldShowCommandBar(input));
    input.mouse_y_px = 24.0f;
    CHECK(!ShouldShowCommandBar(input));

    // A bar shorter than the zone never makes the zone smaller than it was.
    CommandBarInput short_bar{true, 15.0f, true, 10.0f, false};
    CHECK(ShouldShowCommandBar(short_bar));
    short_bar.mouse_y_px = 20.0f;
    CHECK(!ShouldShowCommandBar(short_bar));

    // The taller bar height does not apply while the bar is hidden.
    CHECK(!ShouldShowCommandBar({true, 23.0f, false, 24.0f, false}));
    return 0;
}

int MenuKeepsBarUp() {
    CHECK(ShouldShowCommandBar({true, 300.0f, true, 24.0f, true}));
    CHECK(ShouldShowCommandBar({false, 0.0f, true, 24.0f, true}));
    return 0;
}

int RectEdges() {
    CHECK(kWindow.Contains(100.0f, 100.0f));
    CHECK(kWindow.Contains(399.9f, 299.9f));
    CHECK(!kWindow.Contains(400.0f, 150.0f));
    CHECK(!kWindow.Contains(150.0f, 300.0f));
    CHECK(!kWindow.Contains(99.9f, 150.0f));
    return 0;
}

int NoRegionsMeansGameOwnsEverything() {
    OverlayInputRouter router;
    CHECK(!router.Route(Move(kInsideX, kInsideY)));
    CHECK(!router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(!router.Route(Button(0, false, kInsideX, kInsideY)));
    CHECK(!router.Route(Wheel(0, 1)));
    CHECK(!router.Route(Simple(Kind::MouseLeave)));
    // Presses and wheel steps the game owns are not the overlay's business; moves still are.
    const auto events = Drained(router);
    CHECK(events.size() == 2);
    CHECK(events[0].kind == Kind::MouseMove);
    CHECK(events[1].kind == Kind::MouseLeave);
    return 0;
}

int MovesCollapse() {
    OverlayInputRouter router;
    router.Route(Move(1, 1));
    router.Route(Move(2, 2));
    router.Route(Move(3, 4));
    auto events = Drained(router);
    CHECK(events.size() == 1);
    CHECK(events[0].kind == Kind::MouseMove && events[0].x == 3 && events[0].y == 4);

    // A press in between keeps the moves on either side apart and in order.
    router.SetCaptureRegions({kWindow});
    router.Route(Move(kInsideX, kInsideY));
    router.Route(Button(0, true, kInsideX, kInsideY));
    router.Route(Move(kInsideX + 1, kInsideY));
    events = Drained(router);
    CHECK(events.size() == 3);
    CHECK(events[0].kind == Kind::MouseMove);
    CHECK(events[1].kind == Kind::MouseButton && events[1].down);
    CHECK(events[2].kind == Kind::MouseMove && events[2].x == kInsideX + 1);

    CHECK(Drained(router).empty());
    return 0;
}

int OverlayTakesWhatIsOverIt() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kBar, kWindow});
    CHECK(router.Route(Move(kInsideX, kInsideY)));
    CHECK(router.Route(Wheel(0, 1)));
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, false, kInsideX, kInsideY)));

    // The bar counts as well.
    CHECK(router.Route(Move(500, 10)));

    // Everything the overlay took reaches it, with the position the click happened at.
    const auto events = Drained(router);
    CHECK(events.size() == 5);
    CHECK(events[0].kind == Kind::MouseMove);
    CHECK(events[1].kind == Kind::MouseWheel && events[1].y == 1);
    CHECK(events[2].kind == Kind::MouseButton && events[2].down && events[2].x == kInsideX &&
          events[2].y == kInsideY);
    CHECK(events[3].kind == Kind::MouseButton && !events[3].down);
    CHECK(events[4].kind == Kind::MouseMove && events[4].y == 10);
    return 0;
}

int GameKeepsWhatIsOutside() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    CHECK(!router.Route(Move(kOutsideX, kOutsideY)));
    CHECK(!router.Route(Wheel(0, 1)));
    CHECK(!router.Route(Button(0, true, kOutsideX, kOutsideY)));
    CHECK(!router.Route(Button(0, false, kOutsideX, kOutsideY)));
    const auto events = Drained(router);
    CHECK(events.size() == 1);
    CHECK(events[0].kind == Kind::MouseMove);
    return 0;
}

int ClickIsNeverActedOnTwice() {
    // Move, press and release onto the window arrive before the UI's next frame; the regions
    // alone decide, however stale that frame is.
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    CHECK(router.Route(Move(kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, false, kInsideX, kInsideY)));
    auto events = Drained(router);
    CHECK(events.size() == 3);

    // The reverse: a click on the game after the pointer was over the window.
    CHECK(!router.Route(Move(kOutsideX, kOutsideY)));
    CHECK(!router.Route(Button(0, true, kOutsideX, kOutsideY)));
    CHECK(!router.Route(Button(0, false, kOutsideX, kOutsideY)));
    events = Drained(router);
    CHECK(events.size() == 1);
    CHECK(events[0].kind == Kind::MouseMove);
    return 0;
}

int PressWithoutPriorMotionUsesItsOwnPosition() {
    // A click can arrive with no motion before it; the press itself says where it landed.
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, false, kInsideX, kInsideY)));
    CHECK(!router.Route(Button(0, true, kOutsideX, kOutsideY)));
    CHECK(!router.Route(Button(0, false, kOutsideX, kOutsideY)));
    return 0;
}

int OverlayDragKeepsItsMotion() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    // Dragged out of the window: the overlay still has the pointer until the release.
    CHECK(router.Route(Move(kOutsideX, kOutsideY)));
    CHECK(router.Route(Wheel(0, 1)));
    // A second button pressed during the drag belongs to the overlay too.
    CHECK(router.Route(Button(1, true, kOutsideX, kOutsideY)));
    CHECK(router.Route(Button(1, false, kOutsideX, kOutsideY)));
    CHECK(router.Route(Button(0, false, kOutsideX, kOutsideY)));
    // Over once released.
    CHECK(!router.Route(Move(kOutsideX, kOutsideY)));
    return 0;
}

int GameDragKeepsItsMotion() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    CHECK(!router.Route(Button(0, true, kOutsideX, kOutsideY)));
    // The game's drag crosses the window: it keeps the motion, the release and the wheel.
    CHECK(!router.Route(Move(kInsideX, kInsideY)));
    CHECK(!router.Route(Wheel(0, 1)));
    CHECK(!router.Route(Button(1, true, kInsideX, kInsideY)));
    CHECK(!router.Route(Button(1, false, kInsideX, kInsideY)));
    CHECK(!router.Route(Button(0, false, kInsideX, kInsideY)));
    // Nothing of that reached the overlay as a press.
    for (const OverlayInputEvent& event : Drained(router)) {
        CHECK(event.kind == Kind::MouseMove);
    }
    // And the window is the overlay's again afterwards.
    CHECK(router.Route(Move(kInsideX, kInsideY)));
    return 0;
}

int RegionChangesApplyToNextPress() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    // The window closes while the button is down: the release is still the overlay's.
    router.SetCaptureRegions({});
    CHECK(router.Route(Button(0, false, kInsideX, kInsideY)));
    // A new press there now goes to the game.
    CHECK(!router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(!router.Route(Button(0, false, kInsideX, kInsideY)));
    return 0;
}

int LeaveForgetsThePointer() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    CHECK(router.Route(Move(kInsideX, kInsideY)));
    CHECK(!router.Route(Simple(Kind::MouseLeave)));
    // With the pointer gone, a wheel step has nowhere to land.
    CHECK(!router.Route(Wheel(0, 1)));
    return 0;
}

int ButtonsAreIndependent() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    // The overlay owns the pointer while it holds a button, so a second press goes to it.
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(router.Route(Button(2, true, kOutsideX, kOutsideY)));
    CHECK(router.Route(Button(2, false, kOutsideX, kOutsideY)));
    CHECK(router.Route(Button(0, false, kOutsideX, kOutsideY)));

    // A button number past the tracked range is never taken and never corrupts state.
    CHECK(!router.Route(Button(200, true, kInsideX, kInsideY)));
    CHECK(!router.Route(Button(200, false, kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, false, kInsideX, kInsideY)));
    return 0;
}

int ResetHandsBackAndCancels() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});

    // Focus is lost with the overlay's button down and never released: nothing stays held.
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(!router.Route(Simple(Kind::Reset)));
    auto events = Drained(router);
    CHECK(events.size() == 1 && events[0].kind == Kind::Reset);

    // The game's next click on that spot is its own, release included.
    router.SetCaptureRegions({});
    CHECK(!router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(!router.Route(Button(0, false, kInsideX, kInsideY)));

    // Same for a game press: after the reset the overlay may take a click again.
    router.SetCaptureRegions({kWindow});
    CHECK(!router.Route(Button(0, true, kOutsideX, kOutsideY)));
    CHECK(!router.Route(Simple(Kind::Reset)));
    CHECK(router.Route(Move(kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(router.Route(Button(0, false, kInsideX, kInsideY)));

    // The pointer position does not survive a reset either.
    CHECK(!router.Route(Simple(Kind::Reset)));
    CHECK(!router.Route(Wheel(0, 1)));

    // Focus coming back is passed on.
    router.Route(Simple(Kind::FocusGained));
    events = Drained(router);
    CHECK(!events.empty() && events.back().kind == Kind::FocusGained);
    return 0;
}

int WheelStepsAddUp() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    router.Route(Move(kInsideX, kInsideY));
    router.Route(Wheel(1, 2));
    router.Route(Wheel(0.5f, 3));
    const auto events = Drained(router);
    CHECK(events.size() == 2);
    CHECK(events[1].kind == Kind::MouseWheel && events[1].x == 1.5f && events[1].y == 5.0f);
    return 0;
}

int QueueIsBounded() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});

    // A move between presses cannot collapse, so this overflows. The moves are expendable;
    // every press and release the overlay took must survive.
    int presses = 0;
    for (int i = 0; i < 100; ++i) {
        router.Route(Move(kInsideX + static_cast<float>(i % 2), kInsideY));
        router.Route(Button(0, true, kInsideX, kInsideY));
        router.Route(Move(kInsideX, kInsideY + static_cast<float>(i % 2)));
        router.Route(Button(0, false, kInsideX, kInsideY));
        presses += 2;
    }
    const auto events = Drained(router);
    CHECK(events.size() <= kOverlayInputQueueCapacity);
    int buttons = 0;
    for (const OverlayInputEvent& event : events) {
        CHECK(event.kind != Kind::Reset);
        buttons += event.kind == Kind::MouseButton ? 1 : 0;
    }
    CHECK(buttons == presses);
    return 0;
}

// A queue holding a move, a wheel step and presses up to the capacity.
void FillWithMoveWheelAndPresses(OverlayInputRouter& router) {
    router.SetCaptureRegions({kWindow});
    router.Route(Move(kInsideX, kInsideY));
    router.Route(Wheel(0, 1));
    for (std::size_t i = 2; i < kOverlayInputQueueCapacity; ++i) {
        router.Route(Button(0, i % 2 == 0, kInsideX, kInsideY));
    }
}

int MoveIsDroppedBeforeWheelBeforeButtons() {
    // One event too many: the position update goes first, the wheel step stays.
    OverlayInputRouter first;
    FillWithMoveWheelAndPresses(first);
    CHECK(first.Route(Button(0, true, kInsideX, kInsideY)));
    auto events = Drained(first);
    CHECK(events.size() == kOverlayInputQueueCapacity);
    int moves = 0;
    int wheels = 0;
    for (const OverlayInputEvent& event : events) {
        moves += event.kind == Kind::MouseMove ? 1 : 0;
        wheels += event.kind == Kind::MouseWheel ? 1 : 0;
    }
    CHECK(moves == 0);
    CHECK(wheels == 1);

    // Two too many: then the wheel step goes, and still no press.
    OverlayInputRouter second;
    FillWithMoveWheelAndPresses(second);
    CHECK(second.Route(Button(0, true, kInsideX, kInsideY)));
    CHECK(second.Route(Button(0, false, kInsideX, kInsideY)));
    events = Drained(second);
    CHECK(events.size() == kOverlayInputQueueCapacity);
    for (const OverlayInputEvent& event : events) {
        CHECK(event.kind == Kind::MouseButton);
    }
    return 0;
}

int OverflowOfPressesRestartsTheOverlay() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    // Nothing but presses: there is no way to keep them all.
    for (std::size_t i = 0; i < kOverlayInputQueueCapacity; ++i) {
        CHECK(router.Route(Button(0, i % 2 == 0, kInsideX, kInsideY)));
    }
    CHECK(router.Route(Button(1, true, kInsideX, kInsideY)));
    const auto events = Drained(router);
    // The overlay is told to forget what it held, then sees the new press on a clean slate.
    CHECK(events.size() == 3);
    CHECK(events[0].kind == Kind::Reset);
    CHECK(events[1].kind == Kind::FocusGained);
    CHECK(events[2].kind == Kind::MouseButton && events[2].button == 1 && events[2].down);

    // The router still knows that press is the overlay's, so its release stays hidden.
    CHECK(router.Route(Button(1, false, kInsideX, kInsideY)));
    return 0;
}

int ThreadsDoNotLoseEvents() {
    OverlayInputRouter router;
    router.SetCaptureRegions({kWindow});
    constexpr int kPairs = 80; // 200 events: below the capacity, so nothing may be dropped
    std::thread producer([&router] {
        for (int i = 0; i < kPairs; ++i) {
            router.Route(Button(0, true, kInsideX, kInsideY));
            router.Route(Button(0, false, kInsideX, kInsideY));
        }
    });
    std::vector<OverlayInputEvent> received;
    std::vector<OverlayInputEvent> events;
    while (received.size() < 2 * kPairs) {
        router.Drain(events);
        received.insert(received.end(), events.begin(), events.end());
        // The UI republishes the same layout every frame while the event thread is busy.
        router.SetCaptureRegions({kWindow});
        std::this_thread::yield();
    }
    producer.join();
    router.Drain(events);
    received.insert(received.end(), events.begin(), events.end());
    CHECK(received.size() == 2 * kPairs);
    for (std::size_t i = 0; i < received.size(); ++i) {
        CHECK(received[i].kind == Kind::MouseButton);
        CHECK(received[i].down == (i % 2 == 0));
    }
    return 0;
}

int FrameTimeHistoryFillsThenWraps() {
    FrameTimeHistory history;
    CHECK(history.Count() == 0);
    CHECK(history.Max() == 0.0f && history.Average() == 0.0f);
    CHECK(history.Offset() == 0);

    // Before the ring wraps the oldest sample is at the start.
    history.Add(10.0f);
    history.Add(30.0f);
    CHECK(history.Count() == 2);
    CHECK(history.Offset() == 0);
    CHECK(history.Values()[0] == 10.0f && history.Values()[1] == 30.0f);
    CHECK(history.Max() == 30.0f);
    CHECK(history.Average() == 20.0f);

    // Fill exactly: nothing has been overwritten yet, the oldest is still index 0.
    FrameTimeHistory full;
    for (std::size_t i = 0; i < kFrameTimeHistoryLength; ++i) {
        full.Add(static_cast<float>(i));
    }
    CHECK(full.Count() == kFrameTimeHistoryLength);
    CHECK(full.Offset() == 0);

    // One more overwrites the oldest and the oldest becomes the next slot.
    full.Add(1000.0f);
    CHECK(full.Count() == kFrameTimeHistoryLength);
    CHECK(full.Offset() == 1);
    CHECK(full.Values()[0] == 1000.0f);
    CHECK(full.Values()[1] == 1.0f);
    CHECK(full.Max() == 1000.0f);
    // The dropped sample (0) no longer counts: sum(1..119) + 1000 over 120 samples.
    const float expected = (119.0f * 120.0f / 2.0f + 1000.0f) / 120.0f;
    CHECK(full.Average() > expected - 0.001f && full.Average() < expected + 0.001f);
    return 0;
}

int TitleIdIsSixteenUpperHexDigits() {
    CHECK(FormatTitleId(0) == "0000000000000000");
    CHECK(FormatTitleId(0x0100000000000B01ull) == "0100000000000B01");
    CHECK(FormatTitleId(0xFFFFFFFFFFFFFFFFull) == "FFFFFFFFFFFFFFFF");
    CHECK(FormatTitleId(0xABCDEFull) == "0000000000ABCDEF");
    return 0;
}

int GuestPcIsPlacedInItsModule() {
    const std::map<std::uint64_t, std::string> modules{
        {0x80004000, "rtld"}, {0x80100000, "main"}, {0x82000000, "sdk"}};

    CHECK(!ResolveGuestPc(0, modules).found);
    // Below the first module there is nothing to attribute it to.
    CHECK(!ResolveGuestPc(0x7fffffff, modules).found);
    CHECK(!ResolveGuestPc(0x80004000, {}).found);

    GuestLocation location = ResolveGuestPc(0x80004000, modules);
    CHECK(location.found && location.module == "rtld" && location.offset == 0);

    location = ResolveGuestPc(0x801234a0, modules);
    CHECK(location.found && location.module == "main" && location.offset == 0x234a0);

    // The base of the next module is that module's offset 0, not the previous one's end.
    location = ResolveGuestPc(0x82000000, modules);
    CHECK(location.found && location.module == "sdk" && location.offset == 0);
    location = ResolveGuestPc(0x81ffffff, modules);
    CHECK(location.found && location.module == "main" && location.offset == 0x1efffff);

    // Past the last module's start it still belongs to it.
    location = ResolveGuestPc(0x90000000, modules);
    CHECK(location.found && location.module == "sdk");
    return 0;
}

int AnyFallbackCounterCounts() {
    CHECK(!FallbackObserved({}));
    CHECK(FallbackObserved({1, 0, 0, 0}));
    CHECK(FallbackObserved({0, 1, 0, 0}));
    CHECK(FallbackObserved({0, 0, 1, 0}));
    CHECK(FallbackObserved({0, 0, 0, 1}));
    return 0;
}

int FallbackVerdictSeparatesAttemptFromRefusal() {
    CHECK(ClassifyFallback({}) == FallbackVerdict::None);
    CHECK(ClassifyFallback({1, 0, 0, 0}) == FallbackVerdict::Attempted);
    CHECK(ClassifyFallback({0, 1, 0, 0}) == FallbackVerdict::Attempted);
    CHECK(ClassifyFallback({0, 0, 1, 0}) == FallbackVerdict::Attempted);
    // A refused attempt is the one case where execution could not go on; it wins over the rest.
    CHECK(ClassifyFallback({0, 0, 0, 1}) == FallbackVerdict::Refused);
    CHECK(ClassifyFallback({5, 3, 2, 1}) == FallbackVerdict::Refused);
    return 0;
}

NativeStatusSnapshot Running() {
    NativeStatusSnapshot snapshot;
    snapshot.game_running = true;
    return snapshot;
}

int ModeTextOnlyClaimsPolicyForRecompiledCode() {
    // Not running: nothing to describe yet.
    CHECK(ClassifyGameBackend(NativeStatusSnapshot{}) == NativeBackendClass::Pending);

    // A plain JIT run has no recompiled image, whatever the strict flag says.
    NativeStatusSnapshot jit = Running();
    jit.dynarmic_backend = true;
    jit.strict_requested = true;
    CHECK(ClassifyGameBackend(jit) == NativeBackendClass::DynarmicJit);
    CHECK(std::strstr(ExecutionModeText(ClassifyGameBackend(jit)), "not applicable") != nullptr);
    CHECK(std::strstr(ExecutionModeText(ClassifyGameBackend(jit)), "hybrid") == nullptr);

    // Registered but not yet executing recompiled blocks.
    NativeStatusSnapshot pending = Running();
    pending.recomp_registered = true;
    CHECK(ClassifyGameBackend(pending) == NativeBackendClass::Pending);
    CHECK(std::strstr(ExecutionModeText(ClassifyGameBackend(pending)), "pending") != nullptr);

    NativeStatusSnapshot active = Running();
    active.recomp_registered = true;
    active.backend_active = true;
    active.static_blocks = 10;
    active.strict_requested = false;
    CHECK(ClassifyGameBackend(active) == NativeBackendClass::HybridAot);
    CHECK(std::strstr(ExecutionModeText(NativeBackendClass::HybridAot), "hybrid") != nullptr);
    active.strict_requested = true;
    CHECK(ClassifyGameBackend(active) == NativeBackendClass::StrictAot);
    CHECK(std::strstr(ExecutionModeText(NativeBackendClass::StrictAot), "strict") != nullptr);
    CHECK(std::strstr(ExecutionModeText(NativeBackendClass::StrictAot), "hybrid") == nullptr);

    // Every class without recompiled code reads the same way.
    CHECK(std::strstr(ExecutionModeText(NativeBackendClass::Nce), "not applicable") != nullptr);
    CHECK(std::strstr(ExecutionModeText(NativeBackendClass::Unknown), "not applicable") != nullptr);
    return 0;
}

int StallsAreMeasuredAsTheyWereAndAnimatedClamped() {
    // A one-second stall: the graph sees all of it, ImGui's animation only a quarter second.
    FrameTiming timing = TimeFrame(1.0f);
    CHECK(timing.measurable);
    CHECK(timing.measured_ms == 1000.0f);
    CHECK(timing.animation_delta_seconds == kMaxAnimationDeltaSeconds);

    // An ordinary frame passes through unchanged on both.
    timing = TimeFrame(0.016f);
    CHECK(timing.measurable);
    CHECK(timing.measured_ms > 15.99f && timing.measured_ms < 16.01f);
    CHECK(timing.animation_delta_seconds == 0.016f);

    // No elapsed time (first frame, or a clock that did not advance): nothing to measure, and
    // ImGui still gets a usable step.
    for (const float no_time : {0.0f, -0.5f}) {
        timing = TimeFrame(no_time);
        CHECK(!timing.measurable);
        CHECK(timing.animation_delta_seconds == kFirstFrameAnimationDeltaSeconds);
    }

    // The history keeps what it is given.
    FrameTimeHistory history;
    history.Add(TimeFrame(0.016f).measured_ms);
    history.Add(TimeFrame(1.0f).measured_ms);
    CHECK(history.Max() == 1000.0f);
    return 0;
}

constexpr std::uint16_t kKeyA = 4;
constexpr std::uint16_t kKeyEnter = 40;

// Negative control: decides every key by the current keyboard request, ignoring who took the press.
struct StatelessKeyRouter {
    bool wanted{};
    bool RouteKey(std::uint16_t, bool, bool) const {
        return wanted;
    }
    void SetKeyboardCapture(bool keys, bool) {
        wanted = keys;
    }
};

// The ownership contract, shared by the real router and the control. Returns false on a breach.
template <class Router>
bool KeyOwnershipHolds(Router& router) {
    // A press the game took keeps its release with the game, even if the overlay starts wanting
    // keys while it is held.
    router.SetKeyboardCapture(false, false);
    if (router.RouteKey(kKeyA, true, false)) {
        return false;
    }
    router.SetKeyboardCapture(true, false);
    if (router.RouteKey(kKeyA, true, true) || router.RouteKey(kKeyA, false, false)) {
        return false;
    }
    // A press the overlay took keeps its repeats and release, even after it stops wanting keys.
    if (!router.RouteKey(kKeyEnter, true, false) || !router.RouteKey(kKeyEnter, true, true)) {
        return false;
    }
    router.SetKeyboardCapture(false, false);
    return router.RouteKey(kKeyEnter, false, false);
}

int KeyFollowsItsPress() {
    OverlayInputRouter router;
    CHECK(KeyOwnershipHolds(router));
    const auto events = Drained(router);
    CHECK(events.size() == 2);
    CHECK(events[0].kind == Kind::KeyDown && events[0].key == kKeyEnter);
    CHECK(events[1].kind == Kind::KeyUp && events[1].key == kKeyEnter);

    // The same contract must reject the stateless control.
    StatelessKeyRouter stateless;
    CHECK(!KeyOwnershipHolds(stateless));

    // Out-of-range scancodes are never taken.
    router.SetKeyboardCapture(true, false);
    CHECK(!router.RouteKey(kOverlayTrackedKeys, true, false));
    return 0;
}

int ResetForgetsKeyOwnership() {
    OverlayInputRouter router;
    router.SetKeyboardCapture(true, false);
    CHECK(router.RouteKey(kKeyA, true, false));
    router.Route(Simple(Kind::Reset));
    const auto events = Drained(router);
    CHECK(events.size() == 1 && events[0].kind == Kind::Reset);
    // The lost release is not the overlay's any more.
    CHECK(!router.RouteKey(kKeyA, false, false));
    CHECK(Drained(router).empty());
    return 0;
}

int TextOnlyWhileKeyboardIsWanted() {
    OverlayInputRouter router;
    CHECK(!router.TextInputWanted());
    CHECK(!router.RouteText("a"));
    CHECK(Drained(router).empty());

    router.SetKeyboardCapture(true, true);
    CHECK(router.TextInputWanted());
    CHECK(router.RouteText("ab"));
    auto events = Drained(router);
    CHECK(events.size() == 1 && events[0].kind == Kind::Text);
    CHECK(std::strcmp(events[0].text, "ab") == 0);

    router.SetKeyboardCapture(false, false);
    CHECK(!router.TextInputWanted());
    CHECK(!router.RouteText("c"));
    return 0;
}

int LongTextSplitsOnCodePoints() {
    OverlayInputRouter router;
    router.SetKeyboardCapture(true, true);
    // 40 three-byte code points: 120 bytes, which cannot fit one event and must never be cut
    // inside a code point.
    std::string text;
    for (int i = 0; i < 40; ++i) {
        text += "\xE3\x81\x82";
    }
    CHECK(router.RouteText(text));
    std::string joined;
    for (const OverlayInputEvent& event : Drained(router)) {
        CHECK(event.kind == Kind::Text);
        const std::size_t length = std::strlen(event.text);
        CHECK(length > 0 && length % 3 == 0 && length < kOverlayTextBytes);
        joined += event.text;
    }
    CHECK(joined == text);
    return 0;
}

int KeyAndTextKeepTheirOrder() {
    OverlayInputRouter router;
    router.SetKeyboardCapture(true, true);
    constexpr int kPairs = 80;
    std::thread producer([&] {
        for (int i = 0; i < kPairs; ++i) {
            router.RouteKey(kKeyA, true, false);
            router.RouteText("a");
            router.RouteKey(kKeyA, false, false);
        }
    });
    producer.join();
    // Everything fits the queue (240 < capacity), so nothing may be missing or reordered.
    const auto events = Drained(router);
    CHECK(events.size() == kPairs * 3);
    for (int i = 0; i < kPairs; ++i) {
        CHECK(events[i * 3].kind == Kind::KeyDown);
        CHECK(events[i * 3 + 1].kind == Kind::Text);
        CHECK(events[i * 3 + 2].kind == Kind::KeyUp);
    }
    return 0;
}

int KeyAndTextOverflowRestartsTheOverlay() {
    OverlayInputRouter router;
    router.SetKeyboardCapture(true, true);
    for (std::size_t i = 0; i < kOverlayInputQueueCapacity + 10; ++i) {
        CHECK(router.RouteKey(static_cast<std::uint16_t>(i % 100 + 1), true, false));
    }
    const auto events = Drained(router);
    CHECK(events.size() <= kOverlayInputQueueCapacity);
    // Presses were lost, so the overlay was told to start over.
    CHECK(events.front().kind == Kind::Reset);
    // Text queued afterwards still arrives, behind the restart.
    CHECK(router.RouteText("x"));
    const auto after = Drained(router);
    CHECK(after.size() == 1 && after[0].kind == Kind::Text);
    return 0;
}

} // namespace

int main() {
    CHECK(HoverZone() == 0);
    CHECK(StaysWhileOverBar() == 0);
    CHECK(MenuKeepsBarUp() == 0);
    CHECK(RectEdges() == 0);
    CHECK(NoRegionsMeansGameOwnsEverything() == 0);
    CHECK(MovesCollapse() == 0);
    CHECK(OverlayTakesWhatIsOverIt() == 0);
    CHECK(GameKeepsWhatIsOutside() == 0);
    CHECK(ClickIsNeverActedOnTwice() == 0);
    CHECK(PressWithoutPriorMotionUsesItsOwnPosition() == 0);
    CHECK(OverlayDragKeepsItsMotion() == 0);
    CHECK(GameDragKeepsItsMotion() == 0);
    CHECK(RegionChangesApplyToNextPress() == 0);
    CHECK(LeaveForgetsThePointer() == 0);
    CHECK(ButtonsAreIndependent() == 0);
    CHECK(ResetHandsBackAndCancels() == 0);
    CHECK(WheelStepsAddUp() == 0);
    CHECK(QueueIsBounded() == 0);
    CHECK(MoveIsDroppedBeforeWheelBeforeButtons() == 0);
    CHECK(OverflowOfPressesRestartsTheOverlay() == 0);
    CHECK(ThreadsDoNotLoseEvents() == 0);
    CHECK(KeyFollowsItsPress() == 0);
    CHECK(ResetForgetsKeyOwnership() == 0);
    CHECK(TextOnlyWhileKeyboardIsWanted() == 0);
    CHECK(LongTextSplitsOnCodePoints() == 0);
    CHECK(KeyAndTextKeepTheirOrder() == 0);
    CHECK(KeyAndTextOverflowRestartsTheOverlay() == 0);
    CHECK(FrameTimeHistoryFillsThenWraps() == 0);
    CHECK(TitleIdIsSixteenUpperHexDigits() == 0);
    CHECK(GuestPcIsPlacedInItsModule() == 0);
    CHECK(AnyFallbackCounterCounts() == 0);
    CHECK(FallbackVerdictSeparatesAttemptFromRefusal() == 0);
    CHECK(ModeTextOnlyClaimsPolicyForRecompiledCode() == 0);
    CHECK(StallsAreMeasuredAsTheyWereAndAnimatedClamped() == 0);
    std::puts("All synthetic OVERLAY LOGIC smoke checks passed.");
    return 0;
}
