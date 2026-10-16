// smoke_graphics_settings_unit: the shared Resolution Scale table and the apply rule
// (common/resolution_scale_table.h, suyu_cmd/overlay/graphics_settings.h).
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "suyu_cmd/overlay/graphics_settings.h"

using namespace SuyuCmd;
using Settings::ResolutionSetup;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

// A setting that records what the hooks did to it.
struct FakeSetting {
    ResolutionSetup value{ResolutionSetup::Res1X};
    std::vector<ResolutionSetup> stored;
    int commits{};

    ResolutionScaleHooks Hooks() {
        return {
            .current = [this] { return value; },
            .store =
                [this](ResolutionSetup setup) {
                    value = setup;
                    stored.push_back(setup);
                },
            .commit = [this] { ++commits; },
        };
    }
};

// Negative control: stores on every call and never checks the index.
UiActionResult NaiveApply(const ResolutionScaleHooks& hooks, std::size_t index) {
    hooks.store(Settings::kResolutionScaleEntries[index % 13].setup);
    hooks.commit();
    return {};
}

// The apply rule, shared by the real function and the control. Returns false on a breach.
template <class Apply>
bool ApplyRuleHolds(Apply apply) {
    FakeSetting setting;
    // A new choice is stored once, committed once, and announced with the next-launch note.
    const UiActionResult changed = apply(setting.Hooks(), 6);
    if (!changed.ok || setting.stored.size() != 1 || setting.stored[0] != ResolutionSetup::Res2X ||
        setting.commits != 1 || changed.message.find("next time") == std::string::npos ||
        changed.message.find("2X") == std::string::npos) {
        return false;
    }
    // The same choice again changes nothing and says nothing.
    const UiActionResult same = apply(setting.Hooks(), 6);
    if (!same.ok || !same.message.empty() || setting.stored.size() != 1 || setting.commits != 1) {
        return false;
    }
    // An index outside the table is refused without touching the setting.
    const UiActionResult bad = apply(setting.Hooks(), 13);
    return !bad.ok && !bad.message.empty() && setting.stored.size() == 1 && setting.commits == 1;
}

int TableCoversEveryChoiceOnce() {
    const auto& entries = Settings::kResolutionScaleEntries;
    CHECK(entries.size() == 13);
    std::set<std::string> labels;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        CHECK(static_cast<std::size_t>(entries[i].setup) == i);
        CHECK(entries[i].label != nullptr && entries[i].label[0] != '\0');
        labels.insert(entries[i].label);
        CHECK(Settings::FindResolutionScaleIndex(entries[i].setup) == i);
    }
    CHECK(labels.size() == entries.size());
    CHECK(std::strcmp(entries[3].label, "1X (720p/1080p)") == 0);
    CHECK(!Settings::FindResolutionScaleIndex(static_cast<ResolutionSetup>(13)));
    return 0;
}

int EveryEntryIsApplied() {
    FakeSetting setting;
    for (std::size_t i = 0; i < Settings::kResolutionScaleEntries.size(); ++i) {
        const UiActionResult result = ApplyResolutionScale(setting.Hooks(), i);
        CHECK(result.ok);
        CHECK(setting.value == Settings::kResolutionScaleEntries[i].setup);
    }
    CHECK(setting.commits == 13);
    return 0;
}

int ApplyRuleHoldsForTheRealFunctionOnly() {
    CHECK(ApplyRuleHolds(ApplyResolutionScale));
    CHECK(!ApplyRuleHolds(NaiveApply));
    return 0;
}

int ActionRunsOnTheEventThreadAndReportsStatus() {
    FakeSetting setting;
    UiActionChannel channel;
    channel.BindEventThread();
    const ResolutionScaleHooks hooks = setting.Hooks();
    CHECK(channel.Post({"Resolution Scale", [&] { return ApplyResolutionScale(hooks, 8); }}));
    // Posting alone changes nothing: the setting moves only when the event thread runs it.
    CHECK(setting.value == ResolutionSetup::Res1X && setting.commits == 0);
    CHECK(channel.RunPending() == 1);
    CHECK(setting.value == ResolutionSetup::Res4X && setting.commits == 1);
    std::vector<UiStatusMessage> statuses;
    channel.DrainStatus(statuses);
    CHECK(statuses.size() == 1 && statuses[0].ok);
    CHECK(statuses[0].text.find("4X") != std::string::npos);
    return 0;
}

} // namespace

int main() {
    CHECK(TableCoversEveryChoiceOnce() == 0);
    CHECK(EveryEntryIsApplied() == 0);
    CHECK(ApplyRuleHoldsForTheRealFunctionOnly() == 0);
    CHECK(ActionRunsOnTheEventThreadAndReportsStatus() == 0);
    std::puts("All synthetic GRAPHICS SETTINGS smoke checks passed.");
    return 0;
}
