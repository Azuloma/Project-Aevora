#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and run the exclusive-access test against a built host tree.

The test compiles with the include paths and defines CMake recorded for the dynarmic spin lock
source (compile_commands.json), so those dependencies have one source of truth, and links the
host tree's own dynarmic, common and fmt libraries. Run it from an MSVC x64 developer shell
(vcvars64) after building the `suyu` target of --build-dir.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

SPIN_LOCK_SOURCE = "common/spin_lock_x64.cpp"
LIBRARIES = (
    "src/dynarmic/src/dynarmic/dynarmic.lib",
    "src/common/common.lib",
    "_deps/fmt-build/fmt.lib",
)


def compiler_flags(build_dir: Path):
    entries = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    for entry in entries:
        if entry["file"].replace("\\", "/").endswith(SPIN_LOCK_SOURCE):
            # A quoted include path may contain spaces; an empty define is written X="".
            tokens = re.findall(r'-I"[^"]*"|-[DI]\S+', entry["command"])
            return [token.replace('=""', "=").replace('"', "") for token in tokens]
    raise RuntimeError(f"{SPIN_LOCK_SOURCE} is not in {build_dir / 'compile_commands.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="host build tree with compile_commands.json and built libraries")
    parser.add_argument("--work", type=Path, required=True,
                        help="output folder for objects and the test executable (outside the repo)")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    args.work.mkdir(parents=True, exist_ok=True)
    exe = args.work / "exclusive_access_test.exe"
    libraries = [args.build_dir / library for library in LIBRARIES]
    missing = [str(library) for library in libraries if not library.is_file()]
    if missing:
        raise RuntimeError(f"build the host tree first; missing: {missing}")

    command = ["cl", "/nologo", "/TP", "/std:c++20", "/O2", "/EHsc", "/MD",
               *compiler_flags(args.build_dir), str(here / "exclusive_access_test.cpp"),
               f"/Fo{args.work}\\", f"/Fe{exe}", "/link", *map(str, libraries),
               "ole32.lib", "shell32.lib"]
    print("building:", " ".join(command[:6]), "...")
    if subprocess.run(command, check=False).returncode != 0:
        return 1
    # The shared stub reads SUYU_SPINLOCK_TTAS once per process, so each setting is its own run;
    # only the exact value 1 enables it.
    for option, expectation in ((None, "default"), ("1", "ttas"), ("2", "default")):
        env = {key: value for key, value in os.environ.items() if key != "SUYU_SPINLOCK_TTAS"}
        if option is not None:
            env["SUYU_SPINLOCK_TTAS"] = option
        print(f"running: {exe} {expectation} (SUYU_SPINLOCK_TTAS={option})")
        if subprocess.run([str(exe), expectation], env=env, check=False).returncode != 0:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
