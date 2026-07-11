#include <monkey_dust/combat/limb_severance.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/platform/md_log.h>

bool LimbSeverance_TryApply(entt::registry& reg, MdEntity e,
                             int limb, float damage, DamageType type,
                             const float hit_dir[3]) {
    if (type != DamageType::Cut)          return false;
    if (damage < SEVER_THRESHOLD_CUT)     return false;
    if (limb < 0 || limb >= LIMB_COUNT)   return false;
    if (!reg.valid(e.Raw()))               return false;

    LimbHealth* lh = reg.try_get<LimbHealth>(e.Raw());
    if (!lh) return false;
    if (lh->IsSevered(limb)) return false; // already gone

    lh->hp[limb] -= damage;
    if (lh->hp[limb] > 0.f) {
        // Cripple but not sever
        lh->flags[limb] |= LimbFlag::CRIPPLED;
        lh->UpdateIncap();
        return false;
    }

    // Sever
    lh->Sever(limb);

    // Spawn detached limb prop at entity's world position
    const WorldTransform* tr = reg.try_get<WorldTransform>(e.Raw());
    if (tr) {
        MdEntity prop(reg.create());
        WorldTransform pt{};
        pt.x = tr->x; pt.y = tr->y; pt.z = tr->z;
        reg.emplace<WorldTransform>(prop.Raw(), pt);

        DetachedLimb dl{};
        dl.source   = e;
        dl.limb_idx = limb;
        dl.velocity[0] = hit_dir ? hit_dir[0] * 3.f : 0.f;
        dl.velocity[1] = 2.f;   // slight upward pop
        dl.velocity[2] = hit_dir ? hit_dir[2] * 3.f : 0.f;
        reg.emplace<DetachedLimb>(prop.Raw(), dl);
    }

    MD_LOG(MD_LOG_INFO, "LimbSeverance: entity limb %d severed (dmg=%.1f)", limb, damage);
    return true;
}

bool LimbSeverance_Equip(entt::registry& reg, MdEntity e,
                          int limb, float prosthetic_max_hp) {
    if (limb < 0 || limb >= LIMB_COUNT) return false;
    if (!reg.valid(e.Raw())) return false;

    LimbHealth* lh = reg.try_get<LimbHealth>(e.Raw());
    if (!lh) return false;

    lh->AttachProsthetic(limb, prosthetic_max_hp);
    return true;
}
