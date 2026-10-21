// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// smoke_aot_cache_unit: the persistent AOT export cache (suyu/aot_export_cache.h) on its own:
// entry naming, producer identity, per-entry lock, staging, atomic publish/replace and the
// validation reasons that name the mismatched item.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

#include "suyu/aot_export_cache.h"

namespace C = suyu::aot_cache;
namespace fs = std::filesystem;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

const C::EntryKey kSplitKey{"0100abc000000000", "windows", "suyu-static", true};
const C::EntryKey kStaticKey{"0100abc000000000", "windows", "suyu-static", false};
const C::ProducerIdentity kProducer{"abc1234", "refs/heads/main"};

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void WriteFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Fills the staging directory with the artifacts of an entry; `tag` tells two builds apart.
void FillStaging(const fs::path& staging, const C::EntryKey& key, const std::string& tag) {
    WriteFile(staging / C::kLauncherDirName / C::kLauncherExecutableName, "launcher " + tag);
    WriteFile(staging / C::kManifestFileName, "manifest " + tag);
    if (key.split_main) {
        WriteFile(staging / C::kModuleSelectionFileName, "selection " + tag);
    }
}

int Naming() {
    std::string error;
    CHECK(C::EntryName(kSplitKey, &error) == "0100abc000000000_windows_suyu-static_split");
    CHECK(C::EntryName(kStaticKey, &error) == "0100abc000000000_windows_suyu-static_all-static");
    CHECK(C::EntryPath("root", kSplitKey, &error) ==
          fs::path("root") / "0100abc000000000_windows_suyu-static_split");
    // Distinct keys never share a name.
    auto other_backend = kSplitKey;
    other_backend.backend = "suyu-hybrid";
    CHECK(C::EntryName(other_backend, &error) != C::EntryName(kSplitKey, &error));

    // A no-JIT launcher has its own entry, and every backend name is a safe key part.
    CHECK(C::BackendName(false, true) == "suyu-static");
    CHECK(C::BackendName(false, false) == "suyu-static-nojit");
    CHECK(C::BackendName(true, true) == "suyu-hybrid");
    auto no_jit = kSplitKey;
    no_jit.backend = C::BackendName(false, false);
    CHECK(C::EntryName(no_jit, &error) == "0100abc000000000_windows_suyu-static-nojit_split");
    CHECK(C::EntryName(no_jit, &error) != C::EntryName(kSplitKey, &error));

    // Each unsafe part is refused with an error that names the field.
    auto bad = kSplitKey;
    bad.title_id_hex = "0100ABC000000000";
    CHECK(!C::EntryName(bad, &error) && Contains(error, "title_id"));
    bad = kSplitKey;
    bad.title_id_hex = "0100abc0";
    CHECK(!C::EntryName(bad, &error) && Contains(error, "title_id"));
    bad = kSplitKey;
    bad.platform = "../windows";
    CHECK(!C::EntryName(bad, &error) && Contains(error, "platform"));
    bad = kSplitKey;
    bad.backend = "";
    CHECK(!C::EntryName(bad, &error) && Contains(error, "backend"));
    bad = kSplitKey;
    bad.backend = "a\\b";
    CHECK(!C::EntryName(bad, &error) && Contains(error, "backend"));
    return 0;
}

int Producer() {
    const C::ProducerIdentity other{"def5678", "refs/heads/dev"};
    CHECK(C::EffectiveProducer(kProducer, std::nullopt) == kProducer);
    CHECK(C::EffectiveProducer(kProducer, other) == other);
    CHECK(!(kProducer == other));
    return 0;
}

int Lock(const fs::path& root) {
    std::string error;
    bool contended = false;
    auto first = C::EntryLock::TryAcquire(root, kSplitKey, &contended, &error);
    CHECK(first && !contended);
    // The same entry is busy; the refusal says so and is not a creation failure.
    auto second = C::EntryLock::TryAcquire(root, kSplitKey, &contended, &error);
    CHECK(!second && contended && Contains(error, "another export"));
    // A different entry is independent.
    auto other = C::EntryLock::TryAcquire(root, kStaticKey, &contended, &error);
    CHECK(other && !contended);
    // Released with the object.
    first.reset();
    auto third = C::EntryLock::TryAcquire(root, kSplitKey, &contended, &error);
    CHECK(third && !contended);
    // An invalid key is an error, not contention.
    auto bad = kSplitKey;
    bad.platform = "";
    CHECK(!C::EntryLock::TryAcquire(root, bad, &contended, &error) && !contended &&
          Contains(error, "platform"));
    return 0;
}

// A cache root the ANSI code page cannot represent: a refused lock reports it without throwing.
int NonAsciiRoot(const fs::path& work) {
    const fs::path root = work / fs::path(u8"cache-\U0001F600-\u65e5\u672c");
    std::string error;
    bool contended = false;
    auto first = C::EntryLock::TryAcquire(root, kSplitKey, &contended, &error);
    CHECK(first && !contended);
    try {
        auto second = C::EntryLock::TryAcquire(root, kSplitKey, &contended, &error);
        CHECK(!second && contended && Contains(error, "another export"));
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "FAIL: threw for a non-ANSI root: %s\n", exception.what());
        return 1;
    }
    return 0;
}

int PublishAndReplace(const fs::path& root) {
    std::string error;
    bool contended = false;
    auto lock = C::EntryLock::TryAcquire(root, kSplitKey, &contended, &error);
    CHECK(lock);
    const auto entry = *C::EntryPath(root, kSplitKey, &error);
    const std::string entry_name = *C::EntryName(kSplitKey, &error);
    std::string mismatch;
    CHECK(!C::ValidateEntry(entry, kSplitKey, kProducer, &mismatch) &&
          Contains(mismatch, C::kEntryRecordFileName));

    // An incomplete staging tree is refused and nothing is published.
    auto staging = C::PrepareStaging(*lock, root, kSplitKey, &error);
    CHECK(staging && fs::is_directory(*staging));
    WriteFile(*staging / C::kManifestFileName, "manifest only");
    CHECK(!C::PublishEntry(*lock, root, kSplitKey, kProducer, &error) &&
          Contains(error, C::kLauncherDirName));
    CHECK(!fs::exists(entry));

    // An empty launcher directory is not a launcher.
    fs::create_directories(*staging / C::kLauncherDirName);
    CHECK(!C::PublishEntry(*lock, root, kSplitKey, kProducer, &error) &&
          Contains(error, C::kLauncherExecutableName));
    // A split entry needs its module selection.
    WriteFile(*staging / C::kLauncherDirName / C::kLauncherExecutableName, "launcher");
    CHECK(!C::PublishEntry(*lock, root, kSplitKey, kProducer, &error) &&
          Contains(error, "recomp_modules.cmake"));

    // Publish the first version.
    staging = C::PrepareStaging(*lock, root, kSplitKey, &error);
    CHECK(staging && !fs::exists(*staging / C::kManifestFileName));  // cleared
    FillStaging(*staging, kSplitKey, "one");
    CHECK(C::PublishEntry(*lock, root, kSplitKey, kProducer, &error));
    CHECK(!fs::exists(*staging));
    CHECK(C::ValidateEntry(entry, kSplitKey, kProducer, &mismatch));
    CHECK(ReadFile(entry / C::kManifestFileName) == "manifest one");
    CHECK(C::EntrySizeBytes(entry) > 0);

    // Producer mismatches name the differing field.
    CHECK(!C::ValidateEntry(entry, kSplitKey, {"zzz", kProducer.build_id}, &mismatch) &&
          mismatch == "producer.suyu_revision");
    CHECK(!C::ValidateEntry(entry, kSplitKey, {kProducer.suyu_revision, "zzz"}, &mismatch) &&
          mismatch == "producer.build_id");
    // The entry belongs to its key: another key's validation names the entry field.
    CHECK(!C::ValidateEntry(entry, kStaticKey, kProducer, &mismatch) && mismatch == "entry");

    // Replace in place: the new version wins, nothing of the old one or a trash tree remains.
    staging = C::PrepareStaging(*lock, root, kSplitKey, &error);
    CHECK(staging);
    FillStaging(*staging, kSplitKey, "two");
    WriteFile(*staging / C::kLauncherDirName / "extra.dll", "x");
    const C::ProducerIdentity next{"def5678", kProducer.build_id};
    CHECK(C::PublishEntry(*lock, root, kSplitKey, next, &error));
    CHECK(C::ValidateEntry(entry, kSplitKey, next, &mismatch));
    CHECK(!C::ValidateEntry(entry, kSplitKey, kProducer, &mismatch));
    CHECK(ReadFile(entry / C::kManifestFileName) == "manifest two");
    CHECK(fs::exists(entry / C::kLauncherDirName / "extra.dll"));
    for (const auto& item : fs::directory_iterator(root)) {
        CHECK(item.path().filename().string().rfind(".trash-", 0) != 0);
        CHECK(item.path().filename().string().rfind(".staging-", 0) != 0);
    }

    // The launcher image is required by validation, not only by publish.
    {
        const auto launcher = entry / C::kLauncherDirName / C::kLauncherExecutableName;
        const auto saved = ReadFile(launcher);
        fs::remove(launcher);
        CHECK(!C::ValidateEntry(entry, kSplitKey, next, &mismatch) &&
              Contains(mismatch, C::kLauncherExecutableName));
        WriteFile(launcher, saved);
    }

    // Tampering with a published entry is caught by validation.
    fs::remove(entry / C::kManifestFileName);
    CHECK(!C::ValidateEntry(entry, kSplitKey, next, &mismatch) &&
          Contains(mismatch, C::kManifestFileName));

    // Leftovers of an interrupted run are cleared by the next PrepareStaging.
    WriteFile(root / (".trash-" + entry_name) / "old", "old");
    WriteFile(root / (".staging-" + entry_name) / "half", "half");
    staging = C::PrepareStaging(*lock, root, kSplitKey, &error);
    CHECK(staging && !fs::exists(*staging / "half"));
    CHECK(!fs::exists(root / (".trash-" + entry_name)));

    // A crash between "old entry set aside" and "new entry in place" leaves only the trash
    // tree; the next PrepareStaging restores it instead of deleting the only complete copy.
    fs::rename(entry, root / (".trash-" + entry_name));
    CHECK(!fs::exists(entry));
    staging = C::PrepareStaging(*lock, root, kSplitKey, &error);
    CHECK(staging && fs::exists(entry / C::kLauncherDirName / C::kLauncherExecutableName) &&
          !fs::exists(root / (".trash-" + entry_name)));

    // An all-static entry publishes without a module selection.
    auto static_lock = C::EntryLock::TryAcquire(root, kStaticKey, &contended, &error);
    CHECK(static_lock);
    staging = C::PrepareStaging(*static_lock, root, kStaticKey, &error);
    CHECK(staging);
    FillStaging(*staging, kStaticKey, "static");
    CHECK(C::PublishEntry(*static_lock, root, kStaticKey, kProducer, &error));
    const auto static_entry = *C::EntryPath(root, kStaticKey, &error);
    CHECK(C::ValidateEntry(static_entry, kStaticKey, kProducer, &mismatch));

    // A producer identity that would corrupt the record is refused before anything moves.
    staging = C::PrepareStaging(*static_lock, root, kStaticKey, &error);
    CHECK(staging);
    FillStaging(*staging, kStaticKey, "bad");
    CHECK(!C::PublishEntry(*static_lock, root, kStaticKey, {"a\nb", "x"}, &error) &&
          Contains(error, "line break"));
    CHECK(ReadFile(static_entry / C::kManifestFileName) == "manifest static");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: smoke_aot_cache_unit <work directory>\n");
        return 2;
    }
    const fs::path work = argv[1];
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work);
    if (const int result = Naming()) {
        return result;
    }
    if (const int result = Producer()) {
        return result;
    }
    if (const int result = Lock(work / "lock")) {
        return result;
    }
    if (const int result = NonAsciiRoot(work / "lock")) {
        return result;
    }
    if (const int result = PublishAndReplace(work / "publish")) {
        return result;
    }
    std::printf("aot_cache_unit: ok\n");
    return 0;
}
