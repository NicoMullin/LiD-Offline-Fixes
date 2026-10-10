/* The plugin's search, run on the real BrgGame-Steam.exe (mapped as a file,
 * nothing runs). It must find all 19 fatal calls. Only run by
 * `build.py --exe PATH`: the game is not part of this repository. */
#define LID_OFFLINEFIXES_NO_DLLMAIN
#include "../dll/lid_offlinefixes.c"

#include <stdlib.h>

/* where they are in game build 5.0.4.4 (1.91) */
static const unsigned long long CALLS_191[FATAL_CALLS] = {
    0x127607A, 0x127615B, 0x12763ED, 0x1279AAF, 0x1283F16, 0x1285F66, 0x1288D7D, 0x1289790, 0x1289DF7,
    0x128A75C, 0x128A83F, 0x128AC42, 0x128B90F, 0x128BE0B, 0x128C34F, 0x128C7FB, 0x128CD5F, 0x128D313,
    0x128DD49,
};

static int by_address(const void *a, const void *b)
{
    unsigned char *x = *(unsigned char *const *)a, *y = *(unsigned char *const *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    struct found fd;
    unsigned char *base;
    HMODULE m;
    int i, same = 1;

    if (argc < 2) { puts("usage: realexe PATH-TO-BrgGame-Steam.exe"); return 2; }
    m = LoadLibraryExA(argv[1], NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    if (!m) { printf("could not map %s (error %lu)\n", argv[1], GetLastError()); return 2; }
    base = (unsigned char *)((ULONG_PTR)m & ~(ULONG_PTR)3);

    find_calls(base, &fd);
    printf("\"Killed zombie\" logs %d time(s) (needs 2); normal log game+0x%llX, fatal log game+0x%llX "
           "(1.91: +0x2750938, +0x27FECA0), log function game+0x%llX (1.91: +0x13FDB0)\n",
           fd.anchor_sites, (unsigned long long)(fd.normal ? fd.normal - base : 0),
           (unsigned long long)(fd.fatal ? fd.fatal - base : 0),
           (unsigned long long)(fd.logf ? fd.logf - base : 0));
    if (fd.missing >= 0)
        printf("no fatal call for: %ls\n", FLOOR_EVENTS[fd.missing]);
    qsort(fd.calls, fd.ncalls, sizeof fd.calls[0], by_address);
    for (i = 0; i < fd.ncalls; i++) {
        unsigned long long at = (unsigned long long)(fd.calls[i] - base);
        same &= at == CALLS_191[i];
        printf("  fatal call game+0x%llX%s\n", at, at == CALLS_191[i] ? "" : "  (not where 1.91 has it)");
    }
    printf("%d of %d found%s\n", fd.ncalls, FATAL_CALLS, fd.too_many ? " (and more)" : "");
    if (!ready(&fd)) { puts("\nFAILED"); return 1; }
    puts(same ? "\nall found, where 1.91 has them" : "\nall found (moved from where 1.91 has them)");
    return 0;
}
