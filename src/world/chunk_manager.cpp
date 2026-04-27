#include <monkey_dust/world/chunk_manager.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/world/transform_soa.h>
#include <monkey_dust/tools/timing_system.h>
#include "raylib.h"
#include <cstdio>
#include <cstring>
#include <chrono>

static constexpr uint32_t CHUNK_MAGIC     = 0x4B554843u; // "CHUK"
static constexpr uint32_t FACTION_BANDITS = 1;
static constexpr uint32_t FACTION_TRADERS = 2;

// POD record used for binary chunk file serialization
struct ChunkEntityRecord {
    uint8_t  kind;
    float    x, z;
    uint32_t faction_id;
    float    hp_current, hp_max;
};

// ─── private helpers ──────────────────────────────────────────

int ChunkManager::FindActive(ChunkCoord coord) const {
    for (int i = 0; i < active_count_; ++i)
        if (active_[i].loaded && active_[i].coord == coord) return i;
    return -1;
}

bool ChunkManager::SaveChunk(int idx) {
    ChunkData& cd = active_[idx];
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", cd.filename);
    FILE* f = fopen(tmp, "wb");
    if (!f) {
        TraceLog(LOG_WARNING, "[ChunkManager] Cannot save %s", cd.filename);
        return false;
    }
    fwrite(&CHUNK_MAGIC, sizeof(CHUNK_MAGIC), 1, f);
    uint32_t count = (uint32_t)cd.entity_count;
    fwrite(&count, sizeof(count), 1, f);

    auto& reg = Registry::Get();
    for (int i = 0; i < cd.entity_count; ++i) {
        entt::entity e = cd.entities[i];
        if (!reg.valid(e)) continue;
        if (save_entity_fn_)
            save_entity_fn_(reg, e, f);
    }
    fclose(f);
    if (rename(tmp, cd.filename) != 0) {
        TraceLog(LOG_WARNING, "[ChunkManager] rename failed for %s", cd.filename);
        return false;
    }
    cd.dirty = false;
    return true;
}

// ─── pending ring buffer ──────────────────────────────────

bool ChunkManager::TryEnqueuePending(ChunkCoord c) {
    int h    = pending_head_.load(std::memory_order_relaxed);
    int next = (h + 1) % MAX_STAGING;
    if (next == pending_tail_.load(std::memory_order_acquire)) return false;
    pending_[h] = {c.x, c.z, true};
    pending_head_.store(next, std::memory_order_release);
    return true;
}

bool ChunkManager::TryDequeuePending(ChunkCoord& out) {
    int t = pending_tail_.load(std::memory_order_relaxed);
    if (t == pending_head_.load(std::memory_order_acquire)) return false;
    PendingCoord pc = pending_[t];
    pending_tail_.store((t + 1) % MAX_STAGING, std::memory_order_release);
    out = {pc.x, pc.z};
    return true;
}

// ─── IO worker ───────────────────────────────────────────

void ChunkManager::IOWorkerLoop() {
    while (io_running_.load(std::memory_order_relaxed)) {
        ChunkCoord coord;
        if (!TryDequeuePending(coord)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        int slot = -1;
        for (int i = 0; i < MAX_STAGING; ++i) {
            if (!staging_[i].ready.load(std::memory_order_acquire) &&
                staging_[i].consumed)
            {
                slot = i;
                break;
            }
        }
        if (slot < 0) continue;

        ChunkLoadStaging& stg = staging_[slot];
        stg.coord      = coord;
        stg.npc_count  = 0;
        stg.tree_count = 0;
        stg.consumed   = false;

        char fname[128];
        snprintf(fname, sizeof(fname), "%s/chunk_%d_%d.bin",
                 chunks_dir_, coord.x, coord.z);

        TIMING_BEGIN("ChunkIO");
        FILE* f = fopen(fname, "rb");
        if (f) {
            uint32_t magic = 0, count = 0;
            fread(&magic, sizeof(magic), 1, f);
            if (magic == CHUNK_MAGIC) {
                fread(&count, sizeof(count), 1, f);
                for (uint32_t i = 0; i < count; ++i) {
                    ChunkEntityRecord rec = {};
                    if (fread(&rec, sizeof(rec), 1, f) != 1) break;
                    if (rec.kind == 0) {
                        if (stg.tree_count < 256) {
                            StagedTree& t = stg.trees[stg.tree_count++];
                            t.x = rec.x; t.z = rec.z; t.scale = 1.0f;
                        }
                    } else {
                        if (stg.npc_count < 64) {
                            StagedNpc& n = stg.npcs[stg.npc_count++];
                            n.x          = rec.x;
                            n.z          = rec.z;
                            n.hp_current = rec.hp_current > 0.0f ? rec.hp_current : 100.0f;
                            n.hp_max     = rec.hp_max     > 0.0f ? rec.hp_max     : 100.0f;
                            n.faction_id = rec.faction_id ? rec.faction_id : FACTION_TRADERS;
                            n.weapon_type = 1;
                            n.armor_class = 1;
                            n.is_bandit   = (n.faction_id == FACTION_BANDITS) ? 1u : 0u;
                            n._pad        = 0;
                        }
                    }
                }
            }
            fclose(f);
        } else {
            // Procedural generation — LCG only, NO Registry calls from IO thread
            float base_x = (float)coord.x * CHUNK_SIZE;
            float base_z = (float)coord.z * CHUNK_SIZE;
            unsigned int rng = (unsigned int)((unsigned int)coord.x * 1664525u +
                                               (unsigned int)coord.z * 1013904223u + 12345u);
            auto lcg = [&]() { rng = rng * 1664525u + 1013904223u; return rng; };

            int tree_count = 5 + (int)(lcg() % 11);
            for (int i = 0; i < tree_count && stg.tree_count < 256; ++i) {
                StagedTree& t = stg.trees[stg.tree_count++];
                t.x = base_x + (float)(lcg() % 1000) / 1000.0f * CHUNK_SIZE;
                t.z = base_z + (float)(lcg() % 1000) / 1000.0f * CHUNK_SIZE;
                t.scale = 1.0f;
            }
            if ((((coord.x % 3) + 3) % 3) == 0) {
                int npc_count = 1 + (int)(lcg() % 3);
                for (int i = 0; i < npc_count && stg.npc_count < 64; ++i) {
                    StagedNpc& n = stg.npcs[stg.npc_count++];
                    n.x          = base_x + (float)(lcg() % 1000) / 1000.0f * CHUNK_SIZE;
                    n.z          = base_z + (float)(lcg() % 1000) / 1000.0f * CHUNK_SIZE;
                    n.hp_current = 100.0f;
                    n.hp_max     = 100.0f;
                    n.faction_id = (lcg() & 1u) ? FACTION_BANDITS : FACTION_TRADERS;
                    n.weapon_type = 1;
                    n.armor_class = 1;
                    n.is_bandit   = (n.faction_id == FACTION_BANDITS) ? 1u : 0u;
                    n._pad        = 0;
                }
            }
        }

        TIMING_END("ChunkIO");
        stg.ready.store(true, std::memory_order_release);
    }
}

// ─── Apply staged (main thread) ──────────────────────────

void ChunkManager::LoadChunkFromStaging(ChunkLoadStaging& stg) {
    if (active_count_ >= MAX_CHUNKS_ACTIVE) return;

    ChunkData& cd = active_[active_count_];
    memset(&cd, 0, sizeof(cd));
    cd.coord  = stg.coord;
    cd.loaded = true;
    cd.dirty  = false;
    snprintf(cd.filename, sizeof(cd.filename),
             "%s/chunk_%d_%d.bin", chunks_dir_, stg.coord.x, stg.coord.z);

    auto& reg = Registry::Get();

    for (int i = 0; i < stg.tree_count &&
         cd.entity_count < MAX_ENTITIES_PER_CHUNK; ++i)
    {
        entt::entity e = reg.create();
        if (spawn_tree_fn_) spawn_tree_fn_(reg, stg.trees[i]);
        cd.entities[cd.entity_count++] = e;
    }

    for (int i = 0; i < stg.npc_count &&
         cd.entity_count < MAX_ENTITIES_PER_CHUNK; ++i)
    {
        entt::entity e = reg.create();
        if (spawn_npc_fn_) spawn_npc_fn_(reg, stg.npcs[i]);
        cd.entities[cd.entity_count++] = e;
    }

    active_count_++;
    TraceLog(LOG_INFO, "[ChunkManager] Applied %d_%d (%d entities)",
             stg.coord.x, stg.coord.z, cd.entity_count);
}

void ChunkManager::ApplyStagedChunks() {
    for (int i = 0; i < MAX_STAGING; ++i) {
        if (!staging_[i].ready.load(std::memory_order_acquire)) continue;
        if (staging_[i].consumed) continue;
        if (active_count_ >= MAX_CHUNKS_ACTIVE) {
            staging_[i].consumed = true;
            staging_[i].ready.store(false, std::memory_order_release);
            continue;
        }
        if (FindActive(staging_[i].coord) >= 0) {
            staging_[i].consumed = true;
            staging_[i].ready.store(false, std::memory_order_release);
            continue;
        }
        LoadChunkFromStaging(staging_[i]);
        staging_[i].consumed = true;
        staging_[i].ready.store(false, std::memory_order_release);
    }
}

// ─── UnloadChunk ─────────────────────────────────────────

void ChunkManager::UnloadChunk(int idx) {
    ChunkData& cd = active_[idx];
    if (!cd.loaded) return;
    if (cd.dirty) SaveChunk(idx);

    auto& reg = Registry::Get();
    for (int i = 0; i < cd.entity_count; ++i) {
        if (!reg.valid(cd.entities[i])) continue;
        if (destroy_entity_fn_) destroy_entity_fn_(reg, cd.entities[i]);
        reg.destroy(cd.entities[i]);
    }

    TraceLog(LOG_INFO, "[ChunkManager] Unloaded %d_%d", cd.coord.x, cd.coord.z);
    active_[idx] = active_[--active_count_];
    active_[active_count_].loaded = false;
}

// ─── public ──────────────────────────────────────────────

void ChunkManager::Init(const char* chunks_dir) {
    strncpy(chunks_dir_, chunks_dir, sizeof(chunks_dir_) - 1);
    chunks_dir_[sizeof(chunks_dir_) - 1] = '\0';
    last_player_chunk_ = {9999, 9999};
    for (int i = 0; i < MAX_STAGING; ++i) {
        staging_[i].ready.store(false);
        staging_[i].consumed = true;
    }
    TraceLog(LOG_INFO, "[ChunkManager] Init dir=%s", chunks_dir_);
}

void ChunkManager::StartIOWorker() {
    io_running_.store(true);
    io_worker_ = std::thread([this] { IOWorkerLoop(); });
}

void ChunkManager::StopIOWorker() {
    io_running_.store(false);
    if (io_worker_.joinable()) io_worker_.join();
}

void ChunkManager::Update(float player_x, float player_z) {
    ApplyStagedChunks();

    ChunkCoord pc = WorldToChunk(player_x, player_z);
    if (pc == last_player_chunk_) return;
    last_player_chunk_ = pc;

    for (int dz = -CHUNK_LOAD_RADIUS; dz <= CHUNK_LOAD_RADIUS; ++dz) {
        for (int dx = -CHUNK_LOAD_RADIUS; dx <= CHUNK_LOAD_RADIUS; ++dx) {
            ChunkCoord c = { pc.x + dx, pc.z + dz };
            if (FindActive(c) < 0)
                TryEnqueuePending(c);
        }
    }

    for (int i = active_count_ - 1; i >= 0; --i) {
        ChunkCoord c = active_[i].coord;
        int dx = c.x - pc.x, dz = c.z - pc.z;
        if (dx < -CHUNK_LOAD_RADIUS || dx > CHUNK_LOAD_RADIUS ||
            dz < -CHUNK_LOAD_RADIUS || dz > CHUNK_LOAD_RADIUS)
        {
            UnloadChunk(i);
        }
    }
}

void ChunkManager::UnloadAll() {
    for (int i = active_count_ - 1; i >= 0; --i)
        UnloadChunk(i);
}
