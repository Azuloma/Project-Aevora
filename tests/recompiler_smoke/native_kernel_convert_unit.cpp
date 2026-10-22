// smoke_native_kernel_convert_unit: value conversions behind the kernel adapters
// (core/native_memory/native_kernel_convert.h) and the resource-limit accounting rule they encode.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/native_memory/native_kernel_convert.h"
#include "core/native_memory/native_process_memory.h"

using namespace Core::NativeMemory;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

constexpr u64 kDramBase = 0x80000000ULL;

int HostPermissionFollowsTheKernelBitTests() {
    const HostPermissionFlags none{};
    CHECK(ToHostPermissionFlags(0, true) == none);
    CHECK((ToHostPermissionFlags(PermUserReadWrite, false) == HostPermissionFlags{true, true, false}));
    CHECK((ToHostPermissionFlags(ToStoredPermission(Permission::Read), false) ==
           HostPermissionFlags{true, false, false}));
    CHECK((ToHostPermissionFlags(ToStoredPermission(Permission::ReadExecute), true) ==
           HostPermissionFlags{true, false, true}));
    CHECK((ToHostPermissionFlags(ToStoredPermission(Permission::ReadExecute), false) ==
           HostPermissionFlags{true, false, false}));
    // True(perm & UserRead) also passes for a kernel-only read bit, as in ConvertToMemoryPermission.
    CHECK((ToHostPermissionFlags(PermKernelRead, false) == HostPermissionFlags{true, false, false}));
    CHECK((ToHostPermissionFlags(PermKernelWrite, false) == HostPermissionFlags{false, true, false}));
    CHECK((ToHostPermissionFlags(PermLockedSource, false) == HostPermissionFlags{true, false, false}));
    return 0;
}

int StoredPermissionInvertsOnlySvcEncodings() {
    for (const Permission permission :
         {Permission::None, Permission::Read, Permission::ReadWrite, Permission::ReadExecute}) {
        Permission back = permission == Permission::Read ? Permission::None : Permission::Read;
        CHECK(FromStoredPermission(ToStoredPermission(permission), back));
        CHECK(back == permission);
    }
    Permission untouched = Permission::ReadWrite;
    CHECK(!FromStoredPermission(PermKernelRead, untouched));
    CHECK(!FromStoredPermission(2 | PermKernelRead | PermKernelWrite, untouched));
    CHECK(!FromStoredPermission(0, untouched));
    CHECK(untouched == Permission::ReadWrite);
    return 0;
}

int PageGroupIdentityIgnoresHowPagesAreSplit() {
    const std::vector<PhysicalExtent> whole{{0x10000, 0x4000}};
    CHECK(SamePhysicalPages(whole, {{0x10000, 0x1000}, {0x11000, 0x3000}}));
    CHECK(!SamePhysicalPages(whole, {{0x10000, 0x1000}, {0x12000, 0x3000}}));
    CHECK(!SamePhysicalPages(whole, {{0x10000, 0x3000}}));
    CHECK(!SamePhysicalPages(whole, {{0x20000, 0x4000}}));
    CHECK(!SamePhysicalPages({{0x1000, 0x1000}, {0x5000, 0x1000}}, {{0x5000, 0x1000}, {0x1000, 0x1000}}));
    return 0;
}

int PhysicalAddressesRoundTripThroughTheDramBase() {
    u64 offset = 0;
    CHECK(PhysicalToDramOffset(kDramBase + 0x12000, kDramBase, offset));
    CHECK(offset == 0x12000);
    CHECK(PhysicalToDramOffset(kDramBase, kDramBase, offset) && offset == 0);
    CHECK(DramOffsetToPhysical(0x12000, kDramBase) == kDramBase + 0x12000);
    u64 untouched = 0x1234;
    CHECK(!PhysicalToDramOffset(kDramBase - 1, kDramBase, untouched));
    CHECK(untouched == 0x1234);
    return 0;
}

int CapacityIsUsePlusLimitHeadroom() {
    CHECK(PhysicalCapacityBytes(0x4000, false, 0, 0) == ~0ULL);
    CHECK(PhysicalCapacityBytes(0x4000, true, 0x100000, 0x20000) == 0x4000 + 0xE0000);
    CHECK(PhysicalCapacityBytes(0x4000, true, 0x100000, 0x100000) == 0x4000);
    CHECK(PhysicalCapacityBytes(0x4000, true, 0x100000, 0x180000) == 0x4000);
    CHECK(PhysicalCapacityBytes(~0ULL - 0x10, true, 0x100000, 0) == ~0ULL);
    return 0;
}

// Hands out consecutive extents and counts how many bytes are outstanding.
class BumpSource final : public PhysicalPageSource {
public:
    bool Allocate(u64 size, std::vector<PhysicalExtent>& out) override {
        if (fail_next) {
            return false;
        }
        out.assign(1, PhysicalExtent{next_, size});
        next_ += size;
        outstanding_bytes_ += size;
        return true;
    }
    void Free(const std::vector<PhysicalExtent>& extents) override {
        for (const PhysicalExtent& extent : extents) {
            outstanding_bytes_ -= extent.size;
        }
    }
    u64 OutstandingBytes() const {
        return outstanding_bytes_;
    }

    bool fail_next = false;

private:
    u64 next_ = 0x100000;
    u64 outstanding_bytes_ = 0;
};

class NullHost final : public HostMemoryMap {
public:
    void Map(u64, u64, u64, u8) override {}
    void Unmap(u64, u64) override {}
    void Protect(u64, u64, u8) override {}
};

// A shared limit: other users can consume bytes between the capacity check and the reservation.
class FakeBudget final : public ResourceBudget {
public:
    static constexpr std::int64_t kLimitBytes = 0x40000;

    bool Reserve(u64 bytes) override {
        reserved_bytes += race_bytes;
        race_bytes = 0;
        if (reserved_bytes + static_cast<std::int64_t>(bytes) > kLimitBytes) {
            return false;
        }
        reserved_bytes += static_cast<std::int64_t>(bytes);
        return true;
    }
    void Release(u64 bytes) override {
        reserved_bytes -= static_cast<std::int64_t>(bytes);
    }

    std::int64_t reserved_bytes = 0;
    std::int64_t race_bytes = 0;
};

// Replays the adapter's per-operation rule against the fake sources.
struct LimitedProcess {
    LimitedProcess()
        : layout(MakeLayout()), accounting(source, budget), memory(layout, 0, accounting, host) {}

    static Layout MakeLayout() {
        LayoutParams params;
        params.type = AddressSpaceType::Bits39;
        params.code_address = 0x800A4000;
        params.code_size = 0x100000;
        struct Zero {
            u64 operator()(u64 min, u64) const {
                return min;
            }
        } zero;
        Layout result;
        if (ComputeLayout(params, zero, result) != Result::Success) {
            std::fprintf(stderr, "layout failed\n");
        }
        return result;
    }

    template <typename Operation>
    Result Run(bool accounted, Operation&& operation) {
        accounting.SetAccounted(accounted);
        memory.SetPhysicalCapacityBytes(PhysicalCapacityBytes(
            memory.Space().PhysicalBytesInUse(), true, FakeBudget::kLimitBytes, budget.reserved_bytes));
        const Result result = accounting.ClassifyFailure(operation(memory));
        accounting.SetAccounted(false);
        return result;
    }

    Layout layout;
    BumpSource source;
    FakeBudget budget;
    NullHost host;
    AccountingPageSource accounting;
    MappedAddressSpace memory;
};

int ResourceLimitBoundsPhysicalMemory() {
    LimitedProcess process;
    const u64 base = process.layout.alias.start;
    CHECK(process.Run(true, [&](MappedAddressSpace& m) { return m.MapPhysicalMemory(base, 0x30000); }) ==
          Result::Success);
    CHECK(process.budget.reserved_bytes == 0x30000);
    CHECK(process.Run(true, [&](MappedAddressSpace& m) {
              return m.MapPhysicalMemory(base + 0x100000, 0x20000);
          }) == Result::LimitReached);
    CHECK(process.budget.reserved_bytes == 0x30000);
    CHECK(process.source.OutstandingBytes() == 0x30000);

    u64 heap = 0;
    process.memory.SetMaxHeapSize(0x1000000);
    CHECK(process.Run(true, [&](MappedAddressSpace& m) { return m.SetHeapSize(heap, 0x200000); }) ==
          Result::LimitReached);
    CHECK(process.Run(true, [&](MappedAddressSpace& m) { return m.UnmapPhysicalMemory(base, 0x30000); }) ==
          Result::Success);
    CHECK(process.budget.reserved_bytes == 0);
    CHECK(process.source.OutstandingBytes() == 0);
    CHECK(process.Run(true, [&](MappedAddressSpace& m) {
              return m.MapPhysicalMemory(base + 0x100000, 0x40000);
          }) == Result::Success);
    CHECK(process.budget.reserved_bytes == FakeBudget::kLimitBytes);
    return 0;
}

int LostReservationRaceLeavesNothingBehind() {
    LimitedProcess process;
    const u64 base = process.layout.alias.start;
    process.budget.race_bytes = 0x30000;
    // The capacity check ran before the other user took its share, so only the reservation can fail.
    CHECK(process.Run(true, [&](MappedAddressSpace& m) { return m.MapPhysicalMemory(base, 0x20000); }) ==
          Result::LimitReached);
    CHECK(process.budget.reserved_bytes == 0x30000);
    CHECK(process.source.OutstandingBytes() == 0);
    CHECK(process.memory.Space().QueryInfo(base).state == MemoryState::Free);
    CHECK(process.memory.Space().PhysicalBytesInUse() == 0);
    return 0;
}

int UnaccountedOperationsDoNotTouchTheLimit() {
    LimitedProcess process;
    const u64 base = process.layout.alias.start;
    CHECK(process.Run(false, [&](MappedAddressSpace& m) { return m.MapPhysicalMemory(base, 0x10000); }) ==
          Result::Success);
    CHECK(process.budget.reserved_bytes == 0);
    CHECK(process.source.OutstandingBytes() == 0x10000);
    CHECK(process.Run(false, [&](MappedAddressSpace& m) { return m.UnmapPhysicalMemory(base, 0x10000); }) ==
          Result::Success);
    CHECK(process.budget.reserved_bytes == 0);
    return 0;
}

int FailedAllocationReturnsTheReservation() {
    BumpSource source;
    FakeBudget budget;
    AccountingPageSource accounting(source, budget);
    accounting.SetAccounted(true);
    std::vector<PhysicalExtent> pages;
    source.fail_next = true;
    CHECK(!accounting.Allocate(0x10000, pages));
    CHECK(budget.reserved_bytes == 0);
    CHECK(accounting.ClassifyFailure(Result::OutOfMemory) == Result::OutOfMemory);
    source.fail_next = false;
    CHECK(accounting.Allocate(0x10000, pages));
    CHECK(budget.reserved_bytes == 0x10000);
    accounting.Free(pages);
    CHECK(budget.reserved_bytes == 0);
    CHECK(source.OutstandingBytes() == 0);
    return 0;
}

int KernelResultCodesStayDistinct() {
    CHECK(ToKernelResultRaw(Result::LimitReached) == KernelResultRaw(132));
    CHECK(HasKernelResult(Result::LimitReached));
    CHECK(!HasKernelResult(Result::NotSupported));
    CHECK(!HasKernelResult(Result::Other));
    return 0;
}

} // namespace

int main() {
    CHECK(HostPermissionFollowsTheKernelBitTests() == 0);
    CHECK(StoredPermissionInvertsOnlySvcEncodings() == 0);
    CHECK(PageGroupIdentityIgnoresHowPagesAreSplit() == 0);
    CHECK(PhysicalAddressesRoundTripThroughTheDramBase() == 0);
    CHECK(CapacityIsUsePlusLimitHeadroom() == 0);
    CHECK(ResourceLimitBoundsPhysicalMemory() == 0);
    CHECK(LostReservationRaceLeavesNothingBehind() == 0);
    CHECK(UnaccountedOperationsDoNotTouchTheLimit() == 0);
    CHECK(FailedAllocationReturnsTheReservation() == 0);
    CHECK(KernelResultCodesStayDistinct() == 0);
    std::puts("All synthetic native kernel conversion smoke checks passed.");
    return 0;
}
