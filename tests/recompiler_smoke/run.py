#!/usr/bin/env python3
"""Compile and execute synthetic ABI 5, ABI 6, ABI 6 + GG1 and ABI 6 + FMI tests, and the
coverage loop; requires only Python, CMake, C/C++."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

# SHA-256 of the whole generated smoke tree (every file, by relative path) with
# the fast-path emit option off. That output must stay byte-identical to ABI 5;
# update this only for a deliberate ABI 5 emitter change, never for ABI 6 work.
# Aevora v0.0.1-dev8: all three recomputed after the v0.0.1-dev4 change to
# the generated CMake job-pool text (shared by every variant). smoke_export is
# now built with /utf-8 on MSVC, as suyu is, so these match clang-cl builds.
ABI5_GOLDEN = "a325a2d05566f9d2efaeead9a855d963cf7e143efef14a0095f505ff92d090e7"
# The same for ABI 6 with FM1 alone (GG1 and FPX1 both off), which neither
# feature may move, and with FM1 and FPX1 (exact native FP) on, tracked so any
# change is deliberate. Recomputed against this integration tree, since the FP
# exactness fixes touch the shared emitter and so this text too; GG1's own
# switch-off contract (DESIGN.md sec 0) guarantees it does not move these.
# FPX_GOLDEN also covers the FZ-aware FPX1 helpers (v0.0.1-dev8).
FM1_GOLDEN = "e2785f0c4e839b23186a2865ef6b241b4b8565524ee71c3bea4b9154b0daefad"
FPX_GOLDEN = "1ef792bc6fb4b7a666c455f71b24e74a08b9170fb66b2d77c1a385134d1c35a5"

# ABI 6 changes only these files; the block sources must be identical.
ABI6_CHANGED = {"CMakeLists.txt", "recomp_export.c", "recomp_runtime.c", "recomp_runtime.h"}
# FPX1 on top of FM1 changes only these: the smoke code has no FP instructions.
FPX_CHANGED = {"CMakeLists.txt", "recomp_export.c", "recomp_runtime.h"}
# GG1 (the generation code guard) changes the block units as well, never the
# dispatch table (src/recompiled_<module>.c) or the file set.
GG1_CHANGED = ABI6_CHANGED | {"recompiled_smoke_0.c", "recompiled_second_0.c"}
# FMI (the FM1 fast path inline at access sites) changes only the runtime pair:
# the header gains the inline helpers and renames, recomp_runtime.c a define.
FMI_CHANGED = {"recomp_runtime.c", "recomp_runtime.h"}
# Direct B/BL chaining changes only the block units.
DIRECT_CHAIN_CHANGED = {"recompiled_smoke_0.c", "recompiled_second_0.c"}
# Shared exact FP bodies (FPE) add the prototypes to the header and the definitions to
# recomp_runtime.c; the synthetic module has no FP site, so its block units stay put.
FPE_CHANGED = {"recomp_runtime.c", "recomp_runtime.h"}
# Vector FPX1 (FPXV) adds one helper to the header; the synthetic module has no vector FADD/FSUB site.
FPXV_CHANGED = {"recomp_runtime.h"}

# name -> SUYU_RECOMP_AB_* switches. abi6gg and ggfpx are GG1's on-top-of-FM1
# and GG1+FPX1-together combinations; neither is golden-hashed (GG1 is not
# switch-off text, so DESIGN.md checks it structurally via check_guard_gen).
VARIANTS = (("abi5", {}),
            ("abi6", {"SUYU_RECOMP_AB_FASTMEM": "1"}),
            ("abi6gg", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_GUARD_GEN": "1"}),
            ("fpx", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_FPX": "1"}),
            ("ggfpx", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_GUARD_GEN": "1",
                       "SUYU_RECOMP_AB_FPX": "1"}),
            ("abi6fmi", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_FM_INLINE": "1"}),
            # Shared exact FP bodies need FPX1; structural, not golden-hashed.
            ("fpxs", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_FPX": "1",
                      "SUYU_RECOMP_AB_FP_SHARED": "1"}),
            # Asked for without FPX1, the option must change nothing.
            ("abi6fps", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_FP_SHARED": "1"}),
            # Vector FPX1 needs FPX1 too; structural, not golden-hashed.
            ("fpxv", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_FPX": "1",
                      "SUYU_RECOMP_AB_FPX_VEC": "1"}),
            ("abi6fv", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_FPX_VEC": "1"}),
            ("ggfmi", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_GUARD_GEN": "1",
                       "SUYU_RECOMP_AB_FM_INLINE": "1"}),
            # Direct B/BL chaining on top of FM1, and of FM1 with GG1: it changes the block
            # units only, and the whole run battery has to hold with it (the "slice" and
            # "hybrid-direct-chain" modes exercise the chained self-loop).
            ("abi6dc", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_DIRECT_CHAIN": "1"}),
            ("ggdc", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_GUARD_GEN": "1",
                      "SUYU_RECOMP_AB_DIRECT_CHAIN": "1"}),
            # Indirect (RET/BR/BLR) chaining alone, and with direct chaining and GG1.
            ("abi6ic", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_INDIRECT_CHAIN": "1"}),
            ("ggdcic", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_GUARD_GEN": "1",
                        "SUYU_RECOMP_AB_DIRECT_CHAIN": "1",
                        "SUYU_RECOMP_AB_INDIRECT_CHAIN": "1"}),
            # Split-export identity on top of ggfpx; structural only, not golden-hashed.
            ("identity", {"SUYU_RECOMP_AB_FASTMEM": "1", "SUYU_RECOMP_AB_GUARD_GEN": "1",
                          "SUYU_RECOMP_AB_FPX": "1", "SUYU_RECOMP_AB_IDENTITY": "1"}))
AB_SWITCHES = ("SUYU_RECOMP_AB_FASTMEM", "SUYU_RECOMP_AB_GUARD_GEN", "SUYU_RECOMP_AB_FPX",
               "SUYU_RECOMP_AB_FM_INLINE", "SUYU_RECOMP_AB_IDENTITY",
               "SUYU_RECOMP_AB_DIRECT_CHAIN", "SUYU_RECOMP_AB_INDIRECT_CHAIN",
               "SUYU_RECOMP_AB_FP_SHARED", "SUYU_RECOMP_AB_FPX_VEC")
# The identity export adds one export to each module's recomp_export.c and nothing else.
IDENTITY_CHANGED = {"recomp_export.c"}
# Shared-image names of the "second" module, per the generator's naming rule
# (recomp_topology.h SharedImageTargetName): Windows DLL or Unix shared object.
IDENTITY_IMAGE_NAMES = ("recompiled_second.dll", "librecompiled_second.so",
                        "librecompiled_second.dylib")
# The bundle id (a SHA-256 digest as hex) the identity test images are built with.
IDENTITY_BUNDLE_HEX = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
# A parent directory with non-ASCII characters, a space and brackets, for loading a
# shared image by a path the narrow ANSI code page cannot represent.
UNICODE_IMAGE_DIR_NAME = "\u30c6\u30b9\u30c8 [dll] dir"

# smoke_gg_host modes: exit 0 (protocol, activation, controls, races) or 86
# (a hook that must end skipping, followed by a changed block).
GG_HOST_PASS = ("skip", "mutate-no-bump", "log-alias", "log-trim", "unstable", "unmapped-code",
                "untracked", "overflow", "disabled", "ctl-ivau-elsewhere", "ctl-map-elsewhere",
                "pointer-prelog", "ctl-writes-elsewhere", "race-bump")
GG_HOST_ABORT = ("hook-map", "hook-unmap", "hook-protect-rx", "hook-protect-rw", "hook-alias",
                 "hook-device", "hook-ivau", "hook-ivau-all", "hook-new-table",
                 "hook-new-process", "hook-rebase", "guest-store", "guest-store-slow",
                 "guest-store-cross", "host-write", "pointer-exposed", "jit-fallback")


def call(args, expected=0, timeout=120, env=None):
    result = subprocess.run([str(arg) for arg in args], check=False, timeout=timeout, env=env)
    if result.returncode != expected:
        raise RuntimeError(f"exit {result.returncode}, expected {expected}: {args}")


def executable(build, name):
    for candidate in (build / name, build / (name + ".exe"), build / "Release" / (name + ".exe")):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"missing executable {name} under {build}")


def tree_hash(root):
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        data = path.read_bytes()
        digest.update(path.relative_to(root).as_posix().encode() + b"\0" +
                      str(len(data)).encode() + b"\0" + data)
    return digest.hexdigest()


def check_differences(base, other, changed, what):
    files_base = sorted(p.relative_to(base) for p in base.rglob("*") if p.is_file())
    files_other = sorted(p.relative_to(other) for p in other.rglob("*") if p.is_file())
    if files_base != files_other:
        raise RuntimeError(f"{what} export produced a different file set")
    for rel in files_base:
        same = (base / rel).read_bytes() == (other / rel).read_bytes()
        if same == (rel.name in changed):
            raise RuntimeError(f"unexpected {what} difference state for {rel.as_posix()}")


def check_guard_gen(abi6, gg):
    check_differences(abi6, gg, GG1_CHANGED, "GG1")
    for module in (gg, gg / "second"):
        header = (module / "recomp_runtime.h").read_text()
        export = (module / "recomp_export.c").read_text()
        if "#define RECOMP_IMAGE_ABI 6\n" not in header or "RECOMP_FEATURE_GUARD_GEN1" not in header:
            raise RuntimeError(f"{module} is not ABI 6 with GG1")
        if "recomp_image_guard_gen_v1" not in export or "recomp_image_fastmem_v1" not in export:
            raise RuntimeError(f"{module}: GG1 handshake presence is wrong")
        units = list((module / "src").glob("*_0.c"))
        if not units or "recomp_code_guard(" in units[0].read_text():
            raise RuntimeError(f"{module}: blocks still call the per-entry guard directly")


def check_outputs(abi5, abi6, fpx):
    for root, golden, what in ((abi5, ABI5_GOLDEN, "ABI 5"), (abi6, FM1_GOLDEN, "ABI 6 FM1"),
                               (fpx, FPX_GOLDEN, "ABI 6 FM1+FPX1")):
        actual = tree_hash(root)
        if actual != golden:
            raise RuntimeError(f"{what} output changed: {actual} != {golden}")
    check_differences(abi5, abi6, ABI6_CHANGED, "ABI 6")
    check_differences(abi6, fpx, FPX_CHANGED, "FPX1")
    for root, abi, fastmem, fpx_on in ((abi5, 5, False, False), (abi6, 6, True, False),
                                       (fpx, 6, True, True)):
        for module in (root, root / "second"):
            header = (module / "recomp_runtime.h").read_text()
            export = (module / "recomp_export.c").read_text()
            if f"#define RECOMP_IMAGE_ABI {abi}\n" not in header:
                raise RuntimeError(f"{module} is not ABI {abi}")
            if ("recomp_image_fastmem_v1" in export) != fastmem:
                raise RuntimeError(f"{module}: fastmem handshake presence is wrong")
            if ("recomp_image_fpx_v1" in export) != fpx_on:
                raise RuntimeError(f"{module}: FPX1 handshake presence is wrong")


def check_ggfpx(abi6gg, ggfpx):
    # FM1+GG1+FPX1 together: both features negotiated on top of the same FM1
    # base, neither one disabling the other. Structural only, like GG1 alone.
    # GG1 is already on in both trees, so only FPX1's own delta should show,
    # exactly as it does going from plain FM1 to FM1+FPX1 (FPX_CHANGED).
    check_differences(abi6gg, ggfpx, FPX_CHANGED, "GG1+FPX1")
    for module in (ggfpx, ggfpx / "second"):
        header = (module / "recomp_runtime.h").read_text()
        export = (module / "recomp_export.c").read_text()
        if ("RECOMP_FEATURE_GUARD_GEN1" not in header) or ("RECOMP_FEATURE_FPX1" not in header):
            raise RuntimeError(f"{module} is missing GG1 or FPX1 in the runtime header")
        if "recomp_image_guard_gen_v1" not in export or "recomp_image_fpx_v1" not in export:
            raise RuntimeError(f"{module}: GG1+FPX1 handshake presence is wrong")


def check_identity(base, identity):
    # The identity export touches only recomp_export.c, in every module, and the
    # image is shared-only (the block is inside RECOMP_SHARED_MODULE).
    check_differences(base, identity, IDENTITY_CHANGED, "image identity")
    for module in (identity, identity / "second"):
        export = (module / "recomp_export.c").read_text(encoding="utf-8")
        if "recomp_image_identity_v1" not in export:
            raise RuntimeError(f"{module}: the identity export is missing")
    for module in (base, base / "second"):
        export = (module / "recomp_export.c").read_text(encoding="utf-8")
        if "recomp_image_identity_v1" in export:
            raise RuntimeError(f"{module}: identity text present with the option off")


def bundle_byte_list(topology_unit, bundle_hex):
    """The bundle id as a C byte list, from the topology header's own conversion."""
    return subprocess.run([str(topology_unit), "--bundle-bytes", bundle_hex], check=True,
                          capture_output=True, text=True, timeout=15).stdout.strip()


def check_identity_image(host, image, bundle_hex):
    call([host, image, "second", bundle_hex], timeout=15)
    # A name or bundle id the image was not built with must be refused.
    call([host, image, "other", bundle_hex], expected=1, timeout=15)
    call([host, image, "second", "0" * 64], expected=1, timeout=15)
    call([host, image, "second", "ff" + bundle_hex[2:]], expected=1, timeout=15)


def check_identity_in_unicode_dir(root, host, image, bundle_hex):
    # The image path is passed as an argument list entry (no shell), through the
    # host's wide entry point, to LoadLibraryW.
    if os.name != "nt":
        print("Unicode image path check skipped: shared images are a Windows-only feature.")
        return
    copy_dir = root / UNICODE_IMAGE_DIR_NAME
    copy_dir.mkdir(parents=True, exist_ok=True)
    copy = copy_dir / image.name
    shutil.copy2(image, copy)
    check_identity_image(host, copy, bundle_hex)


def run_identity_host(root, run):
    host = executable(run, "identity_host")
    images = [p for name in IDENTITY_IMAGE_NAMES for p in (run / "shared").rglob(name)]
    if len(images) != 1:
        raise RuntimeError(f"expected one shared image of module 'second' under {run}: {images}")
    check_identity_image(host, images[0], IDENTITY_BUNDLE_HEX)
    check_identity_in_unicode_dir(root / "unicode-run", host, images[0], IDENTITY_BUNDLE_HEX)


# The registry/bundle checks: smoke_topology_unit --write-registry-v1 writes the registry the
# mixed-bundle host (bundle_host) loads. Module "smoke" is static, "second" is the DLL.
# case -> items the refusal message must name. Cases that need the DLL loaded are
# Windows-only; the rest refuse before any image is loaded and run everywhere.
BUNDLE_REFUSALS = (
    ("header-mismatch", ("entry_size",), False),
    ("header-counts", ("entry_count",), False),
    ("dll-file-escapes", ("second", "plain file name"), False),
    ("static-null-pointer", ("smoke", "lookup"), False),
    ("abi-mismatch", ("second", "ABI"), False),
    ("abi-mismatch-image", ("smoke", "image ABI"), False),
    ("feature-mismatch", ("second", "differ from module", "smoke"), False),
    ("feature-mismatch-fm1", ("second", "lack FM1"), False),
    ("feature-mismatch-gg1", ("second", "differ from module", "smoke"), False),
    ("static-null-features", ("smoke", "features pointer"), False),
    ("static-null-fastmem", ("smoke", "fastmem_v1 pointer"), False),
    ("static-null-guard-gen", ("smoke", "guard_gen_v1 pointer"), False),
    ("static-null-fpx", ("smoke", "fpx_v1 pointer"), False),
    # The host's own handshake values disagree with every image; the first module checked,
    # the static "smoke", refuses before the DLL is consulted.
    ("host-fastmem-layout", ("smoke", "fastmem (FM1) layout"), False),
    ("host-fpx-layout", ("smoke", "FPX1 handshake"), False),
    ("host-gg1-version", ("smoke", "generation guard (GG1) handshake"), False),
    ("feature-mismatch-image", ("smoke", "features"), False),
    ("inventory-empty", ("empty",), False),
    ("inventory-missing-middle", ("main", "missing"), False),
    ("inventory-extra", ("subsdk0", "does not contain"), False),
    ("inventory-duplicate-name", ("main", "more than once"), False),
    ("dll-missing", ("second", "recompiled_second.dll"), True),
    ("identity-bundle-mismatch", ("second", "bundle"), True),
    ("identity-name-mismatch", ("other", "second"), True),
    ("guard-disagreement", ("second", "guard"), True),
)


def bundle_registry_arguments(identity_tree):
    """ABI, feature bits and handshake switches, read from the generated runtime header."""
    header = (identity_tree / "second" / "recomp_runtime.h").read_text(encoding="utf-8")
    abi_line = next(line for line in header.splitlines()
                    if line.startswith("#define RECOMP_IMAGE_ABI "))
    fastmem = "RECOMP_FEATURE_FASTMEM_PT1" in header
    guard_gen = "RECOMP_FEATURE_GUARD_GEN1" in header
    fpx = "RECOMP_FEATURE_FPX1" in header
    # recomp_image_features.h: bit 0 FM1, bit 1 GG1, bit 2 FPX1.
    features = (1 if fastmem else 0) | (2 if guard_gen else 0) | (4 if fpx else 0)
    return [abi_line.split()[2], features, int(fastmem), int(guard_gen), int(fpx)]


def write_bundle_registry(root, topology_unit, identity_tree):
    """recomp_registration.c (registry v1) from the production writer."""
    registry = root / "bundle-registration.c"
    call([topology_unit, "--write-registry-v1", registry, IDENTITY_BUNDLE_HEX,
          *bundle_registry_arguments(identity_tree), "smoke", "second", "--dll", "second"],
         timeout=15)
    return registry


def bundle_case(host, case, directory, expected_exit, items, env=None, cwd=None):
    result = subprocess.run([str(host), case, str(directory)], check=False, timeout=60, env=env,
                            cwd=cwd,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    output = result.stdout + result.stderr
    missing = [item for item in items if item not in output]
    if result.returncode != expected_exit or missing:
        raise RuntimeError(f"bundle case {case}: exit {result.returncode}, expected "
                           f"{expected_exit}; missing {missing} in:\n{output[-1500:]}")


def bundle_stub_cases(host, root, run, on_windows):
    """A file of the DLL's name that is not a generated image: refused, naming the module."""
    garbage_dir = root / "bundle-garbage-dll"
    garbage_dir.mkdir(parents=True, exist_ok=True)
    (garbage_dir / IDENTITY_IMAGE_NAMES[0]).write_bytes(b"not a PE image\0" * 64)
    bundle_case(host, "dll-invalid", garbage_dir, 1, ("second", "cannot load DLL"))
    stub = find_one(run / "stub", IDENTITY_IMAGE_NAMES)
    bundle_case(host, "dll-missing-export", stub.parent, 1,
                ("second", "does not export recomp_image_lookup"))
    return 2


# The Win32 prefix that lifts MAX_PATH for creating the long directory in this test.
EXTENDED_LENGTH_PREFIX = "\\\\?\\"


def bundle_path_cases(host, root, image):
    """The image directory as the process was given it: relative, from another current
    directory, with spaces and brackets, and beyond MAX_PATH (loads, or says why not)."""
    other_cwd = root / "bundle-other-cwd"
    other_cwd.mkdir(parents=True, exist_ok=True)
    relative = os.path.relpath(image.parent, other_cwd)
    result = subprocess.run([str(host), "ok", relative], check=False, timeout=60, cwd=other_cwd,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0 or "bundle ok" not in result.stdout:
        raise RuntimeError(f"bundle ok from another current directory with a relative image "
                           f"directory '{relative}': exit {result.returncode}:\n"
                           f"{(result.stdout + result.stderr)[-800:]}")
    bundle_case(host, "ok", image.parent, 0, ("bundle ok",), cwd=other_cwd)
    deep = root / "bundle-long-path"
    for _ in range(0, 6):
        deep = deep / ("long path segment [dll] " + "x" * 40)
    # A path above MAX_PATH is created through the extended-length prefix.
    os.makedirs(EXTENDED_LENGTH_PREFIX + str(deep), exist_ok=True)
    shutil.copy2(image, EXTENDED_LENGTH_PREFIX + str(deep / image.name))
    result = subprocess.run([str(host), "ok", str(deep)], check=False, timeout=60,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    output = result.stdout + result.stderr
    if result.returncode == 0 and "bundle ok" in output:
        outcome = "loaded"
    elif result.returncode == 1 and "second" in output and "cannot load DLL" in output:
        outcome = "refused with a diagnostic"
    else:
        raise RuntimeError(f"bundle ok beyond MAX_PATH ({len(str(deep))} chars): exit "
                           f"{result.returncode}:\n{output[-800:]}")
    print(f"Bundle path beyond MAX_PATH ({len(str(deep))} chars): {outcome}.")
    return 3


def run_bundle_host(root, run, image):
    host = executable(run, "bundle_host")
    on_windows = os.name == "nt"
    image_dir = image.parent
    empty_dir = root / "bundle-empty-dir"
    empty_dir.mkdir(parents=True, exist_ok=True)
    cases = 0
    for case in ("inventory-ok",):
        bundle_case(host, case, empty_dir, 0, ())
        cases += 1
    # One static module with a stub guard: the loader needs no image for it.
    bundle_case(host, "guard-uniform-not-negotiated", empty_dir, 0, ("not negotiated",))
    cases += 1
    if on_windows:
        bundle_case(host, "ok", image_dir, 0, ("bundle ok",))
        # Generated blocks of the static and the DLL image through one host bridge.
        bundle_case(host, "exec", image_dir, 0, ("exec ok",))
        cases += 1
        copy_dir = root / "unicode-bundle" / UNICODE_IMAGE_DIR_NAME
        copy_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(image, copy_dir / image.name)
        bundle_case(host, "dll-in-unicode-dir", copy_dir, 0, ("bundle ok",))
        cases += 2
        cases += bundle_stub_cases(host, root, run, on_windows)
        cases += bundle_path_cases(host, root, image)
    else:
        print("Bundle DLL checks skipped: shared images are a Windows-only feature.")
        bundle_case(host, "ok", image_dir, 1, ("not supported on this platform", "second"))
        cases += 1
    for case, items, needs_dll in BUNDLE_REFUSALS:
        if needs_dll and not on_windows:
            continue
        # dll-missing points at an empty directory; every other case sees the image.
        bundle_case(host, case, empty_dir if case == "dll-missing" else image_dir, 1, items)
        cases += 1
    print(f"Bundle checks passed: {cases} cases (registry v1 loader, bindings, refusals).")


# The three-module split fixture (export.cpp --xmod): static "smoke", the DLL "second" between
# them, static "third". One registry, written by the production writer, names all three.
XMOD_MODULES = ("smoke", "second", "third")
XMOD_DLL_MODULE = "second"
XMOD_GUARD_ABORT_EXIT = 86  # xmod_exec.c: the guard stopped the probe block before its effect


def check_xmod_guard(host, image, image_dir):
    """The probe block of one image after a one-bit code change: the guard must name it."""
    result = subprocess.run([str(host), f"guard-{image}", str(image_dir)], check=False, timeout=60,
                            env=dict(os.environ, SUYU_RECOMP_STRICT="1"), capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    match = re.search(r"probe pc (\w+) expected (\w+) read (\w+)", result.stdout)
    if not match:
        raise RuntimeError(f"xmod guard-{image}: no probe line in:\n{result.stdout[-800:]}")
    pc, expected, read = match.groups()
    message = f"unsupported code change or unavailable code at 0x{pc} (expected {expected}, read {read})"
    if result.returncode != XMOD_GUARD_ABORT_EXIT or message not in result.stderr:
        raise RuntimeError(f"xmod guard-{image}: exit {result.returncode}, expected "
                           f"{XMOD_GUARD_ABORT_EXIT} with '{message}' in:\n{result.stderr[-800:]}")


def check_bridge_required_list(source, tree):
    """The host's required-callback list is exactly the callbacks the generated runtime falls
    back from: a callback the runtime guards with `host_mem && ...->name` but the list lacks
    would run on a private per-image state without the host noticing."""
    header = (tree / "recomp_runtime.h").read_text(encoding="utf-8")
    runtime = (tree / "recomp_runtime.c").read_text(encoding="utf-8")
    list_text = (source / "src/core/arm/recomp/recomp_bridge_required.h").read_text(encoding="utf-8")
    listed = set(re.findall(r"X\(\s*(\w+)\s*\)", list_text.split("#define RECOMP_CHECK", 1)[0]))
    struct = re.search(r"typedef\s+struct\s+RecompHostMem\s*\{(.*?)\}\s*RecompHostMem\s*;",
                       header, re.DOTALL)
    if not struct:
        raise RuntimeError("the generated recomp_runtime.h has no RecompHostMem definition")
    function_pointers = set(re.findall(r"\(\s*\*\s*(\w+)\s*\)\s*\(", struct.group(1)))
    guarded = set(re.findall(r"host_mem\s*&&\s*c->host_mem->(\w+)", runtime)) & function_pointers
    # An empty set would mean the patterns stopped matching, not that nothing is required.
    if not listed or not guarded or listed != guarded:
        raise RuntimeError(f"required bridge callbacks {sorted(listed)} differ from the callbacks "
                           f"the generated runtime guards {sorted(guarded)}")


def run_xmod(root, exporter, build, topology_unit, switches, source):
    tree = root / "generated-xmod"
    tree.mkdir()
    env = dict(os.environ)
    for key in AB_SWITCHES:
        env.pop(key, None)
    env.update(switches)
    call([exporter, "--xmod", tree], env=env)
    check_bridge_required_list(source, tree)
    registry = root / "xmod-registration.c"
    call([topology_unit, "--write-registry-v1", registry, IDENTITY_BUNDLE_HEX,
          *bundle_registry_arguments(tree), *XMOD_MODULES, "--dll", XMOD_DLL_MODULE], timeout=15)
    run = build("run-xmod", f"-DXMOD_DIR={tree.as_posix()}",
                f"-DIDENTITY_BUNDLE_BYTES={bundle_byte_list(topology_unit, IDENTITY_BUNDLE_HEX)}",
                f"-DREGISTRY_C={registry.as_posix()}")
    host = executable(run, "xmod_host")
    image_dir = find_one(run / "shared", IDENTITY_IMAGE_NAMES).parent
    cases = 0
    if os.name == "nt":
        bundle_case(host, "ok", image_dir, 0, ("bundle ok",))
        # Exclusive reservation, CLREX, pairs, counter and FADD across the three images.
        bundle_case(host, "exec", image_dir, 0, ("exec ok",))
        cases += 2
        # A changed code word must stop the probe block in each image, the DLL included.
        for image in range(len(XMOD_MODULES)):
            check_xmod_guard(host, image, image_dir)
            cases += 1
    else:
        print("Three-module DLL checks skipped: shared images are a Windows-only feature.")
        bundle_case(host, "ok", image_dir, 1, ("not supported on this platform", "second"))
        cases += 1
    print(f"Three-module split checks passed: {cases} cases (static/DLL/static).")


def prebuild_cmake(cmake, source, build_dir, exefs, modules, compiler_options):
    """Configure src/suyu_cmd/recomp_modules; returns (exit code, output)."""
    result = subprocess.run(
        [cmake, "-S", str(source / "src" / "suyu_cmd" / "recomp_modules"), "-B", str(build_dir),
         "-DCMAKE_BUILD_TYPE=Release", *compiler_options,
         f"-DSUYU_CMD_RECOMP_DIR={exefs.as_posix()}", f"-DSUYU_RECOMP_MODULES={';'.join(modules)}"],
        check=False, timeout=300, capture_output=True, text=True, encoding="utf-8",
        errors="replace")
    return result.returncode, result.stdout + result.stderr


def find_one(root, names):
    found = [p for name in names for p in root.rglob(name)]
    if len(found) != 1:
        raise RuntimeError(f"expected exactly one of {names} under {root}, found {found}")
    return found[0]


def check_pe_size_of_image(topology_unit, image):
    """The exporter's PE reader on a real linked image: SizeOfImage is plausible."""
    image_limit_bytes = 2 * 1024 ** 3
    text = subprocess.run([str(topology_unit), "--pe-size-of-image", str(image)], check=True,
                          capture_output=True, text=True, timeout=15).stdout.strip()
    size_of_image = int(text)
    # SizeOfImage is the section-aligned in-memory size; it is not compared with the
    # file length (a file may carry debug data past the sections, or be smaller).
    if not 0 < size_of_image < image_limit_bytes:
        raise RuntimeError(f"{image}: SizeOfImage {size_of_image} is not in (0, 2 GiB)")


def check_prebuild(root, cmake, source, identity_tree, topology_unit, identity_run_dir,
                   compiler_options):
    """The clang-cl prebuild project against a split topology written by the production writer."""
    bundle = IDENTITY_BUNDLE_HEX
    exefs = root / "prebuild-exefs"
    shutil.copytree(identity_tree, exefs / "smoke", ignore=shutil.ignore_patterns("second"))
    shutil.copytree(identity_tree / "second", exefs / "second")
    (exefs / "recomp_registration.c").write_text("/* empty registry for the prebuild test */\n")
    modules_cmake = exefs / "recomp_modules.cmake"

    def write_modules(text_args):
        call([topology_unit, "--write-modules-cmake", modules_cmake, *text_args])

    def expect_failure(name, modules, edit, item):
        write_modules((bundle, "smoke", "second", "--dll", "second"))
        text = modules_cmake.read_text()
        edited = edit(text)
        if edited == text:
            raise RuntimeError(f"prebuild negative case {name}: the edit changed nothing")
        modules_cmake.write_text(edited)
        code, output = prebuild_cmake(cmake, source, root / f"prebuild-{name}", exefs, modules,
                                      compiler_options)
        if code == 0 or item not in output:
            raise RuntimeError(f"prebuild negative case {name}: exit {code}, expected a failure "
                               f"naming '{item}'\n{output[-2000:]}")

    on_windows = os.name == "nt"
    if on_windows:
        # Each broken binding must fail the configure and name the broken item.
        expect_failure("missing-target", ("smoke", "second"),
                       lambda t: t.replace("set(SUYU_RECOMP_DLL_TARGET_second recompiled_second)\n",
                                           ""),
                       "SUYU_RECOMP_DLL_TARGET_second")
        expect_failure("version", ("smoke", "second"),
                       lambda t: t.replace("TOPOLOGY_VERSION 1", "TOPOLOGY_VERSION 2"),
                       "topology version '2'")
        expect_failure("dll-not-built", ("smoke",), lambda t: t + "\n", "DLL module 'second'")
        # Only the published file name differs from what the target produces.
        expect_failure("file-name", ("smoke", "second"),
                       lambda t: t.replace("set(SUYU_RECOMP_DLL_FILE_second recompiled_second.dll)",
                                           "set(SUYU_RECOMP_DLL_FILE_second other.dll)"),
                       "'other.dll'")
        expect_failure("missing-bytes", ("smoke", "second"),
                       lambda t: "".join(line for line in t.splitlines(keepends=True)
                                         if "BUNDLE_ID_BYTES" not in line),
                       "SUYU_RECOMP_BUNDLE_ID_BYTES")
        write_modules((bundle, "smoke", "second", "--dll", "second"))
    else:
        # Shared images, and so the DLL binding, are a Windows-only feature
        # (their file names are .dll): only the all-static topology is checked.
        print("Prebuild split (DLL) checks skipped: shared images are a Windows-only feature.")
        write_modules(("-", "smoke", "second"))
    build_dir = root / "prebuild"
    code, output = prebuild_cmake(cmake, source, build_dir, exefs, ("smoke", "second"),
                                  compiler_options)
    if code != 0:
        raise RuntimeError(f"prebuild configure failed:\n{output[-3000:]}")
    targets = ["recomp_static_smoke"] + (["recompiled_second"] if on_windows else [])
    for target in targets:
        call([cmake, "--build", build_dir, "--config", "Release", "--target", target,
              "--parallel", "2"], timeout=300)
    find_one(build_dir / "lib", ("recomp_static_smoke.lib", "librecomp_static_smoke.a"))
    if on_windows:
        image = find_one(build_dir / "bin", ("recompiled_second.dll",))
        call([executable(identity_run_dir, "identity_host"), image, "second", bundle], timeout=15)
        check_pe_size_of_image(topology_unit, image)
        check_identity_in_unicode_dir(root / "unicode-prebuild", executable(identity_run_dir,
                                      "identity_host"), image, bundle)
        # The static module must not have been built as a shared image.
        if list(build_dir.rglob("recompiled_smoke.dll")):
            raise RuntimeError("the static module was built as a shared image")
    if on_windows:
        # Every module DLL-bound (main-only exports): the prebuild project builds the image
        # and no static runtime library, which the launcher configure must not require.
        write_modules((bundle, "second", "--dll", "second"))
        all_dll_dir = root / "prebuild-all-dll"
        code, output = prebuild_cmake(cmake, source, all_dll_dir, exefs, ("second",),
                                      compiler_options)
        if code != 0:
            raise RuntimeError(f"all-DLL prebuild configure failed:\n{output[-3000:]}")
        call([cmake, "--build", all_dll_dir, "--config", "Release", "--target",
              "recompiled_second", "--parallel", "2"], timeout=300)
        find_one(all_dll_dir / "bin", ("recompiled_second.dll",))
        if list((all_dll_dir / "lib").glob("*recomp_runtime_shared*")) if (all_dll_dir / "lib").is_dir() else []:
            raise RuntimeError("the all-DLL prebuild produced a static runtime library; the "
                               "launcher's configure only skips it when none exists")
    # Hybrid retry: the exporter rewrites recomp_modules.cmake with a reduced
    # list after configuring with the full one. The next build regenerates the
    # project against the rewritten file and must still succeed.
    write_modules(("-", "smoke"))
    call([cmake, "--build", build_dir, "--config", "Release", "--target", "recomp_static_smoke",
          "--parallel", "2"], timeout=300)
    print("Prebuild consumer checks passed.")


def check_fm_inline(base, fmi, what):
    # FMI must leave the block units, CMake text and handshakes untouched.
    check_differences(base, fmi, FMI_CHANGED, what)
    for module in (fmi, fmi / "second"):
        header = (module / "recomp_runtime.h").read_text(encoding="utf-8")
        runtime = (module / "recomp_runtime.c").read_text(encoding="utf-8")
        if "#define recomp_load64 recomp_fmi_load64" not in header:
            raise RuntimeError(f"{module}: FMI renames are missing")
        if "\n#define RECOMP_RUNTIME_C 1\n" not in runtime.split('#include "recomp_runtime.h"')[0]:
            raise RuntimeError(f"{module}: recomp_runtime.c does not keep its helper names")


def check_fp_shared(fpx, shared, without_fpx, abi6):
    # FPE adds one definition and one prototype per op and width, and nothing else.
    check_differences(fpx, shared, FPE_CHANGED, "FPE")
    names = [f"recomp_fpe_{op}{width}" for op in ("add", "sub", "div", "fma", "recps", "rsqrts", "sqrt")
             for width in (32, 64)]
    for module, plain in ((shared, fpx), (shared / "second", fpx / "second")):
        header = (module / "recomp_runtime.h").read_text(encoding="utf-8")
        runtime = (module / "recomp_runtime.c").read_text(encoding="utf-8")
        plain_text = (plain / "recomp_runtime.h").read_text(encoding="utf-8") + \
            (plain / "recomp_runtime.c").read_text(encoding="utf-8")
        for name in names:
            if f"uint64_t {name}(GuestContext* c," not in header:
                raise RuntimeError(f"{module}: no prototype for {name}")
            if f"uint64_t {name}(GuestContext* c," not in runtime or f"{name}(" in plain_text:
                raise RuntimeError(f"{module}: {name} is not defined exactly with FPE")
    # Without FPX1 the exact body is the main path: FPE is requested but nothing changes.
    for module in (without_fpx, without_fpx / "second"):
        base = abi6 / module.relative_to(without_fpx)
        check_differences(base, module, set(), "FPE without FPX1")


def check_fpx_vec(fpx, vec, without_fpx, abi6):
    # FPXV adds the all-lane helper to the header and nothing else.
    check_differences(fpx, vec, FPXV_CHANGED, "FPXV")
    for module, plain in ((vec, fpx), (vec / "second", fpx / "second")):
        header = (module / "recomp_runtime.h").read_text(encoding="utf-8")
        plain_header = (plain / "recomp_runtime.h").read_text(encoding="utf-8")
        if "recomp_fpx_vaddsub32" not in header or "recomp_fpx_vaddsub32" in plain_header:
            raise RuntimeError(f"{module}: the FPXV helper is not present exactly with FPXV")
    # Without FPX1 the option is requested but nothing changes.
    for module in (without_fpx, without_fpx / "second"):
        check_differences(abi6 / module.relative_to(without_fpx), module, set(), "FPXV without FPX1")


def check_direct_chain(base, chained, what, direct=True, indirect=False):
    # Chaining changes the block units only. Direct chaining takes the unconditional
    # B/BL whose target is a block of the same module; indirect chaining takes RET,
    # BR and BLR through the module's block index. Conditional branches never chain.
    check_differences(base, chained, DIRECT_CHAIN_CHANGED, what)
    for module, plain_module, name in ((chained, base, "smoke"),
                                        (chained / "second", base / "second", "second")):
        unit = (module / "src" / f"recompiled_{name}_0.c").read_text(encoding="utf-8")
        target = f"blk_{name}_0000000000001000"
        chain = (f"c->pc=g_module_base+0x1000ULL; if (--c->chain_budget <= 0) return; "
                 f"RECOMP_TAIL_CALL({target}) }}")
        if (chain in unit) != direct:
            raise RuntimeError(f"{module}: the 1000 self-branch is "
                               f"{'not ' if direct else ''}a direct chain")
        chained_calls = unit.count("RECOMP_TAIL_CALL(blk_")
        if (chained_calls != 0) != direct or chained_calls > 1:
            raise RuntimeError(f"{module}: a branch other than the in-module B/BL was chained")
        # The smoke code holds one RET and one BLR and no BR.
        if unit.count("RECOMP_TAIL_CALL(_f)") != (2 if indirect else 0):
            raise RuntimeError(f"{module}: RET/BLR indirect chaining is "
                               f"{'missing' if indirect else 'present'}")
        if "/* RET */" not in unit or "/* BLR */" not in unit:
            raise RuntimeError(f"{module}: the RET/BLR markers are missing")
        if "#define RECOMP_TAIL_CALL(f)" not in unit:
            raise RuntimeError(f"{module}: RECOMP_TAIL_CALL is not defined")
        plain = (plain_module / "src" / f"recompiled_{name}_0.c").read_text(encoding="utf-8")
        if "RECOMP_TAIL_CALL" in plain:
            raise RuntimeError(f"{base}: chaining leaked into the switched-off output")


def check_direct_chain_bl(root, exporter, build):
    """Chained BL, RET, BR and BLR, executed: all four on/off combinations (the controls)."""
    env = dict(os.environ)
    for key in AB_SWITCHES:
        env.pop(key, None)
    for name, direct, indirect in (("bl-plain", False, False), ("bl-direct", True, False),
                                   ("bl-indirect", False, True), ("bl-both", True, True)):
        out = root / name
        switches = {}
        if direct:
            switches["SUYU_RECOMP_AB_DIRECT_CHAIN"] = "1"
        if indirect:
            switches["SUYU_RECOMP_AB_INDIRECT_CHAIN"] = "1"
        call([exporter, "--bl", out], env=dict(env, **switches))
        unit = (out / "src" / "recompiled_bl_0.c").read_text(encoding="utf-8")
        if ("RECOMP_TAIL_CALL(blk_bl_000000000000100c)" in unit) != direct:
            raise RuntimeError(f"{name}: the BL is not {'chained' if direct else 'plain'} in the text")
        if ("RECOMP_TAIL_CALL(_f)" in unit) != indirect:
            raise RuntimeError(f"{name}: RET/BR/BLR are not "
                               f"{'chained' if indirect else 'plain'} in the text")
        run = build(f"run-{name}", f"-DBL_DIR={out.as_posix()}",
                    f"-DBL_EXPECT_CHAIN={int(direct)}", f"-DBL_EXPECT_INDIRECT={int(indirect)}")
        call([executable(run, "smoke_bl")], timeout=15)
    print("Chain execution checks passed.")


def check_coverage_loop(root, exporter, build):
    """recomp_gaps.json end to end: record a miss, re-export with it, run statically."""
    env = dict(os.environ)
    for key in AB_SWITCHES:
        env.pop(key, None)
    gaps = root / "loop-gaps.json"

    def export(name, gaps_file=None):
        out = root / name
        call([exporter, "--loop", out] + ([gaps_file] if gaps_file else []), env=env)
        return out

    first = export("loop-1")
    call([executable(build("run-loop-1", f"-DLOOP_DIR={first.as_posix()}"), "smoke_loop"), "record", gaps])
    # No usable gaps must leave the output byte-identical: an empty file, and
    # gaps recorded against another build of the module.
    empty = root / "loop-empty.json"
    empty.write_text('{"schema": "suyu-recomp-gaps", "schema_version": 1, "runs": 3, '
                     '"clean_runs": 3}\n')
    other = root / "loop-other.json"
    other.write_text(gaps.read_text().replace("5eedc0de", "5eedc0df"))
    for name, gaps_file in (("loop-empty", empty), ("loop-other", other)):
        if tree_hash(export(name, gaps_file)) != tree_hash(first):
            raise RuntimeError(f"{name}: output changed without a matching gap")
    second = export("loop-2", gaps)
    if tree_hash(second) == tree_hash(first):
        raise RuntimeError("the recorded gap did not reach block discovery")
    call([executable(build("run-loop-2", f"-DLOOP_DIR={second.as_posix()}"), "smoke_loop"), "static", gaps])
    print("Coverage loop checks passed.")


def find_nlohmann_include(explicit):
    """Directory holding nlohmann/json.hpp, for the build-identity parser test.

    The headers come from the host build's CPM cache (CPM_SOURCE_CACHE), so there is no fixed
    path; --nlohmann-include or NLOHMANN_INCLUDE_DIR names it directly.
    """
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("NLOHMANN_INCLUDE_DIR"):
        candidates.append(Path(os.environ["NLOHMANN_INCLUDE_DIR"]))
    cache = os.environ.get("CPM_SOURCE_CACHE")
    if cache:
        candidates.extend(sorted(Path(cache).glob("nlohmann_json/*/include")))
    for candidate in candidates:
        if (candidate / "nlohmann" / "json.hpp").is_file():
            return candidate
    raise SystemExit("smoke: nlohmann/json.hpp not found for smoke_build_identity_unit; pass "
                     "--nlohmann-include or set NLOHMANN_INCLUDE_DIR (or CPM_SOURCE_CACHE)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--cc", help="explicit C compiler path, if needed")
    parser.add_argument("--cxx", help="explicit C++ compiler path, if needed")
    parser.add_argument("--nlohmann-include", type=Path,
                        help="directory containing nlohmann/json.hpp (default: search the CPM cache)")
    parser.add_argument("--only-xmod", action="store_true",
                        help="run only the three-module split fixture (static/DLL/static)")
    args = parser.parse_args()
    nlohmann_include = find_nlohmann_include(args.nlohmann_include)
    source = args.source.resolve()
    test_source = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="suyu-recompiler-smoke-") as tmp:
        root = Path(tmp)
        compiler_options = []
        for key, value in (("C", args.cc), ("CXX", args.cxx)):
            if value:
                compiler_options.append(f"-DCMAKE_{key}_COMPILER={Path(value).as_posix()}")

        def build(name, *options):
            directory = root / name
            call([args.cmake, "-S", test_source, "-B", directory,
                  f"-DSUYU_SOURCE={source.as_posix()}", "-DCMAKE_BUILD_TYPE=Release", *compiler_options,
                  *options])
            call([args.cmake, "--build", directory, "--config", "Release", "--parallel", "2"])
            return directory

        # Which generator the script uses is cmake's default unless -G is passed in
        # the environment; record it so a result can be tied to it.
        cache_generator = None
        export_build = build("export", f"-DNLOHMANN_INCLUDE_DIR={nlohmann_include.as_posix()}")
        for line in (export_build / "CMakeCache.txt").read_text(encoding="utf-8",
                                                                errors="replace").splitlines():
            if line.startswith("CMAKE_GENERATOR:"):
                cache_generator = line.split("=", 1)[1]
        print(f"CMake generator: {cache_generator}")
        exporter = executable(export_build, "smoke_export")
        if args.only_xmod:
            identity_switches = dict(VARIANTS)["identity"]
            run_xmod(root, exporter, build, executable(export_build, "smoke_topology_unit"),
                     identity_switches, source)
            return
        call([executable(export_build, "smoke_gaps_unit")])
        call([executable(export_build, "smoke_env_option_unit")])
        call([executable(export_build, "smoke_thread_time_unit")])
        call([executable(export_build, "smoke_overlay_logic_unit")])
        call([executable(export_build, "smoke_build_identity_unit")])
        call([executable(export_build, "smoke_topology_unit")])
        call([executable(export_build, "smoke_aot_cache_unit"), root / "aot-cache-unit"])
        check_coverage_loop(root, exporter, build)
        check_direct_chain_bl(root, exporter, build)
        generated = {}
        for name, switches in VARIANTS:
            generated[name] = root / f"generated-{name}"
            generated[name].mkdir()
            env = dict(os.environ)
            for key in AB_SWITCHES:
                env.pop(key, None)
            env.update(switches)
            call([exporter, generated[name]], env=env)
        check_outputs(generated["abi5"], generated["abi6"], generated["fpx"])
        check_guard_gen(generated["abi6"], generated["abi6gg"])
        check_ggfpx(generated["abi6gg"], generated["ggfpx"])
        check_fm_inline(generated["abi6"], generated["abi6fmi"], "FMI")
        check_fp_shared(generated["fpx"], generated["fpxs"], generated["abi6fps"], generated["abi6"])
        check_fpx_vec(generated["fpx"], generated["fpxv"], generated["abi6fv"], generated["abi6"])
        check_fm_inline(generated["abi6gg"], generated["ggfmi"], "GG1+FMI")
        check_direct_chain(generated["abi6"], generated["abi6dc"], "direct chain")
        check_direct_chain(generated["abi6gg"], generated["ggdc"], "GG1+direct chain")
        check_direct_chain(generated["abi6"], generated["abi6ic"], "indirect chain",
                           direct=False, indirect=True)
        check_direct_chain(generated["abi6gg"], generated["ggdcic"], "GG1+direct+indirect chain",
                           direct=True, indirect=True)
        check_identity(generated["ggfpx"], generated["identity"])

        for name, switches in VARIANTS:
            guard_gen = "SUYU_RECOMP_AB_GUARD_GEN" in switches
            identity_options = []
            if ("SUYU_RECOMP_AB_DIRECT_CHAIN" in switches or
                    "SUYU_RECOMP_AB_INDIRECT_CHAIN" in switches):
                identity_options.append("-DSMOKE_DIRECT_CHAIN=1")
            if name == "identity":
                topology_unit = executable(export_build, "smoke_topology_unit")
                registry_c = write_bundle_registry(root, topology_unit, generated[name])
                identity_options = [
                    f"-DIDENTITY_BUNDLE_HEX={IDENTITY_BUNDLE_HEX}",
                    f"-DIDENTITY_BUNDLE_BYTES={bundle_byte_list(topology_unit, IDENTITY_BUNDLE_HEX)}",
                    f"-DREGISTRY_C={registry_c.as_posix()}"]
            run = build(f"run-{name}", f"-DGENERATED_DIR={generated[name].as_posix()}",
                        *identity_options)
            runner = executable(run, "smoke_run")
            hybrid_env = dict(os.environ, SUYU_RECOMP_STRICT="0")
            for mode in ("hybrid-mutated-entry", "hybrid-unmapped-entry",
                         "hybrid-middle-entry", "hybrid-direct-chain"):
                call([runner, mode], timeout=15, env=hybrid_env)
            for mode in ("slice", "ordinary-page", "cross-page", "special-page",
                         "mem-ordinary", "mem-unmapped", "mem-special", "mem-cross",
                         "mem-unaligned", "mem-limit", "mem-protected"):
                call([runner, mode], timeout=15)
            for mode in ("mutated", "mutated-entry", "unmapped-zero",
                         "cross-page-mutated", "cross-page-unmapped"):
                call([runner, mode], expected=86, timeout=15,
                     env=dict(os.environ, SUYU_RECOMP_STRICT="1"))
            call([executable(run, "smoke_static")], timeout=15)
            if name != "abi5":
                call([executable(run, "smoke_features")], timeout=15)
            if name == "identity":
                run_identity_host(root, run)
                run_bundle_host(root, run, find_one(run / "shared", IDENTITY_IMAGE_NAMES))
                run_xmod(root, exporter, build, executable(export_build, "smoke_topology_unit"),
                         switches, source)
                check_prebuild(root, args.cmake, source, generated[name],
                               executable(export_build, "smoke_topology_unit"), run,
                               compiler_options)
            if guard_gen:
                host = executable(run, "smoke_gg_host")
                for mode in GG_HOST_PASS:
                    call([host, mode], timeout=120)
                for mode in GG_HOST_ABORT:
                    call([host, mode], expected=86, timeout=15,
                         env=dict(os.environ, SUYU_RECOMP_STRICT="1"))
                # The cross-thread catch, at a spread of mutation times.
                for delay in range(0, 2000, 40):
                    call([host, f"race-mutate-{delay}"], expected=86, timeout=15,
                         env=dict(os.environ, SUYU_RECOMP_STRICT="1"))
            print(f"All synthetic {name.upper()} smoke checks passed.")


if __name__ == "__main__":
    main()
