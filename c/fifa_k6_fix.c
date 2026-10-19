/*
 * fifa_k6_fix.c - start-up loader fix for FIFA International Soccer (DOS)
 *
 * C version of fifa_k6_fix.py with identical behaviour, for people who want to
 * run the patcher directly on the DOS machine. It needs a 32-bit compiler
 * (Open Watcom with -bt=dos -l=dos4g or wcl386, DJGPP, or gcc on any modern
 * system). The whole EXE (about 800 KB) is read into memory.
 *
 * The tool contains NO game code or data. It only looks for two short byte
 * patterns in the loader at the end of your own FIFA.EXE and changes 7 bytes.
 * See README.md and docs/TECHNICAL.md.
 *
 * Usage:
 *   fifa_k6_fix FIFA.EXE                 patch in place (backup: FIFA.EXE.orig)
 *   fifa_k6_fix --check FIFA.EXE         only report, change nothing
 *   fifa_k6_fix -o FIFA_K6.EXE FIFA.EXE  write a patched copy
 *   fifa_k6_fix --no-backup FIFA.EXE     patch in place without backup
 *
 * Public domain (Unlicense). No warranty.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "1.1"
#define MAX_SIZE 67108864UL   /* 64 MB */
#define MAX_DISTANCE 0x400UL
#define A_LEN 6
#define W (-1)                /* wildcard */

/* Site A: mov [abs32],ecx / add eax,0A5A5A5A5h / call rel32 */
static const int SITE_A_ORIG[] = {
    0x89, 0x0D, W, W, W, 0x00, 0x05, 0xA5, 0xA5, 0xA5, 0xA5, 0xE8, W, W, W, W };
static const int SITE_A_DONE[] = {
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x05, 0xA5, 0xA5, 0xA5, 0xA5, 0xE8, W, W, W, W };

/* Site B: cpuid / and ah,0Fh / cmp ah,4 / jz|jmp short / ... / popf / popa / ret */
#define B_JUMP_OFFSET 8
static const int SITE_B_ORIG[] = {
    0x0F, 0xA2, 0x80, 0xE4, 0x0F, 0x80, 0xFC, 0x04, 0x74, 0x10,
    0x8B, 0x44, 0x24, 0x1C, 0x29, 0x44, 0x24, 0x20,
    0x81, 0x44, 0x24, 0x20, 0xA5, 0xA5, 0xA5, 0xA5, 0x9D, 0x61, 0xC3 };
static const int SITE_B_DONE[] = {
    0x0F, 0xA2, 0x80, 0xE4, 0x0F, 0x80, 0xFC, 0x04, 0xEB, 0x10,
    0x8B, 0x44, 0x24, 0x1C, 0x29, 0x44, 0x24, 0x20,
    0x81, 0x44, 0x24, 0x20, 0xA5, 0xA5, 0xA5, 0xA5, 0x9D, 0x61, 0xC3 };

#define COUNT(a) ((unsigned)(sizeof(a) / sizeof((a)[0])))

/* Count matches of a pattern; remember the offset of the first one. */
static unsigned find_all(const unsigned char *d, unsigned long n,
                         const int *pat, unsigned plen, unsigned long *first)
{
    unsigned long i;
    unsigned j, found = 0;

    if (n < plen)
        return 0;
    for (i = 0; i + plen <= n; i++) {
        for (j = 0; j < plen; j++) {
            if (pat[j] != W && d[i + j] != (unsigned char)pat[j])
                break;
        }
        if (j == plen) {
            if (found == 0)
                *first = i;
            found++;
        }
    }
    return found;
}

static unsigned long crc32_of(const unsigned char *d, unsigned long n)
{
    unsigned long crc = 0xFFFFFFFFUL, i;
    int k;

    for (i = 0; i < n; i++) {
        crc ^= d[i];
        for (k = 0; k < 8; k++)
            crc = (crc & 1UL) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFUL;
}

enum { S_UNKNOWN = 0, S_UNPATCHED, S_PATCHED, S_MIXED };

static int analyse(const unsigned char *d, unsigned long n,
                   unsigned long *a_off, unsigned long *b_off)
{
    unsigned long a1 = 0, a2 = 0, b1 = 0, b2 = 0;
    unsigned na_o, na_d, nb_o, nb_d;
    unsigned na, nb;

    na_o = find_all(d, n, SITE_A_ORIG, COUNT(SITE_A_ORIG), &a1);
    na_d = find_all(d, n, SITE_A_DONE, COUNT(SITE_A_DONE), &a2);
    nb_o = find_all(d, n, SITE_B_ORIG, COUNT(SITE_B_ORIG), &b1);
    nb_d = find_all(d, n, SITE_B_DONE, COUNT(SITE_B_DONE), &b2);
    na = na_o + na_d;
    nb = nb_o + nb_d;

    if (na == 0 || nb == 0) {
        printf("The loader signature was not found (site A: %u, site B: %u).\n"
               "This file is either not an affected build of the game, or it is a\n"
               "variant this tool does not know yet.\n", na, nb);
        return S_UNKNOWN;
    }
    if (na != 1 || nb != 1) {
        printf("Ambiguous: found %u candidate(s) for site A and %u for site B "
               "(exactly one of each is expected).\n", na, nb);
        return S_UNKNOWN;
    }
    *a_off = na_o ? a1 : a2;
    *b_off = nb_o ? b1 : b2;

    if (!(*a_off > *b_off && *a_off - *b_off < MAX_DISTANCE)) {
        printf("The two signatures were found, but not in the expected layout "
               "(site A at 0x%lX, site B at 0x%lX).\n",
               *a_off, *b_off + B_JUMP_OFFSET);
        return S_UNKNOWN;
    }
    if (na_o && nb_o) {
        printf("Both patch sites found, file is unpatched.\n");
        return S_UNPATCHED;
    }
    if (na_d && nb_d) {
        printf("Both patch sites already carry the fix.\n");
        return S_PATCHED;
    }
    printf("Only one of the two patch sites carries the fix (site A at file offset "
           "0x%lX, site B at 0x%lX). The file was modified by something else.\n",
           *a_off, *b_off + B_JUMP_OFFSET);
    return S_MIXED;
}

/* Informational only: the loader looks for its own EXE by a hard-coded name that is stored
 * 0xF8 bytes behind the start of site B's pattern (same loader blob in all known builds). */
#define NAME_OFFSET_FROM_B 0xF8UL

static void print_loader_name(const unsigned char *d, unsigned long n, unsigned long b_off)
{
    unsigned long start = b_off + NAME_OFFSET_FROM_B;
    char name[16];
    unsigned i = 0;
    unsigned char c;

    if (start + 14 > n)
        return;
    while (i < 8) {
        c = d[start + i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            break;
        name[i] = (char)c;
        i++;
    }
    if (i == 0 || d[start + i] != '.')
        return;
    if ((d[start + i + 1] | 0x20) != 'e' || (d[start + i + 2] | 0x20) != 'x' ||
        (d[start + i + 3] | 0x20) != 'e' || d[start + i + 4] != 0)
        return;
    name[i] = '\0';
    printf("The loader looks for a file named '%s.exe' in the current directory, "
           "so the EXE must keep exactly this name.\n", name);
}

static unsigned char *read_file(const char *path, unsigned long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long len;

    if (!f)
        return NULL;
    fseek(f, 0L, SEEK_END);
    len = ftell(f);
    fseek(f, 0L, SEEK_SET);
    if (len < 0 || (unsigned long)len > MAX_SIZE) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (unsigned long)len;
    return buf;
}

static int write_file(const char *path, const unsigned char *d, unsigned long n)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return 0;
    if (fwrite(d, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

static void usage(void)
{
    printf("fifa_k6_fix " VERSION " - AMD K6 start-up fix for FIFA International Soccer (DOS)\n\n"
           "usage: fifa_k6_fix [--check] [--no-backup] [-o OUTPUT] FIFA.EXE\n\n"
           "  --check       only report the state of the file, change nothing\n"
           "  -o OUTPUT     write the patched file there instead of patching in place\n"
           "  --no-backup   patch in place without creating FIFA.EXE.orig\n");
}

int main(int argc, char **argv)
{
    const char *exe = NULL, *out = NULL;
    int check = 0, nobackup = 0, i, state;
    unsigned char *data, *patched;
    unsigned long size = 0, a_off = 0, b_off = 0, k, ndiff;
    char *tmp, *bak;
    const char *target;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check")) {
            check = 1;
        } else if (!strcmp(argv[i], "--no-backup")) {
            nobackup = 1;
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-h") ||
                   !strcmp(argv[i], "--help") || !strcmp(argv[i], "/?")) {
            usage();
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "error: unknown option %s\n", argv[i]);
            usage();
            return 1;
        } else if (!exe) {
            exe = argv[i];
        } else {
            usage();
            return 1;
        }
    }
    if (!exe) {
        usage();
        return 1;
    }

    data = read_file(exe, &size);
    if (!data) {
        fprintf(stderr, "error: cannot read %s (missing, unreadable or larger than 64 MB)\n", exe);
        return 1;
    }
    printf("File   : %s\n", exe);
    printf("Size   : %lu bytes\n", size);
    printf("CRC-32 : %08lX\n", crc32_of(data, size));
    if (size < 2 || data[0] != 'M' || data[1] != 'Z')
        printf("warning: file does not start with 'MZ', it is not a DOS executable.\n");

    state = analyse(data, size, &a_off, &b_off);
    if (state == S_UNPATCHED || state == S_PATCHED)
        print_loader_name(data, size, b_off);

    if (state == S_MIXED) {
        printf("\nNothing was changed. Restore the original FIFA.EXE (for example from the\n"
               "FIFA.EXE.orig backup) and run this tool again.\n");
        free(data);
        return 1;
    }
    if (state == S_UNKNOWN) {
        printf("\nNothing was changed. If you are sure this is the game EXE, please report it\n"
               "and include the size and CRC-32 shown above (see README.md).\n");
        free(data);
        return 1;
    }
    if (state == S_PATCHED) {
        printf("Nothing to do.\n");
        if (out && !write_file(out, data, size)) {
            fprintf(stderr, "error: cannot write %s\n", out);
            free(data);
            return 1;
        }
        free(data);
        return 0;
    }

    printf("  site A (self-modifying store) at file offset 0x%lX: %02X %02X %02X %02X %02X %02X\n",
           a_off, data[a_off], data[a_off + 1], data[a_off + 2],
           data[a_off + 3], data[a_off + 4], data[a_off + 5]);
    printf("  site B (CPU family test)      at file offset 0x%lX: %02X %02X\n",
           b_off + B_JUMP_OFFSET, data[b_off + B_JUMP_OFFSET], data[b_off + B_JUMP_OFFSET + 1]);

    if (check) {
        printf("\n--check: file needs the fix, nothing was changed.\n");
        free(data);
        return 0;
    }

    patched = (unsigned char *)malloc((size_t)size + 1);
    if (!patched) {
        fprintf(stderr, "error: out of memory\n");
        free(data);
        return 1;
    }
    memcpy(patched, data, (size_t)size);
    for (k = 0; k < A_LEN; k++)
        patched[a_off + k] = 0x90;
    patched[b_off + B_JUMP_OFFSET] = 0xEB;

    /* Self-check: exactly 7 bytes differ and the result is recognised as patched. */
    ndiff = 0;
    for (k = 0; k < size; k++)
        if (data[k] != patched[k])
            ndiff++;
    printf("Self-check: ");
    {
        unsigned long a2, b2;
        int st;
        /* analyse() prints; silence is not needed, the line above labels it */
        st = analyse(patched, size, &a2, &b2);
        if (ndiff != 7UL || st != S_PATCHED) {
            fprintf(stderr, "internal error: self-check failed, nothing was written.\n");
            free(data);
            free(patched);
            return 2;
        }
    }

    target = out ? out : exe;
    if (!out && !nobackup) {
        bak = (char *)malloc(strlen(exe) + 6);
        if (!bak) {
            free(data);
            free(patched);
            return 1;
        }
        strcpy(bak, exe);
        strcat(bak, ".orig");
        if (file_exists(bak)) {
            fprintf(stderr, "error: backup file %s already exists. Move it away, use -o to "
                            "write a separate file, or --no-backup.\n", bak);
            free(bak);
            free(data);
            free(patched);
            return 1;
        }
        if (!write_file(bak, data, size)) {
            fprintf(stderr, "error: cannot write backup %s\n", bak);
            free(bak);
            free(data);
            free(patched);
            return 1;
        }
        printf("Backup : %s\n", bak);
        free(bak);
    }

    if (out) {
        if (!write_file(out, patched, size)) {
            fprintf(stderr, "error: cannot write %s\n", out);
            free(data);
            free(patched);
            return 1;
        }
    } else {
        /* write a temporary file first, then replace the original */
        tmp = (char *)malloc(strlen(exe) + 5);
        if (!tmp) {
            free(data);
            free(patched);
            return 1;
        }
        strcpy(tmp, exe);
        strcat(tmp, ".tmp");
        if (!write_file(tmp, patched, size)) {
            fprintf(stderr, "error: cannot write %s\n", tmp);
            remove(tmp);
            free(tmp);
            free(data);
            free(patched);
            return 1;
        }
        remove(exe);
        if (rename(tmp, exe) != 0) {
            fprintf(stderr, "error: cannot replace %s; the patched copy is %s\n", exe, tmp);
            free(tmp);
            free(data);
            free(patched);
            return 1;
        }
        free(tmp);
    }

    printf("Patched: %s  (7 bytes changed)\n", target);
    printf("CRC-32 : %08lX\n", crc32_of(patched, size));
    free(data);
    free(patched);
    return 0;
}
