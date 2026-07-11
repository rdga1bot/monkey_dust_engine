#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <monkey_dust/combat/damage_calc.h>
#include <monkey_dust/combat/hit_zones.h>
#include <entt/entt.hpp>
#include <cstdint>

struct Combat {
    WeaponStats weapon;
    ArmorStats  armor;
    float       zone_hp[(int)HitZone::COUNT];
    MdEntity target;
    float        last_attack_ms;
    float        death_timer;
    bool         is_dead;
    unsigned int rng_state;
    uint16_t     atk_voc_id = 0;   // audio ID played on successful hit (0 = silent)

    static Combat MakeBandit() {
        Combat c = {};
        c.weapon       = Weapons::Sword;
        c.armor        = Armors::Leather;
        c.is_dead      = false;
        c.last_attack_ms = 0.0f;
        c.rng_state    = 42u;
        for (int i = 0; i < (int)HitZone::COUNT; ++i)
            c.zone_hp[i] = ZONE_TABLE[i].cripple_hp * 2.0f;
        return c;
    }

    static Combat MakeTrader() {
        Combat c = {};
        c.weapon       = Weapons::Fists;
        c.armor        = Armors::None;
        c.is_dead      = false;
        c.last_attack_ms = 0.0f;
        c.rng_state    = 77u;
        for (int i = 0; i < (int)HitZone::COUNT; ++i)
            c.zone_hp[i] = ZONE_TABLE[i].cripple_hp * 2.0f;
        return c;
    }
};
