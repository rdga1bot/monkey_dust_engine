#pragma once
#include <cstdint>

// Live-tunable parameter registry (ARCHITECTURE_IDEAS.md #1, id Tech-style CVars).
// Binds a name to the ADDRESS of an existing float/int/bool variable — `Set()`
// writes straight through the pointer, so every call site that already reads
// that variable sees the new value immediately. No per-site glue code needed.
// Fixed array, no std::map (matches project convention; lookups only happen
// from console commands, never per-frame).
enum class CVarType : uint8_t { Float, Int, Bool };

class CVarRegistry {
public:
    static CVarRegistry& Get() { static CVarRegistry inst; return inst; }

    static constexpr int MAX_CVARS = 128;
    static constexpr int NAME_LEN  = 48;

    void RegisterFloat(const char* name, float* target);
    void RegisterInt(const char* name, int* target);
    void RegisterBool(const char* name, bool* target);

    // Parses value_str per the cvar's type and writes through its pointer.
    // Returns false if name is not registered.
    bool Set(const char* name, const char* value_str);

    // Formats "name = value" into out (out_len bytes). Returns false if not found.
    bool Format(const char* name, char* out, int out_len) const;

    int Count() const { return count_; }
    // idx in [0, Count()). Formats "name = value" into out.
    bool FormatAt(int idx, char* out, int out_len) const;

private:
    CVarRegistry() = default;

    struct Entry {
        char     name[NAME_LEN] = {};
        CVarType type           = CVarType::Float;
        void*    ptr            = nullptr;
    };

    Entry entries_[MAX_CVARS] = {};
    int    count_             = 0;

    Entry* Find(const char* name);
    const Entry* Find(const char* name) const;
    void Register(const char* name, CVarType type, void* target);
    static void FormatEntry(const Entry& e, char* out, int out_len);
};
