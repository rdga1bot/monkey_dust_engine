#include <monkey_dust/combat/damage_calc.h>
#include <monkey_dust/components/health.h>
#include <monkey_dust/world/world_transform.h>
#include <cmath>

void AoeApply(entt::registry& reg, const AoeHit& h) {
    if (h.radius <= 0.f || h.damage <= 0.f) return;

    const float r2 = h.radius * h.radius;
    const ArmorStats no_armor = Armors::None;

    auto view = reg.view<LimbHealth, WorldTransform>();
    for (auto [e, lh, tr] : view.each()) {
        float dx = tr.x - h.origin[0];
        float dz = tr.z - h.origin[2];
        float dist2 = dx * dx + dz * dz;
        if (dist2 >= r2) continue;

        float falloff = 1.f - sqrtf(dist2) / h.radius;
        WeaponStats wpn = { h.damage * falloff, h.type, h.radius, 0, 0.f };
        float dmg = CalcDamage(wpn, no_armor);

        static constexpr int TORSO = 1;
        lh.hp[TORSO] -= dmg;
        if (lh.hp[TORSO] < 0.f) lh.hp[TORSO] = 0.f;
        if (lh.hp[TORSO] <= 0.f) lh.flags[TORSO] |= LimbFlag::CRIPPLED;
        lh.UpdateIncap();
    }
}
