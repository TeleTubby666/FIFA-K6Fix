#!/usr/bin/env python3
"""
fifa_k6_fix.py - start-up loader fix for FIFA International Soccer (DOS)

Fixes the crash
    Abnormal program termination: Memory protection fault
    CS:EIP = 000Fh:0009C4E8h        (the address may differ)
that the game shows right after start on AMD K6 CPUs.

The tool contains NO game code or data. It only looks for two short byte
patterns in the loader at the end of your own FIFA.EXE and changes 7 bytes.
See README.md and docs/TECHNICAL.md for what exactly is changed and why.

Usage:
    python fifa_k6_fix.py FIFA.EXE              patch in place (backup: FIFA.EXE.orig)
    python fifa_k6_fix.py --check FIFA.EXE      only report, change nothing
    python fifa_k6_fix.py -o FIFA_K6.EXE FIFA.EXE   write a patched copy

Public domain (Unlicense). No warranty.
"""

import argparse
import hashlib
import os
import re
import sys
import tempfile

VERSION = "1.0"

ANY3 = rb"[\x00-\xff]{3}"
ANY4 = rb"[\x00-\xff]{4}"

# --------------------------------------------------------------------------
# Site A - self-modifying store directly in front of the instruction it modifies
#
#   89 0D <abs32>      mov  [abs32], ecx      <- overwrites the immediate of ...
#   05 A5 A5 A5 A5     add  eax, 0A5A5A5A5h   <- ... this instruction
#   E8 <rel32>         call cpu_check
#
# Patch: the six bytes of the store become NOPs.
# --------------------------------------------------------------------------
SITE_A_ORIG = re.compile(rb"\x89\x0d" + ANY3 + rb"\x00" + rb"\x05\xa5\xa5\xa5\xa5\xe8" + ANY4)
SITE_A_DONE = re.compile(rb"\x90{6}" + rb"\x05\xa5\xa5\xa5\xa5\xe8" + ANY4)
A_LEN = 6

# --------------------------------------------------------------------------
# Site B - CPU family test inside cpu_check
#
#   0F A2              cpuid
#   80 E4 0F           and  ah, 0Fh
#   80 FC 04           cmp  ah, 4
#   74 10              jz   done              <- becomes EB 10 (jmp short done)
#   8B 44 24 1C        mov  eax, [esp+1Ch]
#   29 44 24 20        sub  [esp+20h], eax
#   81 44 24 20 A5 A5 A5 A5   add dword [esp+20h], 0A5A5A5A5h
#   9D 61 C3           popf / popa / ret
# --------------------------------------------------------------------------
B_COMMON_HEAD = rb"\x0f\xa2\x80\xe4\x0f\x80\xfc\x04"
B_COMMON_TAIL = rb"\x10\x8b\x44\x24\x1c\x29\x44\x24\x20\x81\x44\x24\x20\xa5\xa5\xa5\xa5\x9d\x61\xc3"
SITE_B_ORIG = re.compile(B_COMMON_HEAD + rb"\x74" + B_COMMON_TAIL)
SITE_B_DONE = re.compile(B_COMMON_HEAD + rb"\xeb" + B_COMMON_TAIL)
B_JUMP_OFFSET = len(b"\x0f\xa2\x80\xe4\x0f\x80\xfc\x04")  # offset of the 74/EB byte

# Site A must follow site B closely (same loader blob).
MAX_DISTANCE = 0x400


def find_all(regex, data):
    # overlapping matches are irrelevant here; patterns cannot overlap themselves
    return [m.start() for m in regex.finditer(data)]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def analyse(data):
    """Return (state, a_off, b_off, message).

    state: 'unpatched', 'patched', 'mixed', 'unknown'
    """
    a_orig = find_all(SITE_A_ORIG, data)
    a_done = find_all(SITE_A_DONE, data)
    b_orig = find_all(SITE_B_ORIG, data)
    b_done = find_all(SITE_B_DONE, data)

    n_a = len(a_orig) + len(a_done)
    n_b = len(b_orig) + len(b_done)

    if n_a == 0 or n_b == 0:
        return ("unknown", None, None,
                "The loader signature was not found (site A: %d, site B: %d). "
                "This file is either not an affected build of the game, or it is a "
                "variant this tool does not know yet." % (n_a, n_b))
    if n_a != 1 or n_b != 1:
        return ("unknown", None, None,
                "Ambiguous: found %d candidate(s) for site A and %d for site B "
                "(exactly one of each is expected). Nothing was changed." % (n_a, n_b))

    a_off = (a_orig or a_done)[0]
    b_off = (b_orig or b_done)[0]

    if not (0 < a_off - b_off < MAX_DISTANCE):
        return ("unknown", None, None,
                "The two signatures were found, but not in the expected layout "
                "(site A at 0x%X, site B at 0x%X). Nothing was changed." % (a_off, b_off + B_JUMP_OFFSET))

    if a_orig and b_orig:
        return ("unpatched", a_off, b_off, "Both patch sites found, file is unpatched.")
    if a_done and b_done:
        return ("patched", a_off, b_off, "Both patch sites already carry the fix.")
    return ("mixed", a_off, b_off,
            "Only one of the two patch sites carries the fix (site A at file offset 0x%X, "
            "site B at 0x%X). The file was modified by something else." % (a_off, b_off + B_JUMP_OFFSET))


def apply_patch(data, a_off, b_off):
    out = bytearray(data)
    out[a_off:a_off + A_LEN] = b"\x90" * A_LEN
    out[b_off + B_JUMP_OFFSET] = 0xEB
    return bytes(out)


def write_atomic(path, payload):
    directory = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(prefix=".fifa_k6_fix_", dir=directory)
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(payload)
        try:
            mode = os.stat(path).st_mode
            os.chmod(tmp, mode)
        except OSError:
            pass
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Fix the AMD K6 start-up crash of FIFA International Soccer (DOS). "
                    "Changes 7 bytes in the loader of your own FIFA.EXE.")
    ap.add_argument("exe", help="path to FIFA.EXE (or the EXE of your version)")
    ap.add_argument("--check", action="store_true",
                    help="only report the state of the file, change nothing")
    ap.add_argument("-o", "--output", metavar="FILE",
                    help="write the patched file here instead of patching in place")
    ap.add_argument("--no-backup", action="store_true",
                    help="patch in place without creating FILE.orig (not recommended)")
    ap.add_argument("--version", action="version", version="fifa_k6_fix " + VERSION)
    args = ap.parse_args(argv)

    try:
        with open(args.exe, "rb") as fh:
            data = fh.read(64 * 1024 * 1024 + 1)
    except OSError as exc:
        print("error: cannot read %s: %s" % (args.exe, exc), file=sys.stderr)
        return 1
    if len(data) > 64 * 1024 * 1024:
        print("error: file is larger than 64 MB, this is not the game EXE.", file=sys.stderr)
        return 1

    print("File   : %s" % args.exe)
    print("Size   : %d bytes" % len(data))
    print("SHA-256: %s" % sha256(data))
    if data[:2] != b"MZ":
        print("warning: file does not start with 'MZ', it is not a DOS executable.")

    state, a_off, b_off, msg = analyse(data)
    print(msg)

    if state == "mixed":
        print("\nNothing was changed. Restore the original FIFA.EXE (for example from the "
              "FIFA.EXE.orig backup) and run this tool again.")
        return 1

    if state == "unknown":
        print("\nNothing was changed. If you are sure this is the game EXE, please report it "
              "and include the size and SHA-256 shown above (see README.md).")
        return 1

    if state == "patched":
        print("Nothing to do.")
        if args.output:
            write_atomic(args.output, data)
            print("Copied unchanged to %s" % args.output)
        return 0

    # state == 'unpatched'
    print("  site A (self-modifying store) at file offset 0x%X: %s"
          % (a_off, data[a_off:a_off + A_LEN].hex(" ").upper()))
    print("  site B (CPU family test)      at file offset 0x%X: %s"
          % (b_off + B_JUMP_OFFSET, data[b_off + B_JUMP_OFFSET:b_off + B_JUMP_OFFSET + 2].hex(" ").upper()))

    if args.check:
        print("\n--check: file needs the fix, nothing was changed.")
        return 0

    patched = apply_patch(data, a_off, b_off)

    # Self-check: re-analyse the result, and make sure exactly 7 bytes differ.
    diff = [i for i in range(len(data)) if data[i] != patched[i]]
    state2 = analyse(patched)[0]
    if state2 != "patched" or len(diff) != 7 or len(patched) != len(data):
        print("internal error: self-check failed, nothing was written.", file=sys.stderr)
        return 2

    target = args.output or args.exe
    if not args.output and not args.no_backup:
        backup = args.exe + ".orig"
        if os.path.exists(backup):
            print("error: backup file %s already exists. Move it away, use -o to write "
                  "a separate file, or --no-backup." % backup, file=sys.stderr)
            return 1
        write_atomic(backup, data)
        print("Backup : %s" % backup)

    write_atomic(target, patched)
    print("Patched: %s  (7 bytes changed)" % target)
    print("SHA-256: %s" % sha256(patched))
    return 0


if __name__ == "__main__":
    sys.exit(main())
