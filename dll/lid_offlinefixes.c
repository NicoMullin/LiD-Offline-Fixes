/*
 * lid-offlinefixes.dll for Let It Die - a plugin for the LiD Plugin Loader that
 * fixes bugs in the game's own offline mode. It changes game code in memory
 * only.
 *
 * Fix 1: floor events about something already gone no longer close the game.
 *
 * While you play a floor, the game sends its built-in offline server a stream
 * of floor events: this Hater was killed, that box was broken, this chest was
 * opened, a Hater's reward was taken. The server looks each one up in the
 * floor's data. When it is not there - most often a Hater still playing its
 * death animation as you take the stairs, so the kill arrives after the floor
 * has changed - the server's code is written to skip that event: it logs
 * "... entity not found", returns, and the caller carries on with the next
 * event. But before returning it also sends the message to the engine's
 * fatal-error log, and that closes the game on the spot (Windows records an
 * APPCRASH in KERNELBASE.dll with exception code 1, and the game's own crash
 * handler writes nothing).
 *
 * The plugin takes out exactly those fatal-error calls - the call instruction
 * only, replaced by a 5-byte no-op - so each handler does what it was written
 * to do: log the message to the game's normal log (Launch.log, which it
 * already does just before) and skip the event. 19 calls, for these events:
 *
 *   killed a Hater, a regular enemy, a boss, a beast, the Four Forcemen,
 *   a Jackal master; killed by a Hater; met a Hater; a Hater's reward and its
 *   Bloodnium; a regular enemy's chest money; a broken object; a floor chest
 *
 * Left fatal on purpose: the same check on your own beasts, materials and
 * vending machine. Those are inventory, not floor events, so "not found" there
 * means something really is wrong.
 *
 * Safety rules:
 *   - Each call is found from its message: the code that loads the message
 *     into the log call, then the call itself (load the log device, call the
 *     log function). Which device is the fatal one is read off the "Killed
 *     zombie" handler, which logs its message twice - first to the normal log,
 *     then to the fatal one. Every message must lead to a fatal call, all of
 *     them must use the same log function, and there must be exactly 19
 *     calls, or nothing is changed.
 *   - Changed in the game: those 19 five-byte calls. No network, no registry,
 *     no files but its own log, lid-offlinefixes.log, next to itself.
 *   - Delete lid-offlinefixes.dll and the game is exactly stock again.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "version.h"

/* ------------------------------------------------------------------ log */

static wchar_t g_log_path[MAX_PATH];
static int g_log_started;

static void log_line(const char *fmt, ...)
{
    char msg[768];
    SYSTEMTIME t;
    va_list ap;
    int n;
    HANDLE f;
    DWORD written;

    if (!g_log_path[0])
        return;
    GetLocalTime(&t);
    n = snprintf(msg, sizeof msg, "%02u:%02u:%02u  ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    n += vsnprintf(msg + n, sizeof msg - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof msg - 3)
        n = (int)sizeof msg - 3;
    msg[n++] = '\r';
    msg[n++] = '\n';

    /* a fresh log each time the game starts */
    f = CreateFileW(g_log_path, g_log_started ? FILE_APPEND_DATA : GENERIC_WRITE,
                    FILE_SHARE_READ, NULL, g_log_started ? OPEN_ALWAYS : CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    g_log_started = 1;
    WriteFile(f, msg, (DWORD)n, &written, NULL);
    CloseHandle(f);
}

/* ------------------------------------------------------------ helpers */

struct span {
    unsigned char *start;
    size_t size;
};

static int section(unsigned char *base, const char *name, struct span *out)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sec;
    WORD i;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (strncmp((const char *)sec->Name, name, IMAGE_SIZEOF_SHORT_NAME) == 0) {
            out->start = base + sec->VirtualAddress;
            out->size = sec->Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

static int inside(struct span s, const void *p, size_t n)
{
    const unsigned char *q = (const unsigned char *)p;
    return q >= s.start && q + n <= s.start + s.size;
}

/* where a rip-relative field points (the instruction ends right after it) */
static unsigned char *rip_target(unsigned char *field)
{
    int32_t r;
    memcpy(&r, field, 4);
    return field + 4 + r;
}

static int write_code(unsigned char *at, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return 1;
}

/* --------------------------------------------------------- the pieces */

/* The floor events whose "not found" closed the game. The first is the anchor
 * that tells the two log devices apart. Keep the texts exactly as the game
 * has them (two spaces in getRewardBloodnium's are the game's). */
static const wchar_t *const FLOOR_EVENTS[] = {
    L"Killed zombie entity not found. zid=%s",
    L"Killed zako entity not found. zkid=%s",
    L"current time Killed from zombie entity not found. zid=%s",
    L"encountPlayerEnemy Killed zombie entity not found. zid=%s",
    L"current time user_get_zombie_reward zombie entity not found. zid=%s",
    L"current time user_get_zombie_reward zombie_reward entity not found. zid=%s",
    L"getRewardBloodnium  zombie entity not found. zid=%s",
    L"current time getZakoTrboxMoney zako entity not found. zkid=%s",
    L"current time getZakoTrboxMoney zakoTrbox entity not found. zkid=%s treid=%s",
    L"current time brekable_object entity not found. eboid=%s",
    L"current time get floor_trbox entity not found. unit=%s, pntid=%s",
    L"rUserPopTrbox not found. unit=%s, pntid=%s",
    L"not found kill beast entity, ebstid=%s",
    L"not found kill mboss entity, embid=%s",
    L"not found kill floor_mboss entity, embid=%s",
    L"not found kill fourforcemen entity, embid=%s",
    L"not found kill floor_fourforcemen entity, embid=%s",
    L"not found kill fourforcemen entity, ffmid=%s",
    L"not found kill jackal master entity, type=%s",
};
#define EVENTS      (int)(sizeof FLOOR_EVENTS / sizeof FLOOR_EVENTS[0])
#define FATAL_CALLS 19          /* some messages share one call, one has two */
#define MAX_COPIES  256         /* 1.91 has 66: most messages three times, one six */
#define MAX_SITES   128

/* A log call: mov rcx,[rip+device] / call log. */
struct site {
    int event;                  /* index in FLOOR_EVENTS */
    unsigned char *call;        /* the E8 */
    unsigned char *device;      /* the global the log device is read from */
    unsigned char *logf;        /* the function called */
};

struct found {
    unsigned char *normal, *fatal, *logf;   /* from the anchor */
    unsigned char *calls[FATAL_CALLS];
    int ncalls;
    int anchor_sites;           /* must be 2 */
    int missing;                /* first event with no fatal call, or -1 */
    int too_many;
};

/* From the instruction after lea rdx,[message]: at most one jump (the
 * compiler shares a tail between messages), up to three register moves
 * (the message's arguments), then mov rcx,[rip+device] and call. */
static int log_call_after(struct span text, unsigned char *p, struct site *out)
{
    int k;

    if (!inside(text, p, 5))
        return 0;
    if (p[0] == 0xE9)
        p = rip_target(p + 1);
    else if (p[0] == 0xEB)
        p = p + 2 + (int8_t)p[1];
    for (k = 0; k < 3; k++) {
        if (!inside(text, p, 3))
            return 0;
        if ((p[0] == 0x48 || p[0] == 0x49 || p[0] == 0x4C || p[0] == 0x4D) && p[1] == 0x8B && p[2] >= 0xC0)
            p += 3;
        else
            break;
    }
    if (!inside(text, p, 12) || p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x0D || p[7] != 0xE8)
        return 0;
    out->device = rip_target(p + 3);
    out->call = p + 7;
    out->logf = rip_target(p + 8);
    return inside(text, out->logf, 1);
}

static void find_calls(unsigned char *base, struct found *fd)
{
    static struct site sites[MAX_SITES];
    unsigned char *copy[MAX_COPIES];
    int copy_event[MAX_COPIES];
    struct span text, rdata;
    int ncopies = 0, nsites = 0, e, i, j;
    size_t at;

    memset(fd, 0, sizeof *fd);
    fd->missing = -1;
    if (!section(base, ".text", &text) || !section(base, ".rdata", &rdata)) {
        fd->missing = 0;
        return;
    }

    /* every copy of every message (the game has each one two or three times) */
    for (e = 0; e < EVENTS; e++) {
        size_t n = (wcslen(FLOOR_EVENTS[e]) + 1) * sizeof(wchar_t);
        for (at = 0; at + n <= rdata.size; at += 2) {
            if (rdata.start[at] == (unsigned char)FLOOR_EVENTS[e][0] &&
                memcmp(rdata.start + at, FLOOR_EVENTS[e], n) == 0 && ncopies < MAX_COPIES) {
                copy[ncopies] = rdata.start + at;
                copy_event[ncopies++] = e;
            }
        }
    }

    /* every lea rdx,[rip+copy] in the code, and the log call it leads to */
    for (at = 0; at + 7 <= text.size; at++) {
        unsigned char *p = text.start + at, *target;
        if (p[0] != 0x48 || p[1] != 0x8D || p[2] != 0x15)
            continue;
        target = rip_target(p + 3);
        for (i = 0; i < ncopies; i++) {
            struct site s;
            if (copy[i] != target)
                continue;
            if (log_call_after(text, p + 7, &s) && nsites < MAX_SITES) {
                s.event = copy_event[i];
                sites[nsites++] = s;
            }
        }
    }

    /* the anchor logs its message twice: the normal log first, then the fatal one */
    {
        struct site *a = NULL, *b = NULL;
        for (i = 0; i < nsites; i++) {
            if (sites[i].event != 0)
                continue;
            fd->anchor_sites++;
            if (!a)
                a = &sites[i];
            else
                b = &sites[i];
        }
        if (fd->anchor_sites != 2 || a->logf != b->logf || a->device == b->device) {
            fd->missing = 0;
            return;
        }
        if (a->call > b->call) {
            struct site *t = a;
            a = b;
            b = t;
        }
        fd->normal = a->device;
        fd->fatal = b->device;
        fd->logf = a->logf;
    }

    /* every message must reach the fatal log; collect the calls, once each */
    for (e = 0; e < EVENTS; e++) {
        int any = 0;
        for (i = 0; i < nsites; i++) {
            int seen = 0;
            if (sites[i].event != e || sites[i].device != fd->fatal || sites[i].logf != fd->logf)
                continue;
            any = 1;
            for (j = 0; j < fd->ncalls; j++)
                seen |= fd->calls[j] == sites[i].call;
            if (seen)
                continue;
            if (fd->ncalls == FATAL_CALLS) {
                fd->too_many = 1;
                continue;
            }
            fd->calls[fd->ncalls++] = sites[i].call;
        }
        if (!any && fd->missing < 0)
            fd->missing = e;
    }
}

/* ------------------------------------------------------------- start */

static int ready(const struct found *fd)
{
    return fd->missing < 0 && !fd->too_many && fd->ncalls == FATAL_CALLS;
}

static void start(void)
{
    static const unsigned char NOP5[5] = {0x0F, 0x1F, 0x44, 0x00, 0x00};
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    struct found fd;
    int i;

    find_calls(base, &fd);
    if (!ready(&fd)) {
        if (fd.missing == 0)
            log_line("the floor-event error calls were not found (the \"Killed zombie\" handler logs its "
                     "message %d time(s), not 2) - nothing changed. A game update may have moved them.",
                     fd.anchor_sites);
        else if (fd.missing > 0)
            log_line("no fatal error call found for \"%ls\" - nothing changed. A game update may have "
                     "moved it.", FLOOR_EVENTS[fd.missing]);
        else
            log_line("found %s%d floor-event error calls, not %d - nothing changed. A game update may "
                     "have moved them.", fd.too_many ? "more than " : "", fd.ncalls, FATAL_CALLS);
        return;
    }
    for (i = 0; i < fd.ncalls; i++) {
        if (fd.calls[i][0] != 0xE8 || rip_target(fd.calls[i] + 1) != fd.logf ||
            !write_code(fd.calls[i], NOP5, sizeof NOP5)) {
            log_line("could not change the call at game+0x%llX - stopped after %d of %d",
                     (unsigned long long)(fd.calls[i] - base), i, fd.ncalls);
            return;
        }
    }
    log_line("ready: %d floor events about something already gone (a Hater killed as you took the "
             "stairs...) are now skipped and logged instead of closing the game (fatal log "
             "game+0x%llX, log function game+0x%llX)",
             fd.ncalls, (unsigned long long)(fd.fatal - base), (unsigned long long)(fd.logf - base));
}

static int running_in_the_game(void)
{
    wchar_t exe[MAX_PATH];
    const wchar_t *name;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);

    if (n == 0 || n >= MAX_PATH)
        return 0;
    name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    return _wcsicmp(name, L"BrgGame-Steam.exe") == 0;
}

#ifndef LID_OFFLINEFIXES_NO_DLLMAIN
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t *slash;
        DWORD n;

        DisableThreadLibraryCalls(inst);
        n = GetModuleFileNameW(inst, g_log_path, MAX_PATH);
        slash = (n && n < MAX_PATH - 32) ? wcsrchr(g_log_path, L'\\') : NULL;
        if (slash)
            wcscpy(slash + 1, L"lid-offlinefixes.log");
        else
            g_log_path[0] = 0;

        if (!running_in_the_game())
            return TRUE;    /* some other program - change nothing */
        log_line("LiD Offline Fixes lid-offlinefixes.dll " LID_DLL_VERSION " loaded");
        start();
    }
    return TRUE;
}
#endif
