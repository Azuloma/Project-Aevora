// smoke_native_process_memory_unit: physical backing, host mapping, locks and run-map rules of the
// standalone mapped address space (core/native_memory/native_process_memory.h).
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "core/native_memory/native_process_memory.h"

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
constexpr u64 kPhysicalCapacityBytes = 512 * MiB;
constexpr u64 kMaxExtentBytes = 0x10000;
constexpr u64 kFirstPhysicalOffset = 0x100000;

// Hands out fragmented extents and rejects a double free or a free of foreign pages.
class FakeSource final : public PhysicalPageSource {
public:
    bool Allocate(u64 size, std::vector<PhysicalExtent>& out) override {
        ++allocate_calls;
        if (allocated_bytes + size > limit_bytes) {
            return false;
        }
        for (u64 left = size; left != 0;) {
            const u64 length = left < kMaxExtentBytes ? left : kMaxExtentBytes;
            out.push_back({next_phys, length});
            next_phys += length + PageSize;
            left -= length;
        }
        if (short_by_one_page) {
            out.back().size -= PageSize;
            if (out.back().size == 0) {
                out.pop_back();
            }
        }
        for (const PhysicalExtent& extent : out) {
            for (u64 page = 0; page < extent.size; page += PageSize) {
                live_pages.insert(extent.phys + page);
            }
            allocated_bytes += extent.size;
        }
        return true;
    }

    void Free(const std::vector<PhysicalExtent>& extents) override {
        for (const PhysicalExtent& extent : extents) {
            for (u64 page = 0; page < extent.size; page += PageSize) {
                if (live_pages.erase(extent.phys + page) == 0) {
                    ++errors;
                }
            }
            allocated_bytes -= extent.size;
        }
    }

    u64 limit_bytes = kPhysicalCapacityBytes;
    u64 allocated_bytes = 0;
    u64 next_phys = kFirstPhysicalOffset;
    bool short_by_one_page = false;
    std::set<u64> live_pages;
    int errors = 0;
    int allocate_calls = 0;
};

// Tracks the host page table per page and flags maps over live pages and unmaps of absent pages.
class FakeHost final : public HostMemoryMap {
public:
    struct Page {
        u64 phys;
        u8 permission;
    };

    void Map(u64 address, u64 size, u64 phys, u8 permission) override {
        for (u64 offset = 0; offset < size; offset += PageSize) {
            if (!pages.emplace(address + offset, Page{phys + offset, permission}).second) {
                ++errors;
            }
        }
    }
    void Unmap(u64 address, u64 size) override {
        for (u64 offset = 0; offset < size; offset += PageSize) {
            if (pages.erase(address + offset) == 0) {
                ++errors;
            }
        }
    }
    void Protect(u64 address, u64 size, u8 permission) override {
        for (u64 offset = 0; offset < size; offset += PageSize) {
            const auto it = pages.find(address + offset);
            if (it == pages.end()) {
                ++errors;
            } else {
                it->second.permission = permission;
            }
        }
    }

    bool IsMapped(u64 address, u64 size) const {
        for (u64 offset = 0; offset < size; offset += PageSize) {
            if (pages.count(address + offset) == 0) {
                return false;
            }
        }
        return true;
    }
    bool IsUnmapped(u64 address, u64 size) const {
        for (u64 offset = 0; offset < size; offset += PageSize) {
            if (pages.count(address + offset) != 0) {
                return false;
            }
        }
        return true;
    }
    u8 PermissionAt(u64 address) const {
        return pages.at(address).permission;
    }

    std::map<u64, Page> pages;
    int errors = 0;
};

struct Fixture {
    Layout layout;
    FakeSource source;
    FakeHost host;
    MappedAddressSpace memory;

    Fixture() : layout(MakeLayout()), memory(layout, kPhysicalCapacityBytes, source, host) {}

    static Layout MakeLayout() {
        LayoutParams params;
        params.type = AddressSpaceType::Bits39;
        params.code_address = 0x800A4000;
        params.code_size = kObservedCodeSize;
        struct Zero {
            u64 operator()(u64 min, u64) const {
                return min;
            }
        } zero;
        Layout layout;
        if (ComputeLayout(params, zero, layout) != Result::Success) {
            std::fprintf(stderr, "layout failed\n");
        }
        return layout;
    }

    // Every host page must resolve to the physical page the mapped space reports for it.
    bool Consistent() const {
        if (source.errors != 0 || host.errors != 0 || memory.InconsistencyCount() != 0) {
            return false;
        }
        for (const auto& [address, page] : host.pages) {
            u64 phys = 0;
            if (!memory.GetPhysicalAddress(address, phys) || phys != page.phys) {
                return false;
            }
        }
        return true;
    }
};

int PhysicalMapAllocatesOnlyFreePages() {
    Fixture f;
    const u64 base = f.layout.alias.start;
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x10000), Result::Success);
    CHECK(f.source.allocated_bytes == 0x10000);
    CHECK(f.host.IsMapped(base, 0x10000));
    CHECK(f.host.PermissionAt(base) == PermUserReadWrite);
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x30000), Result::Success);
    CHECK(f.source.allocated_bytes == 0x30000);
    CHECK(f.host.IsMapped(base, 0x30000));
    CHECK(f.Consistent());

    u64 phys = 0;
    CHECK(f.memory.GetPhysicalAddress(base + 0x2000, phys));
    CHECK(f.source.live_pages.count(phys) == 1);
    std::vector<PhysicalExtent> extents;
    CHECK(f.memory.QueryPhysicalExtents(base, 0x30000, extents));
    u64 total = 0;
    for (const PhysicalExtent& extent : extents) {
        total += extent.size;
    }
    CHECK(total == 0x30000);

    CHECK_RESULT(f.memory.UnmapPhysicalMemory(base, 0x30000), Result::Success);
    CHECK(f.source.allocated_bytes == 0);
    CHECK(f.source.live_pages.empty());
    CHECK(f.host.pages.empty());
    CHECK(!f.memory.GetPhysicalAddress(base, phys));
    CHECK(f.Consistent());
    return 0;
}

int PhysicalMapRollsBackOnFailure() {
    Fixture f;
    const u64 base = f.layout.alias.start;
    f.source.limit_bytes = 0x10000;
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x20000), Result::OutOfMemory);
    CHECK(f.source.allocated_bytes == 0 && f.host.pages.empty());
    CHECK(f.memory.Space().QueryInfo(base).state == MemoryState::Free);

    f.source.limit_bytes = 0;
    const int calls_before_invalid = f.source.allocate_calls;
    CHECK_RESULT(f.memory.MapPhysicalMemory(f.layout.heap.start, 0x10000),
                 Result::InvalidMemoryRegion);
    CHECK_RESULT(f.memory.MapPhysicalMemory(base + 1, 0x10000), Result::InvalidAddress);
    f.memory.SetPhysicalCapacityBytes(0x10000);
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x20000), Result::LimitReached);
    CHECK(f.source.allocate_calls == calls_before_invalid);
    f.source.limit_bytes = kPhysicalCapacityBytes;
    CHECK(f.source.allocated_bytes == 0 && f.host.pages.empty());

    f.memory.SetPhysicalCapacityBytes(kPhysicalCapacityBytes);
    f.source.short_by_one_page = true;
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x20000), Result::OutOfMemory);
    CHECK(f.source.live_pages.empty() && f.host.pages.empty());
    CHECK(f.Consistent());
    return 0;
}

int HeapGrowsAndShrinksWithBacking() {
    Fixture f;
    f.memory.SetMaxHeapSize(64 * MiB);
    u64 address = 0;
    CHECK_RESULT(f.memory.SetHeapSize(address, 0x400000), Result::Success);
    CHECK(address == f.layout.heap.start);
    CHECK(f.source.allocated_bytes == 0x400000);
    CHECK(f.host.IsMapped(address, 0x400000));
    CHECK_RESULT(f.memory.SetHeapSize(address, 0x200000), Result::Success);
    CHECK(f.source.allocated_bytes == 0x200000);
    CHECK(f.host.IsMapped(address, 0x200000));
    CHECK(f.host.IsUnmapped(address + 0x200000, 0x200000));
    const int calls_before_invalid = f.source.allocate_calls;
    CHECK_RESULT(f.memory.SetHeapSize(address, 0x1000), Result::InvalidSize);
    CHECK_RESULT(f.memory.SetHeapSize(address, 128 * MiB), Result::OutOfMemory);
    CHECK(f.source.allocated_bytes == 0x200000);
    f.memory.SetPhysicalCapacityBytes(0x200000);
    CHECK_RESULT(f.memory.SetHeapSize(address, 0x400000), Result::LimitReached);
    CHECK(f.source.allocated_bytes == 0x200000);
    CHECK(f.source.allocate_calls == calls_before_invalid);
    CHECK_RESULT(f.memory.SetHeapSize(address, 0), Result::Success);
    CHECK(f.source.live_pages.empty() && f.host.pages.empty());
    CHECK(f.Consistent());
    return 0;
}

int StackAliasMovesTheHostMapping() {
    Fixture f;
    const u64 src = f.layout.alias.start;
    const u64 dst = f.layout.stack.start + 0x10000;
    CHECK_RESULT(f.memory.MapPhysicalMemory(src, 0x20000), Result::Success);
    u64 src_phys = 0;
    CHECK(f.memory.GetPhysicalAddress(src + 0x3000, src_phys));

    CHECK_RESULT(f.memory.MapMemory(dst, src, 0x20000), Result::Success);
    CHECK(f.host.IsUnmapped(src, 0x20000));
    CHECK(f.host.IsMapped(dst, 0x20000));
    u64 dst_phys = 0;
    CHECK(f.memory.GetPhysicalAddress(dst + 0x3000, dst_phys) && dst_phys == src_phys);
    CHECK(f.source.allocated_bytes == 0x20000);
    CHECK(f.Consistent());

    CHECK_RESULT(f.memory.UnmapPhysicalMemory(src, 0x20000), Result::InvalidCurrentMemory);
    CHECK(f.source.allocated_bytes == 0x20000 && f.host.IsMapped(dst, 0x20000));

    CHECK_RESULT(f.memory.UnmapMemory(dst, src, 0x20000), Result::Success);
    CHECK(f.host.IsUnmapped(dst, 0x20000));
    CHECK(f.host.IsMapped(src, 0x20000));
    CHECK(f.host.PermissionAt(src) == PermUserReadWrite);
    CHECK(f.source.allocated_bytes == 0x20000);
    u64 restored = 0;
    CHECK(f.memory.GetPhysicalAddress(src + 0x3000, restored) && restored == src_phys);
    CHECK(!f.memory.GetPhysicalAddress(dst, restored));
    CHECK(f.Consistent());

    CHECK_RESULT(f.memory.UnmapPhysicalMemory(src, 0x20000), Result::Success);
    CHECK(f.source.live_pages.empty() && f.host.pages.empty());
    return 0;
}

int PermissionChangesReachTheHost() {
    Fixture f;
    const u64 base = f.layout.alias.start;
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x4000), Result::Success);
    CHECK_RESULT(f.memory.SetMemoryPermission(base, 0x2000, Permission::Read), Result::Success);
    CHECK(f.host.PermissionAt(base) == ToStoredPermission(Permission::Read));
    CHECK(f.host.PermissionAt(base + 0x2000) == PermUserReadWrite);
    CHECK_RESULT(f.memory.SetMemoryPermission(base, 0x2000, Permission::ReadExecute),
                 Result::InvalidNewMemoryPermission);
    CHECK(f.host.PermissionAt(base) == ToStoredPermission(Permission::Read));
    CHECK(f.Consistent());
    return 0;
}

int ExternalPagesAreMappedButNeverFreed() {
    Fixture f;
    const u64 tls = f.layout.kernel_map.start + 0x8000;
    CHECK_RESULT(f.memory.MapThreadLocalPage(tls, 0x7000), Result::Success);
    u64 phys = 0;
    CHECK(f.memory.GetPhysicalAddress(tls, phys) && phys == 0x7000);
    CHECK(f.host.IsMapped(tls, PageSize));
    CHECK_RESULT(f.memory.MapThreadLocalPage(tls, 0x8000), Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.UnmapThreadLocalPage(tls), Result::Success);
    CHECK(f.host.pages.empty() && !f.memory.GetPhysicalAddress(tls, phys));

    const u64 shared = f.layout.alias_code.start + 0x4000;
    const std::vector<PhysicalExtent> group = {{0x20000, 0x2000}, {0x80000, 0x3000}};
    CHECK_RESULT(f.memory.MapSharedMemory(shared, 0x4000, Permission::Read, group),
                 Result::InvalidSize);
    CHECK(f.host.pages.empty());
    CHECK_RESULT(f.memory.MapSharedMemory(shared, 0x5000, Permission::Read, group), Result::Success);
    CHECK(f.host.IsMapped(shared, 0x5000));
    CHECK(f.host.PermissionAt(shared) == ToStoredPermission(Permission::Read));
    CHECK(f.memory.GetPhysicalAddress(shared + 0x1000, phys) && phys == 0x21000);
    CHECK(f.memory.GetPhysicalAddress(shared + 0x2000, phys) && phys == 0x80000);
    CHECK_RESULT(f.memory.UnmapSharedMemory(shared, 0x5000), Result::Success);
    CHECK(f.host.pages.empty());

    const u64 io = f.layout.alias_code.start + 0x40000;
    CHECK_RESULT(f.memory.MapIoMemory(io, 0x5000, Permission::ReadWrite, group), Result::Success);
    CHECK(f.memory.Space().QueryInfo(io).state == MemoryState::Io);
    CHECK_RESULT(f.memory.UnmapSharedMemory(io, 0x5000), Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.UnmapIoMemory(io, 0x5000), Result::Success);
    CHECK(f.host.pages.empty() && f.source.errors == 0 && f.source.allocated_bytes == 0);
    CHECK(f.Consistent());
    return 0;
}

int CodeSegmentsFollowProcessPermissions() {
    Fixture f;
    const u64 code = f.layout.code.start;
    const std::vector<PhysicalExtent> backing = {{0x100000, 0x3000}};
    CHECK_RESULT(f.memory.MapProcessMemory(code, 0x3000, MemoryState::Normal, Permission::Read,
                                           backing),
                 Result::InvalidCombination);
    CHECK_RESULT(f.memory.MapProcessMemory(code, 0x3000, MemoryState::Code, Permission::Read, backing),
                 Result::Success);
    CHECK_RESULT(f.memory.SetProcessMemoryPermission(code, 0x3000, Permission::ReadExecute),
                 Result::Success);
    CHECK(f.memory.Space().QueryInfo(code).state == MemoryState::Code);
    CHECK(f.host.PermissionAt(code) == ToStoredPermission(Permission::ReadExecute));
    CHECK_RESULT(f.memory.SetProcessMemoryPermission(code, 0x3000, Permission::None),
                 Result::Success);
    CHECK(f.memory.Space().QueryInfo(code).state == MemoryState::Code);
    CHECK(f.host.PermissionAt(code) == ToStoredPermission(Permission::None));
    CHECK_RESULT(f.memory.SetProcessMemoryPermission(code, 0x3000, Permission::ReadWrite),
                 Result::Success);
    CHECK(f.memory.Space().QueryInfo(code).state == MemoryState::CodeData);
    CHECK(f.host.PermissionAt(code) == PermUserReadWrite);
    CHECK_RESULT(f.memory.SetProcessMemoryPermission(code + 0x10000000, 0x1000, Permission::Read),
                 Result::InvalidCurrentMemory);
    CHECK(f.Consistent());
    return 0;
}

int TransferMemoryLocksPinTheRange() {
    Fixture f;
    const u64 base = f.layout.alias.start;
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x8000), Result::Success);
    std::vector<PhysicalExtent> pages;
    CHECK_RESULT(f.memory.LockForTransferMemory(base, 0x8000, Permission::Read, pages),
                 Result::Success);
    u64 total = 0;
    for (const PhysicalExtent& extent : pages) {
        total += extent.size;
    }
    CHECK(total == 0x8000);
    const MemoryInfo info = f.memory.Space().QueryInfo(base);
    CHECK(info.attribute == AttributeLocked && info.size == 0x8000);
    CHECK(f.host.PermissionAt(base) == ToStoredPermission(Permission::Read));

    CHECK_RESULT(f.memory.LockForTransferMemory(base, 0x8000, Permission::Read, pages),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.UnmapPhysicalMemory(base, 0x8000), Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.SetMemoryPermission(base, 0x8000, Permission::Read),
                 Result::InvalidCurrentMemory);
    CHECK(f.source.allocated_bytes == 0x8000);

    CHECK_RESULT(f.memory.UnlockForTransferMemory(base, 0x8000), Result::Success);
    CHECK(f.memory.Space().QueryInfo(base).attribute == AttributeNone);
    CHECK(f.host.PermissionAt(base) == PermUserReadWrite);
    CHECK_RESULT(f.memory.UnlockForTransferMemory(base, 0x8000), Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.LockForTransferMemory(base, 0x8000, Permission::None, pages),
                 Result::Success);
    CHECK(f.host.PermissionAt(base) == ToStoredPermission(Permission::None));
    CHECK(f.memory.Space().QueryInfo(base).permission == 0);
    CHECK_RESULT(f.memory.UnlockForTransferMemory(base, 0x8000), Result::Success);
    CHECK(f.host.PermissionAt(base) == PermUserReadWrite);
    CHECK_RESULT(f.memory.UnmapPhysicalMemory(base, 0x8000), Result::Success);
    CHECK(f.Consistent());
    return 0;
}

int DeviceMapLocksAreCounted() {
    Fixture f;
    const u64 base = f.layout.alias.start;
    CHECK_RESULT(f.memory.MapPhysicalMemory(base, 0x8000), Result::Success);
    bool is_io = true;
    CHECK_RESULT(f.memory.LockForMapDevice(base, 0x8000, 0, false, true, is_io), Result::Success);
    CHECK(!is_io);
    CHECK_RESULT(f.memory.LockForMapDevice(base, 0x4000, 0, false, true, is_io), Result::Success);
    CHECK((f.memory.Space().QueryInfo(base).attribute & AttributeDeviceShared) != 0);

    CHECK_RESULT(f.memory.UnlockForDevice(base, 0x8000), Result::Success);
    CHECK((f.memory.Space().QueryInfo(base).attribute & AttributeDeviceShared) != 0);
    CHECK((f.memory.Space().QueryInfo(base + 0x4000).attribute & AttributeDeviceShared) == 0);
    CHECK_RESULT(f.memory.UnlockForDevice(base, 0x8000), Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.UnlockForDevice(base, 0x4000), Result::Success);
    CHECK(f.memory.Space().QueryInfo(base).attribute == AttributeNone);
    CHECK(f.memory.Space().QueryInfo(base).size == 0x8000);
    CHECK_RESULT(f.memory.UnlockForDevice(base, 0x4000), Result::InvalidCurrentMemory);

    std::vector<PhysicalExtent> pages;
    CHECK_RESULT(f.memory.LockForTransferMemory(base, 0x8000, Permission::Read, pages),
                 Result::Success);
    CHECK_RESULT(f.memory.LockForMapDevice(base, 0x8000, 0, false, true, is_io),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.UnlockForTransferMemory(base, 0x8000), Result::Success);

    const u64 read_only = base + 0x100000;
    const u8 read_permission = ToStoredPermission(Permission::Read);
    CHECK_RESULT(f.memory.MapPhysicalMemory(read_only, 0x4000), Result::Success);
    CHECK_RESULT(f.memory.SetMemoryPermission(read_only, 0x4000, Permission::Read), Result::Success);
    CHECK_RESULT(f.memory.LockForMapDevice(read_only, 0x4000, PermUserReadWrite, false, true, is_io),
                 Result::InvalidCurrentMemory);
    CHECK_RESULT(f.memory.LockForMapDevice(read_only, 0x4000, read_permission, false, true, is_io),
                 Result::Success);
    CHECK_RESULT(f.memory.UnlockForDevice(read_only, 0x4000), Result::Success);
    CHECK(f.Consistent());
    return 0;
}

int FinalizeReleasesEverything() {
    Fixture f;
    f.memory.SetMaxHeapSize(64 * MiB);
    u64 heap = 0;
    CHECK_RESULT(f.memory.SetHeapSize(heap, 0x200000), Result::Success);
    const u64 alias = f.layout.alias.start;
    CHECK_RESULT(f.memory.MapPhysicalMemory(alias, 0x20000), Result::Success);
    CHECK_RESULT(f.memory.MapMemory(f.layout.stack.start, alias, 0x10000), Result::Success);
    CHECK_RESULT(f.memory.MapThreadLocalPage(f.layout.kernel_map.start, 0x7000), Result::Success);
    CHECK_RESULT(f.memory.MapSharedMemory(f.layout.alias_code.start, 0x2000, Permission::Read,
                                          {{0x40000, 0x2000}}),
                 Result::Success);
    CHECK(!f.host.pages.empty() && f.source.allocated_bytes == 0x200000 + 0x20000);
    f.memory.Finalize();
    CHECK(f.host.pages.empty());
    CHECK(f.source.live_pages.empty() && f.source.allocated_bytes == 0);
    u64 phys = 0;
    CHECK(!f.memory.GetPhysicalAddress(alias, phys));
    CHECK(f.Consistent());
    return 0;
}

// Reference model: one optional physical page per virtual page.
int RunMapMatchesAPerPageModel() {
    constexpr u64 kPages = 64;
    Detail::RunMap<Detail::Backing> runs;
    std::map<u64, u64> model;
    u64 state = 12345;
    auto next = [&](u64 limit) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (state >> 33) % limit;
    };
    for (int step = 0; step < 4000; ++step) {
        const u64 first = next(kPages);
        const u64 count = 1 + next(kPages - first);
        const u64 address = first * PageSize;
        if (next(3) == 0) {
            runs.Erase(address, count * PageSize);
            for (u64 page = first; page < first + count; ++page) {
                model.erase(page);
            }
        } else {
            const u64 phys = (1 + next(1000)) * 0x100000;
            runs.Assign(address, count * PageSize, Detail::Backing{phys});
            for (u64 page = first; page < first + count; ++page) {
                model[page] = phys + (page - first) * PageSize;
            }
        }
        for (u64 page = 0; page < kPages; ++page) {
            u64 seen = ~0ULL;
            const bool covered = runs.Visit(page * PageSize, PageSize,
                                            [&](u64, u64, const Detail::Backing& backing) {
                                                seen = backing.phys;
                                            });
            const auto it = model.find(page);
            CHECK(covered == (it != model.end()));
            CHECK(!covered || seen == it->second);
        }
    }
    return 0;
}

} // namespace

int main() {
    CHECK(PhysicalMapAllocatesOnlyFreePages() == 0);
    CHECK(PhysicalMapRollsBackOnFailure() == 0);
    CHECK(HeapGrowsAndShrinksWithBacking() == 0);
    CHECK(StackAliasMovesTheHostMapping() == 0);
    CHECK(PermissionChangesReachTheHost() == 0);
    CHECK(ExternalPagesAreMappedButNeverFreed() == 0);
    CHECK(CodeSegmentsFollowProcessPermissions() == 0);
    CHECK(TransferMemoryLocksPinTheRange() == 0);
    CHECK(DeviceMapLocksAreCounted() == 0);
    CHECK(FinalizeReleasesEverything() == 0);
    CHECK(RunMapMatchesAPerPageModel() == 0);
    std::puts("All synthetic native process memory smoke checks passed.");
    return 0;
}
