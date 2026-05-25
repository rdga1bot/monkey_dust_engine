// JoltWorld — Jolt Physics character controller + ragdoll foundation.
// References: https://jrouwe.github.io/JoltPhysics/

// Must be first
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Character/CharacterBase.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>

#include <monkey_dust/physics/jolt_world.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ── Jolt boilerplate: object layers ─────────────────────────────────────────
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING     = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS = 2;
}

struct JoltWorld::BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
    JPH::BroadPhaseLayer obj_to_bp[Layers::NUM_LAYERS];
    BPLayerInterface() {
        obj_to_bp[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        obj_to_bp[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }
    uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return obj_to_bp[l];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
        return l == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
    }
#endif
};

struct JoltWorld::OVBPLayerPair final : public JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer ol, JPH::BroadPhaseLayer bl) const override {
        if (ol == Layers::NON_MOVING) return bl == BroadPhaseLayers::MOVING;
        return true;
    }
};

struct JoltWorld::OVBroadPhase final : public JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::NON_MOVING) return b == Layers::MOVING;
        if (a == Layers::MOVING)     return true;
        return false;
    }
};

// ── Init / Shutdown ──────────────────────────────────────────────────────────
void JoltWorld::Init(int max_bodies) {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    temp_alloc_    = new JPH::TempAllocatorImpl(8 * 1024 * 1024); // 8 MB
    job_system_    = new JPH::JobSystemThreadPool(
                         JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1);
    bp_layer_iface_ = new BPLayerInterface();
    ovbp_layer_pair_= new OVBPLayerPair();
    ovbp_filter_    = new OVBroadPhase();

    const uint max_body_pairs   = (uint)max_bodies * 2;
    const uint max_constraints  = (uint)max_bodies;
    physics_system_.Init((uint)max_bodies, 0, max_body_pairs, max_constraints,
                         *bp_layer_iface_, *ovbp_layer_pair_, *ovbp_filter_);

    // Build shared capsule shape (0.35m radius, 0.9m half-height → 1.8m tall)
    JPH::CapsuleShapeSettings css(0.9f, 0.35f);
    css.mUserData = 0;
    auto cs_result = css.Create();
    if (cs_result.HasError()) {
        fprintf(stderr, "[Jolt] CapsuleShape create failed: %s\n",
                cs_result.GetError().c_str());
        return;
    }
    // Offset: capsule center is at height 0.9+0.35=1.25m → shift up by 1.25m so feet at y=0
    JPH::RotatedTranslatedShapeSettings rts(
        JPH::Vec3(0, 1.25f, 0), JPH::Quat::sIdentity(), cs_result.Get());
    auto rts_result = rts.Create();
    if (rts_result.HasError()) {
        fprintf(stderr, "[Jolt] RotatedTranslatedShape failed\n"); return;
    }
    char_shape_ = rts_result.Get();

    ready_ = true;
    fprintf(stdout, "[Jolt] Init OK  max_bodies=%d\n", max_bodies);
}

void JoltWorld::Shutdown() {
    for (int i = 0; i < char_count_; ++i) {
        if (chars_[i]) { delete chars_[i]; chars_[i] = nullptr; }
    }
    char_count_ = 0;
    char_shape_ = nullptr;
    delete bp_layer_iface_;   bp_layer_iface_  = nullptr;
    delete ovbp_layer_pair_;  ovbp_layer_pair_ = nullptr;
    delete ovbp_filter_;      ovbp_filter_     = nullptr;
    delete temp_alloc_;       temp_alloc_      = nullptr;
    delete job_system_;       job_system_      = nullptr;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance; JPH::Factory::sInstance = nullptr;
    ready_ = false;
}

// ── Character creation/destroy ───────────────────────────────────────────────
JPH::CharacterVirtual* JoltWorld::CreateCharacter(float x, float y, float z,
                                                    float /*radius*/,
                                                    float /*half_height*/) {
    if (!ready_ || char_count_ >= MAX_CHARS) return nullptr;

    JPH::CharacterVirtualSettings cvs;
    cvs.mShape           = char_shape_;
    cvs.mMaxSlopeAngle   = JPH::DegreesToRadians(45.f);
    cvs.mMaxStrength     = 100.f;
    cvs.mCharacterPadding= 0.02f;
    cvs.mPenetrationRecoverySpeed = 1.f;
    cvs.mPredictiveContactDistance = 0.1f;

    auto* c = new JPH::CharacterVirtual(&cvs,
        JPH::RVec3(x, y, z), JPH::Quat::sIdentity(), 0, &physics_system_);
    c->SetLinearVelocity(JPH::Vec3::sZero());

    chars_[char_count_++] = c;
    return c;
}

void JoltWorld::DestroyCharacter(JPH::CharacterVirtual* c) {
    if (!c) return;
    for (int i = 0; i < char_count_; ++i) {
        if (chars_[i] != c) continue;
        delete c;
        chars_[i] = chars_[--char_count_];
        chars_[char_count_] = nullptr;
        return;
    }
}

// ── Step ────────────────────────────────────────────────────────────────────
void JoltWorld::Step(float dt) {
    if (!ready_) return;

    JPH::CharacterVirtual::ExtendedUpdateSettings eu_settings;
    eu_settings.mStickToFloorStepDown      = JPH::Vec3(0, -0.5f, 0);
    eu_settings.mWalkStairsStepUp          = JPH::Vec3(0,  0.4f, 0);
    eu_settings.mWalkStairsMinStepForward  = 0.02f;
    eu_settings.mWalkStairsCosAngleForwardContact = 0.7f;

    JPH::BroadPhaseLayerFilter  bp_filter;
    JPH::ObjectLayerFilter      obj_filter;
    JPH::BodyFilter             body_filter;
    JPH::ShapeFilter            shape_filter;

    for (int i = 0; i < char_count_; ++i) {
        if (!chars_[i]) continue;
        chars_[i]->ExtendedUpdate(dt, JPH::Vec3(0, -9.81f, 0),
                                   eu_settings,
                                   physics_system_.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                                   physics_system_.GetDefaultLayerFilter(Layers::MOVING),
                                   body_filter, shape_filter, *temp_alloc_);
    }

    // Jolt deadlocks on physics_system_.Update() when called with zero active bodies
    // (static-only world with JobSystemThreadPool). CharacterVirtual handles its own
    // collision queries via ExtendedUpdate() and does not need this call.
    if (physics_system_.GetNumActiveBodies(JPH::EBodyType::RigidBody) == 0) return;
    physics_system_.Update(dt, 1, temp_alloc_, job_system_);
}

// ── Terrain chunk as static MeshShape ────────────────────────────────────────
JPH::BodyID JoltWorld::AddTerrainMesh(const float* heights, int grid,
                                       float cell_size,
                                       float origin_x, float origin_z)
{
    if (!ready_) return JPH::BodyID();
    const int N = grid;
    JPH::TriangleList tris;
    tris.reserve((size_t)(N * N * 2));
    auto pos = [&](int c, int r) -> JPH::Float3 {
        float h = heights[r * (N + 1) + c];
        return JPH::Float3(origin_x + c * cell_size, h, origin_z + r * cell_size);
    };
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            tris.push_back(JPH::Triangle(pos(c,r), pos(c+1,r), pos(c,r+1)));
            tris.push_back(JPH::Triangle(pos(c+1,r), pos(c+1,r+1), pos(c,r+1)));
        }
    }
    JPH::MeshShapeSettings mss(tris);
    auto result = mss.Create();
    if (result.HasError()) {
        fprintf(stderr, "[Jolt] TerrainMesh failed: %s\n", result.GetError().c_str());
        return JPH::BodyID();
    }
    JPH::BodyCreationSettings bcs(result.Get(),
        JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    return physics_system_.GetBodyInterface().CreateAndAddBody(
        bcs, JPH::EActivation::DontActivate);
}

void JoltWorld::RemoveBody(JPH::BodyID id)
{
    if (!ready_ || id.IsInvalid()) return;
    auto& bi = physics_system_.GetBodyInterface();
    bi.RemoveBody(id);
    bi.DestroyBody(id);
}

// P-NG-6.3: Replace PCG terrain body with HeightFieldShape (more efficient than trimesh).
// Downsamples hmap from samples×samples → 129×129 (HeightFieldShape requires (N-1)%2==0).
void JoltWorld::ReplaceTerrainBody(const float* hmap, int samples,
                                    float scale_xz, float off_x, float off_z)
{
    if (!ready_) return;

    // Remove previous terrain body
    if (!terrain_body_.IsInvalid()) {
        auto& bi = physics_system_.GetBodyInterface();
        bi.RemoveBody(terrain_body_);
        bi.DestroyBody(terrain_body_);
        terrain_body_ = JPH::BodyID();
    }

    // HeightFieldShape requires (N-1) divisible by block_size (default 2) → use N=129.
    static constexpr int HF_RES = 129;
    static float s_hf[HF_RES * HF_RES];
    float inv = (float)(samples - 1) / (float)(HF_RES - 1);
    for (int z = 0; z < HF_RES; ++z)
        for (int x = 0; x < HF_RES; ++x) {
            int sx = (int)(x * inv + 0.5f); if (sx >= samples) sx = samples - 1;
            int sz = (int)(z * inv + 0.5f); if (sz >= samples) sz = samples - 1;
            s_hf[z * HF_RES + x] = hmap[sz * samples + sx];
        }

    float hf_cell = scale_xz * (float)(samples - 1) / (float)(HF_RES - 1);
    JPH::HeightFieldShapeSettings hfs(
        s_hf,
        JPH::Vec3(off_x, 0.f, off_z),
        JPH::Vec3(hf_cell, 1.f, hf_cell),
        (JPH::uint32)HF_RES);

    auto result = hfs.Create();
    if (result.HasError()) {
        fprintf(stderr, "[Jolt] HeightField: %s\n", result.GetError().c_str());
        return;
    }

    JPH::BodyCreationSettings bcs(result.Get(),
        JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    terrain_body_ = physics_system_.GetBodyInterface().CreateAndAddBody(
        bcs, JPH::EActivation::DontActivate);
}

float JoltWorld::CastRay(float fx, float fy, float fz,
                          float tx, float ty, float tz)
{
    if (!ready_) return 1.f;
    JPH::Vec3 origin(fx, fy, fz);
    JPH::Vec3 dir(tx - fx, ty - fy, tz - fz);
    JPH::RRayCast ray(origin, dir);
    JPH::RayCastResult hit;
    if (physics_system_.GetNarrowPhaseQuery().CastRay(
            ray, hit,
            physics_system_.GetDefaultBroadPhaseLayerFilter(Layers::NON_MOVING),
            physics_system_.GetDefaultLayerFilter(Layers::NON_MOVING)))
        return hit.mFraction;
    return 1.f;
}

// ── Static terrain mesh ──────────────────────────────────────────────────────
void JoltWorld::AddStaticMesh(const float* verts, int nv,
                               const int* tris, int nt) {
    if (!ready_) return;

    JPH::TriangleList triangles;
    triangles.reserve((size_t)nt);
    for (int i = 0; i < nt; ++i) {
        const int* t = tris + i * 3;
        const float* a = verts + t[0] * 3;
        const float* b = verts + t[1] * 3;
        const float* c = verts + t[2] * 3;
        triangles.push_back(JPH::Triangle(
            JPH::Float3(a[0],a[1],a[2]),
            JPH::Float3(b[0],b[1],b[2]),
            JPH::Float3(c[0],c[1],c[2])));
    }

    JPH::MeshShapeSettings mss(triangles);
    auto result = mss.Create();
    if (result.HasError()) {
        fprintf(stderr, "[Jolt] MeshShape failed: %s\n", result.GetError().c_str());
        return;
    }

    JPH::BodyCreationSettings bcs(result.Get(),
        JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    physics_system_.GetBodyInterface().CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
    fprintf(stdout, "[Jolt] Terrain mesh added: %d verts / %d tris\n", nv, nt);
}
