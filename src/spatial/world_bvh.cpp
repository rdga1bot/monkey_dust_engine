#include <monkey_dust/spatial/world_bvh.h>
#include <vendor/tiny_bvh.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace md {

// ── Singleton ─────────────────────────────────────────────────────────────────
WorldBVH& WorldBVH::Get() {
    static WorldBVH inst;
    return inst;
}

// ── Build ─────────────────────────────────────────────────────────────────────
void WorldBVH::Build(const float* verts, int tri_count) {
    Shutdown();
    if (!verts || tri_count <= 0) return;

    // Copy vertex data (BVH needs it alive during traversal).
    const size_t bytes = (size_t)tri_count * 9u * sizeof(float);
    verts_ = (float*)malloc(bytes);
    if (!verts_) return;
    memcpy(verts_, verts, bytes);
    tri_count_ = tri_count;

    // Build tinybvh SAH BVH.
    // tinybvh expects bvhvec4 (x,y,z,w) per vertex — 4 floats per vertex.
    // Our format: 3 floats per vertex, 3 verts per tri = 9 floats per tri.
    // Repack to bvhvec4 (pad w=0).
    tinybvh::bvhvec4* packed =
        (tinybvh::bvhvec4*)malloc((size_t)tri_count * 3u * sizeof(tinybvh::bvhvec4));
    if (!packed) { free(verts_); verts_ = nullptr; return; }

    for (int i = 0; i < tri_count * 3; ++i) {
        packed[i] = tinybvh::bvhvec4(verts_[i*3+0], verts_[i*3+1],
                                      verts_[i*3+2], 0.f);
    }

    auto* bvh = new tinybvh::BVH();
    bvh->Build(packed, tri_count);
    free(packed);

    bvh_   = bvh;
    built_ = true;
    fprintf(stdout, "[WorldBVH] built: %d tris\n", tri_count);
}

// ── Shutdown ──────────────────────────────────────────────────────────────────
void WorldBVH::Shutdown() {
    if (bvh_) { delete static_cast<tinybvh::BVH*>(bvh_); bvh_ = nullptr; }
    if (verts_) { free(verts_); verts_ = nullptr; }
    tri_count_ = 0;
    built_     = false;
}

// ── RayIntersect ──────────────────────────────────────────────────────────────
WorldRayHit WorldBVH::RayIntersect(const float* orig, const float* dir,
                                    float max_t) const {
    WorldRayHit result;
    if (!built_ || !bvh_) return result;

    tinybvh::Ray ray(
        tinybvh::bvhvec3(orig[0], orig[1], orig[2]),
        tinybvh::bvhvec3(dir[0],  dir[1],  dir[2]),
        max_t
    );

    static_cast<tinybvh::BVH*>(bvh_)->Intersect(ray);

    if (ray.hit.t < max_t) {
        result.t      = ray.hit.t;
        result.tri_idx = (int)ray.hit.prim;
        // Compute world-space hit position.
        result.world_x = orig[0] + dir[0] * ray.hit.t;
        result.world_y = orig[1] + dir[1] * ray.hit.t;
        result.world_z = orig[2] + dir[2] * ray.hit.t;
    }
    return result;
}

} // namespace md
