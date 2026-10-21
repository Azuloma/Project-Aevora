// smoke_boundary_inventory_unit: the SUYU_BOUNDARY_INVENTORY recorder (core/boundary_inventory.h).
// Driven directly with an injected clock; negative controls drop the behavior a check relies on.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "core/boundary_inventory.h"

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
