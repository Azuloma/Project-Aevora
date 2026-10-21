# SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic tests of tools/structural_gate/structural_gate.py (stdlib only, no game files).

STRUCTURAL_GATE_TOOL points the tests at a mutated copy of the tool for the negative controls.
Run: python -B -I -X utf8 tests/structural_gate/test_structural_gate.py
"""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(os.environ.get('STRUCTURAL_GATE_TOOL') or ROOT / 'tools/structural_gate/structural_gate.py')
spec = importlib.util.spec_from_file_location('structural_gate', TOOL)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

FILLER_SYMBOLS = 1500
SECTION_RVA = 0x1000
SECTION_FILE_OFFSET = 0x200
GUID = '0123ABCD-4567-89EF-0123-456789ABCDEF'
AGE = 7
DYNARMIC_SYMBOL = '?Run@Jit@A64@Dynarmic@@XZ'

# One independent, realistic MSVC-mangled name per production pattern: (component, pattern) -> name.
ORACLE = {
    ('jit', '@Dynarmic@@'): '?ADCS@TranslatorVisitor@A64@Dynarmic@@QEAA_N_NW4Reg@23@11@Z',
    ('kernel_memory', 'KPageTable'): '?GetRegionSize@KPageTableBase@Kernel@@QEBA_KW4MemoryState@Svc@2@@Z',
    ('kernel_memory', 'KMemoryManager'): '?Allocate@KMemoryManager@Kernel@@QEAAXXZ',
    ('kernel_memory', 'KMemoryBlockManager'): '?Initialize@KMemoryBlockManagerUpdateAllocator@Kernel@@AEAAXXZ',
    ('scheduler', 'KScheduler'): '?Activate@KScheduler@Kernel@@QEAAXAEAVKernelCore@2@@Z',
    ('scheduler', 'KPriorityQueue'): '?PushFront@KPriorityQueueImpl@Kernel@@QEAAXXZ',
    ('scheduler', 'KHardwareTimer'): '?Initialize@KHardwareTimer@Kernel@@QEAAXXZ',
    ('scheduler', '@Timing@Core@@'): '?ScheduleLoopingEvent@CoreTiming@Timing@Core@@QEAAXXZ',
    ('service_framework', 'ServiceFrameworkBase'): '?Invoke@ServiceFrameworkBase@Service@@QEAAXXZ',
    ('service_framework', 'HLERequestContext'): '?Set@IFoo@Service@@AEAAXAEAVHLERequestContext@2@@Z',
    ('service_framework', 'ServerManager@Service@@'): '??1?$unique_ptr@VServerManager@Service@@@std@@QEAA@XZ',
    ('filesystem', '@FileSys@@'): '?GetEntryType@VfsFilesystem@FileSys@@UEBA?AW4VfsEntryType@2@XZ',
    ('hid', '@HID@Service@@'): '?OnRelease@ConsoleSixAxis@HID@Service@@UEAAXXZ',
    ('hid', '@HID@Core@@'): '??0ConsoleStatus@HID@Core@@QEAA@XZ',
    ('hid', '@InputCommon@@'): '?Get@Stick@InputCommon@@QEAAXXZ',
    ('audio', '@AudioCore@@'): '?GetWorkbuffer@CompressorInfo@Renderer@AudioCore@@UEAA_KH@Z',
    ('gpu_maxwell', '@Tegra@@'): '?Foo@MemoryManager@Tegra@@QEAAXXZ',
    ('gpu_maxwell', '@VideoCommon@@'): '?OnGPUASRegister@?$TextureCache@UP@OpenGL@@@VideoCommon@@EEAAX_K@Z',
    ('gpu_maxwell', '@Shader@@'): '?GetGotoVariable@IREmitter@IR@Shader@@QEAAXXZ',
    ('gpu_maxwell', '@Nvidia@Service@@'): '?QueryEvent@nvhost_ctrl_gpu@Devices@Nvidia@Service@@UEAAXXZ',
    ('emulator_system', '@System@Core@@'): '?Load@System@Core@@QEAA?AW4SystemResultStatus@2@XZ',
}

# One independent DLL name per production import pattern: (component, pattern) -> DLL.
IMPORT_ORACLE = {
    ('jit', 'dynarmic'): 'dynarmic.dll',
    ('hid', 'hid_core'): 'hid_core.dll',
    ('hid', 'input_common'): 'input_common.dll',
    ('audio', 'audio_core'): 'audio_core.dll',
    ('gpu_maxwell', 'video_core'): 'video_core.dll',
    ('gpu_maxwell', 'shader_recompiler'): 'shader_recompiler.dll',
}
HANG_LIMIT_SECONDS = 30
STDERR_FLOOD_BYTES = 2 << 20


def build_pe(imports, pe32_plus=True, codeview=None, directory_count=16):
    """A minimal PE with one .idata section; imports maps DLL name -> names (str) or ordinals (int)."""
    thunk = 8 if pe32_plus else 4
    optional_size = 240 if pe32_plus else 224
    body = bytearray()
    descriptors_size = (len(imports) + 1) * 20
    body += bytes(descriptors_size)
    descriptor_fields = []
    for dll, entries in imports.items():
        table_at = len(body)
        body += bytes((len(entries) + 1) * thunk)
        name_at = len(body)
        body += dll.encode() + b'\0'
        for index, entry in enumerate(entries):
            if isinstance(entry, int):
                value = entry | (1 << (63 if pe32_plus else 31))
            else:
                if len(body) % 2:
                    body += b'\0'
                value = SECTION_RVA + len(body)
                body += b'\0\0' + entry.encode() + b'\0'
            body[table_at + index * thunk:table_at + (index + 1) * thunk] = value.to_bytes(thunk, 'little')
        descriptor_fields.append((SECTION_RVA + table_at, SECTION_RVA + name_at))
    for index, (lookup, name) in enumerate(descriptor_fields):
        body[index * 20:index * 20 + 20] = struct.pack('<IIIII', lookup, 0, 0, name, lookup)
    debug_size = 0
    debug_at = 0
    if codeview:
        guid_text, age = codeview
        parts = guid_text.split('-')
        record = (b'RSDS' + struct.pack('<IHH', int(parts[0], 16), int(parts[1], 16), int(parts[2], 16)) +
                  bytes.fromhex(parts[3] + parts[4]) + struct.pack('<I', age) + b'x.pdb\0')
        record_at = len(body)
        body += record
        debug_at = len(body)
        debug_size = 28
        body += struct.pack('<IIHHIIII', 0, 0, 0, 0, 2, len(record), SECTION_RVA + record_at,
                            SECTION_FILE_OFFSET + record_at)
    optional = bytearray(optional_size)
    struct.pack_into('<H', optional, 0, 0x20B if pe32_plus else 0x10B)
    count_at = 108 if pe32_plus else 92
    struct.pack_into('<I', optional, count_at, directory_count)
    directory = count_at + 4
    struct.pack_into('<II', optional, directory + 8, SECTION_RVA, descriptors_size)
    struct.pack_into('<II', optional, directory + 48, SECTION_RVA + debug_at if debug_size else 0, debug_size)
    image = bytearray(0x40)
    image[0:2] = b'MZ'
    struct.pack_into('<I', image, 0x3C, 0x40)
    image += b'PE\0\0' + struct.pack('<HHIIIHH', 0x8664, 1, 0, 0, 0, optional_size, 0x22)
    image += optional
    section = b'.idata\0\0' + struct.pack('<IIIIIIHHI', len(body), SECTION_RVA, len(body),
                                         SECTION_FILE_OFFSET, 0, 0, 0, 0, 0x40000040)
    image += section
    image += bytes(SECTION_FILE_OFFSET - len(image))
    return bytes(image) + bytes(body)


SECTION_HEADER_AT = 0x40 + 24 + 240
DIRECTORY_AT = 0x40 + 24 + 112
IMPORT_ENTRY_AT = DIRECTORY_AT + 8


def run_gate(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = gate.main([str(a) for a in argv])
        except SystemExit as exit_request:
            code = exit_request.code
    return code, out.getvalue(), err.getvalue()


class GateTestCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory(prefix='structural_gate_test_')
        self.addCleanup(self.dir.cleanup)
        self.path = Path(self.dir.name)

    def write(self, name, data):
        target = self.path / name
        target.write_bytes(data) if isinstance(data, bytes) else target.write_text(data, encoding='utf-8')
        return target

    def symbols(self, *extra):
        return self.write('symbols.txt', '\n'.join([f'filler_{n}' for n in range(FILLER_SYMBOLS)] + list(extra)))


class ImportParsing(GateTestCase):
    def test_reads_names_and_ordinals_of_both_pe_kinds(self):
        imports = {'KERNEL32.dll': ['CreateFileW', 'ExitProcess'], 'user32.dll': [7, 'MessageBoxW']}
        for pe32_plus in (True, False):
            image = self.write('a.exe', build_pe(imports, pe32_plus))
            self.assertEqual(gate.parse_imports(image),
                             {'kernel32.dll': ['CreateFileW', 'ExitProcess'], 'user32.dll': ['#7', 'MessageBoxW']})

    def test_an_image_without_an_import_directory_has_no_imports(self):
        data = bytearray(build_pe({'kernel32.dll': ['A']}))
        struct.pack_into('<II', data, IMPORT_ENTRY_AT, 0, 0)
        self.assertEqual(gate.parse_imports(self.write('none.exe', bytes(data))), {})

    def reject(self, data, pattern):
        with self.assertRaisesRegex(gate.GateError, pattern):
            gate.parse_imports(self.write('bad.exe', bytes(data)))

    def test_rejects_corrupt_headers(self):
        good = build_pe({'kernel32.dll': ['A']})
        self.reject(b'XX' + good[2:], 'no MZ')
        self.reject(good[:0x40] + b'XXXX' + good[0x44:], 'no PE signature')
        self.reject(good[:0x100], 'truncated')
        self.reject(good[:0x58] + b'\x01\x00' + good[0x5A:], 'magic')
        self.reject(build_pe({'kernel32.dll': ['A']}, directory_count=200), 'do not fit')

    def test_rejects_a_malformed_import_directory(self):
        base = build_pe({'kernel32.dll': ['A']})
        data = bytearray(base)
        struct.pack_into('<I', data, IMPORT_ENTRY_AT + 4, 0)
        self.reject(data, 'malformed import directory')
        data = bytearray(base)
        struct.pack_into('<II', data, IMPORT_ENTRY_AT, 0, 40)
        self.reject(data, 'malformed import directory')
        # A descriptor whose name is zero but whose other fields are not.
        data = bytearray(base)
        struct.pack_into('<I', data, SECTION_FILE_OFFSET + 12, 0)
        self.reject(data, 'no DLL name')
        # The directory is only one descriptor long, so no terminator fits.
        data = bytearray(base)
        struct.pack_into('<II', data, IMPORT_ENTRY_AT, SECTION_RVA, 20)
        self.reject(data, 'no terminating descriptor')

    def test_rejects_data_outside_the_file_backed_section(self):
        data = bytearray(build_pe({'kernel32.dll': ['A']}))
        struct.pack_into('<I', data, SECTION_FILE_OFFSET + 12, 0x900000)
        self.reject(data, 'outside the file data')
        data = bytearray(build_pe({'kernel32.dll': ['A']}))
        struct.pack_into('<I', data, SECTION_HEADER_AT + 16, 1)
        self.reject(data, 'outside the file data')

    def test_rejects_an_unterminated_name(self):
        data = bytearray(build_pe({'k' * 5: ['A']}))
        data[-1:] = b'x'
        data += b'y' * (gate.MAX_IMPORT_NAME_LENGTH + 8)
        struct.pack_into('<II', data, SECTION_HEADER_AT + 8, len(data) - SECTION_FILE_OFFSET, SECTION_RVA)
        struct.pack_into('<I', data, SECTION_HEADER_AT + 16, len(data) - SECTION_FILE_OFFSET)
        self.reject(data, 'unterminated or over-long')


class CodeView(GateTestCase):
    def test_reads_the_guid_and_age(self):
        image = self.write('a.exe', build_pe({'kernel32.dll': ['A']}, codeview=(GUID, AGE)))
        self.assertEqual(gate.read_codeview(image), (GUID, AGE))

    def test_an_image_without_debug_info_has_none(self):
        self.assertIsNone(gate.read_codeview(self.write('a.exe', build_pe({'kernel32.dll': ['A']}))))


class SymbolParsing(unittest.TestCase):
    def test_publics_dumps_count_only_symbol_records(self):
        lines = ['  9642528 | S_PUB32 [size = 32] `?Run@Jit@Dynarmic@@XZ`', '           flags = function, addr = 1:2',
                 '', '  7 | S_PUB32 [size = 40] `blk_sdk_1`']
        self.assertEqual(list(gate.publics_names(lines)), ['?Run@Jit@Dynarmic@@XZ', 'blk_sdk_1'])

    def test_plain_lists_count_every_non_empty_line(self):
        self.assertEqual(list(gate.plain_names(['a', '  b  ', '', ' '])), ['a', 'b'])


class DenyList(unittest.TestCase):
    def test_oracle_and_production_patterns_are_the_same_set(self):
        production = {(name, pattern) for name, entry in gate.DENY_LISTS.items() for pattern in entry['symbols']}
        self.assertEqual(set(ORACLE), production)

    def test_each_oracle_name_is_flagged_for_its_component_by_its_pattern(self):
        for (component, pattern), name in ORACLE.items():
            with self.subTest(component=component, pattern=pattern):
                self.assertIn(pattern, name)
                _, result = gate.evaluate([name], {})
                self.assertGreaterEqual(result[component]['symbol_matches'], 1)
                alone = {component: {'milestones': ('N1',), 'symbols': (pattern,), 'imports': ()}}
                self.assertEqual(gate.evaluate([name], {}, alone)[1][component]['symbol_matches'], 1)

    def test_import_oracle_and_production_patterns_are_the_same_set(self):
        production = {(name, pattern) for name, entry in gate.DENY_LISTS.items() for pattern in entry['imports']}
        self.assertEqual(set(IMPORT_ORACLE), production)

    def test_each_oracle_dll_is_flagged_for_its_component(self):
        for (component, pattern), dll in IMPORT_ORACLE.items():
            with self.subTest(component=component, pattern=pattern):
                _, result = gate.evaluate(['benign'], {dll: ['#17']})
                self.assertGreaterEqual(result[component]['import_matches'], 1)
                self.assertEqual(gate.failing_components(result, ['N9']).count(component), 1)

    def test_a_benign_symbol_matches_nothing(self):
        _, result = gate.evaluate(['memcpy', 'blk_sdk_0000000000008bb4', '?Run@Foo@Bar@@XZ'], {})
        self.assertEqual(sum(e['symbol_matches'] for e in result.values()), 0)

    def test_milestones_cover_the_roadmap(self):
        self.assertEqual(gate.milestones_of(gate.DENY_LISTS), ['N1', 'N2', 'N3', 'N4', 'N5', 'N6', 'N7', 'N8', 'N9'])

    def test_import_patterns_are_checked(self):
        custom = {'bad': {'milestones': ('N1',), 'symbols': (), 'imports': ('evil',)}}
        _, result = gate.evaluate(['x'], {'evil.dll': ['A'], 'ok.dll': ['evilFunction', 'B']}, custom)
        self.assertEqual(result['bad']['import_matches'], 2)
        self.assertEqual(gate.failing_components(result, ['N1']), ['bad'])

    def test_a_weakened_list_would_pass_where_the_real_one_fails(self):
        weakened = {k: dict(v, symbols=tuple(p for p in v['symbols'] if p != '@Dynarmic@@'))
                    for k, v in gate.DENY_LISTS.items()}
        names = [DYNARMIC_SYMBOL]
        _, real = gate.evaluate(names, {})
        _, weak = gate.evaluate(names, {}, weakened)
        self.assertEqual(gate.failing_components(real, ['N1']), ['jit'])
        self.assertEqual(gate.failing_components(weak, ['N1']), [])

    def test_only_required_milestones_fail(self):
        _, result = gate.evaluate(['?Run@KPageTableBase@Kernel@@XZ'], {})
        self.assertEqual(gate.failing_components(result, ['N1']), [])
        self.assertEqual(gate.failing_components(result, ['N2']), ['kernel_memory'])
        self.assertEqual(gate.failing_components(result, ['N9']), ['kernel_memory'])


class CommandLine(GateTestCase):
    def setUp(self):
        super().setUp()
        self.image = self.write('launcher.exe', build_pe({'kernel32.dll': ['ExitProcess']}, codeview=(GUID, AGE)))

    def gate_file(self, symbols, *options):
        return run_gate(self.image, '--symbols-file', symbols, '--accept-unverified-symbols', *options)

    def test_baseline_reports_and_never_fails(self):
        code, out, _ = run_gate(self.image, '--symbols-file', self.symbols(DYNARMIC_SYMBOL),
                                '--json-out', self.path / 'r.json')
        self.assertEqual(code, 0)
        self.assertIn('baseline', out)
        self.assertIn('"symbol_matches": 1', (self.path / 'r.json').read_text(encoding='utf-8'))

    def test_gate_fails_when_a_required_component_is_present(self):
        code, out, _ = self.gate_file(self.symbols(DYNARMIC_SYMBOL), '--require-absent', 'N1')
        self.assertEqual(code, 1)
        self.assertIn('RESULT: FAIL jit', out)

    def test_gate_passes_when_the_required_components_are_gone(self):
        code, out, _ = self.gate_file(self.symbols(DYNARMIC_SYMBOL), '--require-absent', 'N2,N3')
        self.assertEqual(code, 0)
        self.assertIn('RESULT: PASS (symbols NOT verified', out)
        self.assertEqual(self.gate_file(self.symbols(), '--require-absent', 'N9')[0], 0)

    def test_a_symbols_file_needs_the_unverified_flag_in_gate_mode(self):
        code, _, err = run_gate(self.image, '--symbols-file', self.symbols(), '--require-absent', 'N1')
        self.assertEqual(code, 2)
        self.assertIn('accept-unverified-symbols', err)

    def test_too_few_symbols_never_pass(self):
        short = self.write('short.txt', 'one\ntwo\n')
        code, _, err = self.gate_file(short, '--require-absent', 'N1')
        self.assertEqual(code, 2)
        self.assertIn('refusing', err)

    def test_the_minimum_cannot_be_turned_off(self):
        empty = self.write('empty.txt', '')
        for minimum in ('0', '-5'):
            with self.subTest(minimum):
                self.assertEqual(self.gate_file(empty, '--require-absent', 'N9', '--min-symbols', minimum)[0], 2)

    def test_publics_metadata_lines_do_not_count_as_symbols(self):
        lines = []
        for n in range(400):
            lines += [f'  {n} | S_PUB32 [size = 32] `sym_{n}`', '           flags = function, addr = 1:2']
        code, _, err = self.gate_file(self.write('dump.txt', '\n'.join(lines)), '--require-absent', 'N1')
        self.assertEqual(code, 2)
        self.assertIn('only 400 symbols', err)

    def fake_pdbutil(self, summary_guid=GUID, summary_age=AGE, publics_exit=0, dynarmic_symbol=True,
                     stderr_bytes=0):
        lines = [f'  1 | S_PUB32 [size = 32] `filler_{n}`' for n in range(FILLER_SYMBOLS)]
        if dynarmic_symbol:
            lines.append(f'  2 | S_PUB32 [size = 32] `{DYNARMIC_SYMBOL}`')
        flood = self.write('flood.txt', b'e' * stderr_bytes)
        listing = self.write('publics.txt', '\n'.join(lines))
        summary = self.write('summary.txt', f'  GUID: {{{summary_guid}}}\n  Age: {summary_age}\n')
        if os.name == 'nt':
            text = (f'@echo off\r\nif "%2"=="-summary" (type "{summary}" & exit /b 0)\r\n'
                    f'type "{flood}" 1>&2\r\ntype "{listing}"\r\nexit /b {publics_exit}\r\n')
            return self.write('fake_pdbutil.bat', text)
        script = self.write('fake_pdbutil.sh', f'#!/bin/sh\nif [ "$2" = "-summary" ]; then cat "{summary}"; exit 0; fi\n'
                                               f'cat "{flood}" 1>&2\ncat "{listing}"\nexit {publics_exit}\n')
        script.chmod(0o755)
        return script

    def run_gate_with_deadline(self, *argv):
        outcome = []
        worker = threading.Thread(target=lambda: outcome.append(run_gate(*argv)), daemon=True)
        worker.start()
        worker.join(HANG_LIMIT_SECONDS)
        self.assertFalse(worker.is_alive(), 'the gate did not finish: it hangs on a full stderr pipe')
        return outcome[0]

    def test_a_flood_of_diagnostics_on_stderr_does_not_hang_the_gate(self):
        pdb = self.write('launcher.pdb', b'pdb')
        tool = self.fake_pdbutil(publics_exit=3, stderr_bytes=STDERR_FLOOD_BYTES)
        code, _, err = self.run_gate_with_deadline(self.image, '--pdb', pdb, '--llvm-pdbutil', tool)
        self.assertEqual(code, 2)
        self.assertIn('llvm-pdbutil failed', err)

    def test_a_denied_dll_import_fails_a_verified_gate(self):
        image = self.write('dyn.exe', build_pe({'dynarmic.dll': [17]}, codeview=(GUID, AGE)))
        tool = self.fake_pdbutil(dynarmic_symbol=False)
        code, out, _ = run_gate(image, '--pdb', self.write('d.pdb', b'p'), '--llvm-pdbutil', tool,
                                '--require-absent', 'N1')
        self.assertEqual(code, 1)
        self.assertIn('import dynarmic.dll', out)
        code, out, _ = run_gate(self.image, '--pdb', self.write('d.pdb', b'p'), '--llvm-pdbutil', tool,
                                '--require-absent', 'N1')
        self.assertEqual((code, 'RESULT: PASS\n' in out), (0, True))

    def test_an_empty_milestone_list_is_an_error_not_a_baseline(self):
        symbols = self.symbols()
        for value in ('', ',', ' '):
            with self.subTest(repr(value)):
                code, _, err = self.gate_file(symbols, '--require-absent', value)
                self.assertEqual(code, 2)
                self.assertIn('names no milestone', err)

    def test_pdb_symbols_come_from_the_tool_and_a_verified_pass_says_so(self):
        pdb = self.write('launcher.pdb', b'pdb')
        code, out, _ = run_gate(self.image, '--pdb', pdb, '--llvm-pdbutil', self.fake_pdbutil(),
                                '--require-absent', 'N1')
        self.assertEqual(code, 1)
        self.assertIn('RESULT: FAIL jit', out)
        code, out, _ = run_gate(self.image, '--pdb', pdb, '--llvm-pdbutil', self.fake_pdbutil(), '--require-absent', 'N2')
        self.assertEqual((code, 'RESULT: PASS\n' in out), (0, True))

    def test_a_pdb_of_another_build_is_rejected(self):
        pdb = self.write('launcher.pdb', b'pdb')
        other_guid = run_gate(self.image, '--pdb', pdb, '--llvm-pdbutil',
                              self.fake_pdbutil(summary_guid='FFFFFFFF-0000-0000-0000-000000000000'))
        other_age = run_gate(self.image, '--pdb', pdb, '--llvm-pdbutil', self.fake_pdbutil(summary_age=AGE + 1))
        for code, _, err in (other_guid, other_age):
            self.assertEqual(code, 2)
            self.assertIn('does not belong', err)

    def test_an_image_without_codeview_cannot_use_a_pdb(self):
        bare = self.write('bare.exe', build_pe({'kernel32.dll': ['A']}))
        code, _, err = run_gate(bare, '--pdb', self.write('x.pdb', b'p'), '--llvm-pdbutil', self.fake_pdbutil())
        self.assertEqual(code, 2)
        self.assertIn('no CodeView', err)

    def test_a_failing_pdbutil_is_an_error(self):
        code, _, err = run_gate(self.image, '--pdb', self.write('x.pdb', b'p'), '--llvm-pdbutil',
                                self.fake_pdbutil(publics_exit=3))
        self.assertEqual(code, 2)
        self.assertIn('llvm-pdbutil failed', err)

    def test_input_errors_exit_with_two(self):
        symbols = self.symbols()
        cases = {
            'unknown milestone': [self.image, '--symbols-file', symbols, '--accept-unverified-symbols',
                                  '--require-absent', 'N42'],
            'no symbol source': [self.image],
            'two symbol sources': [self.image, '--symbols-file', symbols, '--pdb', symbols],
            'missing image': [self.path / 'none.exe', '--symbols-file', symbols],
            'not a pe': [self.write('x.exe', b'not a pe image'), '--symbols-file', symbols],
            'missing pdb': [self.image, '--pdb', self.path / 'none.pdb', '--llvm-pdbutil', self.fake_pdbutil()],
            'missing symbols file': [self.image, '--symbols-file', self.path / 'none.txt'],
            'missing pdbutil': [self.image, '--pdb', self.write('y.pdb', b'p'),
                                '--llvm-pdbutil', self.path / 'no_such_tool'],
        }
        for name, argv in cases.items():
            with self.subTest(name):
                self.assertEqual(run_gate(*argv)[0], 2)


if __name__ == '__main__':
    unittest.main()
