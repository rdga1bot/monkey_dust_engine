#pragma once
// M55 perception tick — moved to engine/ (all deps are engine types).
// SenseSystemUpdate(now_ms): call once per logic tick, after frame_flags dispatch.
// Visual cone: max contribution from ViewConeSet; Audio: linear falloff 15m.
// Rising edge on threshold_hi → writes last_activated_ms + last_known_x/z (Visual).
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/ai/sense_registry.h>
#include <monkey_dust/ecs/registry.h>
#include <cmath>

static constexpr float SENSE_AUDIO_RADIUS_M = 15.0f;
static constexpr float SENSE_PI             = 3.14159265f;
static constexpr float SENSE_RAD2DEG        = 180.0f / SENSE_PI;

static inline float sense_wrap_angle(float a) {
    while (a >  SENSE_PI) a -= 2.0f * SENSE_PI;
    while (a < -SENSE_PI) a += 2.0f * SENSE_PI;
    return a;
}

static inline float sense_cone_activation(const ViewCone& cone,
                                          float dist, float angle_abs_deg) {
    if (dist > cone.length_m)                    return 0.f;
    if (angle_abs_deg > cone.h_angle_deg * 0.5f) return 0.f;
    float t = 1.f - dist / cone.length_m;
    return cone.dist_lo + t * (cone.dist_hi - cone.dist_lo);
}

inline void SenseSystemUpdate(float now_ms) {
    auto& reg = Registry::Get();

    entt::entity player = entt::null;
    reg.view<AgentState>().each([&](entt::entity e, const AgentState& as) {
        if (player == entt::null && as.lcflags.test(lcf::IS_PLAYER))
            player = e;
    });
    if (player == entt::null) return;

    const WorldTransform* pwt = reg.try_get<WorldTransform>(player);
    if (!pwt) return;

    auto uint32_now = static_cast<uint32_t>(now_ms);

    reg.view<SenseComponent, WorldTransform, AgentState>().each([&](
        entt::entity /*e*/, SenseComponent& sc,
        const WorldTransform& wt, AgentState& as)
    {
        if (as.lcflags.test(lcf::IS_PLAYER)) return;

        float dx   = pwt->x - wt.x, dz = pwt->z - wt.z;
        float dist = sqrtf(dx * dx + dz * dz);
        float angle_to   = atan2f(dx, dz);
        float angle_diff = fabsf(sense_wrap_angle(angle_to - wt.rot_y)) * SENSE_RAD2DEG;

        // Visual (index 0)
        const ViewConeSet* vcs = SenseRegistry::Get().At(sc.cone_set_idx);
        float visual_act = 0.f;
        if (vcs) {
            for (int c = 0; c < vcs->cone_count; ++c) {
                float contrib = sense_cone_activation(vcs->cones[c], dist, angle_diff);
                if (contrib > visual_act) visual_act = contrib;
            }
        }
        bool was_hi = sc.activation[0] >= sc.threshold_hi;
        sc.activation[0] = visual_act;
        if (!was_hi && visual_act >= sc.threshold_hi) {
            sc.last_activated_ms[0] = uint32_now;
            sc.last_known_x = pwt->x;
            sc.last_known_z = pwt->z;
        }

        // Audio (index 1): linear falloff within 15m
        float audio_act = fmaxf(0.f, 1.f - dist / SENSE_AUDIO_RADIUS_M);
        was_hi = sc.activation[1] >= sc.threshold_hi;
        sc.activation[1] = audio_act;
        if (!was_hi && audio_act >= sc.threshold_hi)
            sc.last_activated_ms[1] = uint32_now;
    });
}
