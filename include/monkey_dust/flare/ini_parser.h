#pragma once

namespace md::flare {

struct IniLine {
    const char* section;   // points into working buffer; "" = global scope
    const char* key;
    const char* value;     // raw string after '=', trimmed
    int         line_num;
};

// Callback-based Flare INI (.txt) parser. Allocates one static 64 KB read
// buffer — safe for files ≤ 64 KB (all known Flare data files qualify).
// Points section/key/value directly into that buffer (no heap allocs).
// section_filter: nullptr = all sections; otherwise only matching sections.
// Returns false on file-open failure.
using IniLineCallback = void(*)(const IniLine& line, void* user_data);

bool ParseIniFile(const char*     file_path,
                  IniLineCallback on_line,
                  void*           user_data,
                  const char*     section_filter = nullptr);

// Split "10,20,30" → out[0..n-1]. Returns count parsed (≤ max).
int ParseIntList  (const char* value, int*   out, int max);
int ParseFloatList(const char* value, float* out, int max);

// Safe bounded strcpy, always null-terminates.
void CopyString(char* dest, const char* src, int dest_size);

} // namespace md::flare
