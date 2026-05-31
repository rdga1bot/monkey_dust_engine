#pragma once
#include <cmath>
#include <cstdint>

// ── VerletChain (V-1 VBfA pattern) ───────────────────────────────────────────
// VBfA lines 7802/46412/78758/225075: 24 Verlet callsites.
// "Verlet num loops: %d" — configurable iteration count.
// "Verlet Move Bone: %d" — bone index driven by chain endpoint.
//
// Secondary bone physics — cloth, banners, hair, chains.
// Independent from main character skeleton (separate update budget).
// NOT ragdoll (Jolt handles ragdoll); lightweight spring solver only.
//
// Usage (per animated entity with cloth/banner):
//   VerletChain chain;
//   chain.InitBanner(anchor_bone, tip_bone, num_links);
//   each frame: chain.Simulate(dt);
//   apply: bone_world[chain.end_bone] = chain.points[chain.point_count-1].pos

static constexpr int MAX_VERLET_POINTS      = 16;
static constexpr int MAX_VERLET_CONSTRAINTS = 24;
static constexpr int DEFAULT_VERLET_ITERS   =  3;  // VBfA "Verlet num loops"

struct VerletPoint {
    float px, py, pz;  // current position
    float ox, oy, oz;  // old position (Verlet: velocity = pos - old_pos)
    float mass_inv;     // 0.0 = anchored (pinned to skeleton bone), 1.0 = free
};

struct VerletConstraint {
    uint8_t a, b;           // point indices (a and b must maintain rest_length)
    float   rest_length;    // target distance between a and b
    float   stiffness;      // 0=loose spring, 1=rigid rod
};

struct VerletChain {
    VerletPoint      points[MAX_VERLET_POINTS];
    VerletConstraint cons[MAX_VERLET_CONSTRAINTS];
    int              point_count = 0;
    int              con_count   = 0;
    int              num_loops   = DEFAULT_VERLET_ITERS;  // "Verlet num loops"
    int              anchor_bone = 0;   // skeleton bone that pins points[0]
    int              end_bone    = -1;  // skeleton bone driven by points[N-1]

    // Init a simple hanging chain (banner/flag top → tip).
    // anchor = pinned bone index, end = driven bone index, links = chain segments.
    void InitBanner(int anchor, int end, int links) {
        if (links < 2) links = 2;
        if (links > MAX_VERLET_POINTS) links = MAX_VERLET_POINTS;
        anchor_bone = anchor;
        end_bone    = end;
        point_count = links;
        con_count   = links - 1;
        for (int i = 0; i < links; ++i) {
            points[i].px = 0.f; points[i].py = -(float)i * 0.5f; points[i].pz = 0.f;
            points[i].ox = points[i].px; points[i].oy = points[i].py; points[i].oz = points[i].pz;
            points[i].mass_inv = (i == 0) ? 0.f : 1.f;  // point[0] anchored
        }
        for (int i = 0; i < links - 1; ++i) {
            cons[i].a = (uint8_t)i; cons[i].b = (uint8_t)(i+1);
            cons[i].rest_length = 0.5f;
            cons[i].stiffness   = 0.8f;
        }
    }

    // Simulate one step. dt in seconds. Call with anchor_pos to pin point[0].
    void Simulate(float dt, float ax, float ay, float az,
                  float gravity_y = -9.8f) {
        // Pin anchor point to bone world pos
        points[0].px = ax; points[0].py = ay; points[0].pz = az;

        // Verlet integration (velocity = pos - old_pos)
        for (int i = 1; i < point_count; ++i) {
            float vx = points[i].px - points[i].ox;
            float vy = points[i].py - points[i].oy;
            float vz = points[i].pz - points[i].oz;
            points[i].ox = points[i].px;
            points[i].oy = points[i].py;
            points[i].oz = points[i].pz;
            float m = points[i].mass_inv;
            points[i].px += vx;
            points[i].py += vy + gravity_y * dt * dt * m;
            points[i].pz += vz;
        }

        // Constraint satisfaction (num_loops iterations — VBfA "Verlet num loops")
        for (int iter = 0; iter < num_loops; ++iter) {
            for (int c = 0; c < con_count; ++c) {
                auto& ca = points[cons[c].a];
                auto& cb = points[cons[c].b];
                float dx = cb.px - ca.px;
                float dy = cb.py - ca.py;
                float dz = cb.pz - ca.pz;
                float dist = sqrtf(dx*dx + dy*dy + dz*dz);
                if (dist < 1e-6f) continue;
                float diff = (dist - cons[c].rest_length) / dist * cons[c].stiffness;
                float ta = ca.mass_inv / (ca.mass_inv + cb.mass_inv + 1e-6f);
                float tb = cb.mass_inv / (ca.mass_inv + cb.mass_inv + 1e-6f);
                ca.px += dx * diff * ta;  ca.py += dy * diff * ta;  ca.pz += dz * diff * ta;
                cb.px -= dx * diff * tb;  cb.py -= dy * diff * tb;  cb.pz -= dz * diff * tb;
            }
        }
    }
};

// Per-entity component: up to 4 Verlet chains (hair, cape, earring, belt).
struct VerletComponent {
    static constexpr int MAX_CHAINS = 4;
    VerletChain chains[MAX_CHAINS];
    int         chain_count = 0;
};
