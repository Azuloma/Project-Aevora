// smoke_access_sites_unit: the SUYU_RECOMP_ACCESS_SITES call-site table and window report
// (core/arm/recomp/recomp_access_sites.h).
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/arm/recomp/recomp_access_sites.h"

using Core::RecompAccessSites;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

std::string Describe(std::uintptr_t address) {
    return "a" + std::to_string(address);
}

int SameFramesMergeAndAnyDifferentFrameSeparates() {
    RecompAccessSites sites;
    auto& slot = sites.Acquire();
    sites.Rebase();
    const RecompAccessSites::Frames base{10, 20, 30};
    for (int i = 0; i < 5; ++i) {
        RecompAccessSites::Count(slot, base);
    }
    for (int i = 0; i < 3; ++i) {
        RecompAccessSites::Count(slot, {10, 20, 31}); // only the outermost frame differs
    }
    RecompAccessSites::Count(slot, {11, 20, 30});     // only the innermost frame differs
    const std::string line = sites.TakeTop(Describe);
    CHECK(line.find("distinct=3 counted=9 overflow=0") != std::string::npos);
    CHECK(line.find("| n=5 a10 a20 a30") != std::string::npos);
    CHECK(line.find("| n=3 a10 a20 a31") != std::string::npos);
    CHECK(line.find("| n=1 a11 a20 a30") != std::string::npos);
    // Busiest first.
    CHECK(line.find("n=5") < line.find("n=3"));
    return 0;
}

int ReportsOnlyTheWindowSincePreviousCall() {
    RecompAccessSites sites;
    auto& slot = sites.Acquire();
    RecompAccessSites::Count(slot, {1, 2, 3}); // before the window opens
    sites.Rebase();
    RecompAccessSites::Count(slot, {1, 2, 3});
    RecompAccessSites::Count(slot, {1, 2, 3});
    const std::string first = sites.TakeTop(Describe);
    CHECK(first.find("distinct=1 counted=2 ") != std::string::npos);
    CHECK(first.find("| n=2 a1 a2 a3") != std::string::npos);
    const std::string second = sites.TakeTop(Describe);
    CHECK(second.find("distinct=0 counted=0 ") != std::string::npos);
    CHECK(second.find("| n=") == std::string::npos);
    return 0;
}

int OnlyTheBusiestSitesAreListed() {
    RecompAccessSites sites;
    auto& slot = sites.Acquire();
    sites.Rebase();
    for (std::uintptr_t site = 1; site <= RecompAccessSites::kTopCount + 4; ++site) {
        for (std::uintptr_t n = 0; n < site; ++n) {
            RecompAccessSites::Count(slot, {site, 0, 0});
        }
    }
    const std::string line = sites.TakeTop(Describe);
    std::size_t listed = 0;
    for (std::size_t at = line.find("| n="); at != std::string::npos; at = line.find("| n=", at + 1)) {
        ++listed;
    }
    CHECK(listed == RecompAccessSites::kTopCount);
    CHECK(line.find("| n=20 a20 ") != std::string::npos);
    CHECK(line.find("| n=1 a1 ") == std::string::npos);
    return 0;
}

int FullProbeRunsAreCountedAsOverflow() {
    RecompAccessSites sites;
    auto& slot = sites.Acquire();
    sites.Rebase();
    constexpr std::uintptr_t kDistinct = 4 * RecompAccessSites::kTableSize;
    for (std::uintptr_t site = 1; site <= kDistinct; ++site) {
        RecompAccessSites::Count(slot, {site, site * 7, 1});
    }
    const std::string line = sites.TakeTop(Describe);
    const std::size_t at = line.find("overflow=");
    CHECK(at != std::string::npos);
    CHECK(std::stoull(line.substr(at + 9)) > 0);
    return 0;
}

int CountsFromThreadsThatAlreadyExited() {
    RecompAccessSites sites;
    sites.Rebase();
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            auto& slot = sites.Acquire();
            for (int n = 0; n < 1000; ++n) {
                RecompAccessSites::Count(slot, {5, 6, 7});
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    const std::string line = sites.TakeTop(Describe);
    CHECK(line.find("distinct=1 counted=4000 ") != std::string::npos);
    CHECK(line.find("| n=4000 a5 a6 a7") != std::string::npos);
    return 0;
}

} // namespace

int main() {
    if (const int rc = SameFramesMergeAndAnyDifferentFrameSeparates()) {
        return rc;
    }
    if (const int rc = ReportsOnlyTheWindowSincePreviousCall()) {
        return rc;
    }
    if (const int rc = OnlyTheBusiestSitesAreListed()) {
        return rc;
    }
    if (const int rc = FullProbeRunsAreCountedAsOverflow()) {
        return rc;
    }
    if (const int rc = CountsFromThreadsThatAlreadyExited()) {
        return rc;
    }
    std::puts("access_sites_unit: ok");
    return 0;
}
