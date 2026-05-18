// OzzAnimPlayer — ozz-animation runtime blend tree evaluator.
#include <monkey_dust/render/ozz_anim_player.h>

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <new>

// ── Helper: cast raw buffer to typed span ────────────────────────────────────
static ozz::span<ozz::math::SoaTransform> soa_span(void* buf, int n_joints) {
    int soa_count = (n_joints + 3) / 4;
    return {reinterpret_cast<ozz::math::SoaTransform*>(buf), (size_t)soa_count};
}
static ozz::span<ozz::math::Float4x4> m44_span(void* buf, int n_joints) {
    return {reinterpret_cast<ozz::math::Float4x4*>(buf), (size_t)n_joints};
}

bool OzzAnimPlayer::LoadSkeleton(const char* path) {
    delete skeleton_; skeleton_ = nullptr;
    ozz::io::File f(path, "rb");
    if (!f.opened()) { fprintf(stderr, "[OzzAnim] Cannot open skeleton: %s\n", path); return false; }
    skeleton_ = new ozz::animation::Skeleton();
    ozz::io::IArchive archive(&f);
    archive >> *skeleton_;
    joint_count_ = skeleton_->num_joints();
    fprintf(stdout, "[OzzAnim] Skeleton loaded: %d joints from %s\n", joint_count_, path);

    // Resize contexts for this skeleton
    auto* ctx_lo = reinterpret_cast<ozz::animation::SamplingJob::Context*>(ctx_lo_buf_);
    auto* ctx_hi = reinterpret_cast<ozz::animation::SamplingJob::Context*>(ctx_hi_buf_);
    new (ctx_lo) ozz::animation::SamplingJob::Context();
    new (ctx_hi) ozz::animation::SamplingJob::Context();
    return true;
}

int OzzAnimPlayer::LoadClip(const char* path) {
    if (clip_count_ >= OZZ_MAX_CLIPS) { fprintf(stderr, "[OzzAnim] Max clips reached\n"); return -1; }
    ozz::io::File f(path, "rb");
    if (!f.opened()) { fprintf(stderr, "[OzzAnim] Cannot open clip: %s\n", path); return -1; }
    clips_[clip_count_] = new ozz::animation::Animation();
    ozz::io::IArchive archive(&f);
    archive >> *clips_[clip_count_];
    fprintf(stdout, "[OzzAnim] Clip[%d]: %s  %.2fs\n",
            clip_count_, clips_[clip_count_]->name(), clips_[clip_count_]->duration());
    return clip_count_++;
}

void OzzAnimPlayer::Shutdown() {
    delete skeleton_; skeleton_ = nullptr;
    for (int i = 0; i < clip_count_; ++i) { delete clips_[i]; clips_[i] = nullptr; }
    clip_count_ = 0; joint_count_ = 0;
}

// ── Single-clip evaluation ───────────────────────────────────────────────────
void OzzAnimPlayer::Eval(int clip_idx, float time_s, float* out_bones) const {
    if (!skeleton_ || clip_idx < 0 || clip_idx >= clip_count_) return;

    auto* anim = clips_[clip_idx];
    float ratio = (anim->duration() > 1e-6f)
                  ? fmodf(time_s, anim->duration()) / anim->duration()
                  : 0.f;

    auto* ctx = reinterpret_cast<ozz::animation::SamplingJob::Context*>(ctx_lo_buf_);
    if (ctx->max_tracks() < anim->num_tracks()) ctx->Resize(anim->num_tracks());

    auto locals = soa_span(buf_locals_lo_, joint_count_);
    {
        ozz::animation::SamplingJob job;
        job.animation = anim;
        job.context   = ctx;
        job.ratio     = ratio;
        job.output    = locals;
        if (!job.Run()) return;
    }

    auto models = m44_span(buf_models_, joint_count_);
    {
        ozz::animation::LocalToModelJob job;
        job.skeleton = skeleton_;
        job.input    = locals;
        job.output   = models;
        if (!job.Run()) return;
    }

    // Convert Float4x4 → column-major float[16] for GPU upload
    for (int i = 0; i < joint_count_; ++i) {
        float* dst = out_bones + i * 16;
        const ozz::math::Float4x4& m = models[i];
        // ozz Float4x4 is column-major: cols[0..3] each SimdFloat4
        ozz::math::StorePtrU(m.cols[0], dst + 0);
        ozz::math::StorePtrU(m.cols[1], dst + 4);
        ozz::math::StorePtrU(m.cols[2], dst + 8);
        ozz::math::StorePtrU(m.cols[3], dst + 12);
    }
    // Identity for remaining slots
    for (int i = joint_count_; i < OZZ_MAX_JOINTS; ++i) {
        float* d = out_bones + i * 16;
        memset(d, 0, 64); d[0]=d[5]=d[10]=d[15]=1.f;
    }
}

// ── Two-layer blend (lower + upper body) ─────────────────────────────────────
void OzzAnimPlayer::EvalBlend(int clip_lo, int clip_hi,
                               float blend_w, float time_lo, float time_hi,
                               int joint_split,
                               float* out_bones) const
{
    if (!skeleton_ || clip_lo < 0 || clip_lo >= clip_count_ ||
                      clip_hi < 0 || clip_hi >= clip_count_) {
        if (clip_lo >= 0 && clip_lo < clip_count_)
            Eval(clip_lo, time_lo, out_bones);
        return;
    }

    auto* anim_lo = clips_[clip_lo];
    auto* anim_hi = clips_[clip_hi];

    // Resize contexts if needed
    auto* ctx_lo = reinterpret_cast<ozz::animation::SamplingJob::Context*>(ctx_lo_buf_);
    auto* ctx_hi = reinterpret_cast<ozz::animation::SamplingJob::Context*>(ctx_hi_buf_);
    if (ctx_lo->max_tracks() < anim_lo->num_tracks()) ctx_lo->Resize(anim_lo->num_tracks());
    if (ctx_hi->max_tracks() < anim_hi->num_tracks()) ctx_hi->Resize(anim_hi->num_tracks());

    float ratio_lo = (anim_lo->duration() > 1e-6f)
                     ? fmodf(time_lo, anim_lo->duration()) / anim_lo->duration() : 0.f;
    float ratio_hi = (anim_hi->duration() > 1e-6f)
                     ? fmodf(time_hi, anim_hi->duration()) / anim_hi->duration() : 0.f;

    auto locals_lo = soa_span(buf_locals_lo_, joint_count_);
    auto locals_hi = soa_span(buf_locals_hi_, joint_count_);
    auto blended   = soa_span(buf_blended_,   joint_count_);

    {
        ozz::animation::SamplingJob job;
        job.animation = anim_lo; job.context = ctx_lo;
        job.ratio = ratio_lo;   job.output  = locals_lo;
        if (!job.Run()) return;
    }
    {
        ozz::animation::SamplingJob job;
        job.animation = anim_hi; job.context = ctx_hi;
        job.ratio = ratio_hi;   job.output  = locals_hi;
        if (!job.Run()) return;
    }

    // Per-joint weight: lower body = blend_w (walk), upper body = 0 (idle)
    // ozz BlendingJob uses SIMD weights stored as SimdFloat4 per joint
    // We use constant layer weights + rest_pose fallback for simplicity
    ozz::animation::BlendingJob::Layer layers[2];
    layers[0].transform = locals_lo;
    layers[0].weight    = blend_w;          // walk lower
    layers[1].transform = locals_hi;
    layers[1].weight    = 1.0f - blend_w;   // idle

    {
        ozz::animation::BlendingJob job;
        job.threshold  = 0.01f;
        job.layers     = layers;
        job.rest_pose  = skeleton_->joint_rest_poses();
        job.output     = blended;
        if (!job.Run()) {
            // fallback to single clip
            Eval(blend_w > 0.5f ? clip_lo : clip_hi,
                 blend_w > 0.5f ? time_lo : time_hi, out_bones);
            return;
        }
    }

    auto models = m44_span(buf_models_, joint_count_);
    {
        ozz::animation::LocalToModelJob job;
        job.skeleton = skeleton_;
        job.input    = blended;
        job.output   = models;
        if (!job.Run()) return;
    }

    for (int i = 0; i < joint_count_; ++i) {
        float* dst = out_bones + i * 16;
        const ozz::math::Float4x4& m = models[i];
        ozz::math::StorePtrU(m.cols[0], dst + 0);
        ozz::math::StorePtrU(m.cols[1], dst + 4);
        ozz::math::StorePtrU(m.cols[2], dst + 8);
        ozz::math::StorePtrU(m.cols[3], dst + 12);
    }
    for (int i = joint_count_; i < OZZ_MAX_JOINTS; ++i) {
        float* d = out_bones + i * 16;
        memset(d, 0, 64); d[0]=d[5]=d[10]=d[15]=1.f;
    }
}
