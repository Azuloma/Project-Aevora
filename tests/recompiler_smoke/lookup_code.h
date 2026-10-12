#pragma once
#include <stdint.h>
/* Synthetic instructions only: eight two-instruction functions, each followed by three zero
   words the block discovery leaves out. Exported with two blocks per unit (export.cpp --lookup),
   so the module has four segments and the flat block index has holes. */
#define LOOKUP_BASE 0x1000
#define LOOKUP_FUNCS 8
#define LOOKUP_COVERED_WORDS (LOOKUP_FUNCS * 2)
static const uint32_t lookup_code[] = {
    0x91000400, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#1; ret */
    0x91000800, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#2; ret */
    0x91000c00, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#3; ret */
    0x91001000, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#4; ret */
    0x91001400, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#5; ret */
    0x91001800, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#6; ret */
    0x91001c00, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#7; ret */
    0x91002000, 0xd65f03c0, 0, 0, 0, /* add x0,x0,#8; ret */
};
