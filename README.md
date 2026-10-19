# FIFA International Soccer (DOS) – AMD K6 start-up fix

A tiny patcher that lets **FIFA International Soccer** (EA Sports, DOS, 1993/94) start on **AMD K6** processors.

It changes **7 bytes** in the start-up loader of *your own* `FIFA.EXE`. It contains no game code, no game data and does not
ship the game. You need a legally obtained copy of the game.

```
Abnormal program termination: Memory protection fault
CS:EIP = 000Fh:0009C4E8h
```

That message right after typing `FIFA` (the address can differ, e.g. `000Fh:000A0697h`) is what this fixes.
Lowering the multiplier, disabling the L1/L2 caches or slowing the CPU down does **not** help, because the cause is not speed.
It is a CPU-dependent value in the game's start-up loader (details in [docs/TECHNICAL.md](docs/TECHNICAL.md)).

## Quick start

1. **Make a copy of your game folder** (the patcher also makes a backup, but be careful anyway).
2. Install [Python 3](https://www.python.org/downloads/) (3.8 or newer, Windows / macOS / Linux), then run:

   ```
   python fifa_k6_fix.py "path/to/FIFA.EXE"
   ```

   The original is kept as `FIFA.EXE.orig`.
3. Copy the patched game folder to your DOS machine and start the game as usual.

   **Keep the file name the game expects.** The start-up loader looks for its own EXE by a hard-coded name in the
   current directory (`fifa.exe` in the standard build), so a renamed copy will most likely not start. The patcher prints
   that name when you run it (also with `--check`).

Useful options:

| Command | Effect |
|---|---|
| `python fifa_k6_fix.py --check FIFA.EXE` | Only report whether the file needs the fix. Changes nothing. |
| `python fifa_k6_fix.py -o FIFA_K6.EXE FIFA.EXE` | Write a patched copy and leave the original untouched. |
| `python fifa_k6_fix.py --no-backup FIFA.EXE` | Patch in place without creating `FIFA.EXE.orig`. |

The tool refuses to touch anything it does not recognise: wrong file, ambiguous matches, or a file that was already
modified by something else. Running it twice is harmless ("Both patch sites already carry the fix").

### Patching directly on the DOS machine

`c/fifa_k6_fix.c` is the same tool in portable C89 (it behaves exactly like the Python version, which the test suite
checks). It needs a **32-bit** compiler because it reads the whole EXE (about 800 KB) into memory:

* Open Watcom: `wcl386 -bt=dos -l=dos4g fifa_k6_fix.c`
* DJGPP: `gcc -O2 -o fk6fix.exe fifa_k6_fix.c`
* any modern system: `cc -O2 -o fifa_k6_fix fifa_k6_fix.c`

On plain DOS the file names must fit 8.3, so rename the source to e.g. `FK6FIX.C` first. The DOS builds are not
tested by the author, only the Linux build is. The C version prints a CRC-32 instead of SHA-256.

### Patching by hand (hex editor)

Search for these two byte patterns in `FIFA.EXE` (`??` = any byte, each must occur exactly once):

| # | Search for | Change |
|---|---|---|
| A | `89 0D ?? ?? ?? 00 05 A5 A5 A5 A5 E8` | replace the first six bytes `89 0D ?? ?? ?? 00` with `90 90 90 90 90 90` |
| B | `0F A2 80 E4 0F 80 FC 04 74 10 8B 44 24 1C` | replace the `74` (9th byte) with `EB` |

For the build listed below this is offset `0x96F43` (A) and `0x96EAA` (B).

### IPS patch

`fifa_k6_fix_v1.ips` contains the same two edits at fixed offsets. **Use it only on the exact build listed below**,
an IPS patch cannot check what it is applied to. For everything else use the patcher.

## Known builds

The patcher recognises these builds (all three were checked with `--check`; the loader is the same code block in each,
the two patch sites are always `0x99` bytes apart):

| Build | Size | SHA-256 | Patch sites (file offset A / B) | Status |
|---|---|---|---|---|
| Standard DOS release (probably the floppy version) | 816,980 | `8f1ae8d4080729df06a8ec4d2fceee820f21b21325b7146f183cc50d06a48971` | `0x96F43` / `0x96EAA` | **tested** on real K6-III+ hardware and in DOSBox-X |
| CD-ROM release | 856,244 | `00956baf8ef9d1cffde9e5e0d85ddc3b9a2e2dff61711523321443cadf545d3c` | `0xA08AF` / `0xA0816` | sites found, not yet tested on hardware |
| Community-made "4 MB" executable for the CD-ROM release (its `FILE_ID.DIZ` says it lets the 8 MB version run on a 4 MB machine) | 835,860 | `30e405ba6e9195bdea6fcfb7316424b3ce15d1c2f405465c14134f8a5aaaf774` | `0x9B907` / `0x9B86E` | sites found, not yet tested on hardware |

For the standard build, CRC-32 is `2D5602DB` before and `4A08FA59` after patching (the C version prints CRC-32 instead of
SHA-256); SHA-256 after patching is `ecdecd544ac0d0ad4e677a6269f671980de1759c2b42745820b9b4bdeeff7395`.

The patcher finds its targets by byte pattern, not by fixed offset, so it also works on other builds of the game as long
as their loader is built the same way. If you have another version (other languages, other releases, ...), run `--check`
on it:

* **"file needs the fix"** or **"already carry the fix"**: it works, please report the size, SHA-256 and the result so the
  table can be extended. Reports from real hardware (does the patched file start?) are the most valuable ones.
* **"loader signature was not found"**: that build differs. Please report its size and SHA-256 (never upload the game
  itself).

The tool also prints the file name the loader looks for (see "Keep the file name" above); run `--check` once on your copy
to see it.

## Tested on

* Real hardware: AMD K6-III+ on an Asus P5A (Super Socket 7), DOS. Before: crash as above. After: the game starts and runs.
* DOSBox-X: the patched file runs.

Reports from other CPUs are welcome. The patch should also be harmless on Intel/Cyrix/other CPUs, because it makes the loader
use the same value as a 486 would on every CPU. A report in the VOGONS forum describes the same message on a Cyrix 486
upgrade; whether this patch fixes that has **not** been verified.

## What exactly does the patch change?

Short version: the loader computes a decryption key. One step depends on whether the CPU notices that an instruction was
overwritten right before it executes (486: no; Pentium: yes) and the loader compensates by asking the CPU family via CPUID.
The K6 reports "Pentium-class" but most likely does not behave like a Pentium in that one spot, so the compensation is
applied wrongly, the key is wrong and the decrypted code is garbage. (This explanation fits every observation made so far,
and the patch built on it works, but the K6 internals involved were not measured separately.) The patch removes the self-modifying store (site A) and
disables the compensation (site B), so every CPU gets the key a 486 gets. Full explanation with the instruction listing:
[docs/TECHNICAL.md](docs/TECHNICAL.md).

The patch does **not** remove or bypass any licence check, serial number, manual question or disc check, and it does not
change the game itself.

## Running the self-test

```
python tests/test_patcher.py
```

The test builds synthetic files that contain only the short patterns and needs no game file. If a C compiler is installed,
it also checks that the C version produces the same output as the Python version.

## Undo

Copy `FIFA.EXE.orig` back over `FIFA.EXE`.

## Disclaimer and licence

This is an independent compatibility tool. It is not affiliated with, endorsed by or sponsored by Electronic Arts, FIFA or
AMD; all names and trademarks belong to their respective owners. The tool contains no part of the game except the two
short byte patterns needed to locate the patch sites. You are responsible for using it only on a copy of the game you are
entitled to use. It comes without any warranty; keep a backup.

The tool and its documentation are released into the public domain under [The Unlicense](LICENSE).

## Changelog

* **1.1**: prints the file name the loader looks for; CD-ROM and "4 MB" builds added to the list of recognised builds.
* **1.0**: first release.
