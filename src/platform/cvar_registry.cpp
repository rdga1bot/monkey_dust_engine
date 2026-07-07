#include <monkey_dust/platform/cvar_registry.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

CVarRegistry::Entry* CVarRegistry::Find(const char* name) {
    for (int i = 0; i < count_; ++i)
        if (strcmp(entries_[i].name, name) == 0) return &entries_[i];
    return nullptr;
}

const CVarRegistry::Entry* CVarRegistry::Find(const char* name) const {
    for (int i = 0; i < count_; ++i)
        if (strcmp(entries_[i].name, name) == 0) return &entries_[i];
    return nullptr;
}

void CVarRegistry::Register(const char* name, CVarType type, void* target) {
    if (!target) return;
    Entry* e = Find(name);
    if (!e) {
        if (count_ >= MAX_CVARS) return;  // budget hit — silently drop, no malloc here
        e = &entries_[count_++];
    }
    snprintf(e->name, NAME_LEN, "%s", name);
    e->type = type;
    e->ptr  = target;
}

void CVarRegistry::RegisterFloat(const char* name, float* target) { Register(name, CVarType::Float, target); }
void CVarRegistry::RegisterInt(const char* name, int* target)     { Register(name, CVarType::Int,   target); }
void CVarRegistry::RegisterBool(const char* name, bool* target)   { Register(name, CVarType::Bool,  target); }

bool CVarRegistry::Set(const char* name, const char* value_str) {
    Entry* e = Find(name);
    if (!e || !e->ptr) return false;
    switch (e->type) {
    case CVarType::Float: *reinterpret_cast<float*>(e->ptr) = strtof(value_str, nullptr); break;
    case CVarType::Int:   *reinterpret_cast<int*>(e->ptr)   = atoi(value_str); break;
    case CVarType::Bool:  *reinterpret_cast<bool*>(e->ptr)  =
                               (strcmp(value_str, "1") == 0 || strcmp(value_str, "true") == 0); break;
    }
    return true;
}

void CVarRegistry::FormatEntry(const Entry& e, char* out, int out_len) {
    switch (e.type) {
    case CVarType::Float: snprintf(out, out_len, "%s = %g", e.name, *reinterpret_cast<float*>(e.ptr)); break;
    case CVarType::Int:   snprintf(out, out_len, "%s = %d", e.name, *reinterpret_cast<int*>(e.ptr)); break;
    case CVarType::Bool:  snprintf(out, out_len, "%s = %s", e.name, *reinterpret_cast<bool*>(e.ptr) ? "true" : "false"); break;
    }
}

bool CVarRegistry::Format(const char* name, char* out, int out_len) const {
    const Entry* e = Find(name);
    if (!e || !e->ptr) return false;
    FormatEntry(*e, out, out_len);
    return true;
}

bool CVarRegistry::FormatAt(int idx, char* out, int out_len) const {
    if (idx < 0 || idx >= count_) return false;
    FormatEntry(entries_[idx], out, out_len);
    return true;
}
