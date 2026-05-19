#pragma once
// SquadController / SquadSystem (VBfA-AI5) — group AI LOD.
//
// Source: Viking Battle for Asgard squad system (tasks.bin AT_SQUAD_ACTIVITY_* states,
// 2026-05-19 RE). VBfA ran 1 pathfind per squad (not per NPC), reducing AI decisions
// from O(n) to O(n/squad_size) — the key to 1000-unit battles.
//
// Architecture:
//   SquadController is a component on a dedicated "squad entity" (not on member NPCs).
//   SquadSystem::Update() iterates squad entities (cheap: ~n/16 iterations) and
//   computes 1 target position per squad per THINK_INTERVAL_MS.
//   Members receive the target via CrowdSystem::SetTarget (ORCA steers them there)
//   and via blackboard (squad_tx/squad_tz) for BT nodes.
//
// Activity ladder (dist = squad_home → player, not per-NPC):
//   > 60m : PatrolRagged — random wander, no pathfinding, cheapest
//   40-60m : Patrol      — move toward home position
//   20-40m : AttackMove  — move toward player (1 ORCA target set per squad)
//   < 20m  : Fight       — BT takes over individual combat; squad only updates BB
//
// Integration in logic_tick.cpp:
//   SquadSystem::Update(dt_ms, player_x, player_z)  — before ai.Update()
//
// Creating squads from world_init.cpp:
//   SquadSystem::CreateSquad(members, count, home_x, home_z, faction_id)

#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/components/nav_agent.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/nav/crowd_system.h>
#include <monkey_dust/ai/fnv.h>
#include <entt/entt.hpp>
#include <cmath>
#include <cstring>

// ── Squad activity states (VBfA AT_SQUAD_ACTIVITY_*) ─────────────────────────
enum class SquadActivity : uint8_t {
    Idle         = 0,   // no target, members stand still
    PatrolRagged = 1,   // random wander within radius, NO pathfinding
    Patrol       = 2,   // return toward home position
    AttackMove   = 3,   // advance on player position
    Fight        = 4,   // full combat — BT drives individuals, squad updates BB only
};

// ── SquadController component (lives on squad entity, not on member NPCs) ─────
struct SquadController {
    static constexpr int   MAX_MEMBERS       = 16;
    static constexpr float THINK_INTERVAL_MS = 2000.f;
    static constexpr float WANDER_INTERVAL_MS = 4000.f; // PatrolRagged retarget

    entt::entity  members[MAX_MEMBERS] = {};
    int           member_count         = 0;

    SquadActivity activity             = SquadActivity::PatrolRagged;
    float         home_x              = 0.f;
    float         home_z              = 0.f;
    float         target_x            = 0.f;
    float         target_z            = 0.f;
    float         patrol_radius       = 40.f;
    uint8_t       faction_id          = 0;

    float         think_accum_ms      = 0.f;
    float         wander_accum_ms     = WANDER_INTERVAL_MS; // fire immediately
    uint32_t      rng_state           = 0xDEADBEEFu;
};

// ── SquadSystem ───────────────────────────────────────────────────────────────
class SquadSystem {
public:
    // Blackboard keys (compile-time FNV-1a) for member BT nodes.
    static constexpr uint32_t kSquadTx = md::fnv1a("squad_tx");
    static constexpr uint32_t kSquadTz = md::fnv1a("squad_tz");

    // Create a squad entity and return its handle.
    // members[count]: pre-spawned NPC entities to include.
    // home_x/z:       spawn-centre used for activity distance checks.
    static entt::entity CreateSquad(const entt::entity* members, int count,
                                    float home_x, float home_z,
                                    uint8_t faction_id,
                                    float patrol_radius = 40.f) {
        auto& reg        = Registry::Get();
        entt::entity sq  = reg.create();
        auto& sc         = reg.emplace<SquadController>(sq);
        sc.home_x        = home_x;
        sc.home_z        = home_z;
        sc.target_x      = home_x;
        sc.target_z      = home_z;
        sc.faction_id    = faction_id;
        sc.patrol_radius = patrol_radius;
        // LCG seed from position so different squads wander differently
        sc.rng_state     = static_cast<uint32_t>(home_x * 73856093.f)
                         ^ static_cast<uint32_t>(home_z * 19349663.f);
        int n = count < SquadController::MAX_MEMBERS ? count : SquadController::MAX_MEMBERS;
        for (int i = 0; i < n; ++i) sc.members[i] = members[i];
        sc.member_count = n;
        return sq;
    }

    // Call once per logic tick (100ms) BEFORE ai.Update().
    // dt_ms:    LOGIC_TICK_S * 1000
    // player_x/z: current player world position
    static void Update(float dt_ms, float player_x, float player_z) {
        auto& reg = Registry::Get();
        reg.view<SquadController>().each([&](entt::entity, SquadController& sc) {
            if (sc.member_count <= 0) return;

            // ── Squad thinks every THINK_INTERVAL_MS ─────────────────────────
            sc.think_accum_ms  += dt_ms;
            sc.wander_accum_ms += dt_ms;
            if (sc.think_accum_ms < SquadController::THINK_INTERVAL_MS) return;
            sc.think_accum_ms = 0.f;

            // ── Activity ladder: dist(home → player), ONE check per squad ────
            float dx   = sc.home_x - player_x;
            float dz   = sc.home_z - player_z;
            float dist = sqrtf(dx * dx + dz * dz);

            SquadActivity new_act;
            if      (dist > 60.f) new_act = SquadActivity::PatrolRagged;
            else if (dist > 40.f) new_act = SquadActivity::Patrol;
            else if (dist > 20.f) new_act = SquadActivity::AttackMove;
            else                  new_act = SquadActivity::Fight;
            sc.activity = new_act;

            // ── Compute squad target ─────────────────────────────────────────
            switch (sc.activity) {
                case SquadActivity::Idle:
                    break;

                case SquadActivity::PatrolRagged:
                    // New random wander point every WANDER_INTERVAL_MS (VBfA "ragged patrol").
                    // NO pathfinding — just pick a point in radius, ORCA steers there.
                    if (sc.wander_accum_ms >= SquadController::WANDER_INTERVAL_MS) {
                        sc.wander_accum_ms = 0.f;
                        sc.rng_state = sc.rng_state * 1664525u + 1013904223u;
                        float ang   = (sc.rng_state & 0xFFFFu) * (6.2832f / 65536.f);
                        float r     = sc.patrol_radius * 0.45f;
                        sc.target_x = sc.home_x + cosf(ang) * r;
                        sc.target_z = sc.home_z + sinf(ang) * r;
                    }
                    break;

                case SquadActivity::Patrol:
                    sc.target_x = sc.home_x;
                    sc.target_z = sc.home_z;
                    break;

                case SquadActivity::AttackMove:
                case SquadActivity::Fight:
                    sc.target_x = player_x;
                    sc.target_z = player_z;
                    break;
            }

            // ── Broadcast target to all members ──────────────────────────────
            BroadcastTarget(sc, reg);
        });
    }

private:
    static void BroadcastTarget(SquadController& sc, entt::registry& reg) {
        const float tx = sc.target_x, tz = sc.target_z;
        const bool  is_fight = (sc.activity == SquadActivity::Fight);

        for (int i = 0; i < sc.member_count; ++i) {
            entt::entity m = sc.members[i];
            if (!reg.valid(m)) continue;

            // Update blackboard so BT nodes (actMoveToTarget etc.) can read squad goal
            auto* bb = reg.try_get<AgentBlackboard>(m);
            if (bb) {
                bb_set_float(*bb, kSquadTx, tx);
                bb_set_float(*bb, kSquadTz, tz);
            }

            // In Fight mode: BT drives individual movement; skip CrowdSystem redirect
            if (is_fight) continue;

            // Non-Fight: SquadSystem drives movement via ORCA (1 target per squad)
            auto* nav = reg.try_get<NavAgent>(m);
            if (!nav) continue;
            nav->target_x = tx;
            nav->target_z = tz;
            nav->is_moving = (sc.activity != SquadActivity::Idle);

            if (nav->crowd_idx >= 0 && CrowdSystem::Get().IsReady())
                CrowdSystem::Get().SetTarget(nav->crowd_idx, tx, tz);
        }
    }
};
