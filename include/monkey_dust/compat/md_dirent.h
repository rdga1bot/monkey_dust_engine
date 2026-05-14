#pragma once
// Windows-compatible shim for POSIX dirent.h (loader/startup code only).
#ifdef _WIN32
#include <windows.h>
#include <string.h>
#include <stdlib.h>

struct dirent { char d_name[MAX_PATH]; };

typedef struct {
    HANDLE           hnd;
    WIN32_FIND_DATAA data;
    struct dirent    ent;
    int              first;
} DIR;

static inline DIR* opendir(const char* path) {
    char buf[MAX_PATH];
    int n = (int)strlen(path);
    if (n + 3 >= MAX_PATH) return NULL;
    memcpy(buf, path, n);
    buf[n] = '\\'; buf[n+1] = '*'; buf[n+2] = '\0';
    DIR* d = (DIR*)malloc(sizeof(DIR));
    if (!d) return NULL;
    d->hnd = FindFirstFileA(buf, &d->data);
    if (d->hnd == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = 1;
    return d;
}

static inline struct dirent* readdir(DIR* d) {
    if (d->first) { d->first = 0; }
    else if (!FindNextFileA(d->hnd, &d->data)) return NULL;
    strncpy(d->ent.d_name, d->data.cFileName, MAX_PATH - 1);
    d->ent.d_name[MAX_PATH - 1] = '\0';
    return &d->ent;
}

static inline void closedir(DIR* d) {
    if (d->hnd != INVALID_HANDLE_VALUE) FindClose(d->hnd);
    free(d);
}
#else
#include <dirent.h>
#endif
