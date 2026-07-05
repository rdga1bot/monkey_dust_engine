#pragma once
// M55 perception tick — moved to engine/ (all deps are engine types).
// PERF-14: md_rsqrtf/md_dist2d used instead of sqrtf for sense distance queries.
// SenseSystemUpdate(now_ms): call once per logic tick, after frame_flags dispatch.
// Visual cone: max contribution from ViewConeSet; Audio: linear falloff 15m.
// Rising edge on threshold_hi → writes last_activated_ms + last_known_x/z (Visual).
// VBfA-AI2: AwarenessLimits caps applied — prevents O(n²) reaction cascades.
// CATHODE RE §7: sense_cooldown_frames — per-NPC throttle when global budget exceeded.
#include <monkey_dust/platform/md_hints.h>
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/stealth_component.h>
#include <monkey_dust/components/noise_emitter.h>
#include <monkey_dust/world/world_transform.h>
#include <monkey_dust/ai/sense_registry.h>
#include <monkey_dust/ai/awareness_limits.h>
#include <monkey_dust/ecs/registry.h>
#include <cmath>
#ifdef __AVX2__
#  include <immintrin.h>
#elif defined(__SSE__)
#  include <xmmintrin.h>
#endif

static constexpr float SENSE_AUDIO_RADIUS_M = 15.0f;
static constexpr float SENSE_PI             = 3.14159265f;
static constexpr float SENSE_RAD2DEG        = 180.0f / SENSE_PI;

// Inverse 4th-power distance falloff.
// (range/dist)^4 — double distance → 1/16th activation.
// Used for audio/smell/vibration where ultra-local effect is desired.
// Linear (old): 1 - dist/range   → gentle slope
// r⁴   (VBfA): (range/dist)^4   → steep cliff, very local
static inline float sense_falloff_r4(float dist, float range) {
    if (dist <= 0.f)    return 1.f;
    if (dist >= range)  return 0.f;
    float ratio = range / dist;
    float r2    = ratio * ratio;
    float r4    = r2 * r2;
    return r4 > 1.f ? 1.f : r4;  // clamp: at dist≈0 r4→∞, cap at 1.0
}

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

    // B-1: StealthComponent on player reduces all observer activation fills.
    const StealthComponent* psc = reg.try_get<StealthComponent>(player);
    float player_stealth = psc ? psc->stealth_factor : 1.f;

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
        float dist = md_dist2d(dx, dz);  // PERF-14: rsqrtps instead of sqrtf
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

        visual_act *= player_stealth;  // B-1: stealth reduces visual detectability
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

        // ── Audio (index 1): VBfA r⁴ falloff. AI-4: audio_range_mult + noise_mult.
        // (range/dist)^4 — 2× farther → 1/16 activation.
        // Replaces linear (1 - dist/range) which was too generous at long range.
        float eff_audio_range = SENSE_AUDIO_RADIUS_M * audio_range_mult;
        // B-1: AudioMovement index=2 uses full stealth; AudioCombat index=1 less affected.
        float audio_act = sense_falloff_r4(dist, eff_audio_range) * fill_mult * noise_mult
                        * player_stealth;
        bool  aud_was_hi = sc.activation[1] >= sc.threshold_hi;
        sc.activation[1] = audio_act;
        if (!aud_was_hi && audio_act >= sc.threshold_hi) {
            if (AwarenessLimits::g_frame.reacted_to_combat
                    < AwarenessLimits::MAX_REACT_TO_COMBAT) {
                sc.last_activated_ms[1] = uint32_now;
                ++AwarenessLimits::g_frame.reacted_to_combat;
            }
        }

        // Clamp all MAX_SENSES activations to [0, 1].
        // B-4: MAX_SENSES=10; AVX2 handles 8+2, SSE handles 4+4+2.
#ifdef __AVX2__
        {
            __m256 zero8 = _mm256_setzero_ps();
            __m256 one8  = _mm256_set1_ps(1.f);
            float* a = sc.activation;
            _mm256_storeu_ps(a, _mm256_min_ps(_mm256_max_ps(_mm256_loadu_ps(a), zero8), one8));
            for (int i = 8; i < MAX_SENSES; ++i) {
                if (a[i] < 0.f) a[i] = 0.f;
                if (a[i] > 1.f) a[i] = 1.f;
            }
        }
#elif defined(__SSE__)
        {
            __m128 zero4 = _mm_setzero_ps();
            __m128 one4  = _mm_set1_ps(1.f);
            float* a = sc.activation;
            _mm_storeu_ps(a + 0, _mm_min_ps(_mm_max_ps(_mm_loadu_ps(a + 0), zero4), one4));
            _mm_storeu_ps(a + 4, _mm_min_ps(_mm_max_ps(_mm_loadu_ps(a + 4), zero4), one4));
            for (int i = 8; i < MAX_SENSES; ++i) {
                if (a[i] < 0.f) a[i] = 0.f;
                if (a[i] > 1.f) a[i] = 1.f;
            }
        }
#else
        for (int i = 0; i < MAX_SENSES; ++i) {
            if (sc.activation[i] < 0.f) sc.activation[i] = 0.f;
            if (sc.activation[i] > 1.f) sc.activation[i] = 1.f;
        }
#endif
    });

    // B-3: NoiseEmitter pass — fill activation[AudioCombat=1] and activation[AudioMovement=2]
    // from nearby noise sources. Separate from the player detection path above.
    reg.view<NoiseEmitter, WorldTransform>().each([&](
        const NoiseEmitter& ne, const WorldTransform& nwt)
    {
        if (ne.noise_radius_m <= 0.f) return;
        float r2 = ne.noise_radius_m * ne.noise_radius_m;
        reg.view<SenseComponent, WorldTransform>().each([&](
            SenseComponent& sc, const WorldTransform& owt)
        {
            float dx = nwt.x - owt.x, dz = nwt.z - owt.z;
            float d2 = dx*dx + dz*dz;
            if (d2 >= r2) return;
            float contrib = 1.f - d2 / r2;  // linear falloff
            auto nt = static_cast<NoiseType>(ne.noise_type);
            if (nt == NoiseType::Weapon || nt == NoiseType::Explosion)
                sc.activation[(int)SenseType::AudioCombat]   += contrib;
            if (nt == NoiseType::Footstep || nt == NoiseType::Vent)
                sc.activation[(int)SenseType::AudioMovement] += contrib;
            if (nt == NoiseType::Voice) {  // voice activates both at half strength
                sc.activation[(int)SenseType::AudioCombat]   += contrib * 0.5f;
                sc.activation[(int)SenseType::AudioMovement] += contrib * 0.5f;
            }
        });
    });

    // B-3: SmellEmitter pass — fill activation[Smell=3] from nearby smell sources.
    reg.view<SmellEmitter, WorldTransform>().each([&](
        const SmellEmitter& se, const WorldTransform& swt)
    {
        if (se.smell_radius_m <= 0.f) return;
        float r2 = se.smell_radius_m * se.smell_radius_m;
        float intensity = (float)se.intensity / 255.f;
        reg.view<SenseComponent, WorldTransform>().each([&](
            SenseComponent& sc, const WorldTransform& owt)
        {
            float dx = swt.x - owt.x, dz = swt.z - owt.z;
            float d2 = dx*dx + dz*dz;
            if (d2 >= r2) return;
            sc.activation[(int)SenseType::Smell] += (1.f - d2 / r2) * intensity;
        });
    });
}
