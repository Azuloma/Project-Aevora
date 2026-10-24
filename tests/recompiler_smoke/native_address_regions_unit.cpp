// smoke_native_address_regions_unit: kernel-free region predicates of KPageTableBase
// (core/native_memory/native_address_regions.h).
#include <cstdint>
#include <cstdio>

#include "core/native_memory/native_address_regions.h"

using namespace Core::NativeMemory;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr u32 kStateCount = 0x18;

// Disjoint regions with distinct starts and sizes, shaped like a 39-bit process.
Layout SeparateLayout() {
    Layout layout;
    layout.width = 39;
    layout.address_space = {0x0, 0x8000000000};
    layout.code = {0x8000000, 0x10000000};
    layout.alias_code = {0x20000000, 0x8000000};
    layout.alias = {0x1000000000, 0x800000000};
    layout.heap = {0x2000000000, 0x400000000};
    layout.stack = {0x3000000000, 0x200000000};
    layout.kernel_map = {0x4000000000, 0x100000000};
    return layout;
}

// Heap and alias overlap, and the other regions cover both, so every exclusion is observable.
Layout OverlappingLayout() {
    Layout layout;
    layout.width = 32;
    layout.address_space = {0x0, 0x40000};
    layout.code = {0x0, 0x40000};
    layout.alias_code = {0x0, 0x40000};
    layout.alias = {0x18000, 0x10000};
    layout.heap = {0x10000, 0x10000};
    layout.stack = {0x0, 0x40000};
    layout.kernel_map = {0x0, 0x40000};
    return layout;
}

// Heap and alias are empty at 0x8000, as for a 32-bit space without an alias region.
Layout EmptyRegionLayout() {
    Layout layout = OverlappingLayout();
    layout.alias = {0x8000, 0};
    layout.heap = {0x8000, 0};
    return layout;
}

int RegionGettersReturnTheirOwnRegion() {
    const Layout layout = SeparateLayout();
    const AddressRegions regions(layout);
    CHECK(regions.AddressSpaceStart() == 0x0 && regions.AddressSpaceSize() == 0x8000000000);
    CHECK(regions.CodeRegionStart() == 0x8000000 && regions.CodeRegionSize() == 0x10000000);
    CHECK(regions.AliasCodeRegionStart() == 0x20000000 && regions.AliasCodeRegionSize() == 0x8000000);
    CHECK(regions.AliasRegionStart() == 0x1000000000 && regions.AliasRegionSize() == 0x800000000);
    CHECK(regions.HeapRegionStart() == 0x2000000000 && regions.HeapRegionSize() == 0x400000000);
    CHECK(regions.StackRegionStart() == 0x3000000000 && regions.StackRegionSize() == 0x200000000);
    CHECK(regions.KernelMapRegionStart() == 0x4000000000 &&
          regions.KernelMapRegionSize() == 0x100000000);
    CHECK(regions.GetLayout().heap == layout.heap);
    return 0;
}

int ContainsFollowsTheAddressSpaceBounds() {
    const AddressRegions regions(SeparateLayout());
    constexpr u64 kEnd = 0x8000000000;
    CHECK(regions.Contains(0));
    CHECK(regions.Contains(kEnd - 1));
    CHECK(!regions.Contains(kEnd));
    CHECK(regions.Contains(0, kEnd));
    CHECK(regions.Contains(kEnd - 0x1000, 0x1000));
    CHECK(!regions.Contains(kEnd - 0x1000, 0x1001));
    CHECK(!regions.Contains(0x1000, 0));
    // addr + size wraps to a value below addr and must not pass the end check.
    CHECK(!regions.Contains(0x1000, 0 - 0x800ULL));
    CHECK(!regions.Contains(kEnd, 1));

    Layout shifted = SeparateLayout();
    shifted.address_space = {0x200000, 0x1000000};
    const AddressRegions shifted_regions(shifted);
    CHECK(!shifted_regions.Contains(0x1FFFFF));
    CHECK(shifted_regions.Contains(0x200000));
    CHECK(!shifted_regions.Contains(0x1FF000, 0x2000));
    return 0;
}

int AliasAndHeapRegionRangesAreExact() {
    const AddressRegions regions(SeparateLayout());
    const u64 alias_start = 0x1000000000;
    const u64 alias_size = 0x800000000;
    CHECK(regions.IsInAliasRegion(alias_start, alias_size));
    CHECK(regions.IsInAliasRegion(alias_start + alias_size - 0x1000, 0x1000));
    CHECK(!regions.IsInAliasRegion(alias_start - 0x1000, 0x2000));
    CHECK(!regions.IsInAliasRegion(alias_start + alias_size - 0x1000, 0x1001));
    CHECK(!regions.IsInAliasRegion(alias_start, 0));

    const u64 heap_start = 0x2000000000;
    const u64 heap_size = 0x400000000;
    CHECK(regions.IsInHeapRegion(heap_start, heap_size));
    CHECK(regions.IsInHeapRegion(heap_start + heap_size - 0x1000, 0x1000));
    CHECK(!regions.IsInHeapRegion(heap_start - 0x1000, 0x2000));
    CHECK(!regions.IsInHeapRegion(heap_start + heap_size - 0x1000, 0x1001));
    CHECK(!regions.IsInHeapRegion(alias_start, 0x1000));
    CHECK(!regions.IsInAliasRegion(heap_start, 0x1000));

    // A range that leaves the address space is outside every region, even one that would fit.
    Layout clipped = SeparateLayout();
    clipped.address_space = {0x0, 0x1000000000};
    const AddressRegions clipped_regions(clipped);
    CHECK(!clipped_regions.IsInAliasRegion(alias_start, 0x1000));
    return 0;
}

int RegionPerStateMatchesTheKernelTable() {
    const Layout layout = SeparateLayout();
    const AddressRegions regions(layout);
    struct Expectation {
        RegionState state;
        Range expected;
    };
    const Expectation table[] = {
        {RegionState::Free, layout.address_space},
        {RegionState::Kernel, layout.address_space},
        {RegionState::Normal, layout.heap},
        {RegionState::Ipc, layout.alias},
        {RegionState::NonSecureIpc, layout.alias},
        {RegionState::NonDeviceIpc, layout.alias},
        {RegionState::Stack, layout.stack},
        {RegionState::Static, layout.kernel_map},
        {RegionState::ThreadLocal, layout.kernel_map},
        {RegionState::Io, layout.alias_code},
        {RegionState::Shared, layout.alias_code},
        {RegionState::AliasCode, layout.alias_code},
        {RegionState::AliasCodeData, layout.alias_code},
        {RegionState::Transferred, layout.alias_code},
        {RegionState::SharedTransferred, layout.alias_code},
        {RegionState::SharedCode, layout.alias_code},
        {RegionState::GeneratedCode, layout.alias_code},
        {RegionState::CodeOut, layout.alias_code},
        {RegionState::Coverage, layout.alias_code},
        {RegionState::Insecure, layout.alias_code},
        {RegionState::Code, layout.code},
        {RegionState::CodeData, layout.code},
    };
    for (const Expectation& row : table) {
        Range region{0xDEAD, 0xBEEF};
        CHECK(regions.TryGetRegion(row.state, region));
        CHECK(region == row.expected);
    }
    // States the kernel treats as unreachable have no region and contain nothing.
    for (const RegionState state : {RegionState::Alias, RegionState::Inaccessible,
                                    static_cast<RegionState>(kStateCount),
                                    static_cast<RegionState>(0xFF)}) {
        Range region;
        CHECK(!regions.TryGetRegion(state, region));
        CHECK(!regions.CanContain(0x1000000, 0x1000, state));
    }
    return 0;
}

int CanContainOnSeparateRegions() {
    const Layout layout = SeparateLayout();
    const AddressRegions regions(layout);
    CHECK(regions.CanContain(0x1000, 0x1000, RegionState::Free));
    CHECK(regions.CanContain(layout.heap.start, 0x1000, RegionState::Kernel));
    CHECK(!regions.CanContain(layout.address_space.end(), 0x1000, RegionState::Free));

    CHECK(regions.CanContain(layout.heap.start, layout.heap.size, RegionState::Normal));
    CHECK(!regions.CanContain(layout.heap.start - 0x1000, 0x2000, RegionState::Normal));
    CHECK(!regions.CanContain(layout.heap.end() - 0x1000, 0x2000, RegionState::Normal));

    CHECK(regions.CanContain(layout.alias.start, 0x1000, RegionState::Ipc));
    CHECK(!regions.CanContain(layout.heap.start, 0x1000, RegionState::Ipc));
    CHECK(regions.CanContain(layout.stack.start, 0x1000, RegionState::Stack));
    CHECK(!regions.CanContain(layout.stack.end() - 0x1000, 0x2000, RegionState::Stack));
    CHECK(regions.CanContain(layout.kernel_map.start, 0x1000, RegionState::Static));
    CHECK(regions.CanContain(layout.kernel_map.start, 0x1000, RegionState::ThreadLocal));
    CHECK(regions.CanContain(layout.code.start, 0x1000, RegionState::Code));
    CHECK(regions.CanContain(layout.code.start, 0x1000, RegionState::CodeData));
    CHECK(!regions.CanContain(layout.alias_code.start, 0x1000, RegionState::Code));
    CHECK(regions.CanContain(layout.alias_code.start, 0x1000, RegionState::Shared));
    CHECK(!regions.CanContain(layout.code.start, 0x1000, RegionState::Shared));

    // Empty ranges and wrapped ranges are never contained.
    CHECK(!regions.CanContain(layout.stack.start, 0, RegionState::Stack));
    CHECK(!regions.CanContain(layout.stack.start + 0x1000, 0 - 0x800ULL, RegionState::Free));

    CHECK(regions.IsInUnsafeAliasRegion(layout.alias_code.start, 0x1000));
    CHECK(!regions.IsInUnsafeAliasRegion(layout.code.start, 0x1000));
    CHECK(!regions.IsInUnsafeAliasRegion(layout.alias_code.end() - 0x1000, 0x2000));
    return 0;
}

int CanContainExcludesHeapAndAliasWhereTheKernelDoes() {
    const AddressRegions regions(OverlappingLayout());
    // Stack-like states may touch neither the heap [0x10000,0x20000) nor the alias [0x18000,0x28000).
    CHECK(regions.CanContain(0x4000, 0x1000, RegionState::Stack));
    CHECK(!regions.CanContain(0x10000, 0x1000, RegionState::Stack));
    CHECK(!regions.CanContain(0x24000, 0x1000, RegionState::Stack));
    CHECK(regions.CanContain(0x2C000, 0x1000, RegionState::Stack));
    CHECK(!regions.CanContain(0xF000, 0x2000, RegionState::Code));
    // Normal may touch the heap but not the alias region.
    CHECK(regions.CanContain(0x14000, 0x1000, RegionState::Normal));
    CHECK(!regions.CanContain(0x18000, 0x1000, RegionState::Normal));
    CHECK(!regions.CanContain(0x4000, 0x1000, RegionState::Normal));
    // Ipc states may touch the alias region but not the heap.
    CHECK(regions.CanContain(0x20000, 0x1000, RegionState::Ipc));
    CHECK(regions.CanContain(0x20000, 0x1000, RegionState::NonSecureIpc));
    CHECK(regions.CanContain(0x20000, 0x1000, RegionState::NonDeviceIpc));
    CHECK(!regions.CanContain(0x18000, 0x1000, RegionState::Ipc));
    CHECK(!regions.CanContain(0x12000, 0x1000, RegionState::Ipc));
    // Free and Kernel only need the address space.
    CHECK(regions.CanContain(0x14000, 0x1000, RegionState::Free));
    CHECK(regions.CanContain(0x1C000, 0x1000, RegionState::Kernel));
    CHECK(!regions.CanContain(0x3F000, 0x2000, RegionState::Free));
    CHECK(regions.CanContain(0x3F000, 0x1000, RegionState::Free));
    CHECK(!regions.IsInUnsafeAliasRegion(0x1C000, 0x1000));
    CHECK(regions.IsInUnsafeAliasRegion(0x4000, 0x1000));
    return 0;
}

int EmptyHeapAndAliasRegionsAreNeverTouched() {
    const AddressRegions regions(EmptyRegionLayout());
    // A range spanning the empty regions' start must not count as touching them.
    CHECK(regions.CanContain(0x7000, 0x2000, RegionState::Stack));
    CHECK(regions.CanContain(0x7000, 0x2000, RegionState::Shared));
    CHECK(!regions.IsInAliasRegion(0x8000, 0x1000));
    CHECK(!regions.IsInHeapRegion(0x8000, 0x1000));
    CHECK(!regions.CanContain(0x8000, 0x1000, RegionState::Normal));
    CHECK(!regions.CanContain(0x8000, 0x1000, RegionState::Ipc));
    return 0;
}

// An empty region at address 0 wraps end() - 1 to the maximum, so the kernel reports every range inside.
int EmptyRegionAtZeroKeepsTheKernelWraparound() {
    Layout layout = OverlappingLayout();
    layout.alias = {0, 0};
    layout.heap = {0, 0};
    const AddressRegions regions(layout);
    CHECK(regions.IsInHeapRegion(0x1000, 0x1000));
    CHECK(regions.IsInAliasRegion(0x1000, 0x1000));
    CHECK(regions.CanContain(0x1000, 0x1000, RegionState::Normal));
    CHECK(regions.CanContain(0x1000, 0x1000, RegionState::Ipc));
    return 0;
}

// Independent model: plain interval overlap on a small grid, no shared code with the header.
bool Overlaps(const Range& region, u64 first, u64 last_exclusive) {
    return region.size != 0 && first < region.start + region.size && region.start < last_exclusive;
}

int CanContainMatchesAnIntervalModelOnAGrid() {
    const Layout layouts[] = {OverlappingLayout(), EmptyRegionLayout()};
    for (const Layout& layout : layouts) {
        const AddressRegions regions(layout);
        for (u32 state_value = 0; state_value <= kStateCount; ++state_value) {
            const RegionState state = static_cast<RegionState>(state_value);
            Range region;
            const bool has_region = regions.TryGetRegion(state, region);
            for (u64 first = 0; first < 0x44000; first += 0x1000) {
                for (u64 length = 0x1000; length <= 0x44000; length += 0x1000) {
                    const u64 last_exclusive = first + length;
                    bool expected = false;
                    if (has_region) {
                        const bool in_region = region.start <= first &&
                                               last_exclusive <= region.start + region.size;
                        const bool in_heap = Overlaps(layout.heap, first, last_exclusive);
                        const bool in_alias = Overlaps(layout.alias, first, last_exclusive);
                        switch (state) {
                        case RegionState::Free:
                        case RegionState::Kernel:
                            expected = in_region;
                            break;
                        case RegionState::Normal:
                            expected = in_region && !in_alias;
                            break;
                        case RegionState::Ipc:
                        case RegionState::NonSecureIpc:
                        case RegionState::NonDeviceIpc:
                            expected = in_region && !in_heap;
                            break;
                        default:
                            expected = in_region && !in_heap && !in_alias;
                            break;
                        }
                    }
                    CHECK(regions.CanContain(first, length, state) == expected);
                }
            }
        }
    }
    return 0;
}

} // namespace

int main() {
    CHECK(RegionGettersReturnTheirOwnRegion() == 0);
    CHECK(ContainsFollowsTheAddressSpaceBounds() == 0);
    CHECK(AliasAndHeapRegionRangesAreExact() == 0);
    CHECK(RegionPerStateMatchesTheKernelTable() == 0);
    CHECK(CanContainOnSeparateRegions() == 0);
    CHECK(CanContainExcludesHeapAndAliasWhereTheKernelDoes() == 0);
    CHECK(EmptyHeapAndAliasRegionsAreNeverTouched() == 0);
    CHECK(EmptyRegionAtZeroKeepsTheKernelWraparound() == 0);
    CHECK(CanContainMatchesAnIntervalModelOnAGrid() == 0);
    std::puts("All synthetic native address region smoke checks passed.");
    return 0;
}
