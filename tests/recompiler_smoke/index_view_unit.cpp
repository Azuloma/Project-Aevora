// Standalone test of the host's block index view (core/arm/recomp/recomp_index_view.h):
// 32-bit table first, legacy 64-bit table second, the image's lookup otherwise.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/arm/recomp/recomp_index_view.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);     \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

// Distinct bodies, so the linker cannot fold them onto one address.
volatile int g_sink;
void BlockA(void*) { g_sink = 1; }
void BlockB(void*) { g_sink = 2; }
void BlockC(void*) { g_sink = 3; }
void Anchor(void*) { g_sink = 4; }

constexpr std::uint64_t kLo = 0x7ff600001000;
constexpr std::size_t kWords = 12;
constexpr std::uint64_t kHi = kLo + (kWords - 1) * 4;

// Word -> block: A covers words 0-2 (an entry in the middle of it is a real case), a gap at 3-4,
// B covers 5, C covers 6-8, a gap at 9-11.
Core::RecompIndexBlock Expected(std::size_t word) {
    if (word <= 2) return BlockA;
    if (word == 5) return BlockB;
    if (word >= 6 && word <= 8) return BlockC;
    return nullptr;
}

// What the image's lookup export answers for any pc; the index view must agree wherever it covers.
Core::RecompIndexBlock Lookup(std::uint64_t pc) {
    if (pc < kLo || pc > kHi || ((pc - kLo) & 3) != 0) return nullptr;
    return Expected(static_cast<std::size_t>((pc - kLo) >> 2));
}

std::uint64_t Address(Core::RecompIndexBlock block) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block));
}

struct FakeImage {
    std::uint64_t anchor = 0;
    std::vector<std::int32_t> table32;
    std::vector<Core::RecompIndexBlock> table64;
    bool offsets_fit = true;
};

FakeImage MakeImage(std::uint64_t anchor) {
    FakeImage image;
    image.anchor = anchor;
    for (std::size_t word = 0; word < kWords; ++word) {
        const auto block = Expected(word);
        image.table64.push_back(block);
        std::int32_t offset = 0;
        if (block) {
            if (suyu_recomp_index32_offset(anchor, Address(block), &offset) !=
                SUYU_RECOMP_INDEX32_OFFSET_OK) {
                image.offsets_fit = false;
            }
        }
        image.table32.push_back(offset);
    }
    return image;
}

FakeImage* g_image = nullptr;
int g_get32_calls = 0;
const char* g_decline_reason = nullptr;
bool g_decline = false;

int Get32(SuyuRecompIndex32V1* out) {
    ++g_get32_calls;
    if (out->version != SUYU_RECOMP_INDEX32_V1_VERSION || out->struct_size != sizeof(*out)) {
        return 0;
    }
    if (g_decline) {
        out->unavailable_reason = g_decline_reason;
        return 0;
    }
    out->lo = kLo;
    out->hi = kHi;
    out->table = g_image->table32.data();
    out->anchor = g_image->anchor;
    return 1;
}

int g_get64_calls = 0;

int Get64(std::uint64_t* lo, std::uint64_t* hi, Core::RecompIndexBlock** table) {
    ++g_get64_calls;
    *lo = kLo;
    *hi = kHi;
    *table = g_image->table64.data();
    return 1;
}

// A new image's legacy export: no table.
int Get64Unavailable(std::uint64_t*, std::uint64_t*, Core::RecompIndexBlock**) {
    return 0;
}

// The host's dispatch rule: the view where it covers a pc, the image's lookup elsewhere.
Core::RecompIndexBlock Dispatch(const Core::RecompIndexView& view, std::uint64_t pc) {
    return view.Covers(pc) ? view.BlockAt(pc) : Lookup(pc);
}

void CheckDispatchAgreesWithLookup(const Core::RecompIndexView& view) {
    for (std::uint64_t pc = kLo - 16; pc < kHi + 32; ++pc) {
        CHECK(Dispatch(view, pc) == Lookup(pc));
    }
    CHECK(Dispatch(view, 0) == nullptr);
    CHECK(Dispatch(view, ~std::uint64_t{0}) == nullptr);
}

void CheckViewAgreesWithLookup(const Core::RecompIndexView& view) {
    CheckDispatchAgreesWithLookup(view);
    for (std::size_t word = 0; word < kWords; ++word) {
        const std::uint64_t pc = kLo + word * 4;
        CHECK(view.Covers(pc));
        CHECK(view.BlockAt(pc) == Expected(word));
    }
    CHECK(!view.Covers(kLo + 2) && !view.Covers(kLo - 4) && !view.Covers(kHi + 4));
}

void NewHostNewImage(std::uint64_t anchor, const char* what) {
    std::fprintf(stderr, "case: new host, new image, %s\n", what);
    FakeImage image = MakeImage(anchor);
    CHECK(image.offsets_fit);
    g_image = &image;
    g_decline = false;
    g_get32_calls = 0;
    std::string note;
    const auto view = Core::AcquireRecompIndexView(Get32, Get64Unavailable, &note);
    CHECK(g_get32_calls == 1);
    CHECK(view.kind == Core::RecompIndexView::Kind::Index32);
    CHECK(note.empty());
    CheckViewAgreesWithLookup(view);
}

}  // namespace

int main() {
    const std::uint64_t low_block = std::min({Address(BlockA), Address(BlockB), Address(BlockC)});
    const std::uint64_t high_block = std::max({Address(BlockA), Address(BlockB), Address(BlockC)});

    // Offsets of either sign: the anchor below every block, above every block, and between.
    NewHostNewImage(low_block - 0x1000, "positive offsets");
    NewHostNewImage(high_block + 0x1000, "negative offsets");
    NewHostNewImage(Address(Anchor), "the image's own anchor function");

    {
        std::fprintf(stderr, "case: an image offering both tables is served by the 32-bit one\n");
        FakeImage image = MakeImage(low_block - 0x1000);
        g_image = &image;
        g_decline = false;
        g_get64_calls = 0;
        std::string note;
        const auto view = Core::AcquireRecompIndexView(Get32, Get64, &note);
        CHECK(view.kind == Core::RecompIndexView::Kind::Index32);
        CHECK(g_get64_calls == 0);
        CHECK(note.empty());
        CheckViewAgreesWithLookup(view);
    }
    {
        std::fprintf(stderr, "case: new host, old image (64-bit table only)\n");
        FakeImage image = MakeImage(0);
        g_image = &image;
        g_get32_calls = 0;
        std::string note;
        const auto view = Core::AcquireRecompIndexView(nullptr, Get64, &note);
        CHECK(g_get32_calls == 0);
        CHECK(view.kind == Core::RecompIndexView::Kind::Index64);
        CHECK(note.empty());
        CheckViewAgreesWithLookup(view);
    }
    {
        std::fprintf(stderr, "case: old host, new image (no 32-bit support, legacy export empty)\n");
        FakeImage image = MakeImage(low_block - 0x1000);
        g_image = &image;
        g_get32_calls = 0;
        std::string note;
        const auto view = Core::AcquireRecompIndexView(nullptr, Get64Unavailable, &note);
        CHECK(g_get32_calls == 0);
        CHECK(view.kind == Core::RecompIndexView::Kind::None);
        CHECK(!view.Covers(kLo) && !view.Covers(0) && !view.Covers(~std::uint64_t{0}));
        // Every address is then the image's lookup.
        CheckDispatchAgreesWithLookup(view);
    }
    {
        std::fprintf(stderr, "case: new image declines its table\n");
        FakeImage image = MakeImage(low_block - 0x1000);
        g_image = &image;
        g_decline = true;
        g_decline_reason = SUYU_RECOMP_INDEX32_WHY_RANGE;
        std::string note;
        auto view = Core::AcquireRecompIndexView(Get32, Get64Unavailable, &note);
        CHECK(view.kind == Core::RecompIndexView::Kind::None);
        CHECK(note.find(SUYU_RECOMP_INDEX32_WHY_RANGE) != std::string::npos);
        CHECK(note.find("lookup export") != std::string::npos);
        g_decline_reason = nullptr;
        note.clear();
        view = Core::AcquireRecompIndexView(Get32, Get64Unavailable, &note);
        CHECK(view.kind == Core::RecompIndexView::Kind::None);
        CHECK(note.find("no reason given") != std::string::npos);
        // An image that declines the 32-bit table but still has a legacy one is served by it.
        g_decline_reason = SUYU_RECOMP_INDEX32_WHY_ALLOC;
        note.clear();
        view = Core::AcquireRecompIndexView(Get32, Get64, &note);
        CHECK(view.kind == Core::RecompIndexView::Kind::Index64);
        CHECK(note.find(SUYU_RECOMP_INDEX32_WHY_ALLOC) != std::string::npos);
        CHECK(note.find("legacy 64-bit table") != std::string::npos);
        CheckViewAgreesWithLookup(view);
        g_decline = false;
    }
    {
        std::fprintf(stderr, "case: a table that cannot be used is not bound\n");
        const auto empty_table = [](SuyuRecompIndex32V1* out) {
            out->lo = kLo;
            out->hi = kHi;
            out->table = nullptr;
            return 1;
        };
        const auto backwards_range = [](SuyuRecompIndex32V1* out) {
            static const std::int32_t entry = 0;
            out->lo = kHi;
            out->hi = kLo;
            out->table = &entry;
            return 1;
        };
        std::string note;
        CHECK(Core::AcquireRecompIndexView(empty_table, nullptr, &note).kind ==
              Core::RecompIndexView::Kind::None);
        CHECK(Core::AcquireRecompIndexView(backwards_range, nullptr, &note).kind ==
              Core::RecompIndexView::Kind::None);
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("index view checks passed\n");
    return 0;
}
