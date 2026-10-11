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
#include "core/arm/recomp/recomp_bridge_required.h"

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
typedef RunSlice (*LookupBlock)(uint64_t);

/* The host's own list of callbacks it refuses to run without, applied to the generated
   RecompHostMem. */
RECOMP_DEFINE_FIRST_MISSING_BRIDGE_CALLBACK(first_missing_bridge_callback, RecompHostMem)

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

/* ---- the host bridge the host refuses to run without ---- */

#define CHECK_NULLED_CALLBACK(callback)                                                        \
    {                                                                                          \
        RecompHostMem damaged = bridge;                                                        \
        const char* missing;                                                                   \
        damaged.callback = NULL;                                                               \
        missing = first_missing_bridge_callback(&damaged);                                     \
        CHECK(missing != NULL && strcmp(missing, #callback) == 0);                             \
    }

/* Every callback on the required list, taken away alone, is named; the complete bridge
   passes. */
static int bridge_required_callbacks(void) {
    CHECK(first_missing_bridge_callback(&bridge) == NULL);
    RECOMP_REQUIRED_BRIDGE_CALLBACKS(CHECK_NULLED_CALLBACK)
    return 1;
}

/* Why the list exists: with the exclusive callbacks missing, the generated runtime of
   each image keeps a private monitor, so a reservation taken in one image is unknown to
   the next and the store fails although nothing else touched the address. */
static int private_monitor_hazard(void) {
    static RecompHostMem damaged;
    uint64_t before;
    reset_scenario();
    damaged = bridge;
    damaged.excl_load = NULL;
    damaged.excl_store = NULL;
    context.host_mem = &damaged;
    before = model_read(ADDR_X, 8);
    context.x[5] = ADDR_X;
    CHECK(step(0, XMOD_LDXR));
    context.x[13] = ~before;
    CHECK(step(1, XMOD_STXR));
    CHECK(context.x[14] == 1);
    CHECK(model_read(ADDR_X, 8) == before);
    CHECK(counts[0].excl_loads == 0 && counts[1].excl_stores == 0);
    return 1;
}

/* ---- a branch that leaves the image: lookup miss and hand-over ---- */

/* The dispatch loop of the launcher, as a model: the first module whose lookup knows the
   pc runs its slice; a pc no module knows is a miss, and a strict host stops there with
   no fallback. Returns the miss pc. */
#define NO_MISS UINT64_C(0)
static uint64_t dispatch_hops(LookupBlock lookup[IMAGE_COUNT], unsigned start_image,
                              unsigned visits[IMAGE_COUNT], unsigned* hand_overs) {
    unsigned guard;
    context.pc = image_base[start_image] + XMOD_HOP;
    *hand_overs = 0;
    for (guard = 0; guard < 4 * IMAGE_COUNT; ++guard) {
        unsigned image;
        for (image = 0; image < IMAGE_COUNT; ++image) {
            if (lookup[image](context.pc)) break;
        }
        if (image == IMAGE_COUNT) return context.pc;
        ++visits[image];
        ++*hand_overs;
        current_image = image;
        context.mem_base_vaddr = image_base[image] + CODE_MODULE_OFFSET;
        context.halted = 0;
        image_run[image](&context);
        if (context.halted != 0) return NO_MISS;
    }
    return NO_MISS;
}

/* A chain of hops from `start_image` walks every image from there to the last, in load
   order, then misses at the pc one spacing past the last image: no image runs it, and
   nothing after the miss changes the guest state. */
static int hop_chain(LookupBlock lookup[IMAGE_COUNT], unsigned start_image) {
    unsigned visits[IMAGE_COUNT] = {0, 0, 0};
    unsigned hand_overs, image;
    uint64_t miss_pc, registers_at_miss;
    reset_scenario();
    miss_pc = dispatch_hops(lookup, start_image, visits, &hand_overs);
    if (miss_pc != image_base[IMAGE_COUNT - 1] + XMOD_BASE_SPACING + XMOD_HOP) {
        fprintf(stderr, "xmod_exec: chain from image %u ended at pc %llx (halted %d, x1 %llu, %u hand-overs)\n",
                start_image, (unsigned long long)context.pc, context.halted,
                (unsigned long long)context.x[1], hand_overs);
    }
    CHECK(miss_pc == image_base[IMAGE_COUNT - 1] + XMOD_BASE_SPACING + XMOD_HOP);
    CHECK(hand_overs == IMAGE_COUNT - start_image);
    CHECK(context.x[1] == IMAGE_COUNT - start_image);
    for (image = 0; image < IMAGE_COUNT; ++image) {
        CHECK(visits[image] == (image >= start_image ? 1u : 0u));
    }
    /* The miss is final: asking again finds nothing and runs nothing. */
    registers_at_miss = context.x[1];
    for (image = 0; image < IMAGE_COUNT; ++image) CHECK(lookup[image](miss_pc) == NULL);
    CHECK(context.x[1] == registers_at_miss && context.pc == miss_pc);
    return 1;
}

/* A pc inside no module (below the first, between none, beyond the last) misses in
   every image's own lookup. */
static int lookup_misses(LookupBlock lookup[IMAGE_COUNT]) {
    const uint64_t outside[] = {0, image_base[0] - 4, image_base[0] + XMOD_HOP - 0x1000,
                                image_base[IMAGE_COUNT - 1] + XMOD_BASE_SPACING + XMOD_HOP};
    unsigned i, image;
    for (i = 0; i < sizeof(outside) / sizeof(outside[0]); ++i) {
        for (image = 0; image < IMAGE_COUNT; ++image) {
            CHECK(lookup[image](outside[i]) == NULL);
        }
    }
    return 1;
}

/* Returns 1 when every cross-image scenario passed. */
int xmod_exec_scenarios(RunSlice run[IMAGE_COUNT], LookupBlock lookup[IMAGE_COUNT],
                        const uint64_t base[IMAGE_COUNT]) {
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
    CHECK(bridge_required_callbacks());
    CHECK(private_monitor_hazard());
    CHECK(lookup_misses(lookup));
    for (i = 0; i < IMAGE_COUNT; ++i) CHECK(hop_chain(lookup, i));
    return 1;
}

/* ---- the guard in each image ---- */

/* The image whose probe block the abort must come from; -1 means the one running. */
static int abort_image = -1;
static uint64_t abort_x0 = 99; /* x0 when the probe block is entered */

static void guard_aborted(int signal_number) {
    const unsigned image = abort_image >= 0 ? (unsigned)abort_image : current_image;
    (void)signal_number;
    /* Intended only when the abort came from the probe block of the mutated image,
       before the block's add ran (x0 still 99). */
    _Exit(context.pc == image_base[image] + XMOD_GUARD_PROBE && context.x[0] == abort_x0
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

/* ---- computed branches across images (XMOD1) ---- */

#define RING_TABLE UINT64_C(0x10400)
#define RING_MAX_TARGETS 8u
#define RING_START_BUDGET 256

/* Resets the scenario and loads the branch-target table and chain budget of a ring run. */
static void ring_prepare(const uint64_t* targets, unsigned count, int budget) {
    unsigned i;
    reset_scenario();
    for (i = 0; i < count && i < RING_MAX_TARGETS; ++i) model_write(RING_TABLE + 8 * i, 8, targets[i]);
    context.x[5] = RING_TABLE;
    context.chain_budget = budget;
}

/* Runs one slice from `offset` in `start`; context.pc is then where control left. */
static void ring_go(unsigned start, unsigned offset) {
    current_image = start;
    context.pc = image_base[start] + offset;
    context.mem_base_vaddr = image_base[start] + CODE_MODULE_OFFSET;
    image_run[start](&context);
}

/* Ring block in `start`; x1 then counts the ring blocks executed. */
static int ring_run(unsigned start, const uint64_t* targets, unsigned count, int budget) {
    ring_prepare(targets, count, budget);
    ring_go(start, XMOD_RING);
    return 1;
}

/* Published: a computed branch follows its target into the owning module, one budget unit per hop.
   Without the table (expect_cross == 0) it leaves the first module, as without XMOD1. */
static int ring_cases(int expect_cross) {
    const uint64_t a = image_base[0] + XMOD_RING, b = image_base[1] + XMOD_RING,
                   c = image_base[2] + XMOD_RING;
    const uint64_t out = image_base[2] + XMOD_BASE_SPACING + XMOD_RING;
    const uint64_t loop[] = {b, c, a, b, c, a, out};
    const uint64_t from_b[] = {c, a, out};

    /* A -> B -> C -> A -> B -> C -> A -> out of the bundle: seven blocks, six hops. */
    CHECK(ring_run(0, loop, 7, RING_START_BUDGET));
    if (expect_cross) {
        CHECK(context.x[1] == 7 && context.pc == out && context.chain_budget == RING_START_BUDGET - 6);
    } else {
        CHECK(context.x[1] == 1 && context.pc == b && context.chain_budget == RING_START_BUDGET);
    }
    /* The DLL as the starting image, and the budget bounds the chain across modules. */
    CHECK(ring_run(1, from_b, 3, RING_START_BUDGET));
    CHECK(context.x[1] == (expect_cross ? 3u : 1u) && context.pc == (expect_cross ? out : c));
    CHECK(ring_run(0, loop, 7, 2));
    CHECK(context.x[1] == (expect_cross ? 2u : 1u) && context.pc == (expect_cross ? c : b));
    CHECK(context.chain_budget == (expect_cross ? 0 : 2));
    CHECK(ring_run(0, loop, 7, 1));
    CHECK(context.x[1] == 1 && context.pc == b && context.chain_budget == (expect_cross ? 0 : 1));
    /* The block the budget stopped at is picked up where it left off, as the host does. */
    CHECK(ring_run(0, loop, 7, 2));
    context.chain_budget = RING_START_BUDGET;
    image_run[expect_cross ? 2 : 1](&context);
    CHECK(context.x[1] == (expect_cross ? 7u : 2u) && context.pc == (expect_cross ? out : c));

    /* Targets no module owns, or that are not a block start, are a miss: nothing runs. */
    {
        const uint64_t below[] = {UINT64_C(0x500)};
        const uint64_t unowned[] = {image_base[2] + XMOD_BASE_SPACING + 0x1000};
        const uint64_t misaligned[] = {b + 2};
        const uint64_t not_code[] = {image_base[1] + 0x800000};
        CHECK(ring_run(0, below, 1, RING_START_BUDGET));
        CHECK(context.x[1] == 1 && context.pc == below[0] && context.chain_budget == RING_START_BUDGET);
        CHECK(ring_run(0, unowned, 1, RING_START_BUDGET));
        CHECK(context.x[1] == 1 && context.pc == unowned[0]);
        CHECK(ring_run(0, misaligned, 1, RING_START_BUDGET));
        CHECK(context.x[1] == 1 && context.pc == misaligned[0]);
        CHECK(ring_run(0, not_code, 1, RING_START_BUDGET));
        CHECK(context.x[1] == 1 && context.pc == not_code[0]);
    }
    /* Another module's SVC and breakpoint blocks end the chain the way they do in-module. */
    {
        const uint64_t svc_target[] = {image_base[1] + XMOD_SVC};
        const uint64_t halt_target[] = {image_base[2] + XMOD_GUARD_PROBE};
        CHECK(ring_run(0, svc_target, 1, RING_START_BUDGET));
        if (expect_cross) {
            CHECK(context.pending_svc == XMOD_SVC_NUMBER && context.pc == svc_target[0] + 4);
        } else {
            CHECK(context.pending_svc == ~UINT64_C(0) && context.pc == svc_target[0]);
        }
        CHECK(ring_run(0, halt_target, 1, RING_START_BUDGET));
        if (expect_cross) {
            CHECK(context.halted == RECOMP_HALT_BREAKPOINT && context.x[0] == 1);
        } else {
            CHECK(context.halted == 0 && context.x[0] == 0 && context.pc == halt_target[0]);
        }
    }
    return 1;
}

/* RET and BLR leave the first module through x30 and x2, as BR does through the table. */
static int return_and_call_cases(int expect_cross) {
    const uint64_t probe1 = image_base[1] + XMOD_GUARD_PROBE, probe2 = image_base[2] + XMOD_GUARD_PROBE;
    ring_prepare(NULL, 0, RING_START_BUDGET);
    context.x[30] = probe1;
    ring_go(0, XMOD_RET);
    if (expect_cross) {
        CHECK(context.halted == RECOMP_HALT_BREAKPOINT && context.x[0] == 1 && context.x[30] == probe1);
    } else {
        CHECK(context.halted == 0 && context.x[0] == 0 && context.pc == probe1);
    }
    ring_prepare(NULL, 0, RING_START_BUDGET);
    context.x[2] = probe2;
    ring_go(0, XMOD_BLR);
    CHECK(context.x[30] == image_base[0] + XMOD_BLR + 4);
    if (expect_cross) {
        CHECK(context.halted == RECOMP_HALT_BREAKPOINT && context.x[0] == 1);
    } else {
        CHECK(context.halted == 0 && context.x[0] == 0 && context.pc == probe2);
    }
    return 1;
}

/* The FP control and status registers live in the shared context, so a block reached through a
   chain sees and updates the same FPSR as one the host entered directly. */
static int fp_cases(int expect_cross) {
    const uint64_t target[] = {image_base[2] + XMOD_FADD};
    static const struct { uint64_t gate; uint32_t a, b, result; uint64_t fpsr; } rows[] = {
        {0, FP_ONE, FP_HALF_ULP, FP_ONE, FPSR_IXC},
        {FPSR_IXC, FP_ONE, FP_HALF_ULP, FP_ONE, FPSR_IXC},
        {0, FP_ONE, FP_ONE, FP_TWO, 0},
        {FPSR_IXC, FP_ONE, FP_ONE, FP_TWO, FPSR_IXC}};
    unsigned i;
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        ring_prepare(target, 1, RING_START_BUDGET);
        context.vreg[1][0] = rows[i].a;
        context.vreg[2][0] = rows[i].b;
        context.vreg[0][0] = ~UINT64_C(0);
        context.fpcr = 0;
        context.fpsr = rows[i].gate;
        ring_go(0, XMOD_RING);
        if (expect_cross) {
            CHECK(context.vreg[0][0] == rows[i].result && context.vreg[0][1] == 0 &&
                  context.fpsr == rows[i].fpsr);
        } else {
            CHECK(context.vreg[0][0] == ~UINT64_C(0) && context.fpsr == rows[i].gate);
        }
    }
    return 1;
}

int xmod_exec_cross(RunSlice run[IMAGE_COUNT], const uint64_t base[IMAGE_COUNT], int expect_cross) {
    set_images(run, base);
    install_bridge();
    return ring_cases(expect_cross) && return_and_call_cases(expect_cross) && fp_cases(expect_cross);
}

/* The code guard of a module reached through a chained branch still stops a changed block
   before its effect (exit EXIT_GUARD_ABORT from guard_aborted); returns only on failure. */
int xmod_exec_cross_guard(RunSlice run[IMAGE_COUNT], const uint64_t base[IMAGE_COUNT]) {
    const uint64_t target[] = {base[2] + XMOD_GUARD_PROBE};
    set_images(run, base);
    install_bridge();
    mutated_fetch_va = base[2] + XMOD_GUARD_PROBE;
    printf("probe pc %llx expected %08x read %08x\n", (unsigned long long)mutated_fetch_va,
           (unsigned)xmod_code[(XMOD_GUARD_PROBE - CODE_MODULE_OFFSET) / 4],
           (unsigned)(xmod_code[(XMOD_GUARD_PROBE - CODE_MODULE_OFFSET) / 4] ^ 1u));
    fflush(stdout);
    signal(SIGABRT, guard_aborted);
    abort_image = 2;
    abort_x0 = 0;
    ring_run(0, target, 1, RING_START_BUDGET);
    fprintf(stderr, "xmod_exec: the chained branch ran a block whose code changed\n");
    return 0;
}
