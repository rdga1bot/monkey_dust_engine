#include <monkey_dust/flare/ini_parser.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace md::flare {

// ── helpers ──────────────────────────────────────────────────────────────────

static const char* SkipWs(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static char* TrimRight(char* p) {
    char* end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        --end;
    *end = '\0';
    return p;
}

// ── public API ───────────────────────────────────────────────────────────────

bool ParseIniFile(const char*     file_path,
                  IniLineCallback on_line,
                  void*           user_data,
                  const char*     section_filter)
{
    static char buf[65536];
    FILE* f = fopen(file_path, "rb");
    if (!f) return false;

    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    // Section state persists across lines.
    static char section_buf[128];
    section_buf[0] = '\0';

    IniLine line_out;
    line_out.section = section_buf;

    char* p   = buf;
    int   num = 0;

    while (*p) {
        ++num;
        // Find end of line.
        char* eol = p;
        while (*eol && *eol != '\n' && *eol != '\r') ++eol;
        char saved = *eol;
        *eol = '\0';

        char* lp = (char*)SkipWs(p);

        // Skip empty lines and comments.
        if (*lp == '\0' || *lp == '#' || *lp == ';') {
            goto next;
        }

        // Section header [name]
        if (*lp == '[') {
            char* close = strchr(lp + 1, ']');
            if (close) {
                int len = (int)(close - (lp + 1));
                if (len < (int)sizeof(section_buf) - 1) {
                    memcpy(section_buf, lp + 1, (size_t)len);
                    section_buf[len] = '\0';
                }
            }
            goto next;
        }

        // Skip if section filter active and doesn't match.
        if (section_filter && strcmp(section_buf, section_filter) != 0) {
            goto next;
        }

        // key=value pair
        {
            char* eq = strchr(lp, '=');
            if (!eq) goto next;

            *eq = '\0';
            char* key   = TrimRight(lp);
            char* value = (char*)SkipWs(eq + 1);
            TrimRight(value);

            line_out.section  = section_buf;
            line_out.key      = key;
            line_out.value    = value;
            line_out.line_num = num;
            on_line(line_out, user_data);
        }

next:
        *eol = saved;
        p = eol;
        if (*p == '\r') ++p;
        if (*p == '\n') ++p;
    }

    return true;
}

int ParseIntList(const char* value, int* out, int max) {
    int count = 0;
    const char* p = value;
    while (*p && count < max) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        out[count++] = (int)strtol(p, (char**)&p, 10);
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ',') ++p;
    }
    return count;
}

int ParseFloatList(const char* value, float* out, int max) {
    int count = 0;
    const char* p = value;
    while (*p && count < max) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        out[count++] = strtof(p, (char**)&p);
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ',') ++p;
    }
    return count;
}

void CopyString(char* dest, const char* src, int dest_size) {
    if (!dest || dest_size <= 0) return;
    strncpy(dest, src ? src : "", (size_t)(dest_size - 1));
    dest[dest_size - 1] = '\0';
}

} // namespace md::flare
