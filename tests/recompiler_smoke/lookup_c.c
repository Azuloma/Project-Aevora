/* Checks the module's 32-bit block index (lookup_code.h, --lookup): lookup against the segment search,
   the offsets, the failures that must leave no table, and the exports. The index unit is included. */
#include "recomp_runtime.h"
#include "core/recompiler/recomp_image_index.h"
#include "lookup_code.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_allocation;
static void* test_calloc(size_t count, size_t size) {
    return fail_allocation ? NULL : calloc(count, size);
}
#define calloc(count, size) test_calloc(count, size)
/* The anchor the module encodes and decodes with; the test moves it to reach both offset signs. */
static uint64_t test_anchor;
#define _IDX_ANCHOR test_anchor
#include "src/recompiled_lookup.c"
#undef calloc

extern void recomp_image_set_base(uint64_t);
extern int recomp_image_index(uint64_t*, uint64_t*, BlockFn**);
extern int recomp_image_index32(SuyuRecompIndex32V1*);
extern BlockFn recomp_image_lookup(uint64_t);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

typedef struct { unsigned hits, holes, outside; } Census;

/* Compares both answers at pc; pc is in the index range, off a 4-byte boundary, or outside it. */
static int compare_at(uint64_t pc, Census* census) {
    BlockFn fast = recomp_lookup(pc), searched = _lookup_search(pc);
    if (fast != searched) {
        fprintf(stderr, "FAIL: lookup at 0x%llx is %p, the search says %p\n",
                (unsigned long long)pc, (void*)fast, (void*)searched);
        return 1;
    }
    if (fast) ++census->hits;
    else if (pc >= _idx_lo && pc <= _idx_hi) ++census->holes;
    else ++census->outside;
    return 0;
}

static int sweep(Census* census) {
    uint64_t pc;
    uint64_t end = LOOKUP_BASE + sizeof(lookup_code) + 0x400;
    memset(census, 0, sizeof(*census));
    for (pc = 0; pc < end; ++pc) {
        if (compare_at(pc, census)) return 1;
    }
    for (pc = 0; pc < 8; ++pc) {
        if (compare_at(UINT64_MAX - pc, census) || compare_at(UINT64_C(1) << (56 + pc), census) ||
            compare_at(_idx_hi + 1 + pc, census) || compare_at(_idx_lo - 1 - pc, census)) {
            return 1;
        }
    }
    return 0;
}

/* The lowest and highest block function address in the module. */
static void block_span(uint64_t* lowest, uint64_t* highest) {
    size_t i;
    unsigned j;
    *lowest = UINT64_MAX;
    *highest = 0;
    for (i = 0; i < _NSEG; ++i) {
        for (j = 0; j < *_segn[i]; ++j) {
            uint64_t fn = (uint64_t)(uintptr_t)_segs[i][j].fn;
            if (fn < *lowest) *lowest = fn;
            if (fn > *highest) *highest = fn;
        }
    }
}

/* Every table entry, decoded with `anchor`, must name the block the search finds for its slot. */
static int table_matches_search(uint64_t anchor, int* positive, int* negative) {
    uint64_t pc;
    *positive = *negative = 0;
    for (pc = _idx_lo; pc <= _idx_hi; pc += 4) {
        int32_t entry = _idx32[(size_t)((pc - _idx_lo) >> 2)];
        BlockFn decoded = (BlockFn)(uintptr_t)suyu_recomp_index32_target(anchor, entry);
        if (decoded != _lookup_search(pc)) return 1;
        if (entry > 0) ++*positive;
        if (entry < 0) ++*negative;
    }
    return 0;
}

static void drop_table(void) {
    free(_idx32);
    _idx32 = NULL;
}

/* Rebuilds with another anchor; lookups then decode with it too. */
static int rebuild_with_anchor(uint64_t anchor, Census* census, int* positive, int* negative) {
    test_anchor = anchor;
    drop_table();
    _idx_why = _build_idx_for(anchor);
    CHECK(_idx_why == NULL && _idx32 != NULL);
    if (sweep(census)) return 1;
    CHECK(census->hits == LOOKUP_COVERED_WORDS);
    return table_matches_search(anchor, positive, negative);
}

/* A failed build must publish nothing; the module then answers from the segment search. */
static int expect_no_table(uint64_t anchor, const char* why) {
    Census census;
    uint64_t lo, hi, view_anchor;
    const int32_t* table;
    const char* view_why = NULL;
    SuyuRecompIndex32V1 out = {SUYU_RECOMP_INDEX32_V1_VERSION, sizeof(out)};
    drop_table();
    test_anchor = anchor;
    _idx_tried = 1;
    _idx_why = _build_idx_for(anchor);
    CHECK(_idx_why != NULL && strcmp(_idx_why, why) == 0);
    CHECK(_idx32 == NULL);
    CHECK(!_recomp_index32_view(&lo, &hi, &table, &view_anchor, &view_why));
    CHECK(view_why != NULL && strcmp(view_why, why) == 0);
    CHECK(!recomp_image_index32(&out) && out.table == NULL);
    CHECK(out.unavailable_reason != NULL && strcmp(out.unavailable_reason, why) == 0);
    if (sweep(&census)) return 1;
    CHECK(census.hits == LOOKUP_COVERED_WORDS);
    return 0;
}

static uint64_t real_anchor;

static int restore_table(void) {
    drop_table();
    test_anchor = real_anchor;
    _idx_why = _build_idx_for(test_anchor);
    CHECK(_idx_why == NULL && _idx32 != NULL);
    return 0;
}

/* The generated offset rule against the shared header's, over the interesting distances. */
static int offsets_match_header(void) {
    static const uint64_t anchors[] = {0, 1, UINT64_C(0x7ff700000000), UINT64_C(0xfffffffff0000000)};
    static const int64_t distances[] = {0, 1, -1, 4, -4, INT32_MAX - 1, INT32_MAX, (int64_t)INT32_MAX + 1,
                                        INT32_MIN + 1, INT32_MIN, (int64_t)INT32_MIN - 1,
                                        INT64_C(0x100000000), -INT64_C(0x100000000)};
    size_t a, d;
    for (a = 0; a < sizeof(anchors) / sizeof(anchors[0]); ++a) {
        for (d = 0; d < sizeof(distances) / sizeof(distances[0]); ++d) {
            uint64_t target = anchors[a] + (uint64_t)distances[d];
            int32_t generated = 12345, shared = 12345;
            int from_generated = _idx_offset(anchors[a], target, &generated);
            int from_header = suyu_recomp_index32_offset(anchors[a], target, &shared);
            CHECK(from_generated == from_header);
            if (from_header == SUYU_RECOMP_INDEX32_OFFSET_OK) {
                CHECK(generated == shared);
                CHECK(suyu_recomp_index32_target(anchors[a], shared) == target);
            } else {
                CHECK(generated == 12345 && shared == 12345);
            }
        }
    }
    CHECK(suyu_recomp_index32_target(UINT64_C(0x1000), 0) == 0);
    return 0;
}

int main(void) {
    Census census;
    uint64_t lowest, highest;
    int positive, negative;
    BlockFn* legacy_table = NULL;
    uint64_t legacy_lo = 1, legacy_hi = 1;
    SuyuRecompIndex32V1 out = {SUYU_RECOMP_INDEX32_V1_VERSION, sizeof(out)};
    SuyuRecompIndex32V1 refused;
    const uint64_t base = UINT64_C(0x40000000);

    real_anchor = (uint64_t)(uintptr_t)&recomp_build_index;
    test_anchor = real_anchor;
    CHECK(_idx32 == NULL && _idx_why != NULL);
    recomp_image_set_base(base);
    CHECK(_idx32 != NULL && _idx_why == NULL);
    CHECK(_NSEG == LOOKUP_FUNCS / 2);
    CHECK(_idx_lo == LOOKUP_BASE);
    block_span(&lowest, &highest);
    CHECK(lowest < highest);
    if (sweep(&census)) return 1;
    CHECK(census.hits == LOOKUP_COVERED_WORDS);
    CHECK(census.holes > 0 && census.outside > 0);
    if (table_matches_search(real_anchor, &positive, &negative)) return 1;
    if (offsets_match_header()) return 1;

    /* Offsets of both signs, and at the int32 limits, decode to the same blocks. */
    if (rebuild_with_anchor(lowest - 0x100, &census, &positive, &negative)) return 1;
    CHECK(positive > 0 && negative == 0);
    if (rebuild_with_anchor(highest + 0x100, &census, &positive, &negative)) return 1;
    CHECK(negative > 0 && positive == 0);
    if (rebuild_with_anchor(highest - (uint64_t)INT32_MAX, &census, &positive, &negative)) return 1;
    if (rebuild_with_anchor(lowest + UINT64_C(0x80000000), &census, &positive, &negative)) return 1;

    /* One past either limit, and an anchor that is a block (offset 0): no table, with the reason. */
    if (expect_no_table(highest - (uint64_t)INT32_MAX - 1, SUYU_RECOMP_INDEX32_WHY_RANGE)) return 1;
    if (expect_no_table(lowest + UINT64_C(0x80000001), SUYU_RECOMP_INDEX32_WHY_RANGE)) return 1;
    if (expect_no_table(highest + UINT64_C(0x100000000), SUYU_RECOMP_INDEX32_WHY_RANGE)) return 1;
    if (expect_no_table(lowest, SUYU_RECOMP_INDEX32_WHY_ZERO)) return 1;
    if (expect_no_table(highest, SUYU_RECOMP_INDEX32_WHY_ZERO)) return 1;

    /* An allocation failure builds nothing, and the reason reaches the view and the export. */
    drop_table();
    test_anchor = real_anchor;
    fail_allocation = 1;
    _idx_tried = 0;
    recomp_image_set_base(base);
    fail_allocation = 0;
    CHECK(_idx32 == NULL && _idx_why != NULL && strcmp(_idx_why, SUYU_RECOMP_INDEX32_WHY_ALLOC) == 0);
    out.unavailable_reason = NULL;
    CHECK(!recomp_image_index32(&out) && out.table == NULL);
    CHECK(out.unavailable_reason != NULL && strcmp(out.unavailable_reason, SUYU_RECOMP_INDEX32_WHY_ALLOC) == 0);
    if (sweep(&census)) return 1;
    CHECK(census.hits == LOOKUP_COVERED_WORDS);
    /* A failed build is not retried: set_base again, with memory available, leaves it as it was. */
    recomp_image_set_base(base);
    CHECK(_idx32 == NULL && strcmp(_idx_why, SUYU_RECOMP_INDEX32_WHY_ALLOC) == 0);
    if (expect_no_table(lowest, SUYU_RECOMP_INDEX32_WHY_ZERO)) return 1;

    /* With the table restored the exports serve it, refuse a caller that speaks another version, and
       the legacy export still answers "no table". */
    if (restore_table()) return 1;
    if (sweep(&census)) return 1;
    CHECK(census.hits == LOOKUP_COVERED_WORDS);
    CHECK(recomp_image_index32(&out));
    CHECK(out.lo == base + _idx_lo && out.hi == base + _idx_hi);
    CHECK(out.anchor == _IDX_ANCHOR && out.table == _idx32);
    CHECK((BlockFn)(uintptr_t)suyu_recomp_index32_target(out.anchor, out.table[0]) ==
          recomp_image_lookup(out.lo));
    CHECK(recomp_image_lookup(out.lo) != NULL);
    memset(&refused, 0, sizeof(refused));
    refused.version = SUYU_RECOMP_INDEX32_V1_VERSION + 1;
    refused.struct_size = sizeof(refused);
    CHECK(!recomp_image_index32(&refused) && refused.table == NULL);
    memset(&refused, 0, sizeof(refused));
    refused.version = SUYU_RECOMP_INDEX32_V1_VERSION;
    refused.struct_size = (uint32_t)sizeof(refused) + 8;
    CHECK(!recomp_image_index32(&refused) && refused.table == NULL);
    CHECK(!recomp_image_index(&legacy_lo, &legacy_hi, &legacy_table) && legacy_table == NULL);

    printf("lookup index checks passed: %u block words, %u gap words, %u outside the range\n",
           census.hits, census.holes, census.outside);
    return 0;
}
