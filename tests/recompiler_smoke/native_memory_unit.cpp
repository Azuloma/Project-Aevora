// smoke_native_memory_unit: layout, query, map/unmap, protect, attribute, heap, physical, shared
// and TLS rules of the standalone native mapper (core/native_memory/native_address_space.h).
#include <cstdint>
#include <cstdio>

#include "core/native_memory/native_address_space.h"

using namespace Core::NativeMemory;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

#define CHECK_RESULT(call, expected)                                                             \
    do {                                                                                         \
        const Result actual_result = (call);                                                     \
        if (actual_result != (expected)) {                                                       \
            std::fprintf(stderr, "FAIL line %d: %s gave %s, expected %s\n", __LINE__, #call,     \
                         ResultName(actual_result).data(), ResultName(expected).data());         \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr u64 kObservedCodeSize = 0x60F7000;
constexpr u64 kPhysicalCapacityBytes = 4 * GiB;
constexpr u64 kObservedLargeMapBytes = 3232497664;
constexpr u64 kObservedLargeUnmapBytes = 3228565504;

struct Lcg {
    u64 state;
    u64 operator()(u64 min, u64 max) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return min + (state >> 33) % (max - min + 1);
    }
};

LayoutParams ObservedParams(u64 code_address) {
    LayoutParams params;
    params.type = AddressSpaceType::Bits39;
    params.code_address = code_address;
    params.code_size = kObservedCodeSize;
    return params;
}

Layout ObservedLayout() {
    Layout layout;
    Lcg unused{0};
    if (ComputeLayout(ObservedParams(0x800A4000), unused, layout) != Result::Success) {
        std::fprintf(stderr, "observed layout failed\n");
    }
    return layout;
}

int LayoutMatchesObservedProcessLayouts() {
    Lcg unused{0};
    Layout first;
    CHECK_RESULT(ComputeLayout(ObservedParams(0x800A4000), unused, first), Result::Success);
    CHECK(first.width == 39);
    CHECK((first.kernel_map == Range{0x86200000, 0x1000000000}));
    CHECK((first.stack == Range{0x1086200000, 0x80000000}));
    CHECK((first.alias == Range{0x1106200000, 0x1000000000}));
    CHECK((first.heap == Range{0x2106200000, 0x200000000}));

    Layout second;
    CHECK_RESULT(ComputeLayout(ObservedParams(0x80C61000), unused, second), Result::Success);
    CHECK(second.kernel_map.start == 0x86E00000);
    CHECK(second.stack.start == 0x1086E00000);
    CHECK(second.alias.start == 0x1106E00000);
    CHECK(second.heap.start == 0x2106E00000);
    return 0;
}

bool RegionsAreDisjointAndInside(const Layout& layout, bool exclusive_stack_and_kernel_map) {
    const Range regions[] = {layout.alias, layout.heap, layout.stack, layout.kernel_map};
    const int count = exclusive_stack_and_kernel_map ? 4 : 2;
    for (int i = 0; i < count; ++i) {
        if (regions[i].end() > layout.address_space.end()) {
            return false;
        }
        for (int j = i + 1; j < count; ++j) {
            if (regions[i].Overlaps(regions[j].start, regions[j].size)) {
                return false;
            }
        }
    }
    return true;
}

int RandomizedLayoutsStayDisjoint() {
    const AddressSpaceType types[] = {AddressSpaceType::Bits39, AddressSpaceType::Bits36,
                                      AddressSpaceType::Bits32,
                                      AddressSpaceType::Bits32WithoutAlias};
    for (const AddressSpaceType type : types) {
        for (u64 seed = 1; seed <= 200; ++seed) {
            LayoutParams params;
            params.type = type;
            params.enable_aslr = true;
            params.code_address = type == AddressSpaceType::Bits39 ? 0x80000000 : 0x200000;
            params.code_size = 0x1000000;
            Lcg random{seed};
            Layout layout;
            CHECK_RESULT(ComputeLayout(params, random, layout), Result::Success);
            CHECK(RegionsAreDisjointAndInside(layout, type == AddressSpaceType::Bits39));
            CHECK(layout.heap.start % RegionAlignment == 0);
            CHECK(layout.alias.size == (type == AddressSpaceType::Bits32WithoutAlias
                                            ? 0
                                            : GetSpaceInfo(layout.width, SpaceKind::Alias).size));
        }
    }
    return 0;
}

int LayoutRejectsBadInputs() {
    Lcg unused{0};
    Layout layout;
    LayoutParams params = ObservedParams(0x800A4000);
    params.code_size = 0;
    CHECK_RESULT(ComputeLayout(params, unused, layout), Result::InvalidAddress);
    params = ObservedParams(0x7FFFFFFFF0);
    CHECK_RESULT(ComputeLayout(params, unused, layout), Result::InvalidAddress);
    return 0;
}

int LayoutDrawsRandomsInKernelOrder() {
    struct RecordingRandom {
        int calls = 0;
        bool bounds_match = true;
        u64 expected_max = 0;
        u64 operator()(u64 min, u64 max) {
            ++calls;
            bounds_match = bounds_match && min == 0 && max == expected_max;
            return static_cast<u64>(calls);
        }
    };
    LayoutParams params = ObservedParams(0x800A4000);
    params.enable_aslr = true;
    const u64 code_end = AlignUp(params.code_address + params.code_size, RegionAlignment);
    const u64 alloc_start = code_end;
    const u64 alias_size = GetSpaceInfo(39, SpaceKind::Alias).size;
    const u64 heap_size = GetSpaceInfo(39, SpaceKind::Heap).size;
    const u64 stack_size = GetSpaceInfo(39, SpaceKind::Stack).size;
    const u64 kernel_map_size = GetSpaceInfo(39, SpaceKind::MapSmall).size;
    RecordingRandom random;
    random.expected_max = ((1ULL << 39) - code_end - alias_size - heap_size - stack_size -
                           kernel_map_size) / RegionAlignment;
    Layout layout;
    CHECK_RESULT(ComputeLayout(params, random, layout), Result::Success);
    CHECK(random.calls == 4);
    CHECK(random.bounds_match);

    CHECK(layout.alias.start == alloc_start + 1 * RegionAlignment);
    CHECK(layout.heap.start == alloc_start + 2 * RegionAlignment + alias_size);
    CHECK(layout.stack.start == alloc_start + 3 * RegionAlignment + alias_size + heap_size);
    CHECK(layout.kernel_map.start ==
          alloc_start + 4 * RegionAlignment + alias_size + heap_size + stack_size);
    return 0;
}

int LayoutHonorsAslrBaseAndExtraAlias() {
    Lcg unused{0};
    LayoutParams params = ObservedParams(0x800A4000);
    params.aslr_space_start = 0x10000000;
    params.extra_alias_region = true;
    Layout layout;
    CHECK_RESULT(ComputeLayout(params, unused, layout), Result::Success);
    CHECK(layout.code.start == 0x10000000 + GetSpaceInfo(39, SpaceKind::Map39Bit).start);
    CHECK(layout.alias.size == GetSpaceInfo(39, SpaceKind::Alias).size + (layout.address_space.size >> 3));

    params.type = AddressSpaceType::Bits36;
    params.code_address = 0x200000;
    CHECK_RESULT(ComputeLayout(params, unused, layout), Result::Success);
    CHECK(layout.alias.size == GetSpaceInfo(36, SpaceKind::Alias).size);
    return 0;
}

int QueryDescribesBlocks() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const Layout& layout = space.GetLayout();
    MemoryInfo info = space.QueryInfo(layout.alias.start + 0x5000);
    CHECK(info.base == 0 && info.size == layout.address_space.end());
    CHECK(info.state == MemoryState::Free);

    info = space.QueryInfo(layout.address_space.end() + 0x1000);
    CHECK(info.state == MemoryState::Inaccessible);
    CHECK(info.base == layout.address_space.end() && info.size == 0 - layout.address_space.end());

    CHECK_RESULT(space.MapProcessMemory(0x800A4000, 0x10000, MemoryState::Code,
                                        Permission::Read),
                 Result::Success);
    info = space.QueryInfo(0x800A8000);
    CHECK(info.base == 0x800A4000 && info.size == 0x10000);
    CHECK(info.state == MemoryState::Code && info.permission == 1);
    CHECK(space.QueryInfo(0x800B4000).state == MemoryState::Free);
    CHECK_RESULT(space.MapProcessMemory(0x800A4000, 0x1000, MemoryState::Code, Permission::Read),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(space.MapProcessMemory(0x800B4000, 0x1000, MemoryState::Normal,
                                        Permission::Read),
                 Result::InvalidCombination);
    CHECK_RESULT(space.MapProcessMemory(layout.alias.start, 0x1000, MemoryState::Code,
                                        Permission::Read),
                 Result::InvalidMemoryRegion);
    return 0;
}

int RangeValidationIsReported() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const u64 alias = space.GetLayout().alias.start;
    CHECK_RESULT(space.MapPhysicalMemory(alias + 1, 0x1000), Result::InvalidAddress);
    CHECK_RESULT(space.MapPhysicalMemory(alias, 0), Result::InvalidSize);
    CHECK_RESULT(space.MapPhysicalMemory(alias, 0x1800), Result::InvalidSize);
    CHECK_RESULT(space.MapPhysicalMemory(space.GetLayout().address_space.end(), 0x1000),
                 Result::InvalidMemoryRegion);
    CHECK_RESULT(space.MapPhysicalMemory(0x1000, 0x1000), Result::InvalidMemoryRegion);
    return 0;
}

int PhysicalMemoryIsAccountedAndCapped() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const u64 alias = space.GetLayout().alias.start;
    CHECK_RESULT(space.MapPhysicalMemory(alias, kObservedLargeMapBytes), Result::Success);
    CHECK(space.PhysicalBytesInUse() == kObservedLargeMapBytes);
    MemoryInfo info = space.QueryInfo(alias + 0x1000);
    CHECK(info.state == MemoryState::Normal && info.permission == 3);
    CHECK(info.base == alias && info.size == kObservedLargeMapBytes);

    CHECK_RESULT(space.MapPhysicalMemory(alias + 0x1000, 0x10000), Result::Success);
    CHECK(space.PhysicalBytesInUse() == kObservedLargeMapBytes);

    const u64 tail = alias + kObservedLargeMapBytes;
    CHECK_RESULT(space.MapPhysicalMemory(tail, GiB), Result::LimitReached);
    CHECK(space.PhysicalBytesInUse() == kObservedLargeMapBytes);
    CHECK(space.QueryInfo(tail).state == MemoryState::Free);

    CHECK_RESULT(space.MapPhysicalMemory(alias + kObservedLargeMapBytes - 0x1000, 0x2000),
                 Result::Success);
    CHECK(space.PhysicalBytesInUse() == kObservedLargeMapBytes + 0x1000);

    CHECK_RESULT(space.UnmapPhysicalMemory(alias, kObservedLargeUnmapBytes), Result::Success);
    CHECK(space.PhysicalBytesInUse() == kObservedLargeMapBytes + 0x1000 - kObservedLargeUnmapBytes);
    CHECK(space.QueryInfo(alias).state == MemoryState::Free);
    CHECK(space.QueryInfo(alias + kObservedLargeUnmapBytes).state == MemoryState::Normal);
    CHECK_RESULT(space.UnmapPhysicalMemory(alias, kObservedLargeUnmapBytes), Result::Success);
    CHECK(space.PhysicalBytesInUse() == kObservedLargeMapBytes + 0x1000 - kObservedLargeUnmapBytes);

    // The kernel maps over any non-Free page and unmaps Normal pages of any permission.
    const u64 other = alias + 8 * GiB;
    CHECK_RESULT(space.MapPhysicalMemory(other, 0x2000), Result::Success);
    CHECK_RESULT(space.SetMemoryAttribute(other, 0x1000, AttributeUncached, AttributeUncached),
                 Result::Success);
    CHECK_RESULT(space.MapPhysicalMemory(other, 0x2000), Result::Success);
    CHECK_RESULT(space.SetMemoryAttribute(other, 0x1000, AttributeUncached, 0), Result::Success);
    CHECK_RESULT(space.SetMemoryPermission(other, 0x1000, Permission::Read), Result::Success);
    CHECK_RESULT(space.UnmapPhysicalMemory(other, 0x2000), Result::Success);
    CHECK(space.QueryInfo(other).state == MemoryState::Free);
    CHECK_RESULT(space.SetMemoryAttribute(other, 0x1000, AttributeUncached, AttributeUncached),
                 Result::InvalidCurrentMemory);

    CHECK_RESULT(space.MapPhysicalMemory(space.GetLayout().heap.start, 0x1000),
                 Result::InvalidMemoryRegion);
    CHECK_RESULT(space.UnmapPhysicalMemory(space.GetLayout().stack.start, 0x1000),
                 Result::InvalidMemoryRegion);
    return 0;
}

int StackRemapRoundTrips() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const Layout& layout = space.GetLayout();
    const u64 src = layout.alias.start + 0x100000;
    const u64 dst = layout.stack.start + 0x40000;
    CHECK_RESULT(space.MapMemory(dst, src, 0x10000), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.MapPhysicalMemory(src, 0x100000), Result::Success);
    const size_t blocks_before = space.BlockCount();

    CHECK_RESULT(space.MapMemory(layout.alias.start + 0x900000, src, 0x10000),
                 Result::InvalidMemoryRegion);
    CHECK_RESULT(space.MapMemory(dst, src, 0x10000), Result::Success);
    MemoryInfo info = space.QueryInfo(src);
    CHECK(info.state == MemoryState::Normal && info.attribute == AttributeLocked);
    CHECK(info.permission == 0 && info.base == src && info.size == 0x10000);
    info = space.QueryInfo(dst);
    CHECK(info.state == MemoryState::Stack && info.permission == 3 && info.size == 0x10000);

    CHECK_RESULT(space.MapMemory(dst + 0x1000, src + 0x10000, 0x1000), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.SetMemoryPermission(src, 0x10000, Permission::Read),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(space.UnmapPhysicalMemory(src, 0x10000), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.UnmapMemory(dst, src + 0x1000, 0x10000), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.UnmapMemory(dst, src + 0x20000, 0x10000), Result::InvalidCurrentMemory);

    CHECK_RESULT(space.UnmapMemory(dst, src, 0x8000), Result::Success);
    CHECK(space.QueryInfo(dst).state == MemoryState::Free);
    CHECK(space.QueryInfo(dst + 0x8000).state == MemoryState::Stack);
    CHECK_RESULT(space.UnmapMemory(dst, src, 0x8000), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.UnmapMemory(dst + 0x8000, src + 0x8000, 0x8000), Result::Success);

    info = space.QueryInfo(src);
    CHECK(info.attribute == AttributeNone && info.permission == 3 && info.size == 0x100000);
    CHECK(space.BlockCount() == blocks_before);
    return 0;
}

int UnmapRejectsAnAliasThatWasNeverMapped() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const Layout& layout = space.GetLayout();
    const u64 first_src = layout.alias.start;
    const u64 second_src = layout.alias.start + 0x100000;
    const u64 dst = layout.stack.start;
    CHECK_RESULT(space.MapPhysicalMemory(first_src, 0x200000), Result::Success);
    CHECK_RESULT(space.MapMemory(dst, first_src, 0x10000), Result::Success);
    CHECK_RESULT(space.MapMemory(dst + 0x10000, second_src, 0x10000), Result::Success);
    CHECK_RESULT(space.UnmapMemory(dst, second_src, 0x10000), Result::InvalidMemoryRegion);
    CHECK(space.QueryInfo(dst).state == MemoryState::Stack);
    CHECK_RESULT(space.UnmapMemory(dst, first_src, 0x10000), Result::Success);
    return 0;
}

int UnmapSpansAdjacentAliasRecords() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const Layout& layout = space.GetLayout();
    const u64 src = layout.alias.start;
    const u64 dst = layout.stack.start;
    CHECK_RESULT(space.MapPhysicalMemory(src, 0x40000), Result::Success);
    CHECK_RESULT(space.MapMemory(dst, src, 0x10000), Result::Success);
    CHECK_RESULT(space.MapMemory(dst + 0x10000, src + 0x10000, 0x10000), Result::Success);
    CHECK_RESULT(space.MapMemory(dst + 0x30000, src + 0x20000, 0x10000), Result::Success);
    CHECK_RESULT(space.UnmapMemory(dst + 0x8000, src + 0x8000, 0x10000), Result::Success);
    CHECK_RESULT(space.UnmapMemory(dst, src, 0x8000), Result::Success);
    CHECK_RESULT(space.UnmapMemory(dst + 0x18000, src + 0x18000, 0x8000), Result::Success);
    CHECK_RESULT(space.UnmapMemory(dst + 0x30000, src + 0x20000, 0x10000), Result::Success);
    CHECK(space.QueryInfo(dst).state == MemoryState::Free);
    CHECK(space.QueryInfo(src).attribute == AttributeNone && space.QueryInfo(src).size == 0x40000);
    return 0;
}

int PermissionsFollowStateFlags() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const u64 alias = space.GetLayout().alias.start;
    CHECK_RESULT(space.MapProcessMemory(0x800A4000, 0x10000, MemoryState::Code, Permission::Read),
                 Result::Success);
    CHECK_RESULT(space.SetMemoryPermission(0x800A4000, 0x10000, Permission::ReadWrite),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(space.SetMemoryPermission(alias, 0x1000, Permission::Read),
                 Result::InvalidCurrentMemory);

    CHECK_RESULT(space.MapPhysicalMemory(alias, 0x4000), Result::Success);
    CHECK_RESULT(space.SetMemoryPermission(alias + 0x1000, 0x1000, Permission::Read), Result::Success);
    MemoryInfo info = space.QueryInfo(alias + 0x1000);
    CHECK(info.base == alias + 0x1000 && info.size == 0x1000 && info.permission == 1);
    CHECK_RESULT(space.SetMemoryPermission(alias, 0x4000, Permission::ReadWrite),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(space.SetMemoryPermission(alias + 0x1000, 0x1000, Permission::ReadWrite),
                 Result::Success);
    CHECK(space.QueryInfo(alias).size == 0x4000);
    CHECK_RESULT(space.SetMemoryPermission(alias, 0x1000, static_cast<Permission>(4)),
                 Result::InvalidNewMemoryPermission);
    return 0;
}

int AttributesMergeAndSplitBlocks() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const u64 alias = space.GetLayout().alias.start;
    CHECK_RESULT(space.MapPhysicalMemory(alias, 0x10000), Result::Success);
    const size_t blocks_before = space.BlockCount();

    CHECK_RESULT(space.SetMemoryAttribute(alias + 0x2000, 0x2000, AttributeUncached, AttributeUncached),
                 Result::Success);
    CHECK(space.BlockCount() == blocks_before + 2);
    MemoryInfo info = space.QueryInfo(alias + 0x3000);
    CHECK(info.attribute == AttributeUncached && info.base == alias + 0x2000 && info.size == 0x2000);

    CHECK_RESULT(space.SetMemoryAttribute(alias + 0x2000, 0x2000, AttributeUncached, 0),
                 Result::Success);
    CHECK(space.BlockCount() == blocks_before);
    CHECK(space.QueryInfo(alias).attribute == AttributeNone);

    CHECK_RESULT(space.SetMemoryAttribute(alias, 0x1000, AttributeLocked, AttributeLocked),
                 Result::InvalidCombination);
    CHECK_RESULT(space.SetMemoryAttribute(alias + 0x20000, 0x1000, AttributeUncached, AttributeUncached),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(space.SetMemoryAttribute(0x800A4000, 0x1000, AttributeUncached, AttributeUncached),
                 Result::InvalidCurrentMemory);
    return 0;
}

int HeapGrowsShrinksAndHonorsLimits() {
    AddressSpace space(ObservedLayout(), 64 * MiB);
    const Layout& layout = space.GetLayout();
    u64 heap = 0;
    CHECK_RESULT(space.SetHeapSize(heap, 16 * MiB), Result::OutOfMemory);
    space.SetMaxHeapSize(48 * MiB);
    CHECK_RESULT(space.SetHeapSize(heap, 0x1000), Result::InvalidSize);
    CHECK_RESULT(space.SetHeapSize(heap, 16 * MiB), Result::Success);
    CHECK(heap == layout.heap.start && space.HeapSizeBytes() == 16 * MiB);
    MemoryInfo info = space.QueryInfo(layout.heap.start + MiB);
    CHECK(info.state == MemoryState::Normal && info.size == 16 * MiB && info.permission == 3);
    CHECK(space.PhysicalBytesInUse() == 16 * MiB);

    CHECK_RESULT(space.SetHeapSize(heap, 64 * MiB), Result::OutOfMemory);
    CHECK_RESULT(space.SetHeapSize(heap, 48 * MiB), Result::Success);
    space.SetMaxHeapSize(64 * MiB);
    CHECK_RESULT(space.SetHeapSize(heap, 64 * MiB), Result::Success);
    CHECK_RESULT(space.SetHeapSize(heap, 66 * MiB), Result::OutOfMemory);

    CHECK_RESULT(space.SetHeapSize(heap, 8 * MiB), Result::Success);
    CHECK(space.PhysicalBytesInUse() == 8 * MiB);
    CHECK(space.QueryInfo(layout.heap.start + 8 * MiB).state == MemoryState::Free);
    CHECK_RESULT(space.SetHeapSize(heap, 0), Result::Success);
    CHECK(space.PhysicalBytesInUse() == 0 && space.BlockCount() == 1);

    CHECK_RESULT(space.SetHeapSize(heap, 16 * MiB), Result::Success);
    CHECK_RESULT(space.SetMemoryAttribute(layout.heap.start, 0x1000, AttributeUncached,
                                          AttributeUncached),
                 Result::Success);
    CHECK_RESULT(space.SetHeapSize(heap, 0), Result::InvalidCurrentMemory);
    return 0;
}

int HeapGrowthIsCappedByPhysicalCapacity() {
    AddressSpace space(ObservedLayout(), 64 * MiB);
    space.SetMaxHeapSize(128 * MiB);
    u64 heap = 0;
    CHECK_RESULT(space.SetHeapSize(heap, 66 * MiB), Result::LimitReached);
    CHECK(space.PhysicalBytesInUse() == 0 && space.HeapSizeBytes() == 0);
    CHECK_RESULT(space.SetHeapSize(heap, 64 * MiB), Result::Success);
    CHECK_RESULT(space.MapPhysicalMemory(space.GetLayout().alias.start, 0x1000),
                 Result::LimitReached);
    return 0;
}

int SharedMemoryAndTlsStayInTheirRegions() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    const Layout& layout = space.GetLayout();
    const u64 shared = 0x90000000;
    CHECK_RESULT(space.MapSharedMemory(shared, 0x10000, Permission::Read), Result::Success);
    MemoryInfo info = space.QueryInfo(shared);
    CHECK(info.state == MemoryState::Shared && info.permission == 1 && info.size == 0x10000);
    CHECK_RESULT(space.MapSharedMemory(shared, 0x1000, Permission::Read), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.MapSharedMemory(layout.alias.start, 0x1000, Permission::Read),
                 Result::InvalidMemoryRegion);
    CHECK_RESULT(space.MapSharedMemory(layout.heap.start, 0x1000, Permission::Read),
                 Result::InvalidMemoryRegion);
    CHECK_RESULT(space.MapSharedMemory(0x1000, 0x1000, Permission::Read), Result::InvalidMemoryRegion);
    CHECK_RESULT(space.MapSharedMemory(shared + 0x10000, 0x1000, Permission::None),
                 Result::InvalidNewMemoryPermission);
    CHECK_RESULT(space.UnmapSharedMemory(shared, 0x8000), Result::Success);
    CHECK(space.QueryInfo(shared + 0x8000).state == MemoryState::Shared);
    CHECK_RESULT(space.UnmapSharedMemory(shared, 0x8000), Result::InvalidCurrentMemory);

    const u64 tls = layout.kernel_map.start + 0x8000;
    CHECK_RESULT(space.MapThreadLocalPage(tls), Result::Success);
    info = space.QueryInfo(tls);
    CHECK(info.state == MemoryState::ThreadLocal && info.permission == 3 && info.size == PageSize);
    CHECK_RESULT(space.MapThreadLocalPage(tls), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.MapThreadLocalPage(layout.stack.start), Result::InvalidMemoryRegion);
    CHECK_RESULT(space.UnmapSharedMemory(tls, PageSize), Result::InvalidCurrentMemory);
    CHECK_RESULT(space.UnmapThreadLocalPage(tls), Result::Success);
    CHECK(space.QueryInfo(tls).state == MemoryState::Free);
    return 0;
}

int IpcMapAliasFailsFast() {
    AddressSpace space(ObservedLayout(), kPhysicalCapacityBytes);
    CHECK_RESULT(space.SetupForIpc(space.GetLayout().alias.start, 0x1000), Result::NotSupported);
    return 0;
}

} // namespace

int main() {
    CHECK(LayoutMatchesObservedProcessLayouts() == 0);
    CHECK(RandomizedLayoutsStayDisjoint() == 0);
    CHECK(LayoutRejectsBadInputs() == 0);
    CHECK(LayoutDrawsRandomsInKernelOrder() == 0);
    CHECK(LayoutHonorsAslrBaseAndExtraAlias() == 0);
    CHECK(QueryDescribesBlocks() == 0);
    CHECK(RangeValidationIsReported() == 0);
    CHECK(PhysicalMemoryIsAccountedAndCapped() == 0);
    CHECK(StackRemapRoundTrips() == 0);
    CHECK(UnmapRejectsAnAliasThatWasNeverMapped() == 0);
    CHECK(UnmapSpansAdjacentAliasRecords() == 0);
    CHECK(PermissionsFollowStateFlags() == 0);
    CHECK(AttributesMergeAndSplitBlocks() == 0);
    CHECK(HeapGrowsShrinksAndHonorsLimits() == 0);
    CHECK(HeapGrowthIsCappedByPhysicalCapacity() == 0);
    CHECK(SharedMemoryAndTlsStayInTheirRegions() == 0);
    CHECK(IpcMapAliasFailsFast() == 0);
    std::puts("All synthetic native memory smoke checks passed.");
    return 0;
}
