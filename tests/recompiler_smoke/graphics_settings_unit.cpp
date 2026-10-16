// smoke_graphics_settings_unit: the shared Resolution Scale table and the apply rule
// (common/resolution_scale_table.h, suyu_cmd/overlay/graphics_settings.h).
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
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
            .save = [this] { ++commits; },
        };
    }
};

// Negative control: stores on every call and never checks the index.
UiActionResult NaiveApply(const ResolutionScaleHooks& hooks, std::size_t index) {
    hooks.store(Settings::kResolutionScaleEntries[index % 13].setup);
    hooks.save();
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

std::string ReadFile(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

std::vector<std::string> EnumNames(const std::string& enums_header) {
    std::smatch match;
    const std::regex declaration{R"(ENUM\(ResolutionSetup,([^)]*)\))"};
    std::vector<std::string> names;
    if (std::regex_search(enums_header, match, declaration)) {
        std::istringstream list{match[1].str()};
        std::string name;
        while (std::getline(list, name, ',')) {
            const auto first = name.find_first_not_of(" \t\r\n");
            const auto last = name.find_last_not_of(" \t\r\n");
            if (first != std::string::npos) {
                names.push_back(name.substr(first, last - first + 1));
            }
        }
    }
    return names;
}

// Compares the Qt menu's table with the shared one and the enum; returns the first mismatch
// by name, or an empty string.
std::string QtTableMismatch(const std::string& main_cpp, const std::string& enums_header) {
    const std::vector<std::string> names = EnumNames(enums_header);
    if (names.size() != Settings::kResolutionScaleEntries.size()) {
        return "ResolutionSetup has " + std::to_string(names.size()) + " values, table has " +
               std::to_string(Settings::kResolutionScaleEntries.size());
    }
    const auto begin = main_cpp.find("SetupResolutionScaleMenu() {");
    const auto end = main_cpp.find("}};", begin);
    if (begin == std::string::npos || end == std::string::npos) {
        return "Qt Resolution Scale table not found in main.cpp";
    }
    const std::string block = main_cpp.substr(begin, end - begin);
    const std::regex entry{
        R"re(ResolutionSetup::(\w+),\s*QT_TRANSLATE_NOOP\("GMainWindow",\s*"([^"]*)"\))re"};
    std::size_t index = 0;
    for (std::sregex_iterator it{block.begin(), block.end(), entry}, last; it != last;
         ++it, ++index) {
        if (index >= names.size()) {
            return "Qt table has an extra entry " + (*it)[1].str();
        }
        if ((*it)[1].str() != names[index]) {
            return "Qt entry " + std::to_string(index) + " is " + (*it)[1].str() + ", enum has " +
                   names[index];
        }
        if ((*it)[2].str() != Settings::kResolutionScaleEntries[index].label) {
            return "Qt label for " + names[index] + " is '" + (*it)[2].str() + "', table has '" +
                   Settings::kResolutionScaleEntries[index].label + "'";
        }
    }
    return index == names.size() ? "" : "Qt table has " + std::to_string(index) + " entries";
}

int QtTableAndEnumMatchTheSharedTable(const std::string& source_root) {
    const std::string main_cpp = ReadFile(source_root + "/src/suyu/main.cpp");
    const std::string enums = ReadFile(source_root + "/src/common/settings_enums.h");
    CHECK(!main_cpp.empty() && !enums.empty());
    const std::string mismatch = QtTableMismatch(main_cpp, enums);
    if (!mismatch.empty()) {
        std::fprintf(stderr, "Resolution Scale drift: %s\n", mismatch.c_str());
    }
    CHECK(mismatch.empty());

    // Negative controls: one Qt label changed, and a value appended to the enum.
    std::string changed = main_cpp;
    const auto at = changed.find("1.5X (1080p/1620p)");
    CHECK(at != std::string::npos);
    changed.replace(at, 4, "1.6X");
    CHECK(QtTableMismatch(changed, enums).find("Res3_2X") != std::string::npos);
    std::string appended = enums;
    const auto enum_end = appended.find("Res8X)");
    CHECK(enum_end != std::string::npos);
    appended.replace(enum_end, 6, "Res8X, Res16X)");
    CHECK(!QtTableMismatch(main_cpp, appended).empty());
    return 0;
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

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: smoke_graphics_settings_unit <source root>\n");
        return 2;
    }
    CHECK(QtTableAndEnumMatchTheSharedTable(argv[1]) == 0);
    CHECK(TableCoversEveryChoiceOnce() == 0);
    CHECK(EveryEntryIsApplied() == 0);
    CHECK(ApplyRuleHoldsForTheRealFunctionOnly() == 0);
    CHECK(ActionRunsOnTheEventThreadAndReportsStatus() == 0);
    std::puts("All synthetic GRAPHICS SETTINGS smoke checks passed.");
    return 0;
}
