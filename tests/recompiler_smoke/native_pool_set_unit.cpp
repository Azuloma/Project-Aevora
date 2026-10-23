// smoke_native_pool_set_unit: region and pool rules of the native physical pool set
// (core/native_memory/native_pool_set.h), the native counterpart of KMemoryManager's managers.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/native_memory/native_pool_set.h"

using namespace Core::NativeMemory;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr u64 kMiB = 0x100000;
constexpr u32 kPoolA = 0;
constexpr u32 kPoolB = 1;
constexpr u32 kPoolC = 2;
constexpr u64 kA0 = 0x100000;
constexpr u64 kB0 = 0x300000;
constexpr u64 kA1 = 0x500000;
constexpr u64 kC0 = 0x1000000;
constexpr u64 kC1 = kC0 + kMiB;

// Pool A has two regions with a pool B region between them; pool C has two adjacent regions.
bool Build(PhysicalPoolSet& set) {
    return set.AddRegion(kPoolA, kA0, kMiB) && set.AddRegion(kPoolB, kB0, kMiB) &&
           set.AddRegion(kPoolA, kA1, kMiB) && set.AddRegion(kPoolC, kC0, kMiB) &&
           set.AddRegion(kPoolC, kC1, kMiB);
}

u64 TotalSize(const std::vector<PhysicalExtent>& extents) {
    u64 total = 0;
    for (const PhysicalExtent& extent : extents) {
        total += extent.size;
    }
    return total;
}

int RegionsDefinePoolsAndSizes() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    CHECK(set.RegionCount() == 5);
    CHECK(set.TotalBytes(kPoolA) == 2 * kMiB);
    CHECK(set.TotalBytes(kPoolB) == kMiB);
    CHECK(set.TotalBytes() == 5 * kMiB);
    CHECK(set.FreeBytes() == 5 * kMiB);
    u32 pool = 99;
    CHECK(set.FindPool(kA1 + 0x1000, pool) && pool == kPoolA);
    CHECK(set.FindPool(kB0, pool) && pool == kPoolB);
    CHECK(set.FindPool(kC1 + kMiB - 1, pool) && pool == kPoolC);
    CHECK(!set.FindPool(kB0 + kMiB, pool));
    return 0;
}

int BadRegionsAreRejected() {
    PhysicalPoolSet set;
    CHECK(set.AddRegion(kPoolA, kA0, kMiB));
    CHECK(!set.AddRegion(kPoolB, kA0 + 0x1000, kMiB));
    CHECK(!set.AddRegion(kPoolB, kA0 - 0x1000, 0x2000));
    CHECK(!set.AddRegion(kPoolB, kA0, kMiB));
    CHECK(!set.AddRegion(kPoolB, kB0, 0));
    CHECK(!set.AddRegion(kPoolB, kB0 + 1, kMiB));
    CHECK(!set.AddRegion(kPoolB, 0xfffffffffffff000ULL, 2 * PageSize));
    CHECK(set.AddRegion(kPoolB, kA0 + kMiB, kMiB));
    CHECK(set.RegionCount() == 2);
    return 0;
}

int GroupAllocationSpansRegionsInOrder() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    std::vector<PhysicalExtent> front;
    CHECK(set.AllocateAndOpen(kPoolA, kMiB + kMiB / 2, PoolDirection::FromFront, front));
    CHECK(front.size() == 2);
    CHECK(front[0].phys == kA0 && front[0].size == kMiB);
    CHECK(front[1].phys == kA1 && front[1].size == kMiB / 2);
    std::vector<PhysicalExtent> back;
    CHECK(set.AllocateAndOpen(kPoolA, kMiB / 2, PoolDirection::FromBack, back));
    CHECK(back.size() == 1 && back[0].phys == kA1 + kMiB / 2);
    return 0;
}

int BackGroupSpansRegionsFromTheTop() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    std::vector<PhysicalExtent> back;
    CHECK(set.AllocateAndOpen(kPoolA, kMiB + kMiB / 2, PoolDirection::FromBack, back));
    CHECK(back.size() == 2);
    CHECK(back[0].phys == kA1 && back[0].size == kMiB);
    CHECK(back[1].phys == kA0 + kMiB / 2 && back[1].size == kMiB / 2);
    return 0;
}

int ShortfallTakesNothing() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    std::vector<PhysicalExtent> pages;
    CHECK(!set.AllocateAndOpen(kPoolA, 2 * kMiB + PageSize, PoolDirection::FromFront, pages));
    CHECK(pages.empty());
    CHECK(set.FreeBytes(kPoolA) == 2 * kMiB);
    CHECK(set.FreeBytes() == 5 * kMiB);
    CHECK(!set.AllocateAndOpen(kPoolA, 0, PoolDirection::FromFront, pages));
    CHECK(!set.AllocateAndOpen(kPoolA, PageSize + 1, PoolDirection::FromFront, pages));
    CHECK(!set.AllocateAndOpen(7, PageSize, PoolDirection::FromFront, pages));
    return 0;
}

int PoolsDoNotShareMemory() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    std::vector<PhysicalExtent> all_b;
    CHECK(set.AllocateAndOpen(kPoolB, kMiB, PoolDirection::FromFront, all_b));
    CHECK(set.FreeBytes(kPoolB) == 0);
    std::vector<PhysicalExtent> more_b;
    CHECK(!set.AllocateAndOpen(kPoolB, PageSize, PoolDirection::FromFront, more_b));
    CHECK(set.FreeBytes(kPoolA) == 2 * kMiB);
    for (const PhysicalExtent& extent : all_b) {
        CHECK(extent.phys >= kB0 && extent.phys + extent.size <= kB0 + kMiB);
    }
    PhysicalExtent run{};
    CHECK(!set.AllocateAndOpenContinuous(kPoolB, PageSize, 1, PoolDirection::FromFront, run));
    return 0;
}

int ContinuousAllocationStaysInOneRegion() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    PhysicalExtent run{};
    CHECK(set.AllocateAndOpenContinuous(kPoolA, 4 * PageSize, 1, PoolDirection::FromFront, run));
    CHECK(run.phys == kA0 && run.size == 4 * PageSize);
    CHECK(set.AllocateAndOpenContinuous(kPoolA, 4 * PageSize, 1, PoolDirection::FromBack, run));
    CHECK(run.phys == kA1 + kMiB - 4 * PageSize);
    CHECK(!set.AllocateAndOpenContinuous(kPoolA, kMiB + kMiB / 2, 1, PoolDirection::FromFront, run));
    CHECK(run.size == 0);
    return 0;
}

int AlignedContinuousAllocationInTheSet() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    constexpr u64 kAlignPages = 16;
    PhysicalExtent front{};
    CHECK(set.AllocateAndOpenContinuous(kPoolA, PageSize, kAlignPages, PoolDirection::FromFront, front));
    CHECK(front.phys == kA0);
    PhysicalExtent next{};
    CHECK(set.AllocateAndOpenContinuous(kPoolA, PageSize, kAlignPages, PoolDirection::FromFront, next));
    CHECK(next.phys == kA0 + kAlignPages * PageSize);
    PhysicalExtent back{};
    CHECK(set.AllocateAndOpenContinuous(kPoolA, PageSize, kAlignPages, PoolDirection::FromBack, back));
    CHECK(back.phys == kA1 + kMiB - kAlignPages * PageSize);
    return 0;
}

int ConcurrentGroupAllocationsFitTheirCapacity() {
    // Two 6-page regions and four threads holding at most 3 pages each: no request may fail.
    PhysicalPoolSet set;
    CHECK(set.AddRegion(kPoolA, kA0, 6 * PageSize));
    CHECK(set.AddRegion(kPoolA, kA1, 6 * PageSize));
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    for (int index = 0; index < 4; ++index) {
        threads.emplace_back([&set, &failed, index] {
            for (int round = 0; round < 3000; ++round) {
                std::vector<PhysicalExtent> pages;
                const u64 page_count = 1 + (round + index) % 3;
                if (!set.AllocateAndOpen(kPoolA, page_count * PageSize, PoolDirection::FromFront,
                                         pages) ||
                    !set.Close(pages)) {
                    failed = true;
                    return;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    CHECK(!failed);
    CHECK(set.FreeBytes(kPoolA) == 12 * PageSize);
    return 0;
}

int ReferencesSplitAtRegionBoundaries() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    std::vector<PhysicalExtent> pages;
    CHECK(set.AllocateAndOpen(kPoolC, 2 * kMiB, PoolDirection::FromFront, pages));
    CHECK(TotalSize(pages) == 2 * kMiB);
    CHECK(set.Open(kC0, 2 * kMiB));
    CHECK(set.Close(kC0, 2 * kMiB));
    CHECK(set.FreeBytes(kPoolC) == 0);
    CHECK(set.Close(kC0, 2 * kMiB));
    CHECK(set.FreeBytes(kPoolC) == 2 * kMiB);
    return 0;
}

int RangesNeedAdjacentRegions() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    std::vector<PhysicalExtent> pages;
    CHECK(set.AllocateAndOpen(kPoolA, 2 * kMiB, PoolDirection::FromFront, pages));
    // A refused range must leave the covered part untouched, so a later exact Close still frees it.
    CHECK(!set.Open(kA0 + kMiB - PageSize, 2 * PageSize));
    CHECK(!set.Open(kA0, 0));
    CHECK(!set.Close(0xfffffffffffff000ULL, 2 * PageSize));
    CHECK(set.FreeBytes(kPoolA) == 0);
    CHECK(set.Close(kA0, kMiB));
    CHECK(set.FreeBytes(kPoolA) == kMiB);
    CHECK(!set.Close(kA1 + kMiB - PageSize, 2 * PageSize));
    CHECK(set.Close(kA1, kMiB));
    CHECK(set.FreeBytes(kPoolA) == 2 * kMiB);
    return 0;
}

int ReservedRangesStayUsed() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    CHECK(set.Reserve(kA0, 4 * PageSize));
    CHECK(set.FreeBytes(kPoolA) == 2 * kMiB - 4 * PageSize);
    CHECK(!set.Reserve(kA0 + kMiB - PageSize, 2 * PageSize));
    CHECK(!set.Reserve(0x10000, PageSize));
    CHECK(!set.Close(kA0, 4 * PageSize));
    PhysicalExtent run{};
    CHECK(set.AllocateAndOpenContinuous(kPoolA, PageSize, 1, PoolDirection::FromFront, run));
    CHECK(run.phys == kA0 + 4 * PageSize);
    return 0;
}

int ClosingExtentsContinuesPastABadOne() {
    PhysicalPoolSet set;
    CHECK(Build(set));
    PhysicalExtent held{};
    CHECK(set.AllocateAndOpenContinuous(kPoolA, 2 * PageSize, 1, PoolDirection::FromFront, held));
    const std::vector<PhysicalExtent> extents{{kA0 + 8 * PageSize, PageSize}, held};
    CHECK(!set.Close(extents));
    CHECK(set.FreeBytes(kPoolA) == 2 * kMiB);
    return 0;
}

} // namespace

int main() {
    CHECK(RegionsDefinePoolsAndSizes() == 0);
    CHECK(BadRegionsAreRejected() == 0);
    CHECK(GroupAllocationSpansRegionsInOrder() == 0);
    CHECK(BackGroupSpansRegionsFromTheTop() == 0);
    CHECK(ShortfallTakesNothing() == 0);
    CHECK(PoolsDoNotShareMemory() == 0);
    CHECK(ContinuousAllocationStaysInOneRegion() == 0);
    CHECK(AlignedContinuousAllocationInTheSet() == 0);
    CHECK(ConcurrentGroupAllocationsFitTheirCapacity() == 0);
    CHECK(ReferencesSplitAtRegionBoundaries() == 0);
    CHECK(RangesNeedAdjacentRegions() == 0);
    CHECK(ReservedRangesStayUsed() == 0);
    CHECK(ClosingExtentsContinuesPastABadOne() == 0);
    std::puts("All synthetic native physical pool set smoke checks passed.");
    return 0;
}
