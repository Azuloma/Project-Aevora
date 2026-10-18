// smoke_perf_hud_unit: percentage math, N/A handling, line format and visibility of the
// performance HUD (suyu_cmd/overlay/perf_hud.h).
#include <cstdio>

#include "suyu_cmd/overlay/perf_hud.h"

using namespace SuyuCmd;

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

namespace {

// Negative control: forgets to divide by the core count.
HudPercent CpuIgnoringCores(std::uint64_t cpu, std::uint64_t wall, std::uint32_t) {
    return RatioPercent(cpu, wall);
}

// The CPU rules, shared by the real function and the control. False on a breach.
template <class Cpu>
bool CpuRulesHold(Cpu cpu) {
    // 2 cores busy for a whole interval on an 8-core host is 25%.
    const HudPercent quarter = cpu(2000, 1000, 8);
    return quarter.available && quarter.percent == 25.0;
}

int CpuIsScaledByCores() {
    CHECK(CpuRulesHold(CpuPercent));
    CHECK(!CpuRulesHold(CpuIgnoringCores));
    return 0;
}

int UnavailableNeverBecomesZero() {
    CHECK(!CpuPercent(10, 0, 8).available);
    CHECK(!CpuPercent(10, 10, 0).available);
    CHECK(!RatioPercent(5, 0).available);
    CHECK(FormatHudPercent(Unavailable()) == "N/A");
    CHECK(FormatHudPercent(RatioPercent(0, 10)) == "0.00%");
    return 0;
}

int ValuesAreClamped() {
    CHECK(MakePercent(250.0).percent == 100.0);
    CHECK(MakePercent(-3.0).percent == 0.0);
    CHECK(RatioPercent(3, 2).percent == 100.0);
    return 0;
}

int LineMatchesTheRequestedLayout() {
    PerfHudSample sample;
    sample.cpu = MakePercent(12.345);
    sample.gpu = MakePercent(100.0);
    sample.gpu_memory = MakePercent(0.5);
    sample.guest_memory = Unavailable();
    sample.system_memory = MakePercent(63.0);
    CHECK(FormatPerfHudLine(sample) ==
          "CPU: 12.35%, GPU: 100.00%, GMem: 0.50%, MMem: N/A, SysMem: 63.00%");
    return 0;
}

int VisibilityStartsOffAndToggles() {
    PerfHudVisibility hud;
    CHECK(!hud.Visible());
    hud.Toggle();
    CHECK(hud.Visible());
    hud.Toggle();
    CHECK(!hud.Visible());
    return 0;
}

} // namespace

int main() {
    CHECK(CpuIsScaledByCores() == 0);
    CHECK(UnavailableNeverBecomesZero() == 0);
    CHECK(ValuesAreClamped() == 0);
    CHECK(LineMatchesTheRequestedLayout() == 0);
    CHECK(VisibilityStartsOffAndToggles() == 0);
    std::puts("All synthetic PERF HUD smoke checks passed.");
    return 0;
}
