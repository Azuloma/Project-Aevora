#pragma once
#include <stdint.h>
/* Synthetic instructions only: no executable or cartridge input.

   The direct B/BL and indirect RET/BR/BLR chaining fixture (export.cpp --bl). A
   call, the code after it, a callee that returns, and one BLR and one BR through
   registers, so the effect of chaining each is visible in what a single block
   function leaves behind. */
static const uint32_t bl_code[] = {
    0x94000003, /* 1000: bl   100c            (x30 = 1004) */
    0x91000421, /* 1004: add  x1,x1,#1        after the call returns */
    0xd4200000, /* 1008: brk  #0 */
    0x91001400, /* 100c: add  x0,x0,#5        the callee */
    0xd65f03c0, /* 1010: ret */
    0xd63f0120, /* 1014: blr  x9              (x30 = 1018) */
    0xd4200000, /* 1018: brk  #0 */
    0xd61f0140, /* 101c: br   x10 */
    0xd4200000, /* 1020: brk  #0 */
};
#define BL_BASE 0x1000
#define BL_CALL 0x1000
#define BL_AFTER_CALL 0x1004
#define BL_BRK 0x1008
#define BL_CALLEE 0x100c
#define BL_BLR 0x1014
#define BL_BLR_BRK 0x1018
#define BL_BR 0x101c
