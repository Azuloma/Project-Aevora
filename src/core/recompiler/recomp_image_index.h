/* SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* The 32-bit block index contract of a recompiled image (index v1), usable from C and C++.
   The generator emits the image side from these definitions; hosts and tests use the functions below. */

#ifndef RECOMP_IMAGE_INDEX_H
#define RECOMP_IMAGE_INDEX_H

#include <stdint.h>

#define SUYU_RECOMP_INDEX32_V1_VERSION 1u

#define RECOMP_EXPORT_INDEX32_V1 "recomp_image_index32"

/* Why an image has no 32-bit table, reported in unavailable_reason. */
#define SUYU_RECOMP_INDEX32_WHY_EMPTY "the module has no blocks"
#define SUYU_RECOMP_INDEX32_WHY_ALLOC "the index allocation failed"
#define SUYU_RECOMP_INDEX32_WHY_RANGE "a block lies more than 2 GiB from the index anchor"
#define SUYU_RECOMP_INDEX32_WHY_ZERO "a block coincides with the index anchor (offset 0 means no block)"
#define SUYU_RECOMP_INDEX32_WHY_NOT_BUILT "the index has not been built"

#define SUYU_RECOMP_INDEX32_OFFSET_OK 0
#define SUYU_RECOMP_INDEX32_OFFSET_RANGE 1
#define SUYU_RECOMP_INDEX32_OFFSET_ZERO 2

/* Host sets version and struct_size. Return 1: [lo, hi] is the absolute guest range, table[(pc - lo) >> 2]
   an aligned pc's entry, a block is anchor + entry (0: none). Else unavailable_reason names why. */
typedef struct SuyuRecompIndex32V1 {
    uint32_t version;     /* SUYU_RECOMP_INDEX32_V1_VERSION */
    uint32_t struct_size; /* sizeof(SuyuRecompIndex32V1) */
    uint64_t lo;
    uint64_t hi;
    const int32_t* table;
    uint64_t anchor; /* absolute address of the image's anchor function */
    const char* unavailable_reason;
} SuyuRecompIndex32V1;

typedef int (*SuyuRecompIndex32V1Fn)(SuyuRecompIndex32V1*);

/* The offset of target from anchor as a table entry. Fails for a distance that does not fit an
   int32_t, and for 0, which the table uses for "no block". */
static inline int suyu_recomp_index32_offset(uint64_t anchor, uint64_t target, int32_t* offset) {
    const int64_t distance = (int64_t)(target - anchor);
    if (distance < INT32_MIN || distance > INT32_MAX) {
        return SUYU_RECOMP_INDEX32_OFFSET_RANGE;
    }
    if (distance == 0) {
        return SUYU_RECOMP_INDEX32_OFFSET_ZERO;
    }
    *offset = (int32_t)distance;
    return SUYU_RECOMP_INDEX32_OFFSET_OK;
}

/* The block address a table entry stands for; 0 for the empty entry. */
static inline uint64_t suyu_recomp_index32_target(uint64_t anchor, int32_t entry) {
    return entry == 0 ? 0 : anchor + (uint64_t)(int64_t)entry;
}

#endif /* RECOMP_IMAGE_INDEX_H */
