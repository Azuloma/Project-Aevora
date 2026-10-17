// smoke_mods_folders_unit: mods/ discovery, the background scanner and the file URL of a folder
// (suyu_cmd/overlay/mods_folders.h), which need neither ImGui nor SDL.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "suyu_cmd/overlay/mods_folders.h"

using namespace SuyuCmd;
namespace fs = std::filesystem;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

void MakeFile(const fs::path& path) {
    std::ofstream file{path};
    file << "x";
}

// Negative controls for the discovery rules.
ModsScan ListEverything(const fs::path& mods_dir) {
    ModsScan scan;
    std::error_code ec;
    for (const auto& title : fs::directory_iterator(mods_dir, ec)) {
        for (const auto& mod : fs::directory_iterator(title.path(), ec)) {
            scan.mods.push_back({Utf8(title.path().filename()), Utf8(mod.path().filename())});
        }
    }
    return scan;
}

ModsScan FoldersOnly(const fs::path& mods_dir) {
    ModsScan scan = ScanMods(mods_dir);
    std::erase_if(scan.mods, [](const ModEntry& mod) { return mod.cheat_file; });
    return scan;
}

// The discovery rules, shared by the real scan and the controls. False on a breach.
template <class Scan>
bool ScanRulesHold(const fs::path& root, Scan scan) {
    fs::create_directories(root / "0100000000000B01" / "zeta");
    fs::create_directories(root / "0100000000000B01" / "alpha");
    fs::create_directories(root / "01000000000000AA" / "mid");
    // A flat cheat file counts as a mod, as the patch manager loads it; other files do not.
    MakeFile(root / "0100000000000B01" / "cheat_x.txt");
    MakeFile(root / "0100000000000B01" / "notes.txt");
    MakeFile(root / "readme.txt");
    const ModsScan found = scan(root);
    const std::vector<ModEntry> expected{{"01000000000000AA", "mid", false},
                                         {"0100000000000B01", "alpha", false},
                                         {"0100000000000B01", "cheat_x.txt", true},
                                         {"0100000000000B01", "zeta", false}};
    return found.mods == expected && !found.truncated && found.errors.empty();
}

int ScanFollowsTheLoaderRules(const fs::path& scratch) {
    CHECK(ScanRulesHold(scratch / "real", [](const fs::path& p) { return ScanMods(p); }));
    CHECK(!ScanRulesHold(scratch / "everything", ListEverything));
    CHECK(!ScanRulesHold(scratch / "folders-only", FoldersOnly));
    return 0;
}

int MissingIsQuietButUnreadableIsReported(const fs::path& scratch) {
    const ModsScan missing = ScanMods(scratch / "does-not-exist");
    CHECK(missing.directory_missing && missing.mods.empty() && missing.errors.empty());

    fs::create_directories(scratch / "empty");
    const ModsScan empty = ScanMods(scratch / "empty");
    CHECK(!empty.directory_missing && empty.mods.empty() && empty.errors.empty());

    // A file where the folder should be is a problem to report, not an empty folder.
    MakeFile(scratch / "a-file");
    const ModsScan not_a_folder = ScanMods(scratch / "a-file");
    CHECK(!not_a_folder.directory_missing && not_a_folder.mods.empty());
    CHECK(not_a_folder.errors.size() == 1 &&
          not_a_folder.errors[0].find("not a folder") != std::string::npos);
    return 0;
}

int NonAsciiNamesSurvive(const fs::path& scratch) {
    const fs::path root = scratch / "unicode";
    fs::create_directories(root / "0100000000000B01" / fs::path(u8"\u30E2\u30C3\u30C9 mod"));
    const ModsScan found = ScanMods(root);
    CHECK(found.mods.size() == 1);
    CHECK(found.mods[0].mod_name == "\xE3\x83\xA2\xE3\x83\x83\xE3\x83\x89 mod");
    return 0;
}

int ListIsBounded(const fs::path& scratch) {
    const fs::path root = scratch / "many" / "0100000000000B01";
    for (std::size_t i = 0; i < kMaxListedMods + 3; ++i) {
        fs::create_directories(root / ("mod" + std::to_string(1000 + i)));
    }
    const ModsScan found = ScanMods(scratch / "many");
    CHECK(found.mods.size() == kMaxListedMods && found.truncated);
    CHECK(found.mods.front().mod_name == "mod1000");
    return 0;
}

// A scan that waits until the test lets it finish.
struct GatedScan {
    std::mutex mutex;
    std::condition_variable changed;
    bool open{};
    int started{};
    int finished{};

    ModsScan operator()(const fs::path& dir) {
        std::unique_lock lock{mutex};
        ++started;
        changed.notify_all();
        changed.wait(lock, [this] { return open; });
        ++finished;
        ModsScan scan;
        scan.mods.push_back({Utf8(dir), "m", false});
        return scan;
    }
    void WaitStarted(int count) {
        std::unique_lock lock{mutex};
        changed.wait(lock, [&] { return started >= count; });
    }
    void Open() {
        std::lock_guard lock{mutex};
        open = true;
        changed.notify_all();
    }
};

// Negative control: scans on the calling thread.
class InlineScanner {
public:
    explicit InlineScanner(ModsScanner::ScanFunction scan_function)
        : scan(std::move(scan_function)) {}
    void Request(fs::path dir, ModsScanner::DoneCallback done) {
        done(scan(dir));
    }

private:
    ModsScanner::ScanFunction scan;
};

// Request must come back while the scan is still running. True when it does.
template <class Scanner>
bool RequestDoesNotWait(Scanner& scanner, GatedScan& gate) {
    std::atomic<bool> returned{false};
    std::thread caller([&] {
        scanner.Request("first", [](ModsScan) {});
        returned = true;
    });
    // The scan is blocked on the gate, so a scanner that runs it inline cannot return.
    for (int i = 0; i < 100 && !returned; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool did_not_wait = returned;
    gate.Open();
    caller.join();
    return did_not_wait;
}

int RequestsReturnAtOnceAndCoalesce() {
    {
        GatedScan gate;
        ModsScanner scanner{[&gate](const fs::path& dir) { return gate(dir); }};
        CHECK(RequestDoesNotWait(scanner, gate));
    }
    {
        GatedScan gate;
        InlineScanner inline_scanner{[&gate](const fs::path& dir) { return gate(dir); }};
        CHECK(!RequestDoesNotWait(inline_scanner, gate));
    }

    // Five requests during one running scan produce the running scan and one follow-up with
    // the newest folder; the results arrive in request order.
    GatedScan gate;
    std::mutex result_mutex;
    std::vector<std::string> results;
    {
        ModsScanner scanner{[&gate](const fs::path& dir) { return gate(dir); }};
        const auto collect = [&](ModsScan scan) {
            std::lock_guard lock{result_mutex};
            results.push_back(scan.mods.front().title_id_folder);
        };
        scanner.Request("r0", collect);
        gate.WaitStarted(1);
        for (int i = 1; i <= 5; ++i) {
            scanner.Request("r" + std::to_string(i), collect);
        }
        gate.Open();
        // The destructor drops work that has not started, so wait for the follow-up first.
        for (int i = 0; i < 400; ++i) {
            {
                std::lock_guard lock{result_mutex};
                if (results.size() >= 2) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK((results == std::vector<std::string>{"r0", "r5"}));
    CHECK(gate.started == 2 && gate.finished == 2);
    return 0;
}

int DestructorWaitsForARunningScan() {
    GatedScan gate;
    std::atomic<bool> delivered{false};
    std::thread opener;
    {
        ModsScanner scanner{[&gate](const fs::path& dir) { return gate(dir); }};
        scanner.Request("x", [&](ModsScan) { delivered = true; });
        gate.WaitStarted(1);
        opener = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            gate.Open();
        });
    } // the destructor blocks until the opener has let the scan finish
    opener.join();
    // The callback ran before the scanner went away: no call can outlive its owner.
    CHECK(delivered);
    return 0;
}

int FileUrlEncodesWhatItMust() {
    CHECK(FileUrlFromPath("C:\\Users\\a b\\mods") == "file:///C:/Users/a%20b/mods");
    CHECK(FileUrlFromPath("/home/me/my mods") == "file:///home/me/my%20mods");
    CHECK(FileUrlFromPath("/a/\xE3\x83\xA2#50%") == "file:///a/%E3%83%A2%2350%25");
    CHECK(FileUrlFromPath("") == "file:///");
    return 0;
}

// Removes only the folder this run created and says so if it cannot.
class ScratchFolder {
public:
    explicit ScratchFolder(const fs::path& base) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = base / ("mods_folders_unit_" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~ScratchFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
        if (ec) {
            std::fprintf(stderr, "cleanup of %s failed: %s\n", path.string().c_str(),
                         ec.message().c_str());
        }
    }
    const fs::path& Path() const {
        return path;
    }

private:
    fs::path path;
};

} // namespace

int main(int argc, char** argv) {
    const fs::path base = argc >= 2 ? fs::path(argv[1]) : fs::temp_directory_path();
    int result = 0;
    {
        const ScratchFolder scratch{base};
        result |= ScanFollowsTheLoaderRules(scratch.Path());
        result |= MissingIsQuietButUnreadableIsReported(scratch.Path());
        result |= NonAsciiNamesSurvive(scratch.Path());
        result |= ListIsBounded(scratch.Path());
    }
    result |= RequestsReturnAtOnceAndCoalesce();
    result |= DestructorWaitsForARunningScan();
    result |= FileUrlEncodesWhatItMust();
    if (result != 0) {
        return 1;
    }
    std::puts("All synthetic MODS FOLDERS smoke checks passed.");
    return 0;
}
