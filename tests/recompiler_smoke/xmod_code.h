#pragma once
#include <stdint.h>
/* Synthetic instructions only: no executable or cartridge input.

   The code of every module of the three-module split fixture (static "smoke",
   shared "second", static "third"). Each block is one instruction ending in brk,
   so a test enters exactly one operation in exactly one image, and registers
   carry the guest state from one image to the next. Operands: x5 = address,
   x13 = scalar value, x10/x11 = pair values, x14 = exclusive status, x20 =
   counter, s1/s2 = FADD inputs, s0 = its result. */
static const uint32_t xmod_code[] = {
    0xc85f7cad, 0xd4200000, /* 1000: ldxr  x13,[x5]            ; brk */
    0xc80e7cad, 0xd4200000, /* 1008: stxr  w14,x13,[x5]        ; brk */
    0xd503305f, 0xd4200000, /* 1010: clrex                     ; brk */
    0xc87f2caa, 0xd4200000, /* 1018: ldxp  x10,x11,[x5]        ; brk */
    0xc82e2caa, 0xd4200000, /* 1020: stxp  w14,x10,x11,[x5]    ; brk */
    0xd53be054, 0xd4200000, /* 1028: mrs   x20,cntvct_el0      ; brk */
    0x1e222820, 0xd4200000, /* 1030: fadd  s0,s1,s2            ; brk */
    0x91000400, 0xd4200000  /* 1038: add   x0,x0,#1            ; brk (guard probe) */
};
#define XMOD_LDXR 0x1000
#define XMOD_STXR 0x1008
#define XMOD_CLREX 0x1010
#define XMOD_LDXP 0x1018
#define XMOD_STXP 0x1020
#define XMOD_CNTVCT 0x1028
#define XMOD_FADD 0x1030
#define XMOD_GUARD_PROBE 0x1038
