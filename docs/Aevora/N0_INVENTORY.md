# N0 Inventory

## 1. Confirmed supported path

The user-confirmed path is: boot to the title screen; load an existing save; move through Jubilife Village and the Obsidian Fieldlands; make one wild catch and one battle; open the main menu, Pokedex, and bag; save; quit. A new game from the start, cutscenes outside this path, and online features are out of scope.

| Step | Segment label | Observe by eye | Inventory evidence | Exercised in test1/test2 |
| --- | --- | --- | --- | --- |
| Boot to the title screen | title | Title screen appears. | RomFS reads and save-data opens (mode 1) first appear in this segment (test2: 52 distinct keys, all new). | Yes |
| Load an existing save | title | Existing save loads. | Save-data reads of the main and second save files appear in the title segment. | Yes (within title) |
| Move through Jubilife Village | village | Movement through the village completes. | One new key in test2; most keys were already seen at the title. | Yes |
| Move through the Obsidian Fieldlands | field | Movement through the field completes. | Save-data opens with mode 2 (writes, including a backup) first appear here in test2, 12 new keys. | Yes |
| Make one wild catch | catch | A wild catch completes. | No new key in test2. | Yes |
| Make one battle | battle | A battle completes. | No new key in test2. | Yes |
| Open the main menu | menu | The main menu opens. | Four new keys in test2. | Yes |
| Open the Pokedex | pokedex | The Pokedex opens. | No new key in test2. | Yes |
| Open the bag | (none) | The bag opens from the main menu. | Not given a segment in test1 or test2; whether it was opened inside the menu segment is not recorded. | No |
| Save | save | Save completes. | No new key in test2. | Yes |
| Quit | (none) | The application quits normally. | Not recorded as a segment; the open remainder of test2 is empty. | No |

The collection procedure is `run_n0.bat <game> <label>`; assign Player 1 in the overlay Controller window first, use F10 to start recording, F9 to end a labeled segment, and quit normally. Human observations and gameplay checks remain local.

### Memory rows

When `SUYU_BOUNDARY_INVENTORY=1`, each report also contains `[memory]` and cumulative `[memory_paused]` sections with `key`, `count`, minimum and maximum bytes, and first-seen order. `[memory_paused]` records boot and paused activity, repeats in every report, and must be merged by maximum count rather than sum; `[memory]` counts are summed across segments. Region matching tests narrow regions first and the spanning 39-bit code region last. Row keys include `svc:0xNN` memory-SVC shapes, `ipc_map <send|receive|exchange> attr=... state=... size=...`, `tls_page`, `shmem_create owner_perm=... user_perm=...`, and `process_layout flags=... pool=... system_pages=...` followed by the process ranges. The option is execution-neutral when disabled. Merged test4 evidence is local at `D:\HisuiWakeBuild\diag\n0\test4\memory_merged.txt`.

## 2. Inventory summary

The union of boot1, test1, and test2 contains 18 reports, 32 distinct SVCs, 44 service interfaces with 121 commands, 34 nvdrv rows across six devices, no audio renderer open, no applet, 7 fs-open rows, and fs-read on two directories: RomFS of the current process and save data only.

Test2 contains 202,179 IPC calls, 29 caller-recorded IPC keys, 24 distinct SVCs, and 61 distinct keys. Its `[callers]` totals equal the `[ipc]` totals for all 29 recorded keys. SVC 0x21 was called 202,276 times. All four-frame IPC chains end inside the SDK module; SVC callers reach the main module for part of the calls. IPC attribution therefore stops at the SDK for now.

The module scan found 563 NVN strings and 15,965 matching dynsym names. The main module contains 524 of the NVN strings; their number matches a table of NVN API function names, which is the expected attach point of N8a (not yet verified by reading the table). The scan completed; scan time was not measured. Scan output is local and is never committed.

## 3. Structural gate

The structural gate uses one shared deny table for all milestones. It checks PE imports and PDB public symbols matched to the image's CodeView GUID and age. Baseline mode reports counts; `--require-absent N1[,N2...]` fails on any match. A symbols text file requires explicit opt-in and is reported as not verified. Delay-load imports are not read, and Linux was not run.

The first baseline report on the current launcher matched 1,007,632 public symbols and 37 imported DLLs. Symbol matches were: jit 4,073; kernel_memory 120; scheduler 170; service_framework 4,423; filesystem 1,839; hid 2,722; audio 1,241; gpu_maxwell 6,015; emulator_system 88. Import-pattern matches were zero for every component. `--require-absent N1` failed with `FAIL jit`, as expected for the JIT-capable launcher.

The gate test command is `python -B -I -X utf8 tests/structural_gate/test_structural_gate.py`; it has 34 tests with an independent oracle, and all 56 tool mutants fail the suite. The gate command is `python -B -I -X utf8 tools/structural_gate/structural_gate.py <image.exe> --pdb <image.pdb> [--require-absent N1] [--json-out F] [--text-out F]`.
