// smoke_native_physical_pool_unit: reference counting, placement and exhaustion rules of the
// native physical page pool (core/native_memory/native_physical_pool.h).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/native_memory/native_physical_pool.h"

using namespace Core::NativeMemory;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr u64 kRegionOffset = 0x1000;
constexpr u64 kRegionPages = 4096;
constexpr u64 kRegionBytes = kRegionPages * PageSize;

u64 TotalSize(const std::vector<PhysicalExtent>& extents) {
    u64 total = 0;
    for (const PhysicalExtent& extent : extents) {
        total += extent.size;
    }
    return total;
}

int FrontAndBackPlacement() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    std::vector<PhysicalExtent> front;
    std::vector<PhysicalExtent> back;
    CHECK(pool.AllocateAndOpen(3 * PageSize, PoolDirection::FromFront, front));
    CHECK(pool.AllocateAndOpen(2 * PageSize, PoolDirection::FromBack, back));
    CHECK(front.size() == 1 && front[0].phys == kRegionOffset && front[0].size == 3 * PageSize);
    CHECK(back.size() == 1 && back[0].phys == kRegionOffset + kRegionBytes - 2 * PageSize);
    CHECK(pool.FreeBytes() == kRegionBytes - 5 * PageSize);
    CHECK(pool.ReferenceCount(front[0].phys) == 1);
    CHECK(pool.ReferenceCount(back[0].phys + PageSize) == 1);
    return 0;
}

int FragmentedAllocationSplitsIntoExtents() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    PhysicalExtent a{};
    PhysicalExtent b{};
    PhysicalExtent c{};
    CHECK(pool.AllocateAndOpenContinuous(4 * PageSize, 1, PoolDirection::FromFront, a));
    CHECK(pool.AllocateAndOpenContinuous(4 * PageSize, 1, PoolDirection::FromFront, b));
    CHECK(pool.AllocateAndOpenContinuous(4 * PageSize, 1, PoolDirection::FromFront, c));
    CHECK(pool.Close(a.phys, a.size));
    CHECK(pool.Close(c.phys, c.size));
    CHECK(pool.FreeRunCount() == 2);
    std::vector<PhysicalExtent> big;
    CHECK(pool.AllocateAndOpen(kRegionBytes - 4 * PageSize, PoolDirection::FromFront, big));
    CHECK(big.size() == 2);
    CHECK(TotalSize(big) == kRegionBytes - 4 * PageSize);
    CHECK(pool.FreeBytes() == 0);
    CHECK(pool.ReferenceCount(b.phys) == 1);
    return 0;
}

int PagesReturnOnlyAtZeroReferences() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    std::vector<PhysicalExtent> pages;
    CHECK(pool.AllocateAndOpen(8 * PageSize, PoolDirection::FromFront, pages));
    CHECK(pool.Open(pages[0].phys, pages[0].size));
    CHECK(pool.ReferenceCount(pages[0].phys) == 2);
    CHECK(pool.Close(pages[0].phys, pages[0].size));
    CHECK(pool.FreeBytes() == kRegionBytes - 8 * PageSize);
    CHECK(pool.Close(pages[0].phys, pages[0].size));
    CHECK(pool.FreeBytes() == kRegionBytes);
    CHECK(pool.FreeRunCount() == 1);
    CHECK(pool.ReferenceCount(pages[0].phys) == 0);
    return 0;
}

int MisuseChangesNothing() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    PhysicalExtent held{};
    CHECK(pool.AllocateAndOpenContinuous(2 * PageSize, 1, PoolDirection::FromFront, held));
    // A range that is half free must not be partially closed or opened.
    CHECK(!pool.Close(held.phys, 4 * PageSize));
    CHECK(pool.ReferenceCount(held.phys) == 1);
    CHECK(pool.ReferenceCount(held.phys + PageSize) == 1);
    CHECK(!pool.Open(held.phys, 4 * PageSize));
    CHECK(pool.ReferenceCount(held.phys) == 1);
    CHECK(!pool.Close(held.phys + 2 * PageSize, PageSize));
    CHECK(pool.FreeBytes() == kRegionBytes - 2 * PageSize);
    CHECK(!pool.Close(held.phys + 1, PageSize));
    CHECK(!pool.Close(kRegionOffset - PageSize, PageSize));
    CHECK(!pool.Close(kRegionOffset + kRegionBytes, PageSize));
    CHECK(!pool.Close(kRegionOffset + kRegionBytes - PageSize, 2 * PageSize));
    CHECK(!pool.Open(held.phys, 0));
    return 0;
}

int ExhaustionLeavesThePoolUnchanged() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    std::vector<PhysicalExtent> first;
    CHECK(pool.AllocateAndOpen(kRegionBytes - PageSize, PoolDirection::FromFront, first));
    std::vector<PhysicalExtent> second;
    CHECK(!pool.AllocateAndOpen(2 * PageSize, PoolDirection::FromFront, second));
    CHECK(second.empty());
    CHECK(pool.FreeBytes() == PageSize);
    PhysicalExtent run{};
    CHECK(!pool.AllocateAndOpenContinuous(2 * PageSize, 1, PoolDirection::FromBack, run));
    CHECK(!pool.AllocateAndOpen(0, PoolDirection::FromFront, second));
    CHECK(!pool.AllocateAndOpen(PageSize + 1, PoolDirection::FromFront, second));
    CHECK(pool.AllocateAndOpen(PageSize, PoolDirection::FromBack, second));
    CHECK(pool.FreeBytes() == 0);
    return 0;
}

int ContinuousAllocationHonoursAlignment() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    constexpr u64 kAlignPages = 512;
    PhysicalExtent front{};
    CHECK(pool.AllocateAndOpenContinuous(PageSize * 4, kAlignPages, PoolDirection::FromFront, front));
    CHECK(front.phys == 0x200000);
    PhysicalExtent back{};
    CHECK(pool.AllocateAndOpenContinuous(PageSize * 4, kAlignPages, PoolDirection::FromBack, back));
    CHECK(back.phys == 0xE00000);
    CHECK(back.phys > front.phys);
    CHECK(back.phys + back.size <= kRegionOffset + kRegionBytes);
    PhysicalExtent bad{};
    CHECK(!pool.AllocateAndOpenContinuous(PageSize, 3, PoolDirection::FromFront, bad));
    CHECK(!pool.AllocateAndOpenContinuous(PageSize, 0, PoolDirection::FromFront, bad));
    return 0;
}

int ContinuousAllocationNeedsOneRun() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    std::vector<PhysicalExtent> all;
    CHECK(pool.AllocateAndOpen(kRegionBytes, PoolDirection::FromFront, all));
    CHECK(pool.Close(kRegionOffset + PageSize, PageSize));
    CHECK(pool.Close(kRegionOffset + 3 * PageSize, PageSize));
    PhysicalExtent run{};
    CHECK(!pool.AllocateAndOpenContinuous(2 * PageSize, 1, PoolDirection::FromFront, run));
    CHECK(pool.FreeBytes() == 2 * PageSize);
    CHECK(pool.Close(kRegionOffset + 2 * PageSize, PageSize));
    CHECK(pool.FreeRunCount() == 1);
    CHECK(pool.AllocateAndOpenContinuous(3 * PageSize, 1, PoolDirection::FromFront, run));
    CHECK(run.phys == kRegionOffset + PageSize);
    return 0;
}

int ReservedPagesStayOutOfTheFreeList() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    CHECK(pool.Reserve(kRegionOffset + 10 * PageSize, 5 * PageSize));
    CHECK(pool.FreeBytes() == kRegionBytes - 5 * PageSize);
    CHECK(!pool.Reserve(kRegionOffset + 12 * PageSize, 4 * PageSize));
    CHECK(pool.FreeBytes() == kRegionBytes - 5 * PageSize);
    CHECK(pool.ReferenceCount(kRegionOffset + 10 * PageSize) == 1);
    std::vector<PhysicalExtent> rest;
    CHECK(pool.AllocateAndOpen(kRegionBytes - 5 * PageSize, PoolDirection::FromFront, rest));
    for (const PhysicalExtent& extent : rest) {
        CHECK(extent.phys + extent.size <= kRegionOffset + 10 * PageSize ||
              extent.phys >= kRegionOffset + 15 * PageSize);
    }
    return 0;
}

int ReservedPagesRejectOpenAndClose() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    CHECK(pool.Reserve(kRegionOffset, PageSize));
    CHECK(!pool.Close(kRegionOffset, PageSize));
    CHECK(!pool.Open(kRegionOffset, PageSize));
    CHECK(pool.ReferenceCount(kRegionOffset) == 1);
    std::vector<PhysicalExtent> rest;
    CHECK(pool.AllocateAndOpen(kRegionBytes - PageSize, PoolDirection::FromFront, rest));
    CHECK(rest.size() == 1 && rest[0].phys == kRegionOffset + PageSize);
    return 0;
}

int PartiallyOpenedRangeKeepsOnlyOpenedPages() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    std::vector<PhysicalExtent> all;
    CHECK(pool.AllocateAndOpen(kRegionBytes, PoolDirection::FromFront, all));
    CHECK(pool.Open(kRegionOffset + PageSize, PageSize));
    CHECK(pool.Open(kRegionOffset + 3 * PageSize, PageSize));
    CHECK(pool.Close(kRegionOffset, 5 * PageSize));
    const u32 expected[5] = {0, 1, 0, 1, 0};
    for (u64 page = 0; page < 5; ++page) {
        CHECK(pool.ReferenceCount(kRegionOffset + page * PageSize) == expected[page]);
    }
    CHECK(pool.FreeBytes() == 3 * PageSize);
    CHECK(pool.FreeRunCount() == 3);
    return 0;
}

int InvalidRegionsGiveAnEmptyPool() {
    const PhysicalPagePool wrapping(0xfffffffffffff000ULL, 2 * PageSize);
    CHECK(!wrapping.IsValid());
    CHECK(wrapping.FreeBytes() == 0);
    PhysicalPagePool unaligned_offset(1, PageSize);
    CHECK(!unaligned_offset.IsValid());
    std::vector<PhysicalExtent> pages;
    CHECK(!unaligned_offset.AllocateAndOpen(PageSize, PoolDirection::FromFront, pages));
    PhysicalPagePool unaligned_size(0, PageSize + 1);
    CHECK(!unaligned_size.IsValid());
    PhysicalPagePool empty(0, 0);
    CHECK(empty.IsValid());
    CHECK(!empty.AllocateAndOpen(PageSize, PoolDirection::FromFront, pages));
    CHECK(!empty.Reserve(0, PageSize));
    return 0;
}

int HugeRangesAreRejected() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    constexpr u64 kHugeBytes = ~u64{0} & ~(PageSize - 1);
    std::vector<PhysicalExtent> held;
    CHECK(pool.AllocateAndOpen(kRegionBytes, PoolDirection::FromFront, held));
    CHECK(!pool.Close(kRegionOffset, kHugeBytes));
    CHECK(!pool.Open(kRegionOffset, kHugeBytes));
    CHECK(!pool.Close(kHugeBytes, PageSize));
    CHECK(!pool.Reserve(kRegionOffset, kHugeBytes));
    std::vector<PhysicalExtent> pages;
    CHECK(!pool.AllocateAndOpen(kHugeBytes, PoolDirection::FromBack, pages));
    CHECK(pool.FreeBytes() == 0);
    CHECK(pool.ReferenceCount(kRegionOffset) == 1);
    return 0;
}

int ClosingExtentsContinuesPastABadOne() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    PhysicalExtent held{};
    CHECK(pool.AllocateAndOpenContinuous(2 * PageSize, 1, PoolDirection::FromFront, held));
    const std::vector<PhysicalExtent> extents{{held.phys + 2 * PageSize, PageSize}, held};
    CHECK(!pool.Close(extents));
    CHECK(pool.FreeBytes() == kRegionBytes);
    return 0;
}

int FragmentedPoolPrefersLargeRuns() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    std::vector<PhysicalExtent> all;
    CHECK(pool.AllocateAndOpen(kRegionBytes, PoolDirection::FromFront, all));
    // Free runs of 10, 200 and 2048 pages, in address order.
    CHECK(pool.Close(kRegionOffset, 10 * PageSize));
    CHECK(pool.Close(kRegionOffset + 20 * PageSize, 200 * PageSize));
    CHECK(pool.Close(kRegionOffset + 1000 * PageSize, 2048 * PageSize));
    std::vector<PhysicalExtent> fitting;
    CHECK(pool.AllocateAndOpen(150 * PageSize, PoolDirection::FromFront, fitting));
    CHECK(fitting.size() == 1 && fitting[0].phys == kRegionOffset + 20 * PageSize);
    // No run holds 2100 pages: the largest run goes first, then the next largest, then a fitting one.
    std::vector<PhysicalExtent> split;
    CHECK(pool.AllocateAndOpen(2100 * PageSize, PoolDirection::FromFront, split));
    CHECK(split.size() == 3);
    CHECK(split[0].phys == kRegionOffset + 1000 * PageSize && split[0].size == 2048 * PageSize);
    CHECK(split[1].phys == kRegionOffset + 170 * PageSize && split[1].size == 50 * PageSize);
    CHECK(split[2].phys == kRegionOffset && split[2].size == 2 * PageSize);
    CHECK(pool.FreeBytes() == 8 * PageSize);
    return 0;
}

int PageSourceInterfaceRoundTrips() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    PhysicalPageSource& source = pool;
    std::vector<PhysicalExtent> pages;
    CHECK(source.Allocate(16 * PageSize, pages));
    CHECK(TotalSize(pages) == 16 * PageSize);
    CHECK(pool.FreeBytes() == kRegionBytes - 16 * PageSize);
    source.Free(pages);
    CHECK(pool.FreeBytes() == kRegionBytes);
    CHECK(pool.FreeRunCount() == 1);
    return 0;
}

int ConcurrentUsersKeepTheTotals() {
    PhysicalPagePool pool(kRegionOffset, kRegionBytes);
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;
    for (int index = 0; index < 4; ++index) {
        threads.emplace_back([&pool, &failed, index] {
            for (int round = 0; round < 500; ++round) {
                std::vector<PhysicalExtent> pages;
                const PoolDirection direction =
                    index % 2 == 0 ? PoolDirection::FromFront : PoolDirection::FromBack;
                if (!pool.AllocateAndOpen((1 + round % 7) * PageSize, direction, pages) ||
                    !pool.Open(pages[0].phys, pages[0].size) ||
                    !pool.Close(pages[0].phys, pages[0].size) || !pool.Close(pages)) {
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
    CHECK(pool.FreeBytes() == kRegionBytes);
    CHECK(pool.FreeRunCount() == 1);
    return 0;
}

} // namespace

int main() {
    CHECK(FrontAndBackPlacement() == 0);
    CHECK(FragmentedAllocationSplitsIntoExtents() == 0);
    CHECK(PagesReturnOnlyAtZeroReferences() == 0);
    CHECK(MisuseChangesNothing() == 0);
    CHECK(ExhaustionLeavesThePoolUnchanged() == 0);
    CHECK(ContinuousAllocationHonoursAlignment() == 0);
    CHECK(ContinuousAllocationNeedsOneRun() == 0);
    CHECK(ReservedPagesStayOutOfTheFreeList() == 0);
    CHECK(ReservedPagesRejectOpenAndClose() == 0);
    CHECK(PartiallyOpenedRangeKeepsOnlyOpenedPages() == 0);
    CHECK(InvalidRegionsGiveAnEmptyPool() == 0);
    CHECK(HugeRangesAreRejected() == 0);
    CHECK(ClosingExtentsContinuesPastABadOne() == 0);
    CHECK(PageSourceInterfaceRoundTrips() == 0);
    CHECK(FragmentedPoolPrefersLargeRuns() == 0);
    CHECK(ConcurrentUsersKeepTheTotals() == 0);
    std::puts("All synthetic native physical pool smoke checks passed.");
    return 0;
}
