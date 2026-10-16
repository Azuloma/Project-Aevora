// smoke_overlay_actions_unit: the UI-to-event-thread action queue and status channel
// (suyu_cmd/overlay/overlay_actions.h), which need neither ImGui nor SDL.
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "suyu_cmd/overlay/overlay_actions.h"

using namespace SuyuCmd;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

UiAction Recording(std::vector<int>& log, int id) {
    return {"a" + std::to_string(id), [&log, id] {
                log.push_back(id);
                return UiActionResult{};
            }};
}

std::vector<UiStatusMessage> Statuses(UiActionChannel& channel) {
    std::vector<UiStatusMessage> out;
    channel.DrainStatus(out);
    return out;
}

int RunsInPostingOrder() {
    UiActionChannel channel;
    channel.BindEventThread();
    std::vector<int> log;
    for (int i = 0; i < 5; ++i) {
        CHECK(channel.Post(Recording(log, i)));
    }
    CHECK(log.empty());
    CHECK(channel.RunPending() == 5);
    CHECK((log == std::vector<int>{0, 1, 2, 3, 4}));
    CHECK(channel.RunPending() == 0);
    // A silent success publishes nothing.
    CHECK(Statuses(channel).empty());
    return 0;
}

int FullQueueRejectsAndReports() {
    UiActionChannel channel;
    channel.BindEventThread();
    std::vector<int> log;
    for (std::size_t i = 0; i < kUiActionQueueCapacity; ++i) {
        CHECK(channel.Post(Recording(log, static_cast<int>(i))));
    }
    CHECK(!channel.Post(Recording(log, 999)));
    const auto statuses = Statuses(channel);
    CHECK(statuses.size() == 1 && !statuses[0].ok);
    CHECK(statuses[0].text.find("a999") != std::string::npos);
    // The accepted ones are intact and in order; the rejected one never runs.
    CHECK(channel.RunPending() == kUiActionQueueCapacity);
    CHECK(log.size() == kUiActionQueueCapacity && log.front() == 0 && log.back() == 63);
    // An action without a body is refused too.
    CHECK(!channel.Post({"empty", nullptr}));
    return 0;
}

int ResultsBecomeStatus() {
    UiActionChannel channel;
    channel.BindEventThread();
    CHECK(channel.Post({"saved", [] { return UiActionResult{true, "Saved"}; }}));
    CHECK(channel.Post({"broken", [] { return UiActionResult{false, ""}; }}));
    CHECK(channel.Post({"named", [] { return UiActionResult{false, "No device"}; }}));
    CHECK(channel.RunPending() == 3);
    const auto statuses = Statuses(channel);
    CHECK(statuses.size() == 3);
    CHECK(statuses[0].ok && statuses[0].text == "Saved");
    CHECK(!statuses[1].ok && statuses[1].text.find("broken") != std::string::npos);
    CHECK(!statuses[2].ok && statuses[2].text == "No device");
    return 0;
}

int StatusQueueDropsOldest() {
    UiActionChannel channel;
    for (std::size_t i = 0; i < kUiStatusQueueCapacity + 5; ++i) {
        channel.PublishStatus({true, std::to_string(i)});
    }
    const auto statuses = Statuses(channel);
    CHECK(statuses.size() == kUiStatusQueueCapacity);
    CHECK(statuses.front().text == "5");
    CHECK(statuses.back().text == std::to_string(kUiStatusQueueCapacity + 4));
    CHECK(Statuses(channel).empty());
    return 0;
}

int ActionPostedWhileRunningWaits() {
    UiActionChannel channel;
    channel.BindEventThread();
    std::vector<int> log;
    CHECK(channel.Post({"outer", [&] {
                            log.push_back(1);
                            channel.Post(Recording(log, 2));
                            return UiActionResult{};
                        }}));
    CHECK(channel.RunPending() == 1);
    CHECK((log == std::vector<int>{1}));
    CHECK(channel.RunPending() == 1);
    CHECK((log == std::vector<int>{1, 2}));
    return 0;
}

// The same checks run against the real channel and a control that executes on the posting thread.
struct InlineChannel {
    bool Post(UiAction action) {
        action.run();
        return true;
    }
    std::size_t RunPending() {
        return 0;
    }
};

template <class Channel>
bool RunsOnlyOnEventThread(Channel& channel) {
    std::thread::id ran_on;
    const std::thread::id event_thread = std::this_thread::get_id();
    if (!channel.Post({"where", [&] {
                           ran_on = std::this_thread::get_id();
                           return UiActionResult{};
                       }})) {
        return false;
    }
    if (ran_on != std::thread::id{}) {
        return false; // ran before the event thread asked for it
    }
    channel.RunPending();
    return ran_on == event_thread;
}

int ActionsRunOnlyOnTheEventThread() {
    UiActionChannel channel;
    channel.BindEventThread();
    CHECK(RunsOnlyOnEventThread(channel));

    InlineChannel inline_channel;
    CHECK(!RunsOnlyOnEventThread(inline_channel));
    return 0;
}

int OtherThreadCannotRunActions() {
    UiActionChannel channel;
    std::vector<int> log;
    CHECK(channel.Post(Recording(log, 1)));
    // Nothing is bound yet, so nobody may run it.
    CHECK(channel.RunPending() == 0);
    channel.BindEventThread();
    std::size_t ran_elsewhere = 99;
    std::thread other([&] { ran_elsewhere = channel.RunPending(); });
    other.join();
    CHECK(ran_elsewhere == 0);
    CHECK(log.empty());
    CHECK(channel.WrongSideCalls() == 2);
    CHECK(channel.RunPending() == 1);
    CHECK((log == std::vector<int>{1}));
    return 0;
}

int ReentrantRunKeepsOrder() {
    UiActionChannel channel;
    channel.BindEventThread();
    std::vector<int> log;
    std::size_t inner_ran = 99;
    CHECK(channel.Post({"a", [&] {
                            log.push_back(1);
                            channel.Post(Recording(log, 3));
                            inner_ran = channel.RunPending();
                            return UiActionResult{};
                        }}));
    CHECK(channel.Post(Recording(log, 2)));
    CHECK(channel.RunPending() == 2);
    CHECK(inner_ran == 0);
    CHECK((log == std::vector<int>{1, 2}));
    CHECK(channel.RunPending() == 1);
    CHECK((log == std::vector<int>{1, 2, 3}));
    return 0;
}

int DetachDropsQueuedAndRefusesPosts() {
    UiActionChannel channel;
    channel.BindEventThread();
    std::vector<int> log;
    auto token = std::make_shared<int>(0);
    CHECK(channel.Post({"held", [token, &log] {
                            log.push_back(1);
                            return UiActionResult{};
                        }}));
    CHECK(token.use_count() == 2);
    channel.DetachEventThread();
    // The dropped action released its captures and never ran.
    CHECK(token.use_count() == 1);
    CHECK(channel.RunPending() == 0);
    CHECK(!channel.Post(Recording(log, 2)));
    CHECK(log.empty());

    // A new window binds again and starts clean.
    channel.BindEventThread();
    CHECK(channel.Post(Recording(log, 3)));
    CHECK(channel.RunPending() == 1);
    CHECK((log == std::vector<int>{3}));
    return 0;
}

int WakeupFollowsEveryAcceptedPost() {
    UiActionChannel channel;
    std::atomic<int> wakes{0};
    channel.SetWakeup([&] { ++wakes; });
    std::vector<int> log;
    CHECK(channel.Post(Recording(log, 1)));
    CHECK(wakes == 1);
    CHECK(!channel.Post({"empty", nullptr}));
    CHECK(wakes == 1);
    return 0;
}

int CrossThreadPostsAreAllRunInOrder() {
    UiActionChannel channel;
    channel.BindEventThread();
    constexpr int kPosts = 2000;
    std::vector<int> log;
    std::atomic<bool> done{false};
    std::thread ui([&] {
        for (int i = 0; i < kPosts;) {
            if (channel.Post(Recording(log, i))) {
                ++i;
            } else {
                std::this_thread::yield(); // full: wait for the event thread to catch up
            }
        }
        done = true;
    });
    std::size_t ran = 0;
    while (!done || ran < static_cast<std::size_t>(kPosts)) {
        ran += channel.RunPending();
        if (ran == static_cast<std::size_t>(kPosts)) {
            break;
        }
    }
    ui.join();
    CHECK(log.size() == static_cast<std::size_t>(kPosts));
    for (int i = 0; i < kPosts; ++i) {
        CHECK(log[static_cast<std::size_t>(i)] == i);
    }
    return 0;
}

} // namespace

int main() {
    CHECK(RunsInPostingOrder() == 0);
    CHECK(FullQueueRejectsAndReports() == 0);
    CHECK(ResultsBecomeStatus() == 0);
    CHECK(StatusQueueDropsOldest() == 0);
    CHECK(ActionPostedWhileRunningWaits() == 0);
    CHECK(ActionsRunOnlyOnTheEventThread() == 0);
    CHECK(OtherThreadCannotRunActions() == 0);
    CHECK(ReentrantRunKeepsOrder() == 0);
    CHECK(DetachDropsQueuedAndRefusesPosts() == 0);
    CHECK(WakeupFollowsEveryAcceptedPost() == 0);
    CHECK(CrossThreadPostsAreAllRunInOrder() == 0);
    std::puts("All synthetic OVERLAY ACTIONS smoke checks passed.");
    return 0;
}
