#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a synthetic, bootable multi-module extracted-ExeFS fixture for suyu (stdlib only).

Writes a sparse set of canonical NSO modules (default: rtld, main, subsdk1, sdk), `main.npdm`
and `romfs.bin`. Everything is synthesised here: no Nintendo keys, firmware, code or game
data are used. The title/program ID 0x0100000000E57A10 is SYNTHETIC and belongs to no real
title.

The loader places the modules contiguously, in canonical order, each at the page-aligned end
of the previous one, so the distance between any two modules is fixed by their image sizes
alone (the base itself is randomised). The program chains through every module with `bl`
instructions whose targets sit in the *next* module, at an address that is not any module's
entry (text+0): a call target that only a cross-module reference reaches.

    rtld.entry -> prints, calls main -> prints, calls subsdk1 -> prints, calls sdk -> prints,
    returns up the chain; rtld sleeps 100 ms (so the debug-string flusher runs) and exits.

Each module prints its own marker with svcOutputDebugString, so a run shows which modules
executed. Optionally one module carries no readable embedded module path (rodata does not
start with {u32 0, s32 length, path}), the case Core::FindModules cannot name.

Usage: python make_multimodule_fixture.py OUTDIR [--modules a,b,c] [--no-path NAME]
"""
import argparse
import struct
import sys
from pathlib import Path

import make_fixture

TITLE_ID = 0x0100000000E57A10  # synthetic; not a real title
MARKER_PREFIX = "SUYU_MULTI_MODULE_OK:"
# Final marker, printed by the last module of the chain on the way back up.
CHAIN_COMPLETE_MARKER = "SUYU_MULTI_MODULE_CHAIN_COMPLETE"
CANONICAL_ORDER = ["rtld", "main"] + ["subsdk%d" % i for i in range(10)] + ["sdk"]
DEFAULT_MODULES = ["rtld", "main", "subsdk1", "sdk"]

PAGE = make_fixture.PAGE
MOD0_OFF = make_fixture.MOD0_OFF
CODE_OFF = make_fixture.CODE_OFF
RODATA_PAGES = 1
DATA_PAGES = 1
MSG_OFF = make_fixture.MSG_OFF

# Text pages per module: uneven on purpose, so module bases are not a uniform stride.
TEXT_PAGES = {"rtld": 1, "main": 2}
DEFAULT_TEXT_PAGES = 1


def text_pages(name: str) -> int:
    return TEXT_PAGES.get(name, DEFAULT_TEXT_PAGES)


def image_size(name: str) -> int:
    """Page-aligned size of the module's loaded image (.text + .rodata + .data, no .bss)."""
    return (text_pages(name) + RODATA_PAGES + DATA_PAGES) * PAGE


def module_offsets(modules) -> dict:
    """Module name -> offset from the first module's base, as the loader lays them out."""
    offsets = {}
    cursor = 0
    for name in modules:
        offsets[name] = cursor
        cursor += image_size(name)
    return offsets


def marker_text(name: str) -> bytes:
    return (MARKER_PREFIX + name).encode() + b"\n"


# --- AArch64 words used by the fixture (hand-assembled; encodings checked against the
# --- single-module fixture's constants) ---

def adrp_x0(page_delta: int) -> int:
    assert 0 <= page_delta < (1 << 21)
    return 0x90000000 | ((page_delta & 3) << 29) | ((page_delta >> 2) << 5)


def add_x0_x0(imm12: int) -> int:
    assert 0 <= imm12 < 0x1000
    return 0x91000000 | (imm12 << 10)


def movz(reg: int, imm16: int, shift16: int = 0) -> int:
    return 0xD2800000 | (shift16 << 21) | (imm16 << 5) | reg


def movk(reg: int, imm16: int, shift16: int) -> int:
    return 0xF2800000 | (shift16 << 21) | (imm16 << 5) | reg


def svc(number: int) -> int:
    return 0xD4000001 | (number << 5)


def bl(byte_offset: int) -> int:
    assert byte_offset % 4 == 0 and -(1 << 27) <= byte_offset < (1 << 27)
    return 0x94000000 | ((byte_offset >> 2) & 0x3FFFFFF)


STR_X30_PRE = 0xF81F0FFE  # str x30, [sp, #-16]!
LDR_X30_POST = 0xF84107FE  # ldr x30, [sp], #16
RET = 0xD65F03C0
B_SELF = 0x14000000


def code_words(modules, name: str, offsets: dict) -> list:
    """The instructions at the module's CODE_OFF: print its marker, call the next module, then
    return (or, for the first module, sleep and exit)."""
    index = modules.index(name)
    is_first = index == 0
    is_last = index == len(modules) - 1
    length = len(marker_text(name))
    # adrp is relative to the page of its own instruction (page 0 of .text for the first
    # words); the module's .rodata sits after its text pages.
    rodata_page_delta = text_pages(name)
    words = []
    if not is_first:
        words.append(STR_X30_PRE)
    words += [
        adrp_x0(rodata_page_delta),
        add_x0_x0(MSG_OFF),
        movz(1, length),
        svc(0x27),  # svcOutputDebugString(x0 = string, x1 = length)
    ]
    if not is_last:
        callee = modules[index + 1]
        call_site = offsets[name] + CODE_OFF + 4 * len(words)
        target = offsets[callee] + CODE_OFF
        words.append(bl(target - call_site))
    else:
        # The chain's turning point: say the whole chain was entered.
        final = len(CHAIN_COMPLETE_MARKER.encode() + b"\n")
        words += [
            adrp_x0(rodata_page_delta),
            add_x0_x0(MSG_OFF + 0x40),
            movz(1, final),
            svc(0x27),
        ]
    if is_first:
        words += [
            movz(0, 0xE100),     # 100,000,000 ns = 0x05F5E100 (low half)
            movk(0, 0x5F5, 1),   # (high half)
            svc(0x0B),           # svcSleepThread(100 ms); keep under 250 ms (see README)
            svc(0x07),           # svcExitProcess()
            B_SELF,              # unreachable safety net
        ]
    else:
        words += [LDR_X30_POST, RET]
    return words


def make_module_nso(modules, name: str, offsets: dict, with_path: bool = True) -> bytes:
    pages = text_pages(name)
    rodata_loc = pages * PAGE
    data_loc = rodata_loc + RODATA_PAGES * PAGE

    text = bytearray()
    text += struct.pack("<I", 0x14000000 | (CODE_OFF >> 2))  # b CODE_OFF (module entry, text+0)
    text += struct.pack("<I", MOD0_OFF)
    text += struct.pack(
        "<7I",
        0x30444F4D,                   # "MOD0"
        data_loc - MOD0_OFF,          # dynamic_offset (rel. to MOD0): lone DT_NULL in .data
        data_loc + 0x10 - MOD0_OFF,   # bss_start (empty bss)
        data_loc + 0x10 - MOD0_OFF,   # bss_end
        0, 0,                         # eh_frame_hdr start/end (none)
        0)                            # runtime module object offset
    assert len(text) == CODE_OFF
    for word in code_words(modules, name, offsets):
        text += struct.pack("<I", word)
    assert len(text) <= pages * PAGE
    text += b"\x00" * (pages * PAGE - len(text))

    if with_path:
        # {u32 0, s32 length, path}: what Core::FindModules reads to name the module.
        path = ("synthetic/%s.nss" % name).encode()
        rodata = bytearray(struct.pack("<Ii", 0, len(path)) + path)
    else:
        # A non-zero first word: FindModules skips this module.
        rodata = bytearray(struct.pack("<Ii", 0xFFFFFFFF, 0))
    assert len(rodata) <= MSG_OFF
    rodata += b"\x00" * (MSG_OFF - len(rodata))
    rodata += marker_text(name)
    rodata += b"\x00" * (MSG_OFF + 0x40 - len(rodata))
    rodata += (CHAIN_COMPLETE_MARKER.encode() + b"\n")
    rodata += b"\x00" * (RODATA_PAGES * PAGE - len(rodata))
    data = bytearray(DATA_PAGES * PAGE)

    file_off = 0x100
    offs = [file_off, file_off + len(text), file_off + len(text) + len(rodata)]
    build_id = ("SYNTHETIC-MULTI-" + name).encode().ljust(0x20, b"\x00")
    hdr = bytearray()
    hdr += b"NSO0"
    hdr += struct.pack("<III", 0, 0, 0)  # version, reserved, flags=0 (no LZ4, no hash check)
    for off, loc, seg, extra in ((offs[0], 0, text, 1),
                                 (offs[1], rodata_loc, rodata, 1),
                                 (offs[2], data_loc, data, 0)):
        hdr += struct.pack("<IIII", off, loc, len(seg), extra)
    hdr += build_id
    hdr += struct.pack("<III", len(text), len(rodata), len(data))
    hdr += b"\x00" * 0x1C
    hdr += b"\x00" * 24
    hdr += b"\x00" * 96
    assert len(hdr) == 0x100, len(hdr)
    nso = bytes(hdr) + bytes(text) + bytes(rodata) + bytes(data)
    return nso


def write_multimodule_fixture(directory, modules=None, no_path=None) -> dict:
    """Writes the fixture into `directory` and returns what a test needs to know about it."""
    modules = list(modules or DEFAULT_MODULES)
    for name in modules:
        if name not in CANONICAL_ORDER:
            raise ValueError("not a canonical module name: " + name)
    if modules != sorted(modules, key=CANONICAL_ORDER.index) or len(set(modules)) != len(modules):
        raise ValueError("modules must be unique and in canonical order: %s" % modules)
    if len(modules) < 2:
        raise ValueError("a multi-module fixture needs at least two modules")
    if no_path is not None and no_path not in modules:
        raise ValueError("no_path module is not in the fixture: " + no_path)
    offsets = module_offsets(modules)
    d = Path(directory)
    d.mkdir(parents=True, exist_ok=True)
    for name in modules:
        (d / name).write_bytes(make_module_nso(modules, name, offsets, with_path=name != no_path))
    (d / "main.npdm").write_bytes(make_fixture.make_npdm(TITLE_ID))
    (d / "romfs.bin").write_bytes(make_fixture.make_romfs())
    return {
        "title_id": TITLE_ID,
        "modules": modules,
        "module_offsets": offsets,
        "image_sizes": {name: image_size(name) for name in modules},
        "markers": [(MARKER_PREFIX + name) for name in modules] + [CHAIN_COMPLETE_MARKER],
        "no_path": no_path,
        "files": modules + ["main.npdm", "romfs.bin"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("outdir")
    parser.add_argument("--modules", default=",".join(DEFAULT_MODULES),
                        help="comma-separated canonical module names, in canonical order")
    parser.add_argument("--no-path", default=None, metavar="NAME",
                        help="give this module no readable embedded module path")
    args = parser.parse_args()
    info = write_multimodule_fixture(args.outdir, args.modules.split(","), args.no_path)
    for name in info["files"]:
        print(name, (Path(args.outdir) / name).stat().st_size)
    print("title_id=0x%016X" % info["title_id"])
    for name, offset in info["module_offsets"].items():
        print("module %-8s +0x%X size 0x%X" % (name, offset, info["image_sizes"][name]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
