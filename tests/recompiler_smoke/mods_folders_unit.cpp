// smoke_mods_folders_unit: mods/ discovery and the file URL of a folder
// (suyu_cmd/overlay/mods_folders.h), which need neither ImGui nor SDL.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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

// Negative control: lists every entry, files included, in directory order.
ModsScan NaiveScan(const fs::path& mods_dir) {
    ModsScan scan;
    std::error_code ec;
    for (const auto& title : fs::directory_iterator(mods_dir, ec)) {
        for (const auto& mod : fs::directory_iterator(title.path(), ec)) {
            scan.mods.push_back({Utf8(title.path().filename()), Utf8(mod.path().filename())});
        }
    }
    return scan;
}

// The discovery rules, shared by the real scan and the control. Returns false on a breach.
template <class Scan>
bool ScanRulesHold(const fs::path& root, Scan scan) {
    fs::create_directories(root / "0100000000000B01" / "zeta");
    fs::create_directories(root / "0100000000000B01" / "alpha");
    fs::create_directories(root / "01000000000000AA" / "mid");
    // A file where a mod folder belongs, and a stray file beside the title folders.
    MakeFile(root / "0100000000000B01" / "notes.txt");
    MakeFile(root / "readme.txt");
    const ModsScan found = scan(root);
    const std::vector<ModEntry> expected{
        {"01000000000000AA", "mid"}, {"0100000000000B01", "alpha"}, {"0100000000000B01", "zeta"}};
    return found.mods == expected && !found.truncated;
}

int ScanListsOnlyFoldersInOrder(const fs::path& scratch) {
    CHECK(ScanRulesHold(scratch / "real", [](const fs::path& p) { return ScanMods(p); }));
    CHECK(!ScanRulesHold(scratch / "naive", [](const fs::path& p) { return NaiveScan(p); }));
    return 0;
}

int MissingOrEmptyFolderIsQuiet(const fs::path& scratch) {
    const ModsScan missing = ScanMods(scratch / "does-not-exist");
    CHECK(missing.directory_missing && missing.mods.empty() && !missing.truncated);

    fs::create_directories(scratch / "empty");
    const ModsScan empty = ScanMods(scratch / "empty");
    CHECK(!empty.directory_missing && empty.mods.empty());

    // A file where the folder should be is treated as missing, not as an error.
    MakeFile(scratch / "a-file");
    CHECK(ScanMods(scratch / "a-file").directory_missing);
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
    // The cut keeps the sorted prefix.
    CHECK(found.mods.front().mod_name == "mod1000");
    return 0;
}

int FileUrlEncodesWhatItMust() {
    CHECK(FileUrlFromPath("C:\\Users\\a b\\mods") == "file:///C:/Users/a%20b/mods");
    CHECK(FileUrlFromPath("/home/me/my mods") == "file:///home/me/my%20mods");
    // Bytes of a UTF-8 name are encoded one by one, and '%' and '#' cannot pass through.
    CHECK(FileUrlFromPath("/a/\xE3\x83\xA2#50%") == "file:///a/%E3%83%A2%2350%25");
    CHECK(FileUrlFromPath("") == "file:///");
    return 0;
}

} // namespace

int main() {
    const fs::path scratch = fs::temp_directory_path() / "aevora_mods_folders_unit";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch);

    int result = 0;
    result |= ScanListsOnlyFoldersInOrder(scratch);
    result |= MissingOrEmptyFolderIsQuiet(scratch);
    result |= NonAsciiNamesSurvive(scratch);
    result |= ListIsBounded(scratch);
    result |= FileUrlEncodesWhatItMust();

    fs::remove_all(scratch, ec);
    if (result != 0) {
        return 1;
    }
    std::puts("All synthetic MODS FOLDERS smoke checks passed.");
    return 0;
}
