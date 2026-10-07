/* SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Executes single-instruction blocks of three images (static, shared, static)
   against ONE host bridge, the way the emulator hands every module the same
   RecompHostMem, with ONE guest context that migrates from image to image like a
   guest thread. Built against the generated recomp_runtime.h, so the context and
   bridge layouts are the generated ones. Called by xmod_host.cpp with the run_slice
   pointers of the production bundle.

   The bridge here is a model of the emulator's shared exclusive monitor and
   counter, not the emulator's own (that needs Core::System). What is checked is the
   image side of the contract: every image reaches the same callbacks, the state
   they keep is shared, and nothing is cached per image. */

#include "recomp_runtime.h"
#include "xmod_code.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_COUNT 3u
#define GUEST_DATA_BASE UINT64_C(0x10000)
#define GUEST_DATA_SIZE 0x4000u
#define ADDR_X UINT64_C(0x10100)
#define ADDR_Y UINT64_C(0x10200)
#define CODE_FETCH_SIZE 0u /* a load of size 0 is an instruction-word fetch */
#define CODE_MODULE_OFFSET 0x1000u
#define FETCH_READABLE_BIT UINT64_C(0x100000000)
#define FPSR_IXC 16u /* the inexact flag, and the FPX1 gate */
#define COUNTER_STEP 1000u
#define EXIT_GUARD_ABORT 86 /* the guard stopped the probe block, as intended */
#define EXIT_GUARD_OTHER 87

#define CHECK(x)                                                                               \
    do {                                                                                       \
        if (!(x)) {                                                                            \
            fprintf(stderr, "xmod_exec FAIL line %d: %s\n", __LINE__, #x);                     \
            return 0;                                                                          \
        }                                                                                      \
    } while (0)

typedef void (*RunSlice)(void*);

static unsigned char guest_data[GUEST_DATA_SIZE];
static RunSlice image_run[IMAGE_COUNT];
static uint64_t image_base[IMAGE_COUNT];
static unsigned current_image;

/* The shared exclusive monitor: one reservation of `reservation_bytes` at an address. */
static uint64_t reservation_addr;
static unsigned reservation_bytes;
static int reservation_valid;
static uint64_t counter_value;

typedef struct {
    unsigned excl_loads, excl_stores, pair_loads, pair_stores, clears, counter_reads;
} ImageCounts;
static ImageCounts counts[IMAGE_COUNT];

/* One bit of one fetched word is flipped for the guard probe (see xmod_exec_guard_probe). */
static uint64_t mutated_fetch_va;

static unsigned char* data_byte(uint64_t va) {
    return va >= GUEST_DATA_BASE && va - GUEST_DATA_BASE < GUEST_DATA_SIZE
               ? guest_data + (va - GUEST_DATA_BASE)
               : NULL;
}

static uint64_t model_read(uint64_t va, unsigned size) {
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < size; ++i) {
        const unsigned char* byte = data_byte(va + i);
        value |= (uint64_t)(byte ? *byte : 0) << (8 * i);
    }
    return value;
}

static void model_write(uint64_t va, unsigned size, uint64_t value) {
    unsigned i;
    for (i = 0; i < size; ++i) {
        unsigned char* byte = data_byte(va + i);
        if (byte) *byte = (unsigned char)(value >> (8 * i));
    }
}

static uint64_t host_load(void* user, uint64_t va, uint32_t size) {
    unsigned image;
    (void)user;
    if (size == CODE_FETCH_SIZE) {
        for (image = 0; image < IMAGE_COUNT; ++image) {
            const uint64_t code = image_base[image] + CODE_MODULE_OFFSET;
            if (va >= code && va - code < sizeof(xmod_code)) {
                uint64_t word = xmod_code[(va - code) / 4];
                if (va == mutated_fetch_va) word ^= 1;
                return word | FETCH_READABLE_BIT;
            }
        }
        return 0;
    }
    return model_read(va, size);
}

static void host_store(void* user, uint64_t va, uint32_t size, uint64_t value) {
    (void)user;
    model_write(va, size, value);
}

static void take_reservation(uint64_t va, unsigned bytes) {
    reservation_addr = va;
    reservation_bytes = bytes;
    reservation_valid = 1;
}

/* A store-exclusive of the same address and width as the load-exclusive succeeds
   once; either way it ends the reservation, as the emulator's monitor does. */
static int consume_reservation(uint64_t va, unsigned bytes) {
    const int ok = reservation_valid && reservation_addr == va && reservation_bytes == bytes;
    reservation_valid = 0;
    return ok;
}

static uint64_t host_excl_load(void* user, uint64_t va, uint32_t size) {
    (void)user;
    ++counts[current_image].excl_loads;
    take_reservation(va, size);
    return model_read(va, size);
}

static uint32_t host_excl_store(void* user, uint64_t va, uint32_t size, uint64_t value) {
    const int ok = consume_reservation(va, size);
    (void)user;
    ++counts[current_image].excl_stores;
    if (ok) model_write(va, size, value);
    return ok ? 0u : 1u;
}

static void host_excl_load_pair(void* user, uint64_t va, uint32_t size, uint64_t* lo,
                                uint64_t* hi) {
    (void)user;
    ++counts[current_image].pair_loads;
    take_reservation(va, 2 * size);
    *lo = model_read(va, size);
    *hi = model_read(va + size, size);
}

static uint32_t host_excl_store_pair(void* user, uint64_t va, uint32_t size, uint64_t lo,
                                     uint64_t hi) {
    const int ok = consume_reservation(va, 2 * size);
    (void)user;
    ++counts[current_image].pair_stores;
    if (ok) {
        model_write(va, size, lo);
        model_write(va + size, size, hi);
    }
    return ok ? 0u : 1u;
}

static void host_clear_excl(void* user) {
    (void)user;
    ++counts[current_image].clears;
    reservation_valid = 0;
}

static uint64_t host_read_cntpct(void* user) {
    (void)user;
    ++counts[current_image].counter_reads;
    counter_value += COUNTER_STEP;
    return counter_value;
}

static RecompHostMem bridge;
static GuestContext context;

static void reset_scenario(void) {
    unsigned i;
    for (i = 0; i < GUEST_DATA_SIZE; ++i) guest_data[i] = (unsigned char)(i * 131u + 1u);
    memset(counts, 0, sizeof(counts));
    reservation_valid = 0;
    counter_value = 0;
    memset(&context, 0, sizeof(context));
    context.pending_svc = ~UINT64_C(0);
    context.chain_budget = 1;
    context.host_mem = &bridge;
    context.mem = (uint8_t*)xmod_code;
    context.mem_size = sizeof(xmod_code);
}

/* Runs the one-instruction block at `offset` in `image` with the migrating context. */
static int step(unsigned image, unsigned offset) {
    current_image = image;
    context.pc = image_base[image] + offset;
    context.mem_base_vaddr = image_base[image] + CODE_MODULE_OFFSET;
    context.halted = 0;
    image_run[image](&context);
    if (context.halted != RECOMP_HALT_BREAKPOINT) {
        fprintf(stderr, "xmod_exec: image %u block %#x halted %d, not at its brk\n", image, offset,
                (int)context.halted);
        return 0;
    }
    return 1;
}

static void install_bridge(void) {
    memset(&bridge, 0, sizeof(bridge));
    bridge.load = host_load;
    bridge.store = host_store;
    bridge.excl_load = host_excl_load;
    bridge.excl_store = host_excl_store;
    bridge.clear_excl = host_clear_excl;
    bridge.read_cntpct = host_read_cntpct;
    bridge.excl_load_pair = host_excl_load_pair;
    bridge.excl_store_pair = host_excl_store_pair;
    /* page_entries stays NULL: every access takes the callback path. */
}

static void set_images(RunSlice run[IMAGE_COUNT], const uint64_t base[IMAGE_COUNT]) {
    unsigned i;
    for (i = 0; i < IMAGE_COUNT; ++i) {
        image_run[i] = run[i];
        image_base[i] = base[i];
    }
}

/* ---- scalar reservation across images ---- */

/* LDXR in `taker`, STXR in `storer`; the store must succeed and land. */
static int scalar_handoff(unsigned taker, unsigned storer, uint64_t value) {
    reset_scenario();
    context.x[5] = ADDR_X;
    CHECK(step(taker, XMOD_LDXR));
    CHECK(context.x[13] == model_read(ADDR_X, 8));
    context.x[13] = value;
    CHECK(step(storer, XMOD_STXR));
    CHECK(context.x[14] == 0);
    CHECK(model_read(ADDR_X, 8) == value);
    CHECK(!reservation_valid);
    CHECK(counts[taker].excl_loads == 1 && counts[storer].excl_stores == 1);
    return 1;
}

/* LDXR in `taker`, CLREX in `clearer`, STXR in `storer`: the store must fail and not land. */
static int scalar_cleared(unsigned taker, unsigned clearer, unsigned storer) {
    uint64_t before;
    reset_scenario();
    context.x[5] = ADDR_X;
    before = model_read(ADDR_X, 8);
    CHECK(step(taker, XMOD_LDXR));
    CHECK(step(clearer, XMOD_CLREX));
    CHECK(counts[clearer].clears == 1);
    context.x[13] = ~before;
    CHECK(step(storer, XMOD_STXR));
    CHECK(context.x[14] == 1);
    CHECK(model_read(ADDR_X, 8) == before);
    return 1;
}

/* STXR to another address than the load-exclusive's fails, and the failed store
   ends the reservation, so a retry at the right address fails too. */
static int scalar_wrong_address(void) {
    uint64_t before_x, before_y;
    reset_scenario();
    before_x = model_read(ADDR_X, 8);
    before_y = model_read(ADDR_Y, 8);
    context.x[5] = ADDR_X;
    CHECK(step(2, XMOD_LDXR));
    context.x[5] = ADDR_Y;
    context.x[13] = ~before_y;
    CHECK(step(0, XMOD_STXR));
    CHECK(context.x[14] == 1);
    CHECK(model_read(ADDR_Y, 8) == before_y);
    context.x[5] = ADDR_X;
    context.x[13] = ~before_x;
    CHECK(step(1, XMOD_STXR));
    CHECK(context.x[14] == 1);
    CHECK(model_read(ADDR_X, 8) == before_x);
    return 1;
}

/* A store-exclusive without any load-exclusive fails in every image. */
static int scalar_without_reservation(void) {
    unsigned image;
    for (image = 0; image < IMAGE_COUNT; ++image) {
        reset_scenario();
        context.x[5] = ADDR_X;
        context.x[13] = 1;
        CHECK(step(image, XMOD_STXR));
        CHECK(context.x[14] == 1);
        CHECK(counts[image].excl_stores == 1);
    }
    return 1;
}

/* ---- pair reservation across images ---- */

static int pair_handoff(unsigned taker, unsigned storer, uint64_t lo, uint64_t hi) {
    reset_scenario();
    context.x[5] = ADDR_X;
    CHECK(step(taker, XMOD_LDXP));
    CHECK(context.x[10] == model_read(ADDR_X, 8));
    CHECK(context.x[11] == model_read(ADDR_X + 8, 8));
    context.x[10] = lo;
    context.x[11] = hi;
    CHECK(step(storer, XMOD_STXP));
    CHECK(context.x[14] == 0);
    CHECK(model_read(ADDR_X, 8) == lo && model_read(ADDR_X + 8, 8) == hi);
    CHECK(!reservation_valid);
    CHECK(counts[taker].pair_loads == 1 && counts[storer].pair_stores == 1);
    /* The success consumed the reservation: the same store again fails. */
    context.x[10] = ~lo;
    CHECK(step(storer, XMOD_STXP));
    CHECK(context.x[14] == 1);
    CHECK(model_read(ADDR_X, 8) == lo);
    return 1;
}

static int pair_cleared(unsigned taker, unsigned clearer, unsigned storer) {
    uint64_t before_lo, before_hi;
    reset_scenario();
    context.x[5] = ADDR_X;
    before_lo = model_read(ADDR_X, 8);
    before_hi = model_read(ADDR_X + 8, 8);
    CHECK(step(taker, XMOD_LDXP));
    CHECK(step(clearer, XMOD_CLREX));
    context.x[10] = ~before_lo;
    context.x[11] = ~before_hi;
    CHECK(step(storer, XMOD_STXP));
    CHECK(context.x[14] == 1);
    CHECK(model_read(ADDR_X, 8) == before_lo && model_read(ADDR_X + 8, 8) == before_hi);
    return 1;
}

/* ---- the counter ---- */

/* One counter source for three images: every read, in whatever image it happens,
   returns the next value of the host's sequence, so time never steps back. */
static int counter_continuity(void) {
    static const unsigned order[] = {0, 1, 2, 2, 1, 0, 1};
    uint64_t previous = 0;
    unsigned i;
    reset_scenario();
    for (i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        context.x[20] = 0;
        CHECK(step(order[i], XMOD_CNTVCT));
        CHECK(context.x[20] == (uint64_t)(i + 1) * COUNTER_STEP);
        CHECK(context.x[20] > previous);
        previous = context.x[20];
    }
    CHECK(counts[0].counter_reads == 2 && counts[1].counter_reads == 3 &&
          counts[2].counter_reads == 2);
    return 1;
}

/* ---- FPX: the same answer in every image, gate open or closed ---- */

#define FP_ONE 0x3f800000u         /* 1.0f */
#define FP_TWO 0x40000000u         /* 2.0f */
#define FP_HALF_ULP 0x33800000u /* 2^-24: half an ulp of 1.0f, so 1.0f + it is a tie */

static int fadd_once(unsigned image, uint64_t gate_fpsr, uint32_t a, uint32_t b,
                     uint64_t* result, uint64_t* upper, uint64_t* fpsr) {
    context.vreg[1][0] = a;
    context.vreg[1][1] = 0;
    context.vreg[2][0] = b;
    context.vreg[2][1] = 0;
    context.vreg[0][0] = ~UINT64_C(0);
    context.vreg[0][1] = ~UINT64_C(0);
    context.fpcr = 0;
    context.fpsr = gate_fpsr;
    if (!step(image, XMOD_FADD)) return 0;
    *result = context.vreg[0][0];
    *upper = context.vreg[0][1];
    *fpsr = context.fpsr;
    return 1;
}

static int fpx_agreement(void) {
    unsigned image;
    reset_scenario();
    for (image = 0; image < IMAGE_COUNT; ++image) {
        uint64_t result, upper, fpsr;
        /* Inexact (a tie rounded to even): the value is 1.0f with the whole FPSR
           equal to the inexact flag alone (no other sticky flag), whether the gate
           was open (flag already set) or closed (exact body). */
        CHECK(fadd_once(image, 0, FP_ONE, FP_HALF_ULP, &result, &upper, &fpsr));
        CHECK(result == FP_ONE && upper == 0 && fpsr == FPSR_IXC);
        CHECK(fadd_once(image, FPSR_IXC, FP_ONE, FP_HALF_ULP, &result, &upper, &fpsr));
        CHECK(result == FP_ONE && upper == 0 && fpsr == FPSR_IXC);
        /* Exact: no flag appears from nothing, and a flag that was set stays. */
        CHECK(fadd_once(image, 0, FP_ONE, FP_ONE, &result, &upper, &fpsr));
        CHECK(result == FP_TWO && upper == 0 && fpsr == 0);
        CHECK(fadd_once(image, FPSR_IXC, FP_ONE, FP_ONE, &result, &upper, &fpsr));
        CHECK(result == FP_TWO && upper == 0 && fpsr == FPSR_IXC);
    }
    return 1;
}

/* The probe block, unmodified, runs in every image: x0 goes from 99 to 100 and the block
   ends at its brk. Without it a probe that aborts always (a wrong expected word, a
   refused guard) would look like the guard working. */
static int guard_probe_control(void) {
    unsigned image;
    for (image = 0; image < IMAGE_COUNT; ++image) {
        reset_scenario();
        context.x[0] = 99;
        CHECK(step(image, XMOD_GUARD_PROBE));
        CHECK(context.x[0] == 100);
    }
    return 1;
}

/* Returns 1 when every cross-image scenario passed. */
int xmod_exec_scenarios(RunSlice run[IMAGE_COUNT], const uint64_t base[IMAGE_COUNT]) {
    static const struct { unsigned taker, storer; } handoffs[] = {
        {0, 1}, {1, 0}, {1, 2}, {2, 1}, {0, 2}, {2, 0}, {0, 0}, {1, 1}, {2, 2}};
    static const struct { unsigned taker, clearer, storer; } clears[] = {
        {0, 1, 2}, {2, 1, 0}, {1, 0, 1}, {1, 2, 1}, {0, 0, 0}};
    unsigned i;
    set_images(run, base);
    install_bridge();
    for (i = 0; i < sizeof(handoffs) / sizeof(handoffs[0]); ++i) {
        CHECK(scalar_handoff(handoffs[i].taker, handoffs[i].storer, 0x1122334455667788u + i));
        CHECK(pair_handoff(handoffs[i].taker, handoffs[i].storer, 0xa1a2a3a4a5a6a7a8u + i,
                           0xb1b2b3b4b5b6b7b8u + i));
    }
    for (i = 0; i < sizeof(clears) / sizeof(clears[0]); ++i) {
        CHECK(scalar_cleared(clears[i].taker, clears[i].clearer, clears[i].storer));
        CHECK(pair_cleared(clears[i].taker, clears[i].clearer, clears[i].storer));
    }
    CHECK(scalar_wrong_address());
    CHECK(scalar_without_reservation());
    CHECK(counter_continuity());
    CHECK(fpx_agreement());
    CHECK(guard_probe_control());
    return 1;
}

/* ---- the guard in each image ---- */

static void guard_aborted(int signal_number) {
    (void)signal_number;
    /* Intended only when the abort came from the probe block of the mutated image,
       before the block's add ran (x0 still 99). */
    _Exit(context.pc == image_base[current_image] + XMOD_GUARD_PROBE && context.x[0] == 99
              ? EXIT_GUARD_ABORT
              : EXIT_GUARD_OTHER);
}

/* Runs the probe block of `image` after changing one bit of its fetched code. With
   the guard negotiated and SUYU_RECOMP_STRICT=1 the image must abort before the
   block's effect: the process then exits EXIT_GUARD_ABORT from guard_aborted. Returns
   0 when the block ran (the guard did not stop it). */
int xmod_exec_guard_probe(RunSlice run[IMAGE_COUNT], const uint64_t base[IMAGE_COUNT],
                          unsigned image) {
    set_images(run, base);
    install_bridge();
    reset_scenario();
    context.x[0] = 99;
    mutated_fetch_va = image_base[image] + XMOD_GUARD_PROBE;
    /* What the guard's diagnostic must name; run.py compares it with the runtime's own
       message ("... at 0x<pc> (expected <word>, read <word>)"). */
    printf("probe pc %llx expected %08x read %08x\n", (unsigned long long)mutated_fetch_va,
           (unsigned)xmod_code[(XMOD_GUARD_PROBE - CODE_MODULE_OFFSET) / 4],
           (unsigned)(xmod_code[(XMOD_GUARD_PROBE - CODE_MODULE_OFFSET) / 4] ^ 1u));
    fflush(stdout);
    signal(SIGABRT, guard_aborted);
    step(image, XMOD_GUARD_PROBE);
    fprintf(stderr, "xmod_exec: image %u ran a block whose code changed (x0 %llu)\n", image,
            (unsigned long long)context.x[0]);
    return 0;
}
