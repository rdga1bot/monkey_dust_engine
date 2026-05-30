#pragma once
// M55 perception tick — moved to engine/ (all deps are engine types).
// SenseSystemUpdate(now_ms): call once per logic tick, after frame_flags dispatch.
// Visual cone: max contribution from ViewConeSet; Audio: linear falloff 15m.
// Rising edge on threshold_hi → writes last_activated_ms + last_known_x/z (Visual).
// VBfA-AI2: AwarenessLimits caps applied — prevents O(n²) reaction cascades.
// CATHODE RE §7: sense_cooldown_frames — per-NPC throttle when global budget exceeded.
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/ai/sense_registry.h>
#include <monkey_dust/ai/awareness_limits.h>
#include <monkey_dust/ecs/registry.h>
#include <cmath>
#ifdef __SSE__
#  include <xmmintrin.h>
#endif

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
    // VBfA-AI2: reset per-tick reaction caps (prevents O(n²) alert cascades)
    AwarenessLimits::g_frame.Reset();

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
        entt::entity e, SenseComponent& sc,
        const WorldTransform& wt, AgentState& as)
    {
        if (as.lcflags.test(lcf::IS_PLAYER)) return;

        // CATHODE RE §7: NPC_SenseLimiter — per-NPC budget throttle.
        // If cooldown > 0, skip sense queries and decrement. Yields ~15% CPU at 300 NPCs.
        if (sc.sense_cooldown_frames > 0) { --sc.sense_cooldown_frames; return; }

        float dx   = pwt->x - wt.x, dz = pwt->z - wt.z;
        float dist = sqrtf(dx * dx + dz * dz);
        float angle_to   = atan2f(dx, dz);
        float angle_diff = fabsf(sense_wrap_angle(angle_to - wt.rot_y)) * SENSE_RAD2DEG;

        // AI-4: read optional SenseModifiers (CATHODE RE §6.4 config-driven ranges).
        // If no SenseModifiers component, defaults (1.0) apply — same as before.
        const SenseModifiers* sm = reg.try_get<SenseModifiers>(e);
        float vis_range_mult    = sm ? sm->visual_range_mult    : 1.f;
        float audio_range_mult  = sm ? sm->audio_range_mult     : 1.f;
        float fill_mult         = sm ? sm->activation_fill_mult : 1.f;
        float darkness_mult     = sm ? sm->darkness_mult        : 1.f;
        float crouch_mult       = sm ? sm->crouch_mult          : 1.f;
        float noise_mult        = sm ? sm->noise_mult           : 1.f;

        // ── Visual (index 0) ─────────────────────────────────────────────────
        // VBfA-AI2: hard range cap. AI-4: apply visual_range_mult from SenseModifiers.
        float visual_act = 0.f;
        float eff_vis_range = AwarenessLimits::MAX_VISION_RANGE * vis_range_mult;
        if (dist <= eff_vis_range) {
            const ViewConeSet* vcs = SenseRegistry::Get().At(sc.cone_set_idx);
            if (vcs) {
                // AI-4: target crouching → apply crouch_mult; darkness → darkness_mult.
                // LocomotionState not directly accessible here; use stance from AgentState flags.
                float stance_factor = as.locomotion_state == LocomotionState::Crouching
                                      ? crouch_mult : 1.f;
                for (int c = 0; c < vcs->cone_count; ++c) {
                    // Scale cone length by vis_range_mult; apply fill_mult + darkness + stance.
                    ViewCone scaled = vcs->cones[c];
                    scaled.length_m *= vis_range_mult;
                    float contrib = sense_cone_activation(scaled, dist, angle_diff);
                    contrib *= fill_mult * darkness_mult * stance_factor;
                    if (contrib > visual_act) visual_act = contrib;
                }
            }
        }

        bool vis_was_hi = sc.activation[0] >= sc.threshold_hi;
        sc.activation[0] = visual_act;
        if (!vis_was_hi && visual_act >= sc.threshold_hi) {
            // VBfA-AI2: cap at MAX_REACT_TO_PLAYER per tick.
            if (AwarenessLimits::g_frame.reacted_to_player
                    < AwarenessLimits::MAX_REACT_TO_PLAYER) {
                sc.last_activated_ms[0] = uint32_now;
                sc.last_known_x = pwt->x;
                sc.last_known_z = pwt->z;
                ++AwarenessLimits::g_frame.reacted_to_player;

                // CATHODE RE §7.8: raise awareness watermark when NPC fully detects player.
                AgentBlackboard* bb = reg.try_get<AgentBlackboard>(e);
                if (bb && bb->awareness_watermark < AwarenessState::Aware) {
                    bb->awareness_watermark = AwarenessState::Aware;
                    bb->watermark_ms = uint32_now;
                }
            }
        }

        // ── Audio (index 1): linear falloff. AI-4: audio_range_mult + noise_mult.
        float eff_audio_range = SENSE_AUDIO_RADIUS_M * audio_range_mult;
        float audio_act = fmaxf(0.f, (1.f - dist / eff_audio_range) * fill_mult * noise_mult);
        bool  aud_was_hi = sc.activation[1] >= sc.threshold_hi;
        sc.activation[1] = audio_act;
        if (!aud_was_hi && audio_act >= sc.threshold_hi) {
            if (AwarenessLimits::g_frame.reacted_to_combat
                    < AwarenessLimits::MAX_REACT_TO_COMBAT) {
                sc.last_activated_ms[1] = uint32_now;
                ++AwarenessLimits::g_frame.reacted_to_combat;
            }
        }

        // VBfA RE §8.5: clamp all 9 activations to [0, 1] using SSE maxps/minps.
        // Processes 8 at once (2×__m128), then 1 scalar remainder.
#ifdef __SSE__
        {
            __m128 zero4 = _mm_setzero_ps();
            __m128 one4  = _mm_set1_ps(1.f);
            float* a = sc.activation;
            // first 8: two 4-float ops
            _mm_storeu_ps(a + 0, _mm_min_ps(_mm_max_ps(_mm_loadu_ps(a + 0), zero4), one4));
            _mm_storeu_ps(a + 4, _mm_min_ps(_mm_max_ps(_mm_loadu_ps(a + 4), zero4), one4));
            // last element
            if (a[8] < 0.f) a[8] = 0.f;
            if (a[8] > 1.f) a[8] = 1.f;
        }
#else
        for (int i = 0; i < MAX_SENSES; ++i) {
            if (sc.activation[i] < 0.f) sc.activation[i] = 0.f;
            if (sc.activation[i] > 1.f) sc.activation[i] = 1.f;
        }
#endif
    });
}
