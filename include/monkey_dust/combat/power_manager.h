#pragma once
#include <monkey_dust/combat/power_def.h>

class PowerManager {
public:
    static PowerManager& Get() { static PowerManager i; return i; }
    bool LoadFromJson(const char* path);
    const PowerDef* Find(int id) const;
    const PowerDef* GetAt(int idx) const { return (idx>=0&&idx<count_)?&powers_[idx]:nullptr; }
    int Count() const { return count_; }
private:
    PowerDef powers_[256];
    int      count_ = 0;
    PowerManager() = default;
};
