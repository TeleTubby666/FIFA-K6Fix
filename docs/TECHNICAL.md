# Technical notes

How the start-up crash on AMD K6 CPUs comes about and what the patch changes.
All of this was derived by disassembling the loader inside the EXE (no source code was available). Section 6 lists what
was verified and what was not.

Numbers refer to the build with SHA-256 `8f1ae8d4…06a48971` (816,980 bytes). In that build
**virtual address = file offset + 0x5450** for everything described here.

## 1. The EXE is a Phar Lap TNT program, not DOS/4GW

| File offset | Content |
|---|---|
| `0x0000` | DOS stub of Phar Lap's "bind" utility (`Copyright (C) 1986-1991 Phar Lap Software`, error texts `GOTNTB …`) |
| `0x1816` | Directory (`DIRH`) of two bound-in files: `fifa.exe` at `0x18A0` (`0x95851` bytes) and `TNTB.EXE` at `0x97100` (`0x30654` bytes) |
| `0x18A0` | `fifa.exe`: Phar Lap flat-model executable (`P3`), 0x180-byte header, run-time parameters, load image at `0x1AA0` (`0x95651` bytes) |
| `0x97100` | `TNTB.EXE`: the Phar Lap TNT DOS extender that runs the program |

The string `DOS/4G` that appears in the file belongs to the extender's list of "other extender present" messages. The game
does not use DOS/4GW.

The load image is high-entropy (encrypted and/or compressed, no readable strings). The only readable part is a small
**loader at the very end of the image** (virtual `0x9C270`–`0x9C541`, file `0x96E20`–`0x970F1`), which is also the
program's entry point (`0x9C310`).

## 2. What the loader does (as far as analysed)

1. Hooks INT 3 through the extender and triggers it, so execution continues in its own handler (presumably an anti-debugging
   construction: a debugger that owns INT 3 changes what happens next).
2. Looks for a file named `fifa.exe` in the current directory (DOS find-first) and reads it into a buffer. **The EXE must
   therefore keep that name.**
3. Derives a 32-bit **key** from values that are identical on every machine for a normal run (return address, selectors,
   saved flags, a few dwords of low memory, the current TF/IF flags). A debugger, breakpoint or modified loader changes
   the result.
4. Passes the key through `cpu_check` (section 3) and adds it to five dwords at `0x9C4E5…0x9C4F5`. Those 20 bytes are
   **encrypted loader code** and only become valid instructions with the correct key.
5. Continues into the decryptor, which uses the same key as a rolling XOR key (`xor [esi],al / inc esi / ror eax,1`), checks
   the loaded image against the file, and finally jumps into the real program.

With a wrong key step 4 yields garbage, the CPU executes it and the extender reports
`Memory protection fault`. The reported `CS:EIP = 000F:0009C4E8` is exactly three bytes into the 20-byte block, i.e. the
second instruction of the garbage. `000F:000A0697`, seen in another run before the patch, lies beyond the end of the image
(memory that is only filled at run time). Presumably it is another way garbage can run, depending on machine state;
neither address appears after the patch.

## 3. The two pieces that make the key CPU-dependent

### Site A: a store into the next instruction

```
9C38D  add  eax,[0]
9C393  mov  [9C39A],ecx             ; 89 0D 9A C3 09 00   <- overwrites the immediate of the next instruction
9C399  add  eax,0A5A5A5A5h          ; 05 A5 A5 A5 A5      (its imm32 is at 9C39A)
9C39E  call cpu_check               ; E8 CE FE FF FF
9C3A3  add  [9C4E5],eax             ; ... five of these
```

The store is directly followed by the instruction it modifies. A 386/486 has already fetched the old instruction into
its prefetch queue and executes it with the **old** constant `0A5A5A5A5h`. A CPU that detects self-modifying code
(Pentium and later) executes the instruction with the **new** value, `ecx`. This is the prefetch-queue behaviour
that DOSBox-X emulates with its `486_prefetch` CPU type (see DOSBox-X issue #2481).

### Site B: the compensation in `cpu_check`

`cpu_check` (entry `9C271`, saves everything with `pusha`/`pushf`) detects the CPU class with the usual flag tricks (AC
and ID bit) and calls `cpuid`:

```
9C2ED  mov  eax,1
9C2F2  cpuid
9C2F4  and  ah,0Fh
9C2F7  cmp  ah,4
9C2FA  jz   done                    ; 74 10   <- family 4 (486): leave the key alone
9C2FC  mov  eax,[esp+1Ch]           ; saved ECX
9C300  sub  [esp+20h],eax           ; saved EAX -= ECX
9C304  add  dword [esp+20h],0A5A5A5A5h
9C30C  popf / popa / ret            ; done
```

For every CPU with a family other than 4 the loader turns "added `ecx`" back into "added `0A5A5A5A5h`".

### The algebra

Let `X` be `eax` before the add, `E` the value of `ecx`, `C = 0A5A5A5A5h`, and `R = X + C` the key the game was built for.

| CPU | modification of the next instruction seen? | CPUID family | resulting key |
|---|---|---|---|
| 386 / 486 | no | none / 4 | `X + C` = **R** |
| Pentium | yes | 5 | `X + E`, then `- E + C` = **R** |
| K6 (assumption, see below) | no | 5 | `X + C`, then `- E + C` = `R + C - E` (wrong) |
| Cyrix 486 upgrade, reported on VOGONS (unverified) | yes | not > 4 | `X + E` (wrong) |

The design works only when "modification seen" and "family != 4" go together. The K6 identifies itself as a
Pentium-class CPU, so the loader subtracts `E` and adds `C` again. The most likely reason for the failure is that the K6
does not pick up the store to the instruction directly behind it (its decoders are already working on it), so the
compensation is applied on top of a key that was already correct. The crash persists with caches disabled and with a lower
multiplier, which fits: it is neither a timing nor a cache-size problem. The K6 mechanism itself was **not** measured; what
was verified is the effect of the patch (section 6).

## 4. The patch

Two edits, 7 bytes in total, so that the key is `X + C` on every CPU:

| Site | File offset (tested build) | Before | After | Effect |
|---|---|---|---|---|
| A | `0x96F43` | `89 0D 9A C3 09 00` | `90 90 90 90 90 90` | The store is removed, the next instruction always uses the constant `0A5A5A5A5h`. |
| B | `0x96EAA` | `74 10` | `EB 10` | The family test always takes the "done" branch, nothing is compensated. |

Both edits are needed: with only A, the K6 would still be "compensated"; with only B, a Pentium would use `E`.

The patched loader then behaves like the original on a 486, the machine the key was designed for, on every CPU. The other
inputs to the key are not touched. Self-modifying writes elsewhere in the loader are either far (more than 0x100 bytes)
from where the modified code runs or are followed by a control transfer (call/return), which flushes the prefetch queue on
every CPU. Nothing else in the loader appears to depend on CPU behaviour in the way site A does.

## 5. Locating the sites in another build

The loader is plain code, so the sites can be found by pattern; absolute addresses differ between builds, the code does
not. This is what `fifa_k6_fix.py` / `fifa_k6_fix.c` do:

* Site A: `89 0D xx xx xx 00 05 A5 A5 A5 A5 E8 xx xx xx xx`, exactly one match in the file.
* Site B: `0F A2 80 E4 0F 80 FC 04 74 10 8B 44 24 1C 29 44 24 20 81 44 24 20 A5 A5 A5 A5 9D 61 C3`, exactly one match.
* A must come after B, less than `0x400` bytes behind it.
* Anything else (no match, several matches, wrong order, only one site already changed) is refused and nothing is written.
* The result is re-analysed before it is written, and exactly seven bytes must differ.

If a build has a different loader (different encryption wrapper, other constants) the tool says so and stops. Such a
build needs its own analysis.

## 6. What was verified, what was not

Verified

* The disassembly of the loader is consistent (all call targets land on instruction boundaries, string and data references
  match the code that uses them).
* The patched file starts and runs on an AMD K6-III+ (Asus P5A) and in DOSBox-X. The unpatched file crashed on the K6-III+
  with `CS:EIP = 000F:0009C4E8` (caches off, lowered multiplier); an earlier run showed `000F:000A0697`.
* The patcher's output is byte-identical to the file that was tested on the hardware. The Python and C versions produce
  identical results; `tests/test_patcher.py` covers patched, unpatched, mixed, ambiguous, foreign-file and existing-backup cases
  on synthetic files.

Not verified

* The actual K6 behaviour at site A (it is the explanation that fits, not a measurement). A small test program that stores
  into the next instruction and reports the result would settle it.
* Other builds of the game (CD-ROM, 8 MB, other languages), other K6 variants (K6, K6-2, K6-III), other CPUs (Cyrix,
  Pentium MMX, K5, Pentium Pro/II).
* The DOS builds of the C version.
