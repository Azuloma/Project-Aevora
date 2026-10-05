#!/usr/bin/env python3
"""Build and run the FM1 differential test (ABI 6 fast path vs. slow path vs. ABI 5,
and the inline FMI fast path), with and without the GG1 runtime."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def call(args, timeout=1800):
    result = subprocess.run([str(arg) for arg in args], check=False, timeout=timeout)
    if result.returncode != 0:
        raise RuntimeError(f"exit {result.returncode}: {args}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator", help="CMake generator, e.g. Ninja")
    parser.add_argument("--cc", help="explicit C compiler path, if needed")
    parser.add_argument("--cxx", help="explicit C++ compiler path, if needed")
    parser.add_argument("--sanitize", action="store_true", help="ASan + UBSan (GCC/Clang)")
    parser.add_argument("--operations", type=int, default=20_000_000)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    test_source = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="suyu-fmdiff-") as tmp:
        build = Path(tmp) / "build"
        options = [f"-DSUYU_SOURCE={args.source.resolve()}", "-DCMAKE_BUILD_TYPE=Release",
                   f"-DFMDIFF_SANITIZE={'ON' if args.sanitize else 'OFF'}"]
        if args.generator:
            options += ["-G", args.generator]
        for key, value in (("C", args.cc), ("CXX", args.cxx)):
            if value:
                options.append(f"-DCMAKE_{key}_COMPILER={value}")
        call([args.cmake, "-S", test_source, "-B", build, *options])
        call([args.cmake, "--build", build, "--config", "Release", "--parallel", "2"])
        for name in ("fmdiff", "fmdiff_gg"):
            for exe in (build / name, build / f"{name}.exe", build / "Release" / f"{name}.exe"):
                if exe.is_file():
                    call([exe, args.operations, args.seed])
                    break
            else:
                raise RuntimeError(f"{name} was not built")


if __name__ == "__main__":
    main()
