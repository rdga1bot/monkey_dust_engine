#pragma once
#include <monkey_dust/ecs/md_entity.h>

// PowerSystem — activates a power for a caster entity.
// Melee/ment: AoE damage within radius (ment at target pos, melee at caster pos).
// Ranged: spawns ProjectileComponent entity moving toward (tx, tz).
namespace md {

class PowerSystem {
public:
    static PowerSystem& Get() { static PowerSystem i; return i; }

    // Returns true if power exists and caster has a valid WorldTransform.
    bool Use(MdEntity caster, int power_id, float tx, float tz);

private:
    PowerSystem() = default;

    void DoMelee(MdEntity caster, float cx, float cz, float radius, float damage);
    void SpawnProjectile(MdEntity caster, float cx, float cz,
                         float tx, float tz, int power_id);
};

} // namespace md
