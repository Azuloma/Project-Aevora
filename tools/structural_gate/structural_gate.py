#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structural gate of the native roadmap (stdlib only): checks a launcher for removed components.

Evidence is the PE import table plus symbol names from the image's PDB (llvm-pdbutil publics,
checked against the image's CodeView GUID and age) or from a text file (link map, llvm-nm output).
Every milestone shares the one deny list in DENY_LISTS below.

Without --require-absent the tool only reports counts (baseline). With it, any match in a listed
milestone's components fails. Too few symbols fail too, so an empty input never passes, and a
text file is accepted in gate mode only with --accept-unverified-symbols (it names no image).
Exit status: 0 report or pass, 1 gate failure, 2 usage or input error.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

# component -> milestones whose "removed" claim it backs, and substrings of symbol or DLL names.
# Symbol patterns are MSVC-mangled ("@Dynarmic@@"); import patterns match lowercase DLL names.
DENY_LISTS = {
    'jit': {
        'milestones': ('N1', 'N9'),
        'symbols': ('@Dynarmic@@',),
        'imports': ('dynarmic',),
    },
    'kernel_memory': {
        'milestones': ('N2', 'N9'),
        'symbols': ('KPageTable', 'KMemoryManager', 'KMemoryBlockManager'),
        'imports': (),
    },
    'scheduler': {
        'milestones': ('N3', 'N9'),
        'symbols': ('KScheduler', 'KPriorityQueue', 'KHardwareTimer', '@Timing@Core@@'),
        'imports': (),
    },
    'service_framework': {
        'milestones': ('N4', 'N9'),
        'symbols': ('ServiceFrameworkBase', 'HLERequestContext', 'ServerManager@Service@@'),
        'imports': (),
    },
    'filesystem': {
        'milestones': ('N5', 'N9'),
        'symbols': ('@FileSys@@',),
        'imports': (),
    },
    'hid': {
        'milestones': ('N6', 'N9'),
        'symbols': ('@HID@Service@@', '@HID@Core@@', '@InputCommon@@'),
        'imports': ('hid_core', 'input_common'),
    },
    'audio': {
        'milestones': ('N7', 'N9'),
        'symbols': ('@AudioCore@@',),
        'imports': ('audio_core',),
    },
    'gpu_maxwell': {
        'milestones': ('N8', 'N9'),
        'symbols': ('@Tegra@@', '@VideoCommon@@', '@Shader@@', '@Nvidia@Service@@'),
        'imports': ('video_core', 'shader_recompiler'),
    },
    'emulator_system': {
        'milestones': ('N9',),
        'symbols': ('@System@Core@@',),
        'imports': (),
    },
}

DEFAULT_MIN_SYMBOLS = 1000
MAX_EXAMPLES = 3
MAX_IMPORT_DESCRIPTORS = 4096
MAX_IMPORTS_PER_DLL = 65536
MAX_IMPORT_NAME_LENGTH = 512
IMPORT_DIRECTORY = 1
DEBUG_DIRECTORY = 6
CODEVIEW_TYPE = 2
DEBUG_ENTRY_BYTES = 28
IMPORT_DESCRIPTOR_BYTES = 20
LLVM_PDBUTIL_FALLBACK = r'C:\Program Files\LLVM\bin\llvm-pdbutil.exe'
PUBLIC_SYMBOL = re.compile(r'S_PUB32 \[size = \d+\] `(.*)`')
PDB_GUID = re.compile(r'GUID: \{([0-9A-Fa-f-]+)\}')
PDB_AGE = re.compile(r'\bAge: (\d+)')


class GateError(Exception):
    """Unusable input; the message names the file and the field."""


class PeImage:
    """Bounds-checked reads of the headers, sections and data directories of a PE file."""

    def __init__(self, handle, path):
        self.handle, self.path = handle, path
        self.file_size = Path(path).stat().st_size
        if self.read_file(0, 2, 'DOS header') != b'MZ':
            raise GateError(f'{path}: not a PE image (no MZ header)')
        pe_offset = struct.unpack('<I', self.read_file(0x3C, 4, 'e_lfanew'))[0]
        if self.read_file(pe_offset, 4, 'PE signature') != b'PE\0\0':
            raise GateError(f'{path}: not a PE image (no PE signature)')
        section_count, optional_size = struct.unpack(
            '<H12xH', self.read_file(pe_offset + 6, 16, 'COFF header'))
        optional_offset = pe_offset + 24
        magic = struct.unpack('<H', self.read_file(optional_offset, 2, 'optional magic'))[0]
        if magic not in (0x10B, 0x20B):
            raise GateError(f'{path}: unknown optional header magic {magic:#x}')
        self.is_pe32_plus = magic == 0x20B
        count_offset = 108 if self.is_pe32_plus else 92
        self.directory_count = struct.unpack(
            '<I', self.read_file(optional_offset + count_offset, 4, 'NumberOfRvaAndSizes'))[0]
        self.directory_offset = optional_offset + count_offset + 4
        if count_offset + 4 + self.directory_count * 8 > optional_size:
            raise GateError(f'{path}: {self.directory_count} data directories do not fit the '
                            f'{optional_size}-byte optional header')
        self.sections = []
        for index in range(section_count):
            raw = self.read_file(optional_offset + optional_size + index * 40, 40, 'section header')
            _, virtual_address, raw_size, raw_pointer = struct.unpack_from('<IIII', raw, 8)
            self.sections.append((virtual_address, raw_size, raw_pointer))

    def read_file(self, offset, size, what):
        self.handle.seek(offset)
        data = self.handle.read(size)
        if len(data) != size:
            raise GateError(f'{self.path}: truncated PE image, cannot read {what} at offset {offset:#x}')
        return data

    def directory(self, index):
        if index >= self.directory_count:
            return 0, 0
        return struct.unpack('<II', self.read_file(self.directory_offset + index * 8, 8,
                                                   f'data directory {index}'))

    def locate(self, rva, length, what):
        """File offset of [rva, rva + length), which must lie in the file-backed part of a section."""
        for address, raw_size, pointer in self.sections:
            if address <= rva and rva + length <= address + raw_size:
                return pointer + (rva - address), address + raw_size - rva
        raise GateError(f'{self.path}: {what} at RVA {rva:#x} lies outside the file data of every section')

    def read_rva(self, rva, length, what):
        offset, _ = self.locate(rva, length, what)
        return self.read_file(offset, length, what)

    def c_string(self, rva, what):
        offset, available = self.locate(rva, 1, what)
        self.handle.seek(offset)
        data = self.handle.read(min(MAX_IMPORT_NAME_LENGTH + 1, available))
        end = data.find(b'\0')
        if end < 0:
            raise GateError(f'{self.path}: unterminated or over-long {what} at RVA {rva:#x}')
        return data[:end].decode('ascii', 'replace')


def milestones_of(deny_lists):
    return sorted({m for entry in deny_lists.values() for m in entry['milestones']})


def parse_imports(path):
    """Return {dll name: [function names]} from the PE import directory; raise GateError if malformed."""
    with open(path, 'rb') as handle:
        image = PeImage(handle, path)
        directory_rva, directory_size = image.directory(IMPORT_DIRECTORY)
        if directory_rva == 0 and directory_size == 0:
            return {}
        if directory_rva == 0 or directory_size < IMPORT_DESCRIPTOR_BYTES:
            raise GateError(f'{path}: malformed import directory (RVA {directory_rva:#x}, size {directory_size})')
        thunk_bytes = 8 if image.is_pe32_plus else 4
        ordinal_flag = 1 << (63 if image.is_pe32_plus else 31)
        imports = {}
        terminated = False
        for index in range(min(directory_size // IMPORT_DESCRIPTOR_BYTES, MAX_IMPORT_DESCRIPTORS)):
            raw = image.read_rva(directory_rva + index * IMPORT_DESCRIPTOR_BYTES,
                                 IMPORT_DESCRIPTOR_BYTES, 'import descriptor')
            lookup_rva, _, _, name_rva, first_thunk_rva = struct.unpack('<IIIII', raw)
            if not any(raw):
                terminated = True
                break
            if name_rva == 0:
                raise GateError(f'{path}: import descriptor {index} has no DLL name')
            dll = image.c_string(name_rva, 'DLL name')
            names = imports.setdefault(dll.lower(), [])
            table_rva = lookup_rva or first_thunk_rva
            for slot in range(MAX_IMPORTS_PER_DLL):
                thunk = int.from_bytes(image.read_rva(table_rva + slot * thunk_bytes, thunk_bytes,
                                                      f'{dll} thunk'), 'little')
                if thunk == 0:
                    break
                names.append(f'#{thunk & 0xFFFF}' if thunk & ordinal_flag
                             else image.c_string(thunk + 2, 'import name'))
            else:
                raise GateError(f'{path}: {dll} lists more than {MAX_IMPORTS_PER_DLL} imports')
        if not terminated:
            raise GateError(f'{path}: import directory has no terminating descriptor within its size')
        return imports


def read_codeview(path):
    """(GUID text, age) of the image's CodeView record, or None when it has none."""
    with open(path, 'rb') as handle:
        image = PeImage(handle, path)
        directory_rva, directory_size = image.directory(DEBUG_DIRECTORY)
        for index in range(directory_size // DEBUG_ENTRY_BYTES):
            raw = image.read_rva(directory_rva + index * DEBUG_ENTRY_BYTES, DEBUG_ENTRY_BYTES, 'debug entry')
            entry_type, data_size, _, data_pointer = struct.unpack_from('<IIII', raw, 12)
            if entry_type != CODEVIEW_TYPE or data_size < 24:
                continue
            record = image.read_file(data_pointer, 24, 'CodeView record')
            if record[:4] != b'RSDS':
                continue
            first, second, third = struct.unpack_from('<IHH', record, 4)
            last_eight = record[12:20]
            guid = (f'{first:08X}-{second:04X}-{third:04X}-{last_eight[:2].hex().upper()}-'
                    f'{last_eight[2:].hex().upper()}')
            return guid, struct.unpack_from('<I', record, 20)[0]
    return None


def publics_names(lines):
    """Names of the S_PUB32 records of an llvm-pdbutil publics dump; other lines are metadata."""
    for line in lines:
        found = PUBLIC_SYMBOL.search(line)
        if found:
            yield found.group(1)


def plain_names(lines):
    for line in lines:
        if line.strip():
            yield line.strip()


def find_pdbutil(explicit):
    for candidate in (explicit, shutil.which('llvm-pdbutil'), LLVM_PDBUTIL_FALLBACK):
        if candidate and Path(candidate).is_file():
            return str(candidate)
    raise GateError('llvm-pdbutil not found; pass --llvm-pdbutil or use --symbols-file')


def pdb_identity(pdb_path, pdbutil):
    """(GUID text, age) from llvm-pdbutil dump -summary."""
    result = subprocess.run([pdbutil, 'dump', '-summary', str(pdb_path)], capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
    guid, age = PDB_GUID.search(result.stdout), PDB_AGE.search(result.stdout)
    if result.returncode != 0 or not guid or not age:
        raise GateError(f'cannot read the GUID and age of {pdb_path} with llvm-pdbutil: '
                        f'{result.stderr.strip()[:300] or "no GUID/Age in the summary"}')
    return guid.group(1).upper(), int(age.group(1))


def check_pdb_matches_image(image, pdb_path, pdbutil):
    wanted = read_codeview(image)
    if wanted is None:
        raise GateError(f'{image}: no CodeView record, so the PDB cannot be matched; use a linked image with debug info')
    found = pdb_identity(pdb_path, pdbutil)
    if (wanted[0].upper(), wanted[1]) != found:
        raise GateError(f'{pdb_path} does not belong to {image}: image GUID/age {wanted[0]}/{wanted[1]}, '
                        f'PDB {found[0]}/{found[1]}')


def pdb_symbols(pdb_path, pdbutil):
    with tempfile.TemporaryFile('w+', encoding='utf-8', errors='replace') as error_log:
        process = subprocess.Popen([pdbutil, 'dump', '-publics', str(pdb_path)], stdout=subprocess.PIPE,
                                   stderr=error_log, text=True, encoding='utf-8', errors='replace')
        yield from publics_names(process.stdout)
        if process.wait() != 0:
            error_log.seek(0)
            raise GateError(f'llvm-pdbutil failed on {pdb_path}: {error_log.read(300).strip()}')


def evaluate(symbol_names, imports, deny_lists=DENY_LISTS):
    """Count matches per component; keeps MAX_EXAMPLES names per component."""
    result = {name: {'milestones': list(entry['milestones']), 'symbol_matches': 0,
                     'import_matches': 0, 'examples': []}
              for name, entry in deny_lists.items()}
    total = 0
    for symbol in symbol_names:
        total += 1
        for name, entry in deny_lists.items():
            if any(pattern in symbol for pattern in entry['symbols']):
                result[name]['symbol_matches'] += 1
                if len(result[name]['examples']) < MAX_EXAMPLES:
                    result[name]['examples'].append(symbol)
    for name, entry in deny_lists.items():
        for dll, functions in imports.items():
            for text in (dll, *functions):
                if any(pattern in text for pattern in entry['imports']):
                    result[name]['import_matches'] += 1
                    if len(result[name]['examples']) < MAX_EXAMPLES:
                        result[name]['examples'].append(f'import {dll}: {text}')
    return total, result


def failing_components(result, required_milestones):
    """Components of the required milestones that still have any match."""
    return sorted(name for name, entry in result.items()
                  if set(entry['milestones']) & set(required_milestones)
                  and (entry['symbol_matches'] or entry['import_matches']))


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def build_report(image, imports, symbol_total, result, required, failing, symbol_source, verified, with_hash):
    return {
        'format': 'structural-gate-report-v1',
        'image': {'name': Path(image).name, 'size': Path(image).stat().st_size,
                  'sha256': sha256_of(image) if with_hash else None},
        'symbol_source': symbol_source,
        'symbols_verified_against_image': verified,
        'symbols_scanned': symbol_total,
        'imported_dlls': sorted(imports),
        'mode': 'gate' if required else 'baseline',
        'required_absent': sorted(required),
        'failing_components': failing,
        'components': result,
    }


def format_text(report):
    lines = [f"structural gate ({report['mode']}): {report['image']['name']}, "
             f"{report['image']['size']} bytes, {report['symbols_scanned']} symbols from "
             f"{report['symbol_source']}, {len(report['imported_dlls'])} imported DLLs"]
    for name, entry in report['components'].items():
        lines.append(f"  {name:18} {','.join(entry['milestones']):8} symbols={entry['symbol_matches']} "
                     f"imports={entry['import_matches']}")
        lines.extend(f'      e.g. {example[:110]}' for example in entry['examples'])
    if report['mode'] == 'gate':
        verdict = 'FAIL ' + ', '.join(report['failing_components']) if report['failing_components'] else 'PASS'
        note = '' if report['symbols_verified_against_image'] else ' (symbols NOT verified against the image)'
        lines.append(f'RESULT: {verdict}{note}')
    return '\n'.join(lines) + '\n'


def positive_int(text):
    value = int(text)
    if value < 1:
        raise argparse.ArgumentTypeError('must be at least 1')
    return value


def read_symbols(args, image):
    """(names, source description, verified); the PDB is matched to the image before it is read."""
    if args.pdb:
        pdbutil = find_pdbutil(args.llvm_pdbutil)
        if not Path(args.pdb).is_file():
            raise GateError(f'PDB not found: {args.pdb}')
        check_pdb_matches_image(image, args.pdb, pdbutil)
        return pdb_symbols(args.pdb, pdbutil), f'pdb {Path(args.pdb).name}', True
    with open(args.symbols_file, encoding='utf-8', errors='replace') as handle:
        lines = handle.read().splitlines()
    names = publics_names(lines) if any('S_PUB32 [' in line for line in lines) else plain_names(lines)
    return names, f'file {Path(args.symbols_file).name}', False


def run(args):
    image = Path(args.image)
    if not image.is_file():
        raise GateError(f'image not found: {image}')
    required = [m.strip() for m in args.require_absent.split(',') if m.strip()] if args.require_absent is not None else []
    if args.require_absent is not None and not required:
        raise GateError('--require-absent names no milestone; leave it out for a baseline report')
    unknown = sorted(set(required) - set(milestones_of(DENY_LISTS)))
    if unknown:
        raise GateError(f'unknown milestone(s) {unknown}; known: {milestones_of(DENY_LISTS)}')
    if bool(args.pdb) == bool(args.symbols_file):
        raise GateError('give exactly one of --pdb and --symbols-file')
    if required and args.symbols_file and not args.accept_unverified_symbols:
        raise GateError('a symbols file names no image; add --accept-unverified-symbols or use --pdb')
    imports = parse_imports(image)
    symbols, source_name, verified = read_symbols(args, image)
    total, result = evaluate(symbols, imports)
    if total < args.min_symbols:
        raise GateError(f'only {total} symbols read from the {source_name} (minimum {args.min_symbols}); '
                        'refusing to report an unchecked image')
    failing = failing_components(result, required) if required else []
    report = build_report(image, imports, total, result, required, failing, source_name, verified,
                          not args.no_hash)
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    text = format_text(report)
    if args.text_out:
        Path(args.text_out).write_text(text, encoding='utf-8')
    sys.stdout.write(text)
    return 1 if failing else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('image', help='launcher PE image (exe or dll)')
    parser.add_argument('--pdb', help='PDB of the image; symbols come from llvm-pdbutil dump -publics')
    parser.add_argument('--symbols-file', help='text file of symbol names (link map, llvm-nm, publics dump)')
    parser.add_argument('--accept-unverified-symbols', action='store_true',
                        help='allow gate mode on a symbols file, which cannot be matched to the image')
    parser.add_argument('--llvm-pdbutil', help='path of llvm-pdbutil (default: PATH, then LLVM install)')
    parser.add_argument('--require-absent', help='comma-separated milestones, for example N1 or N9')
    parser.add_argument('--min-symbols', type=positive_int, default=DEFAULT_MIN_SYMBOLS)
    parser.add_argument('--json-out', help='write the machine-readable report here')
    parser.add_argument('--text-out', help='write the text report here')
    parser.add_argument('--no-hash', action='store_true', help='skip the SHA-256 of the image')
    try:
        return run(parser.parse_args(argv))
    except GateError as error:
        sys.stderr.write(f'structural_gate: {error}\n')
    except OSError as error:
        sys.stderr.write(f'structural_gate: I/O error: {error}\n')
    return 2


if __name__ == '__main__':
    sys.exit(main())
