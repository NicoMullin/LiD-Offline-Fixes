/* Stand-in for BrgGame-Steam.exe (see make_harness.py). Built as
 * BrgGame-Steam.exe next to lid-offlinefixes.dll, so the plugin takes it for
 * the game. It runs every handler once before loading the plugin and once
 * after, counting what each one sends to the fatal log.
 *
 * Without --drop: after loading, only the inventory handlers (@KEEP@) still
 * reach the fatal log, and the plugin's log says it is ready.
 * With --drop (@DROP@ >= 0): one floor event has no fatal call, as after a game
 * update; the plugin must change nothing at all. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int normal_device, fatal_device;
void *g_normal = &normal_device;
void *g_fatal = &fatal_device;

static int fatal_count, normal_count;
static wchar_t last_fatal[256];

void fake_logf(void *device, const wchar_t *fmt, ...)
{
    if (device == &fatal_device) {
        fatal_count++;
        wcsncpy(last_fatal, fmt, 255);
    } else if (device == &normal_device) {
        normal_count++;
    }
}

__asm__(
@ASM@
);

typedef int (*handler_fn)(void);
extern handler_fn handlers[];
extern int handler_count;

static int run_all(int *returned_true)
{
    int i;
    fatal_count = normal_count = 0;
    *returned_true = 0;
    for (i = 0; i < handler_count; i++)
        *returned_true += handlers[i]() != 0;
    return fatal_count;
}

int main(void)
{
    char log[4096] = {0};
    int drop = @DROP@, keep = @KEEP@;
    int before, after, normal_before, yes, fails = 0;
    FILE *f;

    before = run_all(&yes);
    normal_before = normal_count;
    printf("before the plugin: %d handlers, %d fatal, %d normal\n", handler_count, before, normal_before);
    if (yes) { puts("FAIL: a handler answered yes"); fails++; }

    if (!LoadLibraryW(L"lid-offlinefixes.dll")) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }
    f = fopen("lid-offlinefixes.log", "rb");
    if (f) { fread(log, 1, sizeof log - 1, f); fclose(f); }
    printf("%s", log);

    after = run_all(&yes);
    printf("after the plugin: %d fatal, %d normal\n", after, normal_count);
    if (yes) { puts("FAIL: a handler answered yes after the change"); fails++; }
    if (normal_count != normal_before) { puts("FAIL: the normal log calls changed"); fails++; }

    if (drop < 0) {
        if (after != keep) {
            printf("FAIL: %d fatal calls left, expected only the %d inventory ones (last: %ls)\n",
                   after, keep, last_fatal);
            fails++;
        } else if (!wcsstr(last_fatal, L"vending machine")) {
            printf("FAIL: the fatal call left is not an inventory one: %ls\n", last_fatal);
            fails++;
        }
        if (!strstr(log, "ready: 19 floor events")) { puts("FAIL: the log does not say ready: 19"); fails++; }
    } else {
        if (after != before) { printf("FAIL: %d fatal calls changed\n", before - after); fails++; }
        if (!strstr(log, "nothing changed") || strstr(log, "ready:")) {
            puts("FAIL: the log does not say nothing changed");
            fails++;
        }
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
