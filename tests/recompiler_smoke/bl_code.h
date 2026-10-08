#pragma once
#include <stdint.h>
/* Synthetic instructions only: no executable or cartridge input.

   The direct B/BL chaining fixture (export.cpp --bl). A call, the code after it,
   and a callee that returns, so the effect of chaining a BL is visible in what a
   single block function leaves behind. */
static const uint32_t bl_code[] = {
    0x94000003, /* 1000: bl   100c            (x30 = 1004) */
    0x91000421, /* 1004: add  x1,x1,#1        after the call returns */
    0xd4200000, /* 1008: brk  #0 */
    0x91001400, /* 100c: add  x0,x0,#5        the callee */
    0xd65f03c0, /* 1010: ret */
};
#define BL_BASE 0x1000
#define BL_CALL 0x1000
#define BL_AFTER_CALL 0x1004
#define BL_BRK 0x1008
#define BL_CALLEE 0x100c
