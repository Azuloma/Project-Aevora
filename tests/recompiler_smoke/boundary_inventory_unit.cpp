// smoke_boundary_inventory_unit: the SUYU_BOUNDARY_INVENTORY recorder (core/boundary_inventory.h).
// Driven directly with an injected clock; negative controls drop the behavior a check relies on.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "core/boundary_inventory.h"
#include "core/boundary_inventory_scan.h"

using Core::BoundaryInventory::FsKind;
using Core::BoundaryInventory::Recorder;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr std::uint64_t kSecondNs = 1'000'000'000ULL;
constexpr std::uint64_t kMillisecondNs = 1'000'000ULL;

bool Contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

// Report after an ioctl on fd 7; the weakened model skips the Close that ends the fd's lifetime.
std::string ReportForIoctlOnClosedFd(bool close_fd) {
    Recorder recorder;
    recorder.RecordNvdrvOpen(7, "/dev/nvhost-gpu");
    recorder.RecordNvdrvIoctl(7, 1, 0x1);
    if (close_fd) {
        recorder.RecordNvdrvClose(7);
    }
    recorder.RecordNvdrvIoctl(7, 1, 0x2);
    return recorder.FormatReport();
}

int CountsSvcsPerCoreAndOverflow() {
    Recorder recorder;
    recorder.RecordSvc(0, 0x19);
    recorder.RecordSvc(1, 0x19);
    recorder.RecordSvc(1, 0x19);
    recorder.RecordSvc(2, 0x1F);
    recorder.RecordSvc(0, 0x100);
    recorder.RecordSvc(Core::BoundaryInventory::kMaxCores, 0x01);
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "[svc]\n0x19\t3\n0x1F\t1\noverflow\t2\n"));
    return 0;
}

int RecordsIpcKeysAndUnimplementedFlag() {
    Recorder recorder;
    recorder.RecordIpc("hid", 4, 100);
    recorder.RecordIpc("hid", 4, 100);
    recorder.RecordIpc("hid", 4, 101);
    recorder.RecordIpc("fsp-srv", 4, 18);
    recorder.RecordIpc("hid", 4, 101, true);
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "[ipc]\nfsp-srv\t4\t18\t1\t0\t2\nhid\t4\t100\t2\t0\t0\nhid\t4\t101\t2\t1\t1\n"));
    return 0;
}

int NvdrvDropsDeviceNameAtClose() {
    const std::string report = ReportForIoctlOnClosedFd(true);
    CHECK(Contains(report, "ioctl	/dev/nvhost-gpu	1	0x00000001	1	"));
    CHECK(Contains(report, "ioctl	<unknown fd>	1	0x00000002	1	"));
    Recorder reused;
    reused.RecordNvdrvOpen(7, "/dev/nvhost-gpu");
    reused.RecordNvdrvClose(7);
    reused.RecordNvdrvOpen(7, "/dev/nvmap");
    reused.RecordNvdrvIoctl(7, 2, 0x3);
    CHECK(Contains(reused.FormatReport(), "ioctl	/dev/nvmap	2	0x00000003	1	"));
    return 0;
}

// The weakened model (Close skipped) must be caught by the closed-fd check above.
int NvdrvNegativeControlIsDetected() {
    const std::string weakened = ReportForIoctlOnClosedFd(false);
    CHECK(!Contains(weakened, "<unknown fd>"));
    CHECK(Contains(weakened, "ioctl	/dev/nvhost-gpu	1	0x00000002	1	"));
    return 0;
}

int AggregatesAudioAppletAndFilesystem() {
    Recorder recorder;
    recorder.RecordAudioOpen("sample_rate=48000");
    recorder.RecordAudioUpdate(100, 50, 10);
    recorder.RecordAudioUpdate(40, 80, 5);
    recorder.RecordApplet(3);
    recorder.RecordFsOpen(FsKind::RomFsCurrentProcess, "/a/b.bin", 1);
    recorder.RecordFsRead("/a/b.bin", 0, 16);
    recorder.RecordFsRead("/a/b.bin", 100, 28);
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "open\tsample_rate=48000\t1\n"));
    CHECK(Contains(report, "update\tcount=2\tinput_min=40\tinput_max=100\toutput_max=80\tperformance_max=10\n"));
    CHECK(Contains(report, "[applet]\n3\t1\t0\n"));
    CHECK(Contains(report, "[fs-open]\nromfs_current_process\t/a/b.bin\t1\t1\t0\n"));
    CHECK(Contains(report, "[fs-read]\n/a/b.bin\t2\t44\t128\t0\n"));
    CHECK(recorder.DistinctKeys() == 5);
    return 0;
}

int FormatsDeterministically() {
    Recorder first;
    Recorder second;
    first.RecordIpc("b", 4, 2);
    first.RecordIpc("a", 4, 1);
    second.RecordIpc("b", 4, 2);
    second.RecordIpc("a", 4, 1);
    CHECK(first.FormatReport() == second.FormatReport());
    return 0;
}

int LimitsWritesToOncePerInterval() {
    Recorder recorder;
    CHECK(recorder.TryTakeWriteSlot(5 * kSecondNs));
    CHECK(!recorder.TryTakeWriteSlot(5 * kSecondNs + 1));
    CHECK(!recorder.TryTakeWriteSlot(14 * kSecondNs));
    CHECK(recorder.TryTakeWriteSlot(15 * kSecondNs));
    CHECK(!recorder.TryTakeWriteSlot(16 * kSecondNs));
    return 0;
}

int RejectsStaleClockSample() {
    Recorder recorder;
    CHECK(recorder.TryTakeWriteSlot(5 * kSecondNs + 1));
    CHECK(!recorder.TryTakeWriteSlot(5 * kSecondNs));
    CHECK(!recorder.TryTakeWriteSlot(14 * kSecondNs));
    CHECK(recorder.TryTakeWriteSlot(15 * kSecondNs + 1));
    return 0;
}

int CountsConcurrentRecordsExactly() {
    Recorder recorder;
    constexpr int kPerThread = 20000;
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < 3; ++core) {
        threads.emplace_back([&recorder, core] {
            for (int n = 0; n < kPerThread; ++n) {
                recorder.RecordSvc(core, 0x20);
                recorder.RecordIpc("hid", 4, 100);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "0x20\t60000\n"));
    CHECK(Contains(report, "hid\t4\t100\t60000\t0\t0\n"));
    return 0;
}

// The number that follows `prefix` in `text`, or UINT64_MAX when the prefix is absent.
std::uint64_t NumberAfter(const std::string& text, const std::string& prefix) {
    const auto at = text.find(prefix);
    if (at == std::string::npos) {
        return UINT64_MAX;
    }
    return std::stoull(text.substr(at + prefix.size()));
}

int SegmentsAreIndependent() {
    Recorder recorder;
    for (int n = 0; n < 3; ++n) {
        recorder.RecordSvc(0, 0x19);
    }
    recorder.RecordSvc(0, 0x100);
    recorder.RecordIpc("hid", 4, 100);
    recorder.RecordIpc("hid", 4, 100);
    recorder.RecordFsOpen(FsKind::Sdmc, "/a", 1);
    const std::string first = recorder.EndSegment("title", 1500 * kMillisecondNs);
    CHECK(Contains(first, "# boundary inventory v1\n# segment label=title elapsed_ms=1500\n"));
    CHECK(Contains(first, "[svc]\n0x19\t3\noverflow\t1\n"));
    CHECK(Contains(first, "hid\t4\t100\t2\t0\t0\n"));
    CHECK(Contains(first, "[fs-open]\nsdmc\t/a\t1\t1\t0\n"));

    recorder.RecordSvc(1, 0x19);
    recorder.RecordIpc("hid", 4, 100);
    const std::string second = recorder.EndSegment("village", 0);
    CHECK(Contains(second, "[svc]\n0x19\t1\noverflow\t0\n"));
    CHECK(Contains(second, "hid\t4\t100\t1\t0\t0\n"));
    CHECK(!Contains(second, "sdmc"));

    const std::string open_segment = recorder.FormatReport();
    CHECK(Contains(open_segment, "[svc]\noverflow\t0\n[ipc]\n[nvdrv]\n[audio]\n[applet]\n"
                                 "[fs-open]\n[fs-read]\n"));
    return 0;
}

int PauseFreezesCountersButNotFdMap() {
    Recorder recorder;
    recorder.SetRecording(false);
    CHECK(!recorder.IsRecording());
    recorder.RecordSvc(0, 0x19);
    recorder.RecordIpc("hid", 4, 100);
    recorder.RecordFsRead("/a", 0, 16);
    recorder.RecordNvdrvOpen(7, "/dev/nvmap");
    recorder.RecordNvdrvIoctl(7, 1, 0x1);
    CHECK(Contains(recorder.FormatReport(), "[svc]\noverflow\t0\n[ipc]\n[nvdrv]\n[audio]\n"
                                            "[applet]\n[fs-open]\n[fs-read]\n"));
    recorder.SetRecording(true);
    recorder.RecordNvdrvIoctl(7, 1, 0x2);
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "ioctl\t/dev/nvmap\t1\t0x00000002\t1\t0\n"));
    CHECK(!Contains(report, "open\t/dev/nvmap"));
    return 0;
}

int FdMapSurvivesSegmentEnd() {
    Recorder recorder;
    recorder.RecordNvdrvOpen(7, "/dev/nvmap");
    recorder.EndSegment("boot", 0);
    recorder.RecordNvdrvIoctl(7, 1, 0x3);
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "ioctl\t/dev/nvmap\t1\t0x00000003\t1\t0\n"));
    CHECK(!Contains(report, "<unknown fd>"));
    return 0;
}

int ConcurrentMarksLoseNothing() {
    Recorder recorder;
    constexpr int kThreads = 3;
    constexpr int kPerThread = 20000;
    std::atomic<int> running{kThreads};
    std::vector<std::thread> threads;
    for (std::size_t core = 0; core < kThreads; ++core) {
        threads.emplace_back([&recorder, &running, core] {
            for (int n = 0; n < kPerThread; ++n) {
                recorder.RecordSvc(core, 0x20);
                recorder.RecordIpc("hid", 4, 100);
            }
            running.fetch_sub(1);
        });
    }
    std::uint64_t svc_sum = 0;
    std::uint64_t ipc_sum = 0;
    const auto add_segment = [&](const std::string& segment) {
        if (const auto svc = NumberAfter(segment, "0x20\t"); svc != UINT64_MAX) {
            svc_sum += svc;
        }
        if (const auto ipc = NumberAfter(segment, "hid\t4\t100\t"); ipc != UINT64_MAX) {
            ipc_sum += ipc;
        }
    };
    while (running.load() > 0) {
        add_segment(recorder.EndSegment("running", 0));
    }
    for (auto& thread : threads) {
        thread.join();
    }
    add_segment(recorder.EndSegment("last", 0));
    CHECK(svc_sum == static_cast<std::uint64_t>(kThreads) * kPerThread);
    CHECK(ipc_sum == static_cast<std::uint64_t>(kThreads) * kPerThread);
    return 0;
}

int NamesAreSafeFileComponents() {
    using Core::BoundaryInventory::OpenReportFileName;
    using Core::BoundaryInventory::SanitizeFileComponent;
    using Core::BoundaryInventory::SegmentFileName;
    CHECK(SegmentFileName("run1", 3, "battle") == "inventory_run1_03_battle.txt");
    CHECK(SegmentFileName("run1", 123, "battle") == "inventory_run1_123_battle.txt");
    CHECK(OpenReportFileName("run1") == "inventory_run1_open.txt");
    // Separators, dots and non-ASCII bytes never reach the file name.
    CHECK(SanitizeFileComponent("..\\a/b c.txt", "x") == "___a_b_c_txt");
    CHECK(SanitizeFileComponent("", "fallback") == "fallback");
    CHECK(SegmentFileName("", 1, "") == "inventory_run_01_segment.txt");
    const std::string long_name(200, 'a');
    CHECK(SanitizeFileComponent(long_name, "x").size() ==
          Core::BoundaryInventory::kMaxFileComponentLength);
    return 0;
}

// Guest memory as a map of 8-byte words; unmapped addresses cannot be read.
struct FakeGuestMemory {
    std::map<std::uint64_t, std::uint64_t> words;
    bool operator()(std::uint64_t address, std::uint64_t& value) const {
        const auto it = words.find(address);
        if (it == words.end()) {
            return false;
        }
        value = it->second;
        return true;
    }
};

Core::BoundaryInventory::CallerFrames Frames(std::uint64_t a, std::uint64_t b = 0,
                                              std::uint64_t c = 0, std::uint64_t d = 0) {
    return {a, b, c, d};
}

int SvcInstructionIsRecognized() {
    using Core::BoundaryInventory::IsSvcInstruction;
    CHECK(IsSvcInstruction(0xD4000421u, 0x21));
    CHECK(!IsSvcInstruction(0xD4000421u, 0x22));
    // HVC #0x21 and a NOP share neighboring bits but are not an SVC.
    CHECK(!IsSvcInstruction(0xD4000422u, 0x21));
    CHECK(!IsSvcInstruction(0xD503201Fu, 0x21));
    return 0;
}

int CallerChainFollowsTheFramePointer() {
    FakeGuestMemory memory;
    memory.words = {{0x1000, 0x2000}, {0x1008, 0xAAA0}, {0x2000, 0x3000}, {0x2008, 0xBBB0}};
    const auto frames = Core::BoundaryInventory::CollectCallerFrames(0x4FFC, 0x9990, 0x1000, memory);
    CHECK(frames == Frames(0x4FFC, 0x9990, 0xAAA0, 0xBBB0));
    return 0;
}

int CallerChainStopsAtTheFirstBadFrame() {
    using Core::BoundaryInventory::CollectCallerFrames;
    FakeGuestMemory memory;
    memory.words = {{0x1000, 0x2000}, {0x1008, 0xAAA0}, {0x2000, 0x3000}, {0x2008, 0xBBB0}};
    // No frame pointer, a misaligned one (whose words are readable), and an unreadable one give
    // only the first two entries.
    memory.words[0x1004] = 0x2000;
    memory.words[0x100C] = 0xAAA0;
    CHECK(CollectCallerFrames(1, 2, 0, memory) == Frames(1, 2));
    CHECK(CollectCallerFrames(1, 2, 0x1004, memory) == Frames(1, 2));
    CHECK(CollectCallerFrames(1, 2, 0x7000, memory) == Frames(1, 2));
    // A zero return address ends the walk.
    memory.words[0x1008] = 0;
    CHECK(CollectCallerFrames(1, 2, 0x1000, memory) == Frames(1, 2));
    memory.words[0x1008] = 0xAAA0;
    // A next frame that does not climb (a loop or a downward link) ends it after one more frame.
    memory.words[0x1000] = 0x1000;
    CHECK(CollectCallerFrames(1, 2, 0x1000, memory) == Frames(1, 2, 0xAAA0));
    memory.words[0x1000] = 0x800;
    CHECK(CollectCallerFrames(1, 2, 0x1000, memory) == Frames(1, 2, 0xAAA0));
    return 0;
}

int ModulesResolveAddresses() {
    Recorder recorder;
    recorder.SetModules({{"sdk", 0x2000, 0x3000}, {"main", 0x1000, 0x2000}});
    CHECK(recorder.Describe(0x1000) == "main+0x0");
    CHECK(recorder.Describe(0x1FFF) == "main+0xFFF");
    CHECK(recorder.Describe(0x2000) == "sdk+0x0");
    CHECK(recorder.Describe(0x3000) == "?0x3000");
    CHECK(recorder.Describe(0xFFF) == "?0xFFF");
    CHECK(recorder.Describe(0) == "-");
    CHECK(recorder.ClaimModuleScan());
    CHECK(!recorder.ClaimModuleScan());
    return 0;
}

int CallersKeepTheFrequentOnes() {
    Recorder recorder;
    for (int n = 0; n < 3; ++n) {
        recorder.RecordCaller("svc:0x21", Frames(1));
    }
    for (int n = 0; n < 2; ++n) {
        recorder.RecordCaller("svc:0x21", Frames(2));
    }
    CHECK(Contains(recorder.FormatReport(), "[callers]\nsvc:0x21\t5\tother=0\n"
                                            "\t3\t?0x1\t-\t-\t-\n\t2\t?0x2\t-\t-\t-\n"));
    // Eight more distinct callers overflow the table; the two frequent ones stay.
    for (std::uint64_t address = 3; address <= 10; ++address) {
        recorder.RecordCaller("svc:0x21", Frames(address));
    }
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "svc:0x21\t13\tother=2\n"));
    CHECK(Contains(report, "\t3\t?0x1\t-\t-\t-\n"));
    CHECK(Contains(report, "\t2\t?0x2\t-\t-\t-\n"));
    // Kept counts plus the evicted ones add up to every call.
    std::uint64_t kept = 0;
    for (std::size_t at = report.find("\n\t"); at != std::string::npos;
         at = report.find("\n\t", at + 1)) {
        kept += std::stoull(report.substr(at + 2));
    }
    CHECK(kept + 2 == 13);
    return 0;
}

int CallersPrintModuleNames() {
    Recorder recorder;
    recorder.SetModules({{"sdk", 0x1000, 0x2000}});
    recorder.RecordCaller("svc:0x21", Frames(0x1004, 0x1100));
    CHECK(Contains(recorder.FormatReport(), "\t1\tsdk+0x4\tsdk+0x100\t-\t-\n"));
    return 0;
}

int CallersFollowPauseAndSegments() {
    Recorder recorder;
    recorder.SetRecording(false);
    recorder.RecordCaller("svc:0x21", Frames(1));
    CHECK(!Contains(recorder.FormatReport(), "svc:0x21"));
    recorder.SetRecording(true);
    recorder.RecordCaller("svc:0x21", Frames(1));
    const std::string ended = recorder.EndSegment("a", 0);
    CHECK(Contains(ended, "svc:0x21\t1\tother=0\n"));
    CHECK(!Contains(recorder.FormatReport(), "svc:0x21"));
    return 0;
}

int IpcCallerComesFromTheStashOfItsThread() {
    using namespace Core::BoundaryInventory;
    CHECK(IsIpcSendSvc(0x21) && IsIpcSendSvc(0x22));
    CHECK(!IsIpcSendSvc(0x1F) && !IsIpcSendSvc(0x23));
    CHECK(IpcCallerKey("hid", 4, 100) == "ipc:hid:4:100");

    Recorder recorder;
    recorder.StashIpcCaller(7, Frames(0x10));
    recorder.StashIpcCaller(8, Frames(0x20));
    recorder.RecordIpcCaller(7, "hid", 4, 100);
    std::string report = recorder.FormatReport();
    CHECK(Contains(report, "ipc:hid:4:100\t1\tother=0\n\t1\t?0x10\t-\t-\t-\n"));
    CHECK(!Contains(report, "?0x20"));
    // The stash is consumed: a second request of thread 7 and an unknown thread add no row.
    recorder.RecordIpcCaller(7, "hid", 4, 100);
    recorder.RecordIpcCaller(9, "hid", 4, 101);
    report = recorder.FormatReport();
    CHECK(Contains(report, "ipc:hid:4:100\t1\tother=0\n"));
    CHECK(!Contains(report, "ipc:hid:4:101"));
    // A newer stash replaces an unread one.
    recorder.StashIpcCaller(8, Frames(0x30));
    recorder.RecordIpcCaller(8, "fsp-srv", 4, 18);
    report = recorder.FormatReport();
    CHECK(Contains(report, "ipc:fsp-srv:4:18\t1\tother=0\n\t1\t?0x30\t-\t-\t-\n"));
    CHECK(!Contains(report, "?0x20"));
    // A paused recorder neither stashes nor records.
    recorder.SetRecording(false);
    recorder.StashIpcCaller(5, Frames(0x40));
    recorder.SetRecording(true);
    recorder.RecordIpcCaller(5, "hid", 4, 102);
    CHECK(!Contains(recorder.FormatReport(), "ipc:hid:4:102"));
    return 0;
}

int IpcStashStaysBounded() {
    using namespace Core::BoundaryInventory;
    Recorder recorder;
    for (std::uint64_t thread = 1; thread <= kMaxIpcStashThreads; ++thread) {
        recorder.StashIpcCaller(thread, Frames(thread));
    }
    // A full table is emptied before the next new thread is added; the newest stash survives.
    recorder.StashIpcCaller(kMaxIpcStashThreads + 1, Frames(0x77));
    recorder.RecordIpcCaller(1, "hid", 4, 1);
    recorder.RecordIpcCaller(kMaxIpcStashThreads + 1, "hid", 4, 2);
    const std::string report = recorder.FormatReport();
    CHECK(!Contains(report, "ipc:hid:4:1\t"));
    CHECK(Contains(report, "ipc:hid:4:2\t1\tother=0\n\t1\t?0x77\t-\t-\t-\n"));
    // Re-stashing a known thread at the limit does not empty the table.
    Recorder full;
    for (std::uint64_t thread = 1; thread <= kMaxIpcStashThreads; ++thread) {
        full.StashIpcCaller(thread, Frames(thread));
    }
    full.StashIpcCaller(5, Frames(0x55));
    full.RecordIpcCaller(6, "hid", 4, 3);
    CHECK(Contains(full.FormatReport(), "ipc:hid:4:3\t1\t"));
    return 0;
}

std::vector<std::uint8_t> Bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

int NvnStringsNeedNulBoundariesAndPrintableText() {
    using namespace Core::BoundaryInventory;
    // Index 0 starts the window; "x" before the second nvn and a control byte disqualify.
    std::string text = "nvnOne";
    text += '\0';
    text += "xnvnTwo";
    text += '\0';
    text += "nvnThree";
    text += '\0';
    text += "nvn";
    text += '\0';
    text += "nvnBad";
    text += '\x01';
    text += '\0';
    text += "nvnOpen";
    const auto window = Bytes(text);
    std::vector<ScanHit> hits;
    FindNvnStrings(window, 0, window.size(), 0x1000, hits);
    CHECK(hits.size() == 2);
    CHECK(hits[0].address == 0x1000 && hits[0].text == "nvnOne");
    CHECK(hits[1].address == 0x1000 + 15 &&hits[1].text == "nvnThree");
    // An over-long string is dropped, one of the maximum length is kept.
    std::string longest = "nvn" + std::string(kMaxScanStringLength - 3, 'a');
    std::string too_long = longest + "a";
    hits.clear();
    const auto edge = Bytes(longest + '\0' + too_long + '\0');
    FindNvnStrings(edge, 0, edge.size(), 0, hits);
    CHECK(hits.size() == 1 && hits[0].text == longest);
    return 0;
}

int NvnScanChunksLoseAndRepeatNothing() {
    using namespace Core::BoundaryInventory;
    std::string text = "pad";
    text += '\0';
    text += "nvnAlpha";
    text += '\0';
    text += "nvnBeta";
    text += '\0';
    const auto data = Bytes(text);
    std::vector<ScanHit> whole;
    FindNvnStrings(data, 0, data.size(), 0, whole);
    CHECK(whole.size() == 2);
    // Every split point gives the same hits once: the window keeps one byte before `first`.
    for (std::size_t split = 1; split < data.size(); ++split) {
        std::vector<ScanHit> hits;
        FindNvnStrings(data, 0, split, 0, hits);
        FindNvnStrings(data, split, data.size(), 0, hits);
        CHECK(hits.size() == whole.size());
    }
    return 0;
}

struct FakeModule {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x400, 0);

    template <typename T>
    void Put(std::size_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof value);
    }
    void PutText(std::size_t offset, const std::string& text) {
        std::memcpy(bytes.data() + offset, text.c_str(), text.size() + 1);
    }
    void PutDynamic(std::size_t index, std::int64_t tag, std::uint64_t value) {
        Put<std::int64_t>(0x40 + index * 16, tag);
        Put<std::uint64_t>(0x48 + index * 16, value);
    }
    void PutSymbol(std::size_t index, std::uint32_t name_offset, std::uint64_t value) {
        Put<std::uint32_t>(0x100 + index * 24, name_offset);
        Put<std::uint64_t>(0x108 + index * 24, value);
    }
    bool Read(std::uint64_t offset, void* destination, std::size_t length) const {
        if (offset > bytes.size() || length > bytes.size() - offset) {
            return false;
        }
        std::memcpy(destination, bytes.data() + offset, length);
        return true;
    }
    std::optional<Core::BoundaryInventory::DynsymLocation> Find() const {
        return Core::BoundaryInventory::FindDynsym(
            [this](std::uint64_t o, void* d, std::size_t n) { return Read(o, d, n); },
            bytes.size());
    }
};

FakeModule ValidModule() {
    FakeModule module;
    module.Put<std::uint32_t>(4, 0x20);
    module.Put<std::uint32_t>(0x20, Core::BoundaryInventory::kModMagic);
    module.Put<std::uint32_t>(0x24, 0x20);
    module.PutDynamic(0, 5, 0x200);
    module.PutDynamic(1, 6, 0x100);
    module.PutDynamic(2, 10, 0x40);
    module.PutDynamic(3, 11, 24);
    module.PutDynamic(4, 0, 0);
    module.PutText(0x201, "nn::x");
    module.PutText(0x207, "memcpy");
    module.PutText(0x20E, "nvnFoo");
    module.PutSymbol(1, 1, 0x1234);
    module.PutSymbol(2, 7, 0x10);
    module.PutSymbol(3, 14, 0);
    return module;
}

int DynsymIsReadWithinBounds() {
    using namespace Core::BoundaryInventory;
    FakeModule module = ValidModule();
    const auto location = module.Find();
    CHECK(location && location->symtab_offset == 0x100 && location->strtab_offset == 0x200 &&
          location->strtab_size == 0x40);
    std::vector<ScanHit> hits;
    ParseDynsym({module.bytes.data() + 0x100, 0x100}, {module.bytes.data() + 0x200, 0x40}, 0x8000,
                hits);
    CHECK(hits.size() == 2);
    CHECK(hits[0].address == 0x8000 + 0x1234 && hits[0].text == "nn::x");
    CHECK(hits[1].address == 0 && hits[1].text == "nvnFoo");
    return 0;
}

int CorruptDynsymFailsInsteadOfLooping() {
    using namespace Core::BoundaryInventory;
    FakeModule bad_magic = ValidModule();
    bad_magic.Put<std::uint32_t>(0x20, 0);
    CHECK(!bad_magic.Find());
    FakeModule bad_header = ValidModule();
    bad_header.Put<std::uint32_t>(4, 0x7FFFFFF0);
    CHECK(!bad_header.Find());
    FakeModule bad_entry_size = ValidModule();
    bad_entry_size.PutDynamic(3, 11, 16);
    CHECK(!bad_entry_size.Find());
    // The DT_NULL sits past the entry limit; the tables themselves are valid.
    FakeModule unterminated = ValidModule();
    unterminated.bytes.resize(0x40 + (kMaxDynamicEntries + 8) * 16);
    for (std::size_t n = 3; n < kMaxDynamicEntries + 4; ++n) {
        unterminated.PutDynamic(n, 1, 0);
    }
    unterminated.PutDynamic(kMaxDynamicEntries + 4, 0, 0);
    CHECK(!unterminated.Find());
    FakeModule strtab_start_outside = ValidModule();
    strtab_start_outside.PutDynamic(0, 5, 0x500);
    CHECK(!strtab_start_outside.Find());
    FakeModule tables_equal = ValidModule();
    tables_equal.PutDynamic(0, 5, 0x100);
    CHECK(!tables_equal.Find());
    FakeModule strtab_outside = ValidModule();
    strtab_outside.PutDynamic(2, 10, 0x300);
    CHECK(!strtab_outside.Find());
    FakeModule size_wraps = ValidModule();
    size_wraps.PutDynamic(2, 10, ~std::uint64_t{0} - 0x10);
    CHECK(!size_wraps.Find());
    FakeModule tables_swapped = ValidModule();
    tables_swapped.PutDynamic(0, 5, 0x100);
    tables_swapped.PutDynamic(1, 6, 0x200);
    CHECK(!tables_swapped.Find());
    FakeModule no_symtab = ValidModule();
    no_symtab.PutDynamic(1, 6, 0);
    CHECK(!no_symtab.Find());
    return 0;
}

int DynsymNamesAreBounded() {
    using namespace Core::BoundaryInventory;
    std::vector<std::uint8_t> symtab(3 * kElf64SymbolBytes, 0);
    std::string strtab_text = "x";
    strtab_text += '\0';
    strtab_text += "nvnA";
    strtab_text += '\0';
    strtab_text += "nvnUnterminated";
    std::vector<std::uint8_t> strtab(strtab_text.begin(), strtab_text.end());
    const auto set_name = [&](std::size_t index, std::uint32_t name_offset) {
        std::memcpy(symtab.data() + index * kElf64SymbolBytes, &name_offset, sizeof name_offset);
    };
    set_name(0, 2);   // "nvnA"
    set_name(1, 7);   // runs to the end of the table without a NUL
    set_name(2, 999); // outside the table
    std::vector<ScanHit> hits;
    ParseDynsym(symtab, strtab, 0, hits);
    CHECK(hits.size() == 1 && hits[0].text == "nvnA");
    // A name over kMaxSymbolNameLength is skipped.
    std::string long_name = "nvn" + std::string(kMaxSymbolNameLength, 'a');
    std::vector<std::uint8_t> long_strtab(long_name.begin(), long_name.end());
    long_strtab.push_back(0);
    std::vector<std::uint8_t> one_symbol(kElf64SymbolBytes, 0);
    hits.clear();
    ParseDynsym(one_symbol, long_strtab, 0, hits);
    CHECK(hits.empty());
    return 0;
}

int ScanNamesAndOutputFormat() {
    using namespace Core::BoundaryInventory;
    CHECK(IsInterestingSymbolName("nvnBootstrapLoader"));
    CHECK(IsInterestingSymbolName("nn::os::Foo"));
    CHECK(IsInterestingSymbolName("_ZN2nn2os3FooEv"));
    CHECK(!IsInterestingSymbolName("memcpy"));
    CHECK(!IsInterestingSymbolName("_ZN3foo3BarEv"));
    CHECK(ScanFileName("a b") == "inventory_a_b_scan.txt");

    Recorder recorder;
    recorder.SetModules({{"main", 0x1000, 0x2000}});
    const std::string out = FormatScan(recorder, {{0x1010, "nvnA"}}, {{0x1020, "nn::B"}});
    CHECK(Contains(out, "# boundary inventory scan v1 (local only, do not commit)\n"
                        "# strings=1 symbols=1\n"));
    CHECK(Contains(out, "string\tmain+0x10\tnvnA\n"));
    CHECK(Contains(out, "symbol\tmain+0x20\tnn::B\n"));
    return 0;
}

} // namespace

int main() {
    if (const int rc = NamesAreSafeFileComponents()) return rc;
    if (const int rc = SvcInstructionIsRecognized()) return rc;
    if (const int rc = CallerChainFollowsTheFramePointer()) return rc;
    if (const int rc = CallerChainStopsAtTheFirstBadFrame()) return rc;
    if (const int rc = ModulesResolveAddresses()) return rc;
    if (const int rc = CallersKeepTheFrequentOnes()) return rc;
    if (const int rc = CallersPrintModuleNames()) return rc;
    if (const int rc = CallersFollowPauseAndSegments()) return rc;
    if (const int rc = IpcCallerComesFromTheStashOfItsThread()) return rc;
    if (const int rc = IpcStashStaysBounded()) return rc;
    if (const int rc = NvnStringsNeedNulBoundariesAndPrintableText()) return rc;
    if (const int rc = NvnScanChunksLoseAndRepeatNothing()) return rc;
    if (const int rc = DynsymIsReadWithinBounds()) return rc;
    if (const int rc = CorruptDynsymFailsInsteadOfLooping()) return rc;
    if (const int rc = DynsymNamesAreBounded()) return rc;
    if (const int rc = ScanNamesAndOutputFormat()) return rc;
    if (const int rc = CountsSvcsPerCoreAndOverflow()) return rc;
    if (const int rc = RecordsIpcKeysAndUnimplementedFlag()) return rc;
    if (const int rc = NvdrvDropsDeviceNameAtClose()) return rc;
    if (const int rc = NvdrvNegativeControlIsDetected()) return rc;
    if (const int rc = AggregatesAudioAppletAndFilesystem()) return rc;
    if (const int rc = FormatsDeterministically()) return rc;
    if (const int rc = LimitsWritesToOncePerInterval()) return rc;
    if (const int rc = RejectsStaleClockSample()) return rc;
    if (const int rc = SegmentsAreIndependent()) return rc;
    if (const int rc = PauseFreezesCountersButNotFdMap()) return rc;
    if (const int rc = FdMapSurvivesSegmentEnd()) return rc;
    if (const int rc = ConcurrentMarksLoseNothing()) return rc;
    if (const int rc = CountsConcurrentRecordsExactly()) return rc;
    std::puts("smoke_boundary_inventory_unit: OK");
    return 0;
}
