#include <monkey_dust/world/offscreen_npc_db.h>
#include <monkey_dust/components/health.h>
#include <monkey_dust/components/npc_needs.h>
#include <monkey_dust/platform/md_log.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#ifndef _WIN32
#  include <dirent.h>
#endif

// LCG for patrol drift — no malloc, no std::rand.
static uint32_t s_rng = 0xDEADBEEFu;
static float s_randf() {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)((s_rng >> 8) & 0xFFFFFF) / (float)0xFFFFFF;
}

void OffscreenNpcDatabase::Tick(float /*dt*/, float game_hours) noexcept {
    const bool is_night = (game_hours >= 18.f || game_hours < 6.f);

    for (int i = 0; i < count_; ++i) {
        OffscreenNpcState& e = entries_[i];

        // pikkoserver SimFidelity: skip or reduce simulation based on zone distance.
        const SimFidelity fid = GetZoneFidelity(e.zone_id);
        if (fid == SimFidelity::Dormant) continue;  // frozen — no simulation

        if (is_night) {
            e.fatigue = (uint8_t)(e.fatigue > 2u ? e.fatigue - 2u : 0u);
            uint32_t h = (uint32_t)e.hunger + 1u;
            e.hunger  = (uint8_t)(h < 255u ? h : 255u);
            e.task_type = OffscreenTask::Rest;
        } else {
            uint32_t h = (uint32_t)e.hunger  + (uint32_t)HUNGER_RATE;
            uint32_t f = (uint32_t)e.fatigue + (uint32_t)FATIGUE_RATE;
            e.hunger  = (uint8_t)(h < 255u ? h : 255u);
            e.fatigue = (uint8_t)(f < 255u ? f : 255u);
            if (e.task_type == OffscreenTask::Rest)
                e.task_type = OffscreenTask::Patrol;
        }

        // Low fidelity: no positional simulation
        if (fid == SimFidelity::Low) continue;

        if (e.task_type == OffscreenTask::Patrol) {
            e.position[0] += (s_randf() - 0.5f) * PATROL_DRIFT_M;
            e.position[2] += (s_randf() - 0.5f) * PATROL_DRIFT_M;
        }
    }
}

void OffscreenNpcDatabase::SetZoneFidelity(uint16_t zone_id,
                                            SimFidelity fidelity) noexcept {
    for (int i = 0; i < fidelity_count_; ++i) {
        if (fidelity_[i].zone_id == zone_id) {
            fidelity_[i].fidelity = fidelity;
            return;
        }
    }
    if (fidelity_count_ < MAX_FIDELITY_ZONES)
        fidelity_[fidelity_count_++] = { zone_id, fidelity };
}

SimFidelity OffscreenNpcDatabase::GetZoneFidelity(uint16_t zone_id) const noexcept {
    for (int i = 0; i < fidelity_count_; ++i)
        if (fidelity_[i].zone_id == zone_id) return fidelity_[i].fidelity;
    return SimFidelity::High;  // default
}

// ── Zone NPC persistence ──────────────────────────────────────────────────────
// File format: 4-byte magic "ONPC" + uint32 count + OffscreenNpcState[count]
static constexpr uint32_t ONPC_MAGIC = 0x43504E4Fu;  // "ONPC"

static void zone_path(char* buf, size_t sz, const char* dir, uint16_t zone_id) {
    snprintf(buf, sz, "%s/zone_%04u.onpc", dir, (unsigned)zone_id);
}

bool OffscreenNpcDatabase::SaveZone(uint16_t zone_id, const char* saves_dir) noexcept {
    // Collect entries for this zone
    OffscreenNpcState tmp[512];
    int n = 0;
    for (int i = 0; i < count_ && n < 512; ++i)
        if (entries_[i].zone_id == zone_id) tmp[n++] = entries_[i];

    if (n == 0) return true;  // nothing to save

    char path[256];
    zone_path(path, sizeof(path), saves_dir, zone_id);
    FILE* f = fopen(path, "wb");
    if (!f) { MD_LOG(MD_LOG_WARNING, "OffscreenNpcDb: cannot write %s", path); return false; }

    uint32_t magic = ONPC_MAGIC, cnt = (uint32_t)n;
    fwrite(&magic, 4, 1, f);
    fwrite(&cnt,   4, 1, f);
    fwrite(tmp, sizeof(OffscreenNpcState), (size_t)n, f);
    fclose(f);
    MD_LOG(MD_LOG_INFO, "OffscreenNpcDb: saved %d NPCs for zone %u", n, (unsigned)zone_id);
    return true;
}

int OffscreenNpcDatabase::LoadZone(uint16_t zone_id, const char* saves_dir) noexcept {
    char path[256];
    zone_path(path, sizeof(path), saves_dir, zone_id);
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    uint32_t magic = 0, cnt = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != ONPC_MAGIC ||
        fread(&cnt,   4, 1, f) != 1 || cnt == 0 || cnt > 512) {
        fclose(f); return 0;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < cnt; ++i) {
        OffscreenNpcState s{};
        if (fread(&s, sizeof(s), 1, f) != 1) break;
        if (count_ >= MAX_OFFSCREEN_NPCS) break;
        // Skip duplicates: if entity_uid already present, discard
        bool dup = false;
        for (int j = 0; j < count_ && !dup; ++j)
            dup = (entries_[j].entity_uid == s.entity_uid && s.entity_uid != 0);
        if (!dup) { entries_[count_++] = s; ++loaded; }
    }
    fclose(f);
    if (loaded > 0)
        MD_LOG(MD_LOG_INFO, "OffscreenNpcDb: restored %d NPCs for zone %u", loaded, (unsigned)zone_id);
    return loaded;
}

int OffscreenNpcDatabase::LoadAllZones(const char* saves_dir) noexcept {
    int total = 0;
#ifndef _WIN32
    DIR* d = opendir(saves_dir);
    if (!d) return 0;
    struct dirent* ent;
    while ((ent = readdir(d))) {
        unsigned zone_id = 0;
        if (sscanf(ent->d_name, "zone_%u.onpc", &zone_id) == 1)
            total += LoadZone((uint16_t)zone_id, saves_dir);
    }
    closedir(d);
#endif
    if (total > 0)
        MD_LOG(MD_LOG_INFO, "OffscreenNpcDb: LoadAllZones restored %d total NPCs", total);
    return total;
}

bool OffscreenNpcDatabase::Capture(entt::entity e, MdRegistry& reg,
                                   uint16_t zone_id) {
    if (count_ >= MAX_OFFSCREEN_NPCS) {
        MD_LOG(MD_LOG_WARNING, "OffscreenNpcDb: full (%d slots)", MAX_OFFSCREEN_NPCS);
        return false;
    }
    if (!reg.Valid(e)) return false;

    OffscreenNpcState& slot = entries_[count_];
    memset(&slot, 0, sizeof(slot));
    slot.zone_id    = zone_id;
    slot.entity_uid = uid_counter_++;
    slot.task_type  = OffscreenTask::Patrol;

    const WorldTransform* tr = reg.TryGet<WorldTransform>(e);
    if (tr) {
        slot.position[0] = tr->x;
        slot.position[1] = tr->y;
        slot.position[2] = tr->z;
    }

    const LimbHealth* lh = reg.TryGet<LimbHealth>(e);
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

    const NpcNeeds* nd = reg.TryGet<NpcNeeds>(e);
    if (nd) {
        slot.hunger  = (uint8_t)(nd->hunger  * 255.f);
        slot.fatigue = (uint8_t)(nd->fatigue * 255.f);
    }

    ++count_;
    return true;
}

void OffscreenNpcDatabase::Spawn(MdRegistry& reg,
                                 float player_x, float player_z,
                                 float spawn_radius_m) {
    const float r2 = spawn_radius_m * spawn_radius_m;
    for (int i = count_ - 1; i >= 0; --i) {
        OffscreenNpcState& s = entries_[i];
        float dx = s.position[0] - player_x;
        float dz = s.position[2] - player_z;
        if (dx * dx + dz * dz > r2) continue;

        entt::entity ne = reg.Create();

        WorldTransform tr{};
        tr.x = s.position[0]; tr.y = s.position[1]; tr.z = s.position[2];
        reg.Emplace<WorldTransform>(ne, tr);

        auto& lh = reg.Emplace<LimbHealth>(ne, LimbHealth::Make(60.f));
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

        auto& nd = reg.Emplace<NpcNeeds>(ne);
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
