#include "recomp_runtime.h"
#include "core/recompiler/recomp_image_index.h"
#include "code.h"
#include <stdio.h>
#include <string.h>

#define MODULE(name) \
    extern unsigned recomp_image_abi_##name(void); \
    extern unsigned recomp_image_guard_v2_##name(unsigned); \
    extern void recomp_image_set_base_##name(uint64_t); \
    extern void recomp_image_run_slice_##name(GuestContext*); \
    extern BlockFn recomp_image_lookup_##name(uint64_t); \
    extern int recomp_image_index_##name(uint64_t*, uint64_t*, BlockFn**); \
    extern int recomp_image_index32_##name(SuyuRecompIndex32V1*); \
    extern int g_recomp_guard_host_v2_##name
MODULE(smoke);
MODULE(second);
#ifdef RECOMP_FEATURE_FASTMEM_PT1
#define FASTMEM_MODULE(name) \
    extern unsigned recomp_image_features_##name(void); \
    extern unsigned recomp_image_fastmem_v1_##name(uint32_t, uint32_t, uint64_t, uint32_t, uint32_t)
FASTMEM_MODULE(smoke);
FASTMEM_MODULE(second);
#endif
#ifdef RECOMP_FEATURE_GUARD_GEN1
extern uint32_t* recomp_image_guard_gen_v1_smoke(uint32_t, uint64_t*, uint64_t*, const uint64_t**);
extern uint32_t* recomp_image_guard_gen_v1_second(uint32_t, uint64_t*, uint64_t*, const uint64_t**);
extern uint32_t g_recomp_gg_word_smoke, g_recomp_gg_word_second;
extern uint64_t g_module_base_smoke, g_module_base_second;
#endif
static BlockFn index_block(const SuyuRecompIndex32V1* view, uint64_t pc) {
    return (BlockFn)(uintptr_t)suyu_recomp_index32_target(view->anchor,
                                                          view->table[(pc - view->lo) >> 2]);
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static void context_at(GuestContext* c, uint64_t base, int budget) {
    memset(c, 0, sizeof(*c));
    c->mem = (uint8_t*)smoke_code;
    c->mem_size = sizeof(smoke_code);
    c->mem_base_vaddr = c->pc = base + 0x1000;
    c->chain_budget = budget;
    c->pending_svc = ~UINT64_C(0);
}

int main(void) {
    const uint64_t first_base = 0x10000, second_base = 0x20000;
    uint64_t first_lo, first_hi, second_lo, second_hi;
    SuyuRecompIndex32V1 first_view = {SUYU_RECOMP_INDEX32_V1_VERSION, sizeof(first_view)};
    SuyuRecompIndex32V1 second_view = {SUYU_RECOMP_INDEX32_V1_VERSION, sizeof(second_view)};
    SuyuRecompIndex32V1 refused;
    uint64_t legacy_lo = 1, legacy_hi = 1;
    BlockFn* legacy_index = NULL;
    GuestContext first, second;
#ifdef RECOMP_FEATURE_FASTMEM_PT1
    CHECK(recomp_image_abi_smoke() == 6 && recomp_image_abi_second() == 6);
    CHECK(recomp_image_features_smoke() & recomp_image_features_second() & RECOMP_FEATURE_FASTMEM_PT1);
#ifdef RECOMP_FEATURE_GUARD_GEN1
    /* GG1 renames: each module owns its word, and its FM1 handshake waits for
       its own GG1 handshake, not the other module's. */
    {
        uint64_t lo, end;
        const uint64_t* base;
        CHECK(recomp_image_fastmem_v1_smoke(12, 5, ~(uint64_t)3, 872, 880) == 0);
        CHECK(recomp_image_guard_gen_v1_smoke(2, &lo, &end, &base) == &g_recomp_gg_word_smoke);
        CHECK(base == &g_module_base_smoke && lo == 0x1000);
        CHECK(recomp_image_fastmem_v1_second(12, 5, ~(uint64_t)3, 872, 880) == 0);
        CHECK(recomp_image_guard_gen_v1_second(2, &lo, &end, &base) == &g_recomp_gg_word_second);
        CHECK(base == &g_module_base_second);
        CHECK(&g_recomp_gg_word_smoke != &g_recomp_gg_word_second);
        CHECK(g_recomp_gg_word_second == RECOMP_GG_VERIFY_ALWAYS);
    }
#endif
    CHECK(recomp_image_fastmem_v1_smoke(12, 5, ~(uint64_t)3, 872, 880) == 1);
    CHECK(recomp_image_fastmem_v1_second(12, 5, ~(uint64_t)3, 872, 880) == 1);
    CHECK(recomp_image_fastmem_v1_second(12, 5, ~(uint64_t)3, 872, 872) == 0);
#else
    CHECK(recomp_image_abi_smoke() == 5 && recomp_image_abi_second() == 5);
#endif
    CHECK(g_recomp_guard_host_v2_smoke == 0 && g_recomp_guard_host_v2_second == 0);
    CHECK(recomp_image_guard_v2_smoke(2) == 2);
    CHECK(g_recomp_guard_host_v2_smoke == 2 && g_recomp_guard_host_v2_second == 0);
    CHECK(recomp_image_guard_v2_second(2) == 2);
    recomp_image_set_base_smoke(first_base);
    recomp_image_set_base_second(second_base);
    /* The legacy 64-bit export answers "no table"; the 32-bit one carries the index. */
    CHECK(!recomp_image_index_smoke(&legacy_lo, &legacy_hi, &legacy_index) && legacy_index == NULL);
    CHECK(!recomp_image_index_second(&legacy_lo, &legacy_hi, &legacy_index) && legacy_index == NULL);
    CHECK(recomp_image_index32_smoke(&first_view));
    CHECK(recomp_image_index32_second(&second_view));
    first_lo = first_view.lo, first_hi = first_view.hi;
    second_lo = second_view.lo, second_hi = second_view.hi;
    CHECK(first_lo == first_base + 0x1000 && second_lo == second_base + 0x1000);
    CHECK(first_hi - first_base == second_hi - second_base);
    CHECK(first_view.table != second_view.table && first_view.anchor != second_view.anchor);
    CHECK(index_block(&first_view, first_lo) == recomp_image_lookup_smoke(first_lo));
    CHECK(index_block(&second_view, second_lo) == recomp_image_lookup_second(second_lo));
    CHECK(index_block(&first_view, first_lo) != index_block(&second_view, second_lo));
    /* A caller that does not speak this version or size is refused, never half-served. */
    memset(&refused, 0, sizeof(refused));
    refused.version = SUYU_RECOMP_INDEX32_V1_VERSION + 1;
    refused.struct_size = sizeof(refused);
    CHECK(!recomp_image_index32_smoke(&refused) && refused.table == NULL);
    memset(&refused, 0, sizeof(refused));
    refused.version = SUYU_RECOMP_INDEX32_V1_VERSION;
    refused.struct_size = (uint32_t)sizeof(refused) - 1;
    CHECK(!recomp_image_index32_smoke(&refused) && refused.table == NULL);
    CHECK(recomp_image_lookup_smoke(second_lo) == NULL);
    CHECK(recomp_image_lookup_second(first_lo) == NULL);
    CHECK(recomp_image_lookup_smoke(first_base + 0x1018) ==
          recomp_image_lookup_smoke(first_base + 0x1014));
    CHECK(recomp_image_lookup_second(second_base + 0x1018) ==
          recomp_image_lookup_second(second_base + 0x1014));
    context_at(&first, first_base, 3);
    context_at(&second, second_base, 5);
    recomp_image_run_slice_smoke(&first);
    recomp_image_run_slice_second(&second);
    CHECK(first.x[0] == 3 && first.pc == first_lo && first.chain_budget == 0);
    CHECK(second.x[0] == 5 && second.pc == second_lo && second.chain_budget == 0);
    /* Rebase one image after both indexes exist; the second must stay put. */
    recomp_image_set_base_smoke(0x30000);
    CHECK(recomp_image_lookup_smoke(first_lo) == NULL);
    CHECK(recomp_image_lookup_smoke(0x31000) == index_block(&first_view, first_lo));
    CHECK(recomp_image_lookup_second(second_lo) == index_block(&second_view, second_lo));
    context_at(&second, second_base, 1);
    recomp_image_run_slice_second(&second);
    CHECK(second.x[0] == 1 && second.pc == second_lo && second.chain_budget == 0);
    puts("PASS two static modules, isolated indexes/bases/guards, one shared runtime");
    return 0;
}
