#include <monkey_dust/world/offscreen_npc_db.h>
#include <monkey_dust/components/health.h>
#include <monkey_dust/components/npc_needs.h>
#include <monkey_dust/platform/md_log.h>
#include <cmath>
#include <cstring>

// LCG for patrol drift — no malloc, no std::rand.
static uint32_t s_rng = 0xDEADBEEFu;
static float s_randf() {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)((s_rng >> 8) & 0xFFFFFF) / (float)0xFFFFFF;
}

void OffscreenNpcDatabase::Tick(float /*dt*/) noexcept {
    for (int i = 0; i < count_; ++i) {
        OffscreenNpcState& e = entries_[i];

        // Hunger / fatigue accumulation (clamped to 255)
        uint32_t h = (uint32_t)e.hunger  + (uint32_t)HUNGER_RATE;
        uint32_t f = (uint32_t)e.fatigue + (uint32_t)FATIGUE_RATE;
        e.hunger  = (uint8_t)(h  < 255u ? h  : 255u);
        e.fatigue = (uint8_t)(f  < 255u ? f  : 255u);

        // Patrol: drift position randomly within PATROL_DRIFT_M per tick
        if (e.task_type == OffscreenTask::Patrol) {
            e.position[0] += (s_randf() - 0.5f) * PATROL_DRIFT_M;
            e.position[2] += (s_randf() - 0.5f) * PATROL_DRIFT_M;
        }

        // Rest: reduce fatigue
        if (e.task_type == OffscreenTask::Rest && e.fatigue > 0) {
            e.fatigue = (uint8_t)(e.fatigue > 4u ? e.fatigue - 4u : 0u);
        }
    }
}

bool OffscreenNpcDatabase::Capture(entt::entity e, entt::registry& reg,
                                   uint16_t zone_id) {
    if (count_ >= MAX_OFFSCREEN_NPCS) {
        MD_LOG(MD_LOG_WARNING, "OffscreenNpcDb: full (%d slots)", MAX_OFFSCREEN_NPCS);
        return false;
    }
    if (!reg.valid(e)) return false;

    OffscreenNpcState& slot = entries_[count_];
    memset(&slot, 0, sizeof(slot));
    slot.zone_id    = zone_id;
    slot.entity_uid = uid_counter_++;
    slot.task_type  = OffscreenTask::Patrol;

    const WorldTransform* tr = reg.try_get<WorldTransform>(e);
    if (tr) {
        slot.position[0] = tr->x;
        slot.position[1] = tr->y;
        slot.position[2] = tr->z;
    }

    const LimbHealth* lh = reg.try_get<LimbHealth>(e);
    if (lh) {
        // Pack HP as uint16 (hp / max * 65535), avoid div-by-zero
        auto pack = [](float hp, float mx) -> uint16_t {
            if (mx <= 0.f) return 0;
            float f = hp / mx;
            if (f < 0.f) f = 0.f;
            if (f > 1.f) f = 1.f;
            return (uint16_t)(f * 65535.f);
        };
        slot.hp_head  = pack(lh->hp[0], lh->max[0]);
        slot.hp_torso = pack(lh->hp[1], lh->max[1]);
        slot.hp_larm  = pack(lh->hp[2], lh->max[2]);
        slot.hp_rarm  = pack(lh->hp[3], lh->max[3]);
        slot.hp_lleg  = pack(lh->hp[4], lh->max[4]);
        slot.hp_rleg  = pack(lh->hp[5], lh->max[5]);
    } else {
        slot.hp_head = slot.hp_torso = slot.hp_larm =
        slot.hp_rarm = slot.hp_lleg = slot.hp_rleg = 0xFFFFu; // full HP
    }

    const NpcNeeds* nd = reg.try_get<NpcNeeds>(e);
    if (nd) {
        slot.hunger  = (uint8_t)(nd->hunger  * 255.f);
        slot.fatigue = (uint8_t)(nd->fatigue * 255.f);
    }

    ++count_;
    return true;
}

void OffscreenNpcDatabase::Spawn(entt::registry& reg,
                                 float player_x, float player_z,
                                 float spawn_radius_m) {
    const float r2 = spawn_radius_m * spawn_radius_m;
    for (int i = count_ - 1; i >= 0; --i) {
        OffscreenNpcState& s = entries_[i];
        float dx = s.position[0] - player_x;
        float dz = s.position[2] - player_z;
        if (dx * dx + dz * dz > r2) continue;

        entt::entity ne = reg.create();

        WorldTransform tr{};
        tr.x = s.position[0]; tr.y = s.position[1]; tr.z = s.position[2];
        reg.emplace<WorldTransform>(ne, tr);

        auto& lh = reg.emplace<LimbHealth>(ne, LimbHealth::Make(60.f));
        auto unpack = [](uint16_t packed, float mx) -> float {
            return (float)packed / 65535.f * mx;
        };
        lh.hp[0] = unpack(s.hp_head,  lh.max[0]);
        lh.hp[1] = unpack(s.hp_torso, lh.max[1]);
        lh.hp[2] = unpack(s.hp_larm,  lh.max[2]);
        lh.hp[3] = unpack(s.hp_rarm,  lh.max[3]);
        lh.hp[4] = unpack(s.hp_lleg,  lh.max[4]);
        lh.hp[5] = unpack(s.hp_rleg,  lh.max[5]);
        lh.UpdateIncap();

        auto& nd = reg.emplace<NpcNeeds>(ne);
        nd.hunger  = s.hunger  / 255.f;
        nd.fatigue = s.fatigue / 255.f;

        Remove(i);
    }
}

void OffscreenNpcDatabase::PurgeZone(uint16_t zone_id) noexcept {
    for (int i = count_ - 1; i >= 0; --i) {
        if (entries_[i].zone_id == zone_id) Remove(i);
    }
}
