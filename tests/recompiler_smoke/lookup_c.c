/* Compares the module's recomp_lookup with its segment search over every address around the
   module (lookup_code.h, exported with --lookup). The index unit is included so its statics are
   reachable: the index (_idx) and the search (_lookup_search) it must agree with. */
#include "recomp_runtime.h"
#include "lookup_code.h"
#include "src/recompiled_lookup.c"
#include <stdio.h>

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

int main(void) {
    Census census = {0, 0, 0};
    BlockFn* index;
    recomp_build_index();
    CHECK(_idx != NULL);
    CHECK(_NSEG == LOOKUP_FUNCS / 2);
    CHECK(_idx_lo == LOOKUP_BASE);
    if (sweep(&census)) return 1;
    CHECK(census.hits == LOOKUP_COVERED_WORDS);
    CHECK(census.holes > 0 && census.outside > 0);

    /* Without the flat index the answer must still come from the search. */
    index = _idx;
    _idx = NULL;
    census.hits = census.holes = census.outside = 0;
    if (sweep(&census)) return 1;
    CHECK(census.hits == LOOKUP_COVERED_WORDS);
    _idx = index;
    printf("lookup index checks passed: %u block words, %u gap words, %u outside the range\n",
           census.hits, census.holes, census.outside);
    return 0;
}
