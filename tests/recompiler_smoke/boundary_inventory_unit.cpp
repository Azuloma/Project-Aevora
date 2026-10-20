// smoke_boundary_inventory_unit: the SUYU_BOUNDARY_INVENTORY recorder (core/boundary_inventory.h).
// Driven directly with an injected clock; negative controls drop the behavior a check relies on.
#include <cstdint>
#include <cstdio>
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
    recorder.MarkIpcUnimplemented("hid", 4, 101);
    const std::string report = recorder.FormatReport();
    CHECK(Contains(report, "[ipc]\nfsp-srv\t4\t18\t1\t0\t2\nhid\t4\t100\t2\t0\t0\nhid\t4\t101\t1\t1\t1\n"));
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

} // namespace

int main() {
    if (const int rc = CountsSvcsPerCoreAndOverflow()) return rc;
    if (const int rc = RecordsIpcKeysAndUnimplementedFlag()) return rc;
    if (const int rc = NvdrvDropsDeviceNameAtClose()) return rc;
    if (const int rc = NvdrvNegativeControlIsDetected()) return rc;
    if (const int rc = AggregatesAudioAppletAndFilesystem()) return rc;
    if (const int rc = FormatsDeterministically()) return rc;
    if (const int rc = LimitsWritesToOncePerInterval()) return rc;
    if (const int rc = RejectsStaleClockSample()) return rc;
    if (const int rc = CountsConcurrentRecordsExactly()) return rc;
    std::puts("smoke_boundary_inventory_unit: OK");
    return 0;
}
