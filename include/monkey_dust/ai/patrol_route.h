#pragma once
// PatrolRoute — cyclic waypoint list for patrol NPCs.
// Attach as ECS component to named NPCs that patrol a fixed route.
// Anonymous NPCs use wander BT instead (no PatrolRoute component).
//
// Kenshi RE: patrol NPCs follow a list of waypoints with wait times;
// 'patrol approaches towns' flag in faction data controls out-of-zone roaming.
//
// Usage:
//   auto& pr = reg.emplace<PatrolRoute>(e);
//   pr.Add(10.f, 20.f, 5.f);
//   pr.Add(30.f, 10.f, 3.f);
//
// BT leaf actPatrolNext:
//   Vec3 wp = route.NextWaypoint();
//   CrowdSystem::SetTarget(e, wp);
//   route.StartWait(route.CurrentWait());  // set wait_timer_s after arrival

static constexpr int   PATROL_MAX_WAYPOINTS = 8;
static constexpr float PATROL_WAIT_MIN_S    = 3.f;
static constexpr float PATROL_WAIT_MAX_S    = 10.f;

struct PatrolRoute {
    float   wp_x[PATROL_MAX_WAYPOINTS];   // world-space X
    float   wp_z[PATROL_MAX_WAYPOINTS];   // world-space Z (Y=terrain)
    float   wait_time[PATROL_MAX_WAYPOINTS]; // seconds to wait at each point
    int     count      = 0;
    int     current    = 0;    // index of next waypoint
    float   wait_timer = 0.f;  // countdown; NPC stands while > 0

    bool IsEmpty()   const noexcept { return count == 0; }
    bool IsWaiting() const noexcept { return wait_timer > 0.f; }

    float NextX() const noexcept { return count > 0 ? wp_x[current] : 0.f; }
    float NextZ() const noexcept { return count > 0 ? wp_z[current] : 0.f; }
    float CurrentWait() const noexcept { return count > 0 ? wait_time[current] : 0.f; }

    void Advance() noexcept {
        if (count == 0) return;
        current = (current + 1) % count;
    }

    void StartWait(float seconds) noexcept { wait_timer = seconds; }

    // Returns true when wait is done and NPC may move.
    bool TickWait(float dt) noexcept {
        if (wait_timer <= 0.f) return true;
        wait_timer -= dt;
        return wait_timer <= 0.f;
    }

    // Append a waypoint (noop if full).
    void Add(float x, float z, float wait_s = PATROL_WAIT_MIN_S) noexcept {
        if (count >= PATROL_MAX_WAYPOINTS) return;
        wp_x[count]      = x;
        wp_z[count]      = z;
        wait_time[count] = wait_s;
        ++count;
    }
};
