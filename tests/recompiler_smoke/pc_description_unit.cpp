// smoke_pc_description_unit: the module+offset text in strict miss and unimplemented-opcode
// diagnostics (core/arm/recomp/recomp_pc_description.h).
#include <cstdint>
#include <cstdio>
#include <string>

#include "core/arm/recomp/recomp_pc_description.h"

using Core::DescribeRecompPc;
using Core::RecompModuleSnapshot;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

RecompModuleSnapshot TwoModules() {
    RecompModuleSnapshot snapshot;
    snapshot.bases = {0x1000, 0x5000};
    snapshot.names = {"rtld", "main"};
    return snapshot;
}

int NamesTheModuleAndOffset() {
    const RecompModuleSnapshot modules = TwoModules();
    CHECK(DescribeRecompPc(modules, 0x1000) == "rtld+0x0");
    CHECK(DescribeRecompPc(modules, 0x4FFF) == "rtld+0x3fff");
    CHECK(DescribeRecompPc(modules, 0x5000) == "main+0x0");
    CHECK(DescribeRecompPc(modules, 0x6123) == "main+0x1123");
    CHECK(DescribeRecompPc(modules, 0x7100123456ULL) == "main+0x710011e456");
    return 0;
}

int FallsBackToTheBarePc() {
    const RecompModuleSnapshot modules = TwoModules();
    CHECK(DescribeRecompPc(modules, 0xFFF) == "0xfff");
    CHECK(DescribeRecompPc(RecompModuleSnapshot{}, 0x5000) == "0x5000");
    return 0;
}

int NeverReadsPastAShortNameList() {
    RecompModuleSnapshot snapshot;
    snapshot.bases = {0x1000, 0x5000};
    snapshot.names = {"rtld"};
    CHECK(DescribeRecompPc(snapshot, 0x5100) == "0x5100");
    return 0;
}

} // namespace

int main() {
    if (const int rc = NamesTheModuleAndOffset()) {
        return rc;
    }
    if (const int rc = FallsBackToTheBarePc()) {
        return rc;
    }
    if (const int rc = NeverReadsPastAShortNameList()) {
        return rc;
    }
    std::puts("pc_description_unit: ok");
    return 0;
}
