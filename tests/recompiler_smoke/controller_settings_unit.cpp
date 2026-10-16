// smoke_controller_settings_unit: the non-blocking rebind session and the Player 1 binding rules
// (suyu_cmd/overlay/rebind_session.h, controller_settings.h), which need no input backend.
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "suyu_cmd/overlay/controller_settings.h"
#include "suyu_cmd/overlay/rebind_session.h"

using namespace SuyuCmd;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

// A poller that records its calls and returns what the test queues.
struct FakePoller {
    int begins{};
    int stops{};
    int polls{};
    bool last_analog{};
    std::string next;

    RebindHooks Hooks() {
        return {
            .begin =
                [this](bool analog) {
                    ++begins;
                    last_analog = analog;
                },
            .next_input =
                [this] {
                    ++polls;
                    std::string out;
                    out.swap(next);
                    return out;
                },
            .stop = [this] { ++stops; },
        };
    }
};

int CaptureEndsTheSession() {
    FakePoller poller;
    RebindSession session{poller.Hooks()};
    CHECK(!session.Active());
    CHECK(session.Poll(0).kind == RebindResult::Kind::None);
    CHECK(poller.polls == 0);

    session.Begin(3, false, 1000);
    CHECK(session.Active() && session.Row() == 3);
    CHECK(poller.begins == 1 && !poller.last_analog);
    // Nothing yet: it keeps listening without blocking.
    CHECK(session.Poll(1010).kind == RebindResult::Kind::None);
    CHECK(session.Active() && poller.stops == 0);

    poller.next = "engine:sdl,button:1";
    const RebindResult result = session.Poll(1020);
    CHECK(result.kind == RebindResult::Kind::Captured);
    CHECK(result.row == 3 && result.captured == "engine:sdl,button:1");
    CHECK(!session.Active() && poller.stops == 1);
    // A finished session does not poll again.
    const int polls = poller.polls;
    CHECK(session.Poll(1030).kind == RebindResult::Kind::None);
    CHECK(poller.polls == polls);
    return 0;
}

int TimeoutIsExactAndStopsThePoller() {
    FakePoller poller;
    RebindSession session{poller.Hooks()};
    session.Begin(0, true, 50000);
    CHECK(poller.last_analog);
    CHECK(session.RemainingMs(50000) == kRebindTimeoutMs);
    CHECK(session.RemainingMs(50000 + 1200) == kRebindTimeoutMs - 1200);
    CHECK(session.Poll(50000 + kRebindTimeoutMs - 1).kind == RebindResult::Kind::None);
    CHECK(session.Active());
    const RebindResult result = session.Poll(50000 + kRebindTimeoutMs);
    CHECK(result.kind == RebindResult::Kind::TimedOut && result.row == 0);
    CHECK(result.captured.empty());
    CHECK(!session.Active() && poller.stops == 1);
    CHECK(session.RemainingMs(60000) == 0);
    return 0;
}

int CaptureAtTheDeadlineWins() {
    FakePoller poller;
    RebindSession session{poller.Hooks()};
    session.Begin(1, false, 0);
    poller.next = "x";
    // The input is checked before the clock, so a press on the last tick is not lost.
    CHECK(session.Poll(kRebindTimeoutMs + 5).kind == RebindResult::Kind::Captured);
    return 0;
}

int CancelAndReplaceKeepBeginStopBalanced() {
    FakePoller poller;
    RebindSession session{poller.Hooks()};
    CHECK(session.Cancel().kind == RebindResult::Kind::None);
    CHECK(poller.stops == 0);

    session.Begin(2, false, 0);
    // A new capture replaces the running one: the old poller is stopped first.
    session.Begin(5, true, 100);
    CHECK(poller.begins == 2 && poller.stops == 1 && session.Row() == 5);
    const RebindResult cancelled = session.Cancel();
    CHECK(cancelled.kind == RebindResult::Kind::Cancelled && cancelled.row == 5);
    CHECK(poller.begins == 2 && poller.stops == 2);
    CHECK(session.Cancel().kind == RebindResult::Kind::None);
    CHECK(poller.stops == 2);
    return 0;
}

// Negative control: never stops the poller on a timeout or a replacement.
class LeakyRebindSession {
public:
    explicit LeakyRebindSession(RebindHooks h) : hooks(std::move(h)) {}
    void Begin(std::size_t, bool analog, std::uint64_t now_ms) {
        hooks.begin(analog);
        active = true;
        started_ms = now_ms;
    }
    RebindResult Poll(std::uint64_t now_ms) {
        if (active && now_ms - started_ms >= kRebindTimeoutMs) {
            active = false;
            return {RebindResult::Kind::TimedOut, 0, {}};
        }
        return {};
    }

private:
    RebindHooks hooks;
    bool active{};
    std::uint64_t started_ms{};
};

// The begin/stop balance rule, shared by the real session and the control.
template <class Session>
bool BeginsAreAlwaysStopped(FakePoller& poller, Session& session) {
    session.Begin(0, false, 0);
    session.Begin(1, false, 10); // replaces the first
    session.Poll(10 + kRebindTimeoutMs); // times out the second
    return poller.begins == poller.stops;
}

int BeginStopBalanceHoldsOnlyForTheRealSession() {
    FakePoller real_poller;
    RebindSession real{real_poller.Hooks()};
    CHECK(BeginsAreAlwaysStopped(real_poller, real));
    FakePoller leaky_poller;
    LeakyRebindSession leaky{leaky_poller.Hooks()};
    CHECK(!BeginsAreAlwaysStopped(leaky_poller, leaky));
    return 0;
}

// What the hooks were asked to do to Player 1, in order.
struct FakePlayer {
    std::vector<std::string> log;

    Player1Hooks Hooks() {
        return {
            .set =
                [this](bool analog, std::size_t index, const std::string& value) {
                    log.push_back(std::string{analog ? "analog" : "button"} + std::to_string(index) +
                                  "=" + value);
                },
            .mark_connected = [this] { log.push_back("connected"); },
            .commit = [this] { log.push_back("commit"); },
        };
    }
};

constexpr BindingLayout kLayout{15, 2};

int LayoutMapsRowsToArrays() {
    CHECK(kLayout.Rows() == 17);
    CHECK(!kLayout.IsAnalog(14) && kLayout.IsAnalog(15));
    CHECK(kLayout.IndexOf(14) == 14 && kLayout.IndexOf(15) == 0 && kLayout.IndexOf(16) == 1);
    return 0;
}

// The rules both the real functions and the control are held to. False on a breach.
template <class Apply, class Clear>
bool BindingRulesHold(Apply apply, Clear clear) {
    FakePlayer player;
    // A capture is written, marks the player connected, and is committed once, last.
    if (!apply(player.Hooks(), kLayout, 16, "stick").ok ||
        player.log != std::vector<std::string>{"analog1=stick", "connected", "commit"}) {
        return false;
    }
    // Nothing captured, or a row that does not exist, changes nothing.
    player.log.clear();
    if (apply(player.Hooks(), kLayout, 2, "").ok || apply(player.Hooks(), kLayout, 17, "a").ok ||
        !player.log.empty()) {
        return false;
    }
    // Clearing writes an empty value and commits once; it does not mark the player connected.
    if (!clear(player.Hooks(), kLayout, 3).ok ||
        player.log != std::vector<std::string>{"button3=", "commit"}) {
        return false;
    }
    player.log.clear();
    return !clear(player.Hooks(), kLayout, 99).ok && player.log.empty();
}

// Negative control: commits before writing, writes empty captures, and has no row check.
UiActionResult NaiveApply(const Player1Hooks& hooks, const BindingLayout& layout, std::size_t row,
                          const std::string& value) {
    hooks.commit();
    hooks.set(layout.IsAnalog(row), layout.IndexOf(row), value);
    hooks.mark_connected();
    return {};
}

UiActionResult NaiveClear(const Player1Hooks& hooks, const BindingLayout& layout,
                          std::size_t row) {
    hooks.set(layout.IsAnalog(row), layout.IndexOf(row), {});
    return {};
}

int BindingRulesHoldOnlyForTheRealFunctions() {
    CHECK(BindingRulesHold(ApplyBinding, ClearBinding));
    CHECK(!BindingRulesHold(NaiveApply, ClearBinding));
    CHECK(!BindingRulesHold(ApplyBinding, NaiveClear));
    return 0;
}

int CapturedBindingFlowsThroughTheActionQueue() {
    FakePoller poller;
    RebindSession session{poller.Hooks()};
    FakePlayer player;
    UiActionChannel channel;
    channel.BindEventThread();

    // The tick that sees the capture posts nothing itself; the result is applied as an action
    // on the event thread and a failure reaches the status channel.
    session.Begin(0, false, 0);
    poller.next = "engine:sdl,button:2";
    const RebindResult captured = session.Poll(5);
    CHECK(captured.kind == RebindResult::Kind::Captured);
    CHECK(channel.Post({"Rebind", [&] {
                            return ApplyBinding(player.Hooks(), kLayout, captured.row,
                                                captured.captured);
                        }}));
    CHECK(player.log.empty());
    CHECK(channel.RunPending() == 1);
    CHECK((player.log == std::vector<std::string>{"button0=engine:sdl,button:2", "connected",
                                                  "commit"}));

    CHECK(channel.Post({"Rebind", [&] { return ApplyBinding(player.Hooks(), kLayout, 99, "x"); }}));
    channel.RunPending();
    std::vector<UiStatusMessage> statuses;
    channel.DrainStatus(statuses);
    CHECK(statuses.size() == 1 && !statuses[0].ok);
    return 0;
}

int SnapshotBoxHandsOverWholeValues() {
    SnapshotBox<ControllerSnapshot> box;
    CHECK(box.Get().rows.empty());
    ControllerSnapshot snapshot;
    snapshot.rows = {{"A", "Button 1"}, {"B", "(unset)"}};
    snapshot.devices = {{"id0", "Pad"}};
    snapshot.rebinding = true;
    snapshot.rebind_row = 1;
    box.Publish(snapshot);
    ControllerSnapshot read = box.Get();
    CHECK(read.rows.size() == 2 && read.rows[1].shown == "(unset)");
    CHECK(read.devices.size() == 1 && read.rebinding && read.rebind_row == 1);

    // A reader on another thread always sees a complete snapshot.
    std::thread writer([&] {
        for (int i = 0; i < 2000; ++i) {
            ControllerSnapshot next;
            next.rows.assign(static_cast<std::size_t>(i % 5), {"r", "v"});
            next.rebind_row = next.rows.size();
            box.Publish(std::move(next));
        }
    });
    bool consistent = true;
    for (int i = 0; i < 2000; ++i) {
        const ControllerSnapshot seen = box.Get();
        const bool whole = seen.rows.size() == seen.rebind_row ||
                           (seen.rows.size() == 2 && seen.rebind_row == 1);
        consistent = consistent && whole;
    }
    writer.join();
    CHECK(consistent);
    return 0;
}

} // namespace

int main() {
    CHECK(CaptureEndsTheSession() == 0);
    CHECK(TimeoutIsExactAndStopsThePoller() == 0);
    CHECK(CaptureAtTheDeadlineWins() == 0);
    CHECK(CancelAndReplaceKeepBeginStopBalanced() == 0);
    CHECK(BeginStopBalanceHoldsOnlyForTheRealSession() == 0);
    CHECK(LayoutMapsRowsToArrays() == 0);
    CHECK(BindingRulesHoldOnlyForTheRealFunctions() == 0);
    CHECK(CapturedBindingFlowsThroughTheActionQueue() == 0);
    CHECK(SnapshotBoxHandsOverWholeValues() == 0);
    std::puts("All synthetic CONTROLLER SETTINGS smoke checks passed.");
    return 0;
}
