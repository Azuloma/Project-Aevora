/* Runs the direct B/BL chaining fixture (bl_code.h). BL_EXPECT_CHAIN is 1 when
   the module was exported with direct chaining and 0 when it was not, so the same
   checks pin both texts: the chained call runs the callee inside one block
   function, the plain one parks the callee's address and returns for the
   run-slice loop to look it up. Either way the slice must reach the same state. */
#include "recomp_runtime.h"
#include "bl_code.h"
#include <stdio.h>
#include <string.h>

#ifndef BL_EXPECT_CHAIN
#error "BL_EXPECT_CHAIN must be 0 or 1"
#endif

extern void recomp_image_set_base(uint64_t);
extern void recomp_image_run_slice(GuestContext*);
extern BlockFn recomp_image_lookup(uint64_t);
extern unsigned recomp_image_guard_v2(unsigned);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static GuestContext context;

static void reset(uint64_t pc, int budget) {
    memset(&context, 0, sizeof(context));
    context.pc = pc;
    context.mem = (uint8_t*)bl_code;
    context.mem_size = sizeof(bl_code);
    context.mem_base_vaddr = BL_BASE;
    context.pending_svc = ~UINT64_C(0);
    context.chain_budget = budget;
}

int main(void) {
    BlockFn call_block;
    recomp_image_guard_v2(2);
    recomp_image_set_base(0);
    call_block = recomp_image_lookup(BL_CALL);
    CHECK(call_block != NULL && recomp_image_lookup(BL_CALLEE) != NULL);

    /* One call of the BL block's own function. */
    reset(BL_CALL, 8);
    call_block(&context);
    CHECK(context.x[30] == BL_AFTER_CALL);
    CHECK(!context.halted && context.pending_svc == ~UINT64_C(0));
#if BL_EXPECT_CHAIN
    /* The callee ran inside the same call, spent one unit of budget, and its RET
       left the return address as the next PC. */
    CHECK(context.x[0] == 5 && context.pc == BL_AFTER_CALL && context.chain_budget == 7);
#else
    CHECK(context.x[0] == 0 && context.pc == BL_CALLEE && context.chain_budget == 8);
#endif

    /* A budget of 1 is spent by the chain itself: the callee's address is parked
       and the callee has not run, exactly as without chaining. */
    reset(BL_CALL, 1);
    call_block(&context);
    CHECK(context.x[30] == BL_AFTER_CALL && context.x[0] == 0 && context.pc == BL_CALLEE);
#if BL_EXPECT_CHAIN
    CHECK(context.chain_budget == 0);
#else
    CHECK(context.chain_budget == 1);
#endif

    /* The whole slice reaches the same end state with or without chaining. */
    reset(BL_CALL, 16);
    recomp_image_run_slice(&context);
    CHECK(context.x[0] == 5 && context.x[1] == 1 && context.x[30] == BL_AFTER_CALL);
    CHECK(context.halted == RECOMP_HALT_BREAKPOINT && context.pc == BL_BRK);

    /* A slice that runs out between the BL and the callee resumes where it parked. */
    reset(BL_CALL, 1);
    recomp_image_run_slice(&context);
    CHECK(context.x[0] == 0 && context.pc == BL_CALLEE && context.chain_budget == 0);
    context.chain_budget = 16;
    recomp_image_run_slice(&context);
    CHECK(context.x[0] == 5 && context.x[1] == 1 && context.halted == RECOMP_HALT_BREAKPOINT);

    puts("PASS direct B/BL chain fixture");
    return 0;
}
