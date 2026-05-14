#pragma once
#include <cstdint>
#include <monkey_dust/platform/md_log.h>

// TypedParamRegistry — MD EntityInterface pattern:
// 7 separate find_parameter<T> specializations → one unified registry.
// Maps uint16_t param_id to a type tag + 4-byte value (covers int/float/bool/enum).
// MAX_PARAMS=32 slots; no heap allocation.
//
// Differs from AgentBlackboard (FNV-keyed, per-entity ECS component):
// TypedParamRegistry is a global prototype/schema registry — what parameters
// exist, their types, and default values (entity instances override via bb).

enum class ParamType : uint8_t {
    None   = 0,
    Int    = 1,
    Float  = 2,
    Bool   = 3,
    Enum8  = 4,   // uint8_t enum stored in low byte
};

struct ParamSchema {
    uint16_t  id;
    ParamType type;
    uint8_t   _pad;
    union {
        int32_t  as_int;
        float    as_float;
        bool     as_bool;
        uint8_t  as_enum8;
        uint32_t raw;
    } default_value;
    const char* name;  // debug only; not used for lookup
};
static_assert(sizeof(ParamSchema) == 16);

class TypedParamRegistry {
public:
    static constexpr uint8_t MAX_PARAMS = 32;

    static TypedParamRegistry& Get() noexcept {
        static TypedParamRegistry inst;
        return inst;
    }

    // Register a schema. Returns false if full or id already registered.
    bool RegisterInt  (uint16_t id, int32_t  def, const char* name = "") noexcept {
        return register_schema(id, ParamType::Int,   name, def);
    }
    bool RegisterFloat(uint16_t id, float    def, const char* name = "") noexcept {
        ParamSchema s{}; s.id = id; s.type = ParamType::Float;
        s.default_value.as_float = def; s.name = name;
        return push(s);
    }
    bool RegisterBool (uint16_t id, bool     def, const char* name = "") noexcept {
        ParamSchema s{}; s.id = id; s.type = ParamType::Bool;
        s.default_value.as_bool = def; s.name = name;
        return push(s);
    }
    bool RegisterEnum8(uint16_t id, uint8_t  def, const char* name = "") noexcept {
        ParamSchema s{}; s.id = id; s.type = ParamType::Enum8;
        s.default_value.as_enum8 = def; s.name = name;
        return push(s);
    }

    const ParamSchema* Find(uint16_t id) const noexcept {
        for (uint8_t i = 0; i < count_; ++i)
            if (schemas_[i].id == id) return &schemas_[i];
        return nullptr;
    }

    // Typed accessors — return default if not registered or wrong type.
    int32_t GetInt  (uint16_t id) const noexcept {
        auto* s = Find(id);
        return (s && s->type == ParamType::Int)   ? s->default_value.as_int   : 0;
    }
    float   GetFloat(uint16_t id) const noexcept {
        auto* s = Find(id);
        return (s && s->type == ParamType::Float) ? s->default_value.as_float : 0.f;
    }
    bool    GetBool (uint16_t id) const noexcept {
        auto* s = Find(id);
        return (s && s->type == ParamType::Bool)  ? s->default_value.as_bool  : false;
    }
    uint8_t GetEnum8(uint16_t id) const noexcept {
        auto* s = Find(id);
        return (s && s->type == ParamType::Enum8) ? s->default_value.as_enum8 : 0u;
    }

    // Type-safe override (mutates default; use per-entity bb for instance values).
    bool SetInt  (uint16_t id, int32_t  v) noexcept { return set_raw(id, ParamType::Int,   v); }
    bool SetFloat(uint16_t id, float    v) noexcept {
        auto* s = Find(id);
        if (!s || s->type != ParamType::Float) return false;
        const_cast<ParamSchema*>(s)->default_value.as_float = v;
        return true;
    }
    bool SetBool (uint16_t id, bool     v) noexcept { return set_raw(id, ParamType::Bool,  (int32_t)v); }
    bool SetEnum8(uint16_t id, uint8_t  v) noexcept { return set_raw(id, ParamType::Enum8, (int32_t)v); }

    void  Clear()  noexcept { count_ = 0; }
    uint8_t count() const noexcept { return count_; }

private:
    ParamSchema schemas_[MAX_PARAMS] = {};
    uint8_t     count_ = 0;

    bool push(const ParamSchema& s) noexcept {
        if (Find(s.id)) {
            MD_LOG(MD_LOG_WARNING, "TypedParamRegistry: id %u already registered", s.id);
            return false;
        }
        if (count_ >= MAX_PARAMS) {
            MD_LOG(MD_LOG_WARNING, "TypedParamRegistry: full (%d params)", MAX_PARAMS);
            return false;
        }
        schemas_[count_++] = s;
        return true;
    }
    bool register_schema(uint16_t id, ParamType t, const char* name, int32_t def) noexcept {
        ParamSchema s{}; s.id = id; s.type = t;
        s.default_value.as_int = def; s.name = name;
        return push(s);
    }
    bool set_raw(uint16_t id, ParamType t, int32_t v) noexcept {
        auto* s = const_cast<ParamSchema*>(Find(id));
        if (!s || s->type != t) return false;
        s->default_value.as_int = v;
        return true;
    }
};
