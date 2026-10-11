#pragma once
#include <stdint.h>
/* Synthetic instructions only: no executable or cartridge input.

   The code of every module of the three-module split fixture (static "smoke",
   shared "second", static "third"). Each block is one instruction ending in brk,
   so a test enters exactly one operation in exactly one image, and registers
   carry the guest state from one image to the next. Operands: x5 = address,
   x13 = scalar value, x10/x11 = pair values, x14 = exclusive status, x20 =
   counter, s1/s2 = FADD inputs, s0 = its result.

   The modules are loaded XMOD_BASE_SPACING apart, in topology order from
   XMOD_FIRST_BASE. The hop block counts in x1 and its branch (the second word,
   at XMOD_HOP + 4) lands on XMOD_HOP of the next module, so a chain of hops walks the
   images in load order and the last one leaves the bundle. The ring block (x5 = table
   of 8-byte targets, x1 = index and count) jumps with BR to the next table entry, which is
   how a guest crosses modules through a computed branch. */
#define XMOD_FIRST_BASE UINT64_C(0x1000000)
#define XMOD_BASE_SPACING UINT64_C(0x1000000)
#define XMOD_HOP_BRANCH (UINT32_C(0x14000000) | (uint32_t)((XMOD_BASE_SPACING - 4) / 4))
static const uint32_t xmod_code[] = {
    0xc85f7cad, 0xd4200000, /* 1000: ldxr  x13,[x5]            ; brk */
    0xc80e7cad, 0xd4200000, /* 1008: stxr  w14,x13,[x5]        ; brk */
    0xd503305f, 0xd4200000, /* 1010: clrex                     ; brk */
    0xc87f2caa, 0xd4200000, /* 1018: ldxp  x10,x11,[x5]        ; brk */
    0xc82e2caa, 0xd4200000, /* 1020: stxp  w14,x10,x11,[x5]    ; brk */
    0xd53be054, 0xd4200000, /* 1028: mrs   x20,cntvct_el0      ; brk */
    0x1e222820, 0xd4200000, /* 1030: fadd  s0,s1,s2            ; brk */
    0x91000400, 0xd4200000, /* 1038: add   x0,x0,#1            ; brk (guard probe) */
    0x91000421, XMOD_HOP_BRANCH, /* 1040: add x1,x1,#1 ; b <1040 of the next module> */
    0xf86178a2, 0x91000421, /* 1048: ldr x2,[x5,x1,lsl #3] ; add x1,x1,#1 */
    0xd61f0040, 0xd4200000, /* 1050: br x2 ; brk (ring block) */
    0xd4000ee1, 0xd4200000, /* 1058: svc #0x77 ; brk */
    0xd65f03c0, 0xd4200000, /* 1060: ret ; brk */
    0xd63f0040, 0xd4200000 /* 1068: blr x2 ; brk */
};
#define XMOD_LDXR 0x1000
#define XMOD_STXR 0x1008
#define XMOD_CLREX 0x1010
#define XMOD_LDXP 0x1018
#define XMOD_STXP 0x1020
#define XMOD_CNTVCT 0x1028
#define XMOD_FADD 0x1030
#define XMOD_GUARD_PROBE 0x1038
#define XMOD_HOP 0x1040
#define XMOD_RING 0x1048
#define XMOD_SVC 0x1058
#define XMOD_SVC_NUMBER 0x77
#define XMOD_RET 0x1060
#define XMOD_BLR 0x1068
