// smoke_xmod_table_unit: XMOD1 shared-table publication (PublishRecompXmod / UnpublishRecompXmod
// in core/arm/recomp/recomp_bundle.cpp) against fake modules, so every refusal path is reachable.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/arm/recomp/recomp_bundle.h"
#include "core/arm/recomp/recomp_image_features.h"

namespace {

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

constexpr size_t kFakeModules = 4;

// Each fake image records the last table it was handed; refuse_index makes one image say no.
struct FakeImage {
    const SuyuRecompXmodTableV1* held{};
    int calls{};
};
FakeImage g_images[kFakeModules + SUYU_RECOMP_XMOD_V1_MAX_MODULES];
int g_refuse_index = -1;

template <size_t Index>
unsigned FakeXmod(const SuyuRecompXmodTableV1* table) {
    ++g_images[Index].calls;
    if (table && static_cast<int>(Index) == g_refuse_index) {
        return 0;
    }
    g_images[Index].held = table;
    return 1;
}

template <size_t Index>
Core::RecompBundleBlockFn FakeLookup(u64) {
    return nullptr;
}

template <size_t... Is>
Core::RecompBundle MakeBundle(std::index_sequence<Is...>) {
    Core::RecompBundle bundle;
    bundle.features = Core::RecompImageFeature::FastmemPT1 | Core::RecompImageFeature::CrossModule1;
    static constexpr const char* names[] = {"rtld", "main", "subsdk0", "sdk"};
    ((bundle.modules.push_back([] {
         Core::RecompBundleModule module;
         module.name = names[Is];
         module.lookup = &FakeLookup<Is>;
         module.xmod_v1 = &FakeXmod<Is>;
         return module;
     }())),
     ...);
    return bundle;
}

void Reset() {
    for (auto& image : g_images) {
        image = {};
    }
    g_refuse_index = -1;
}

bool Contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int TotalCalls() {
    int total = 0;
    for (const auto& image : g_images) {
        total += image.calls;
    }
    return total;
}

} // namespace

int main() {
    using Bases = std::vector<std::pair<size_t, u64>>;
    std::string error;

    {
        // A bundle without XMOD1 is left alone.
        Reset();
        auto bundle = MakeBundle(std::make_index_sequence<kFakeModules>{});
        bundle.features = Core::RecompImageFeature::FastmemPT1;
        CHECK(Core::PublishRecompXmod(bundle, {}, &error));
        CHECK(bundle.xmod_table == nullptr && TotalCalls() == 0);
    }
    {
        // The table is sorted by base whatever order the bases arrive in, and every image gets
        // the same pointer.
        Reset();
        auto bundle = MakeBundle(std::make_index_sequence<kFakeModules>{});
        const Bases bases{{0, 0x8000000}, {1, 0x2000000}, {2, 0x6000000}, {3, 0x4000000}};
        CHECK(Core::PublishRecompXmod(bundle, bases, &error));
        const SuyuRecompXmodTableV1* table = bundle.xmod_table;
        CHECK(table && table->version == SUYU_RECOMP_XMOD_V1_VERSION && table->count == 4);
        CHECK(table->struct_size == sizeof(SuyuRecompXmodTableV1));
        CHECK(table->entry_size == sizeof(SuyuRecompXmodEntryV1));
        CHECK(table->entries[0].base == 0x2000000 && table->entries[1].base == 0x4000000);
        CHECK(table->entries[2].base == 0x6000000 && table->entries[3].base == 0x8000000);
        CHECK(table->entries[0].lookup == &FakeLookup<1>);
        CHECK(table->entries[3].lookup == &FakeLookup<0>);
        for (size_t i = 0; i < kFakeModules; ++i) {
            CHECK(g_images[i].held == table);
        }
        // A second publish needs an unpublish first, which clears every image.
        CHECK(!Core::PublishRecompXmod(bundle, bases, &error) && Contains(error, "already"));
        Core::UnpublishRecompXmod(bundle);
        CHECK(bundle.xmod_table == nullptr);
        for (size_t i = 0; i < kFakeModules; ++i) {
            CHECK(g_images[i].held == nullptr);
        }
        CHECK(Core::PublishRecompXmod(bundle, bases, &error) && bundle.xmod_table != nullptr);
    }
    {
        // One image refusing leaves nothing published, and the error names it.
        Reset();
        g_refuse_index = 2;
        auto bundle = MakeBundle(std::make_index_sequence<kFakeModules>{});
        const Bases bases{{0, 0x1000}, {1, 0x2000}, {2, 0x3000}, {3, 0x4000}};
        CHECK(!Core::PublishRecompXmod(bundle, bases, &error));
        CHECK(Contains(error, "subsdk0") && Contains(error, "refused"));
        CHECK(bundle.xmod_table == nullptr);
        for (size_t i = 0; i < kFakeModules; ++i) {
            CHECK(g_images[i].held == nullptr);
        }
    }
    {
        // Validation refusals happen before any image is called.
        Reset();
        auto bundle = MakeBundle(std::make_index_sequence<kFakeModules>{});
        CHECK(!Core::PublishRecompXmod(
            bundle, Bases{{0, 0x1000}, {1, 0x2000}, {2, 0x2000}, {3, 0x4000}}, &error));
        CHECK(Contains(error, "share load base") && Contains(error, "0x2000"));
        CHECK(!Core::PublishRecompXmod(bundle, Bases{{0, 0x1000}, {1, 0x2000}}, &error));
        CHECK(!Core::PublishRecompXmod(
            bundle, Bases{{0, 0x1000}, {1, 0x2000}, {1, 0x3000}, {3, 0x4000}}, &error));
        CHECK(Contains(error, "repeated"));
        bundle.modules[3].xmod_v1 = nullptr;
        CHECK(!Core::PublishRecompXmod(
            bundle, Bases{{0, 0x1000}, {1, 0x2000}, {2, 0x3000}, {3, 0x4000}}, &error));
        CHECK(Contains(error, "sdk") && Contains(error, "xmod_v1"));
        CHECK(TotalCalls() == 0 && bundle.xmod_table == nullptr);
    }
    {
        // More modules than the table holds is refused with the limit in the message.
        Reset();
        Core::RecompBundle bundle;
        bundle.features = Core::RecompImageFeature::CrossModule1;
        Bases bases;
        for (size_t i = 0; i < SUYU_RECOMP_XMOD_V1_MAX_MODULES + 1; ++i) {
            Core::RecompBundleModule module;
            module.name = "m" + std::to_string(i);
            module.lookup = &FakeLookup<0>;
            module.xmod_v1 = &FakeXmod<0>;
            bundle.modules.push_back(std::move(module));
            bases.emplace_back(i, 0x1000 * (i + 1));
        }
        CHECK(!Core::PublishRecompXmod(bundle, bases, &error) && Contains(error, "16"));
        CHECK(TotalCalls() == 0);
    }
    std::printf("PASS xmod_table_unit\n");
    return 0;
}
