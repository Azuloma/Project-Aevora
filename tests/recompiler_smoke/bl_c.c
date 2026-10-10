/* Runs the chaining fixture (bl_code.h); BL_EXPECT_CHAIN/BL_EXPECT_INDIRECT say which chains the
   module was exported with. A chained transfer runs its target in one call and spends one budget
   unit, an unchained one parks the address; either way the slice must reach the same end state. */
#include "recomp_runtime.h"
#include "bl_code.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(BL_EXPECT_CHAIN) || !defined(BL_EXPECT_INDIRECT)
#error "BL_EXPECT_CHAIN and BL_EXPECT_INDIRECT must be 0 or 1"
#endif
/* BL_EXPECT_START is 1 when the callee was exported with a chain start entry. */
#ifndef BL_EXPECT_START
#define BL_EXPECT_START 0
#endif

extern void recomp_image_set_base(uint64_t);
extern void recomp_image_run_slice(GuestContext*);
extern BlockFn recomp_image_lookup(uint64_t);
extern unsigned recomp_image_guard_v2(unsigned);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

#if BL_EXPECT_START
extern void blk_bl_000000000000100c_s(GuestContext*);
#endif

static GuestContext context;

static uint64_t module_base;

static void reset(uint64_t pc, int budget) {
    memset(&context, 0, sizeof(context));
    context.pc = module_base + pc;
    context.mem = (uint8_t*)bl_code;
    context.mem_size = sizeof(bl_code);
    context.mem_base_vaddr = module_base + BL_BASE;
    context.pending_svc = ~UINT64_C(0);
    context.chain_budget = budget;
}

/* Every address the checks use is relative to the module base: with a base other
   than zero a lookup that forgot to subtract it would miss every target. */
#define AT(offset) (module_base + (offset))

static int run_checks(uint64_t base) {
    BlockFn call_block, callee_block, blr_block, br_block;
    module_base = base;
    recomp_image_set_base(base);
    call_block = recomp_image_lookup(AT(BL_CALL));
    callee_block = recomp_image_lookup(AT(BL_CALLEE));
    blr_block = recomp_image_lookup(AT(BL_BLR));
    br_block = recomp_image_lookup(AT(BL_BR));
    CHECK(call_block && callee_block && blr_block && br_block);
    CHECK(recomp_image_lookup(AT(0x5000)) == NULL);

    /* One call of the BL block's own function. */
    reset(BL_CALL, 8);
    call_block(&context);
    CHECK(context.x[30] == AT(BL_AFTER_CALL));
#if BL_EXPECT_CHAIN && BL_EXPECT_INDIRECT
    /* BL into the callee, then its RET into the code after the call: two budget units. */
    CHECK(context.x[0] == 5 && context.x[1] == 1 && context.halted == RECOMP_HALT_BREAKPOINT);
    CHECK(context.pc == AT(BL_BRK) && context.chain_budget == 6);
#elif BL_EXPECT_CHAIN
    CHECK(context.x[0] == 5 && context.x[1] == 0 && !context.halted);
    CHECK(context.pc == AT(BL_AFTER_CALL) && context.chain_budget == 7);
#else
    CHECK(context.x[0] == 0 && !context.halted);
    CHECK(context.pc == AT(BL_CALLEE) && context.chain_budget == 8);
#endif

    /* A budget of 1 is spent by the chain itself: the callee's address is parked
       and the callee has not run, exactly as without chaining. */
    reset(BL_CALL, 1);
    call_block(&context);
    CHECK(context.x[30] == AT(BL_AFTER_CALL) && context.x[0] == 0 && context.pc == AT(BL_CALLEE));
    CHECK(context.chain_budget == (BL_EXPECT_CHAIN ? 0 : 1));

    /* RET from the callee, entered directly. */
    reset(BL_CALLEE, 8);
    context.x[30] = AT(BL_AFTER_CALL);
    callee_block(&context);
    CHECK(context.x[0] == 5);
#if BL_EXPECT_INDIRECT
    CHECK(context.x[1] == 1 && context.halted == RECOMP_HALT_BREAKPOINT);
    CHECK(context.pc == AT(BL_BRK) && context.chain_budget == 7);
#else
    CHECK(context.x[1] == 0 && !context.halted);
    CHECK(context.pc == AT(BL_AFTER_CALL) && context.chain_budget == 8);
#endif
    /* A RET with no budget left parks its target and runs nothing more. */
    reset(BL_CALLEE, 1);
    context.x[30] = AT(BL_AFTER_CALL);
    callee_block(&context);
    CHECK(context.x[0] == 5 && context.x[1] == 0 && !context.halted);
    CHECK(context.pc == AT(BL_AFTER_CALL) && context.chain_budget == (BL_EXPECT_INDIRECT ? 0 : 1));

    /* BLR through a register: the return address is set before the target runs. */
    reset(BL_BLR, 8);
    context.x[9] = AT(BL_CALLEE);
    blr_block(&context);
    CHECK(context.x[30] == AT(BL_BLR_BRK));
#if BL_EXPECT_INDIRECT
    CHECK(context.x[0] == 5 && context.halted == RECOMP_HALT_BREAKPOINT);
    CHECK(context.pc == AT(BL_BLR_BRK) && context.chain_budget == 6);
#else
    CHECK(context.x[0] == 0 && !context.halted);
    CHECK(context.pc == AT(BL_CALLEE) && context.chain_budget == 8);
#endif

    /* BR through a register, then the callee's RET to x30. */
    reset(BL_BR, 8);
    context.x[10] = AT(BL_CALLEE);
    context.x[30] = AT(BL_AFTER_CALL);
    br_block(&context);
#if BL_EXPECT_INDIRECT
    CHECK(context.x[0] == 5 && context.x[1] == 1 && context.halted == RECOMP_HALT_BREAKPOINT);
    CHECK(context.pc == AT(BL_BRK) && context.chain_budget == 6);
#else
    CHECK(context.x[0] == 0 && !context.halted);
    CHECK(context.pc == AT(BL_CALLEE) && context.chain_budget == 8);
#endif

    /* A RET or BR into the middle of a block enters through the block's own entry
       switch: the callee's RET runs, its ADD does not. */
    reset(BL_BR, 8);
    context.x[10] = AT(BL_CALLEE + 4);
    context.x[30] = AT(BL_AFTER_CALL);
    br_block(&context);
#if BL_EXPECT_INDIRECT
    CHECK(context.x[0] == 0 && context.x[1] == 1 && context.halted == RECOMP_HALT_BREAKPOINT);
    CHECK(context.pc == AT(BL_BRK) && context.chain_budget == 6);
#else
    CHECK(context.x[0] == 0 && !context.halted);
    CHECK(context.pc == AT(BL_CALLEE + 4) && context.chain_budget == 8);
#endif

    /* A target the module has no block for goes back to the run-slice loop and the
       host dispatcher unchanged, spending no budget. */
    reset(BL_BR, 8);
    context.x[10] = 0x5000; /* below a non-zero base */
    br_block(&context);
    CHECK(context.pc == 0x5000 && !context.halted && context.chain_budget == 8);

    /* The whole slice reaches the same end state with or without chaining. */
    reset(BL_CALL, 16);
    recomp_image_run_slice(&context);
    CHECK(context.x[0] == 5 && context.x[1] == 1 && context.x[30] == AT(BL_AFTER_CALL));
    CHECK(context.halted == RECOMP_HALT_BREAKPOINT && context.pc == AT(BL_BRK));

    /* A slice that runs out between the BL and the callee resumes where it parked. */
    reset(BL_CALL, 1);
    recomp_image_run_slice(&context);
    CHECK(context.x[0] == 0 && context.pc == AT(BL_CALLEE) && context.chain_budget == 0);
    context.chain_budget = 16;
    recomp_image_run_slice(&context);
    CHECK(context.x[0] == 5 && context.x[1] == 1 && context.halted == RECOMP_HALT_BREAKPOINT);

    /* Callees of 8 and 9 instructions (the chain start threshold) end the same way. */
    reset(BL_CALL8, 32);
    recomp_image_run_slice(&context);
    CHECK(context.x[2] == 7 && context.halted == RECOMP_HALT_BREAKPOINT && context.pc == AT(0x1028));
    reset(BL_CALL9, 32);
    recomp_image_run_slice(&context);
    CHECK(context.x[2] == 8 && context.halted == RECOMP_HALT_BREAKPOINT && context.pc == AT(0x1054));

    return 0;
}

#if BL_EXPECT_START
/* The start entry runs the same code guard as the general entry: with the callee's code changed
   neither runs it (hosted + SUYU_RECOMP_STRICT=0 reports halted=UNHANDLED instead of aborting). */
static int check_start_entry_guard(void) {
    static uint32_t changed_code[sizeof(bl_code) / sizeof(bl_code[0])];
    memcpy(changed_code, bl_code, sizeof(changed_code));
    changed_code[(BL_CALLEE - BL_BASE) / 4] ^= 0x400u;
#ifdef _WIN32
    _putenv_s("SUYU_RECOMP_STRICT", "0");
#else
    setenv("SUYU_RECOMP_STRICT", "0", 1);
#endif
    module_base = 0;
    recomp_image_set_base(0);
    reset(BL_CALLEE, 8);
    context.mem = (uint8_t*)changed_code;
    blk_bl_000000000000100c_s(&context);
    CHECK(context.halted == RECOMP_HALT_UNHANDLED && context.x[0] == 0 && context.chain_budget == 8);
    reset(BL_CALLEE, 8);
    context.mem = (uint8_t*)changed_code;
    recomp_image_lookup(BL_CALLEE)(&context);
    CHECK(context.halted == RECOMP_HALT_UNHANDLED && context.x[0] == 0);
    /* Control: unchanged code runs through the start entry. */
    reset(BL_CALLEE, 8);
    context.x[30] = BL_AFTER_CALL;
    blk_bl_000000000000100c_s(&context);
    CHECK(context.x[0] == 5 && context.halted != RECOMP_HALT_UNHANDLED);
    return 0;
}
#endif

int main(void) {
    recomp_image_guard_v2(2);
    /* Offsets and addresses coincide at base 0; the second run moves the module. */
    if (run_checks(0) || run_checks(UINT64_C(0x40000))) {
        return 1;
    }
#if BL_EXPECT_START
    if (check_start_entry_guard()) {
        return 1;
    }
#endif
    puts("PASS chain fixture");
    return 0;
}
