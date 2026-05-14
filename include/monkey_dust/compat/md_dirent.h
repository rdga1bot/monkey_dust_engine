#pragma once
// Windows-compatible shim for POSIX dirent.h + S_IS* stat macros (loader/startup code only).
#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// d_type constants (POSIX values)
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

// S_IS* macros — not provided by MSVC's sys/stat.h
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

struct dirent {
    char    d_name[MAX_PATH];
    uint8_t d_type;
};

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
    d->ent.d_type = (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &d->ent;
}

static inline void closedir(DIR* d) {
    if (d->hnd != INVALID_HANDLE_VALUE) FindClose(d->hnd);
    free(d);
}
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
