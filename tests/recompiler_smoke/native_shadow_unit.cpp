// smoke_native_shadow_unit: adoption of an observed table and result/block comparison rules of
// the shadow comparer (core/native_memory/native_shadow.h).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/native_memory/native_shadow.h"

using namespace Core::NativeMemory;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr u64 kPhysicalCapacityBytes = 4 * GiB;
constexpr u64 kCodeSizeBytes = 0x60F7000;

struct Lcg {
    u64 state;
    u64 operator()(u64 min, u64 max) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return min + (state >> 33) % (max - min + 1);
    }
};

Layout MakeLayout() {
    LayoutParams params;
    params.type = AddressSpaceType::Bits39;
    params.code_address = 0x800A4000;
    params.code_size = kCodeSizeBytes;
    Lcg unused{0};
    Layout layout;
    if (ComputeLayout(params, unused, layout) != Result::Success) {
        std::fprintf(stderr, "layout failed\n");
    }
    return layout;
}

// Tiles the address space with Free blocks around the ascending, non-overlapping special blocks.
std::vector<StoredBlock> MakeTable(const Layout& layout, const std::vector<StoredBlock>& specials) {
    std::vector<StoredBlock> table;
    u64 cursor = layout.address_space.start;
    for (const StoredBlock& block : specials) {
        if (block.base > cursor) {
            table.push_back({cursor, block.base - cursor, MemoryState::Free, 0, AttributeNone});
        }
        table.push_back(block);
        cursor = block.base + block.size;
    }
    if (cursor < layout.address_space.end()) {
        table.push_back({cursor, layout.address_space.end() - cursor, MemoryState::Free, 0,
                         AttributeNone});
    }
    return table;
}

StoredBlock Normal(u64 base, u64 size) {
    return {base, size, MemoryState::Normal, PermUserReadWrite, AttributeNone};
}

int SyncAdoptsHeapAndPhysicalAccounting() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.Space().SetMaxHeapSize(layout.heap.size);
    const u64 heap_bytes = 0x400000;
    shadow.Sync(MakeTable(layout, {Normal(layout.heap.start, heap_bytes)}), heap_bytes);
    CHECK(shadow.SyncFailures() == 0);
    CHECK(shadow.Space().HeapSizeBytes() == heap_bytes);
    CHECK(shadow.Space().PhysicalBytesInUse() == heap_bytes);

    u64 out = 0;
    CHECK(shadow.Space().SetHeapSize(out, 0) == Result::Success);
    CHECK(shadow.Space().PhysicalBytesInUse() == 0);
    CHECK(shadow.Space().QueryInfo(layout.heap.start).state == MemoryState::Free);
    return 0;
}

int SyncRejectsATableThatDoesNotTileTheSpace() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    std::vector<StoredBlock> gap = MakeTable(layout, {Normal(layout.heap.start, 0x200000)});
    gap.erase(gap.begin() + 1);
    shadow.Sync(gap, 0);
    CHECK(shadow.SyncFailures() == 1);
    CHECK(shadow.TotalMismatches() == 1);

    std::vector<StoredBlock> short_table = MakeTable(layout, {});
    short_table.back().size -= PageSize;
    shadow.Sync(short_table, 0);
    CHECK(shadow.SyncFailures() == 2);
    return 0;
}

int SyncKeepsTheObservedBoundaries() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.Sync(MakeTable(layout, {Normal(layout.alias.start, 0x200000),
                                   Normal(layout.alias.start + 0x200000, 0x200000)}),
                0x400000);
    const MemoryInfo info = shadow.Space().QueryInfo(layout.alias.start);
    CHECK(info.base == layout.alias.start);
    CHECK(info.size == 0x200000);
    return 0;
}

int MatchingResultsAndBlocksCountAsAgreement() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.Sync(MakeTable(layout, {}), 0);
    const u64 address = layout.alias.start;
    CHECK(shadow.Space().MapPhysicalMemory(address, 0x400000) == Result::Success);
    CHECK(shadow.CompareResult(ShadowOp::MapPhysicalMemory, address, 0x400000, Result::Success,
                               Result::Success));
    shadow.CompareBlocks(ShadowOp::MapPhysicalMemory, address, 0x400000,
                         {Normal(address, 0x400000)});
    const ShadowCounts& counts = shadow.Counts(ShadowOp::MapPhysicalMemory);
    CHECK(counts.compared == 1);
    CHECK(counts.result_mismatch == 0 && counts.state_mismatch == 0 && counts.boundary_only == 0);
    CHECK(shadow.TotalMismatches() == 0);
    CHECK(shadow.TakeReports().empty());
    return 0;
}

int DifferentResultsAreReportedWithTheOperation() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    CHECK(!shadow.CompareResult(ShadowOp::SetMemoryPermission, 0x1000, 0x2000, Result::Success,
                                Result::InvalidCurrentMemory));
    CHECK(shadow.Counts(ShadowOp::SetMemoryPermission).result_mismatch == 1);
    CHECK(shadow.TotalMismatches() == 1);
    const std::vector<std::string> reports = shadow.TakeReports();
    CHECK(reports.size() == 1);
    CHECK(reports[0].find("SetMemoryPermission") != std::string::npos);
    CHECK(reports[0].find("InvalidCurrentMemory") != std::string::npos);
    CHECK(reports[0].find("0x1000") != std::string::npos);
    CHECK(shadow.TakeReports().empty());
    return 0;
}

int DifferentBlockFieldsAreStateMismatches() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.Sync(MakeTable(layout, {Normal(layout.alias.start, 0x200000)}), 0x200000);

    StoredBlock other_state = Normal(layout.alias.start, 0x200000);
    other_state.state = MemoryState::Stack;
    shadow.CompareBlocks(ShadowOp::MapMemory, layout.alias.start, 0x200000, {other_state});
    StoredBlock other_permission = Normal(layout.alias.start, 0x200000);
    other_permission.permission = PermLockedSource;
    shadow.CompareBlocks(ShadowOp::MapMemory, layout.alias.start, 0x200000, {other_permission});
    StoredBlock other_attribute = Normal(layout.alias.start, 0x200000);
    other_attribute.attribute = AttributeLocked;
    shadow.CompareBlocks(ShadowOp::MapMemory, layout.alias.start, 0x200000, {other_attribute});

    CHECK(shadow.Counts(ShadowOp::MapMemory).state_mismatch == 3);
    CHECK(shadow.TotalMismatches() == 3);
    CHECK(shadow.TakeReports().size() == 3);
    return 0;
}

int KernelBitsOfPermissionDoNotCountAsMismatch() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.Sync(MakeTable(layout, {Normal(layout.alias.start, 0x200000)}), 0x200000);
    shadow.CompareBlocks(ShadowOp::SetMemoryPermission, layout.alias.start, 0x200000,
                         {Normal(layout.alias.start, 0x200000)});
    CHECK(shadow.TotalMismatches() == 0);
    return 0;
}

int MergedOrSplitBoundariesAreCountedSeparately() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.Sync(MakeTable(layout, {Normal(layout.alias.start, 0x400000)}), 0x400000);

    shadow.CompareBlocks(ShadowOp::MapMemory, layout.alias.start, 0x400000,
                         {Normal(layout.alias.start, 0x200000),
                          Normal(layout.alias.start + 0x200000, 0x200000)});
    CHECK(shadow.Counts(ShadowOp::MapMemory).boundary_only == 2);
    CHECK(shadow.Counts(ShadowOp::MapMemory).state_mismatch == 0);
    CHECK(shadow.TotalMismatches() == 0);
    return 0;
}

int ASplitShadowBlockStillChecksEveryPiece() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    StoredBlock second_half = Normal(layout.alias.start + 0x200000, 0x200000);
    second_half.state = MemoryState::Stack;
    shadow.Sync(MakeTable(layout, {Normal(layout.alias.start, 0x200000), second_half}), 0);

    shadow.CompareBlocks(ShadowOp::MapMemory, layout.alias.start, 0x400000,
                         {Normal(layout.alias.start, 0x400000)});
    CHECK(shadow.Counts(ShadowOp::MapMemory).state_mismatch == 1);
    return 0;
}

int OnlyTheComparedRangeIsChecked() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.Sync(MakeTable(layout, {Normal(layout.alias.start, 0x200000)}), 0x200000);
    StoredBlock outside = Normal(layout.alias.start + 0x200000, 0x200000);
    outside.state = MemoryState::Stack;
    shadow.CompareBlocks(ShadowOp::MapMemory, layout.alias.start, 0x200000,
                         {Normal(layout.alias.start, 0x200000), outside});
    CHECK(shadow.TotalMismatches() == 0);
    return 0;
}

int PendingReportsAreBounded() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    for (size_t index = 0; index < Shadow::MaxPendingReports * 3; ++index) {
        shadow.CompareResult(ShadowOp::SetHeapSize, 0, 0, Result::Success, Result::OutOfMemory);
    }
    CHECK(shadow.TakeReports().size() == Shadow::MaxPendingReports);
    CHECK(shadow.Counts(ShadowOp::SetHeapSize).result_mismatch == Shadow::MaxPendingReports * 3);
    return 0;
}

int SummaryListsEveryOperation() {
    const Layout layout = MakeLayout();
    Shadow shadow(layout, kPhysicalCapacityBytes);
    shadow.CompareResult(ShadowOp::UnmapMemory, 0, 0, Result::Success, Result::Success);
    const std::string summary = shadow.Summary();
    for (u32 op = 0; op < static_cast<u32>(ShadowOp::Count); ++op) {
        CHECK(summary.find(std::string(ShadowOpName(static_cast<ShadowOp>(op))) + "=") !=
              std::string::npos);
    }
    CHECK(summary.find("UnmapMemory=1/0/0/0/0") != std::string::npos);
    shadow.CountSkipped(ShadowOp::UnmapMemory);
    CHECK(shadow.Summary().find("UnmapMemory=1/0/0/0/1") != std::string::npos);
    CHECK(shadow.TotalMismatches() == 0);
    return 0;
}

} // namespace

int main() {
    CHECK(SyncAdoptsHeapAndPhysicalAccounting() == 0);
    CHECK(SyncRejectsATableThatDoesNotTileTheSpace() == 0);
    CHECK(SyncKeepsTheObservedBoundaries() == 0);
    CHECK(MatchingResultsAndBlocksCountAsAgreement() == 0);
    CHECK(DifferentResultsAreReportedWithTheOperation() == 0);
    CHECK(DifferentBlockFieldsAreStateMismatches() == 0);
    CHECK(KernelBitsOfPermissionDoNotCountAsMismatch() == 0);
    CHECK(MergedOrSplitBoundariesAreCountedSeparately() == 0);
    CHECK(ASplitShadowBlockStillChecksEveryPiece() == 0);
    CHECK(OnlyTheComparedRangeIsChecked() == 0);
    CHECK(PendingReportsAreBounded() == 0);
    CHECK(SummaryListsEveryOperation() == 0);
    std::puts("All synthetic native shadow smoke checks passed.");
    return 0;
}
