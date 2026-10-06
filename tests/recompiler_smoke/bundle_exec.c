/* SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Executes generated blocks of two different images (one static, one shared)
   against ONE host bridge, the way the emulator hands every module the same
   RecompHostMem. Built against the generated recomp_runtime.h, so the context and
   bridge layouts are the generated ones. Called by bundle_host.cpp with the
   production bundle's run_slice pointers.

   The synthetic code has no CLREX, no exclusive pair and no counter read, so what
   is checked across the boundary is the memory block (loads, stores, pairs, Q, and
   a LDXR/STXR pair) served by the shared callbacks: both images must reach the
   same load/store/exclusive callbacks and the shared reservation state, and give
   the byte-model result. */

#include "recomp_runtime.h"
#include "code.h"

#include <stdio.h>
#include <string.h>

#define GUEST_DATA_BASE UINT64_C(0x10000)
#define GUEST_DATA_SIZE 0x4000u
#define LOAD_X5 UINT64_C(0x10010)
#define STORE_X6 UINT64_C(0x11020)
#define IMAGE_COUNT 2u
#define CODE_FETCH_SIZE 0u /* a load of size 0 is an instruction-word fetch */

static unsigned char guest_data[GUEST_DATA_SIZE];
static uint64_t image_base[IMAGE_COUNT];
static unsigned current_image;
static uint64_t reservation_addr;
static int reservation_valid;
static unsigned excl_loads[IMAGE_COUNT], excl_stores[IMAGE_COUNT], plain_accesses[IMAGE_COUNT];

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
        /* The word, plus the "readable" marker bit the generated guard expects
           (see run.c's checked_load). */
        for (image = 0; image < IMAGE_COUNT; ++image) {
            const uint64_t code = image_base[image] + 0x1000;
            if (va >= code && va - code < sizeof(smoke_code)) {
                return smoke_code[(va - code) / 4] | UINT64_C(0x100000000);
            }
        }
        return 0;
    }
    ++plain_accesses[current_image];
    return model_read(va, size);
}

static void host_store(void* user, uint64_t va, uint32_t size, uint64_t value) {
    (void)user;
    ++plain_accesses[current_image];
    model_write(va, size, value);
}

static uint64_t host_excl_load(void* user, uint64_t va, uint32_t size) {
    (void)user;
    ++excl_loads[current_image];
    reservation_addr = va;
    reservation_valid = 1;
    return model_read(va, size);
}

static uint32_t host_excl_store(void* user, uint64_t va, uint32_t size, uint64_t value) {
    const int ok = reservation_valid && reservation_addr == va;
    (void)user;
    ++excl_stores[current_image];
    reservation_valid = 0;
    if (ok) model_write(va, size, value);
    return ok ? 0u : 1u;
}

static RecompHostMem bridge;

static int run_image(unsigned image, void (*run_slice)(void*)) {
    GuestContext context;
    uint64_t expected_x5_word;
    current_image = image;
    memset(&context, 0, sizeof(context));
    context.pc = image_base[image] + SMOKE_MEM_BLOCK;
    context.mem = (uint8_t*)smoke_code;
    context.mem_size = sizeof(smoke_code);
    context.mem_base_vaddr = image_base[image] + 0x1000;
    context.pending_svc = ~UINT64_C(0);
    context.chain_budget = 1;
    context.host_mem = &bridge;
    context.x[5] = LOAD_X5;
    context.x[6] = STORE_X6;
    expected_x5_word = model_read(LOAD_X5, 8);
    run_slice(&context);
    if (context.halted != RECOMP_HALT_BREAKPOINT) {
        fprintf(stderr, "exec: image %u did not reach the block's brk (halted %d)\n", image,
                (int)context.halted);
        return 0;
    }
    /* LDXR/STXR on the shared reservation succeeded (status 0) and the loaded
       words are the model's. */
    if (context.x[14] != 0 || context.x[13] != expected_x5_word ||
        context.x[4] != expected_x5_word) {
        fprintf(stderr, "exec: image %u produced a wrong result (x14 %llu)\n", image,
                (unsigned long long)context.x[14]);
        return 0;
    }
    /* The block's last store (STLR x12) put the loaded word at x6. */
    if (model_read(STORE_X6, 8) != context.x[12]) {
        fprintf(stderr, "exec: image %u left a wrong word at the store address\n", image);
        return 0;
    }
    return 1;
}

/* Returns 1 when both images ran the block through the shared bridge. */
int bundle_exec_cross_images(void (*run_slice_first)(void*), void (*run_slice_second)(void*),
                             uint64_t first_base, uint64_t second_base) {
    unsigned i;
    for (i = 0; i < GUEST_DATA_SIZE; ++i) guest_data[i] = (unsigned char)(i * 131u + 1u);
    image_base[0] = first_base;
    image_base[1] = second_base;
    bridge.load = host_load;
    bridge.store = host_store;
    bridge.excl_load = host_excl_load;
    bridge.excl_store = host_excl_store;
    /* page_entries stays NULL: every access takes the callback path. */
    if (!run_image(0, run_slice_first) || !run_image(1, run_slice_second)) {
        return 0;
    }
    for (i = 0; i < IMAGE_COUNT; ++i) {
        if (excl_loads[i] != 1 || excl_stores[i] != 1 || plain_accesses[i] == 0) {
            fprintf(stderr, "exec: image %u reached the shared callbacks %u/%u/%u times\n", i,
                    excl_loads[i], excl_stores[i], plain_accesses[i]);
            return 0;
        }
    }
    /* Each STXR consumed the reservation its own LDXR took through the shared state. */
    if (reservation_valid) {
        fprintf(stderr, "exec: a reservation survived the STXR\n");
        return 0;
    }
    return 1;
}
