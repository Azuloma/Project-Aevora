/* SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project */
/* SPDX-License-Identifier: GPL-3.0-or-later */

/* Registry v1: the single definition of the layout a split (static + shared
   image) export's generated recomp_registration.c fills in and the launcher
   reads, usable from C and C++. The exporter writes only values (see
   WriteRecompRegistryV1C in recomp_topology.h); no struct is spelled out in a
   second place.

   Entries are in NSO load order and may mix statically linked modules (function
   pointers filled in) and shared-image modules (pointers NULL; the launcher loads
   dll_file_utf8 and resolves the exports below by name). */

#ifndef RECOMP_REGISTRY_V1_H
#define RECOMP_REGISTRY_V1_H

#include <stdint.h>

#include "recomp_image_identity.h"

#define SUYU_RECOMP_REGISTRY_V1_VERSION 1u

#define SUYU_RECOMP_BINDING_STATIC 0u
#define SUYU_RECOMP_BINDING_DLL 1u

/* Export names of a shared image, as the generator emits them (unrenamed). A
   static module's same functions carry a per-module suffix instead
   (recomp_image_lookup_<module>, ...). The identity export is named by
   RECOMP_IMAGE_IDENTITY_V1_SYMBOL. */
#define RECOMP_EXPORT_LOOKUP "recomp_image_lookup"
#define RECOMP_EXPORT_SET_BASE "recomp_image_set_base"
#define RECOMP_EXPORT_RUN_SLICE "recomp_image_run_slice"
#define RECOMP_EXPORT_ABI "recomp_image_abi"
#define RECOMP_EXPORT_GUARD_V2 "recomp_image_guard_v2"
#define RECOMP_EXPORT_FEATURES "recomp_image_features"
#define RECOMP_EXPORT_FASTMEM_V1 "recomp_image_fastmem_v1"
#define RECOMP_EXPORT_GUARD_GEN_V1 "recomp_image_guard_gen_v1"
#define RECOMP_EXPORT_FPX_V1 "recomp_image_fpx_v1"
#define RECOMP_EXPORT_XMOD_V1 "recomp_image_xmod_v1"

#define SUYU_RECOMP_XMOD_V1_VERSION 1u
#define SUYU_RECOMP_XMOD_V1_MAX_MODULES 16u

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*SuyuRecompBlockFn)(void*);

/* XMOD1: one immutable table the host gives every image, ascending by load base. The owner of a pc is
   the last entry with base <= pc; its lookup (absolute pc) returns a normal block entry or NULL. */
typedef struct SuyuRecompXmodEntryV1 {
    uint64_t base;
    SuyuRecompBlockFn (*lookup)(uint64_t);
} SuyuRecompXmodEntryV1;

typedef struct SuyuRecompXmodTableV1 {
    uint32_t version;     /* SUYU_RECOMP_XMOD_V1_VERSION */
    uint32_t struct_size; /* sizeof(SuyuRecompXmodTableV1) */
    uint32_t entry_size;  /* sizeof(SuyuRecompXmodEntryV1) */
    uint32_t count;       /* used entries, 1..SUYU_RECOMP_XMOD_V1_MAX_MODULES */
    SuyuRecompXmodEntryV1 entries[SUYU_RECOMP_XMOD_V1_MAX_MODULES];
} SuyuRecompXmodTableV1;

typedef struct SuyuRecompRegistryEntryV1 {
    const char* canonical_name;  /* NSO name: rtld, main, subsdkN, sdk */
    uint32_t binding;            /* SUYU_RECOMP_BINDING_* */
    const char* dll_file_utf8;   /* DLL only: plain basename beside the launcher; NULL for static */
    uint32_t image_abi;          /* expected recomp_image_abi() */
    uint32_t expected_features;  /* expected recomp_image_features() (0 for ABI 5) */
    /* Static entries only (NULL for DLL entries; the launcher resolves DLL exports by name). */
    SuyuRecompBlockFn (*lookup)(uint64_t);
    void (*set_base)(uint64_t);
    SuyuRecompBlockFn run_slice;
    unsigned (*image_abi_fn)(void);
    unsigned (*guard_v2)(unsigned);
    unsigned (*features)(void);                                               /* NULL unless FM1 */
    unsigned (*fastmem_v1)(uint32_t, uint32_t, uint64_t, uint32_t, uint32_t); /* NULL unless FM1 */
    uint32_t* (*guard_gen_v1)(uint32_t, uint64_t*, uint64_t*, const uint64_t**); /* NULL unless GG1 */
    unsigned (*fpx_v1)(uint32_t, uint32_t, uint64_t);                         /* NULL unless FPX1 */
    /* Takes the table (NULL clears it); returns 1 when accepted. NULL unless XMOD1. */
    unsigned (*xmod_v1)(const SuyuRecompXmodTableV1*);
} SuyuRecompRegistryEntryV1;

typedef struct SuyuRecompRegistryV1 {
    uint32_t version;     /* SUYU_RECOMP_REGISTRY_V1_VERSION */
    uint32_t struct_size; /* sizeof(SuyuRecompRegistryV1) */
    uint32_t entry_size;  /* sizeof(SuyuRecompRegistryEntryV1) */
    uint32_t entry_count, static_count, dll_count;
    uint32_t bundle_abi;
    uint8_t bundle_id[RECOMP_BUNDLE_ID_BYTES];
    const SuyuRecompRegistryEntryV1* entries; /* NSO load order */
} SuyuRecompRegistryV1;

/* Defined by the generated recomp_registration.c. */
const SuyuRecompRegistryV1* suyu_recomp_registry_v1(void);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_REGISTRY_V1_H */
