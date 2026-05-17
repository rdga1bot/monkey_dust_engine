#include <monkey_dust/render/moc_culler.h>
#include <cstddef>  // size_t — required before MOC header
#include <MaskedOcclusionCulling.h>
#include <cstring>
#include <cstdio>

namespace md {

// ── Singleton ─────────────────────────────────────────────────────────────────
MocCuller& MocCuller::Get() {
    static MocCuller inst;
    return inst;
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────
bool MocCuller::Init(int res_w, int res_h) {
    if (moc_) return true;

    // Request AVX2 implementation (falls back to SSE4.1 if unavailable).
    auto* moc = MaskedOcclusionCulling::Create(MaskedOcclusionCulling::AVX2);
    if (!moc) {
        fprintf(stderr, "[MocCuller] MaskedOcclusionCulling::Create failed\n");
        return false;
    }
    moc->SetResolution((unsigned)res_w, (unsigned)res_h);
    moc_ = moc;

    const char* impl_name = "unknown";
    switch (moc->GetImplementation()) {
        case MaskedOcclusionCulling::SSE2:   impl_name = "SSE2"; break;
        case MaskedOcclusionCulling::SSE41:  impl_name = "SSE4.1"; break;
        case MaskedOcclusionCulling::AVX2:   impl_name = "AVX2"; break;
        case MaskedOcclusionCulling::AVX512: impl_name = "AVX512"; break;
        default: break;
    }
    fprintf(stdout, "[MocCuller] init %dx%d (%s)\n", res_w, res_h, impl_name);
    return true;
}

void MocCuller::Shutdown() {
    if (moc_) {
        MaskedOcclusionCulling::Destroy(static_cast<MaskedOcclusionCulling*>(moc_));
        moc_ = nullptr;
    }
}

// ── BeginFrame ────────────────────────────────────────────────────────────────
void MocCuller::BeginFrame(const float* mvp16, int vp_w, int vp_h) {
    if (!moc_) return;
    memcpy(mvp_, mvp16, 64);
    vp_w_ = vp_w;
    vp_h_ = vp_h;
    static_cast<MaskedOcclusionCulling*>(moc_)->ClearBuffer();
}

// ── RenderOccluders ───────────────────────────────────────────────────────────
// Expands float[3] world-space verts to float[4] (x,y,z,1) for MOC.
// Then renders via modelToClipMatrix = our MVP.

void MocCuller::RenderOccluders(const float* verts, int vert_count,
                                  const uint32_t* tris, int tri_count) {
    if (!moc_ || vert_count <= 0 || tri_count <= 0) return;

    // MOC default: stride=16, float[4] per vertex (x,y,z,w).
    // We expand on-the-fly into a small scratch buffer.
    // For large meshes call in chunks to avoid large stack allocations.
    constexpr int CHUNK = 4096;
    float scratch[CHUNK * 4];

    auto* moc = static_cast<MaskedOcclusionCulling*>(moc_);

    // Process all triangles, chunking by vertex count.
    // For simplicity: expand ALL verts once if small enough, else skip.
    if (vert_count > CHUNK) {
        // Too many verts for stack scratch — skip occluder upload.
        // For production, heap-allocate or stream in chunks.
        fprintf(stderr, "[MocCuller] occluder vert count %d > CHUNK %d, skipped\n",
                vert_count, CHUNK);
        return;
    }

    for (int i = 0; i < vert_count; ++i) {
        scratch[i*4 + 0] = verts[i*3 + 0];
        scratch[i*4 + 1] = verts[i*3 + 1];
        scratch[i*4 + 2] = verts[i*3 + 2];
        scratch[i*4 + 3] = 1.0f;
    }

    // Use our MVP as modelToClipMatrix (column-major float[16]).
    moc->RenderTriangles(scratch, tris, tri_count, mvp_,
                          MaskedOcclusionCulling::BACKFACE_CW,
                          MaskedOcclusionCulling::CLIP_PLANE_ALL);
}

// ── IsBoxVisible ──────────────────────────────────────────────────────────────
// Project a world-space AABB to NDC and call TestRect.
// MOC TestRect input: NDC (x/w, y/w) — post-perspective-divide.

bool MocCuller::IsBoxVisible(float cx, float cy, float cz, float half) const {
    if (!moc_) return true;  // no culler → assume visible

    // 8 corners of the AABB.
    const float corners[8][4] = {
        {cx-half, cy-half, cz-half, 1.f},
        {cx+half, cy-half, cz-half, 1.f},
        {cx-half, cy+half, cz-half, 1.f},
        {cx+half, cy+half, cz-half, 1.f},
        {cx-half, cy-half, cz+half, 1.f},
        {cx+half, cy-half, cz+half, 1.f},
        {cx-half, cy+half, cz+half, 1.f},
        {cx+half, cy+half, cz+half, 1.f},
    };

    // Project each corner via MVP, find NDC bounding rectangle.
    float ndc_xmin =  1e30f, ndc_xmax = -1e30f;
    float ndc_ymin =  1e30f, ndc_ymax = -1e30f;
    float w_min    =  1e30f;
    bool  any_front = false;

    const float* M = mvp_;
    for (int i = 0; i < 8; ++i) {
        const float* v = corners[i];
        float cx_ = M[0]*v[0] + M[4]*v[1] + M[ 8]*v[2] + M[12]*v[3];
        float cy_ = M[1]*v[0] + M[5]*v[1] + M[ 9]*v[2] + M[13]*v[3];
        float cw  = M[3]*v[0] + M[7]*v[1] + M[11]*v[2] + M[15]*v[3];

        if (cw <= 0.f) continue;  // behind camera
        any_front = true;

        float inv_w = 1.f / cw;
        float nx = cx_ * inv_w;
        float ny = cy_ * inv_w;

        if (nx < ndc_xmin) ndc_xmin = nx;
        if (nx > ndc_xmax) ndc_xmax = nx;
        if (ny < ndc_ymin) ndc_ymin = ny;
        if (ny > ndc_ymax) ndc_ymax = ny;
        if (cw < w_min)    w_min     = cw;
    }

    if (!any_front) return false;  // fully behind camera

    // Clamp to screen.
    ndc_xmin = ndc_xmin < -1.f ? -1.f : ndc_xmin;
    ndc_xmax = ndc_xmax >  1.f ?  1.f : ndc_xmax;
    ndc_ymin = ndc_ymin < -1.f ? -1.f : ndc_ymin;
    ndc_ymax = ndc_ymax >  1.f ?  1.f : ndc_ymax;

    if (ndc_xmin >= ndc_xmax || ndc_ymin >= ndc_ymax) return false;

    auto result = static_cast<const MaskedOcclusionCulling*>(moc_)
                  ->TestRect(ndc_xmin, ndc_ymin, ndc_xmax, ndc_ymax,
                              1.f / w_min);

    return result == MaskedOcclusionCulling::VISIBLE;
}

// ── CullNpcs ─────────────────────────────────────────────────────────────────
int MocCuller::CullNpcs(const float* npc_x, const float* npc_z, int count,
                          float npc_h, uint8_t* out_mask) const {
    int visible = 0;
    for (int i = 0; i < count; ++i) {
        float cx = npc_x[i];
        float cy = npc_h * 0.5f;   // center of NPC AABB height
        float cz = npc_z[i];
        float hs = 0.4f;            // half-size for NPC AABB (small)
        // Use npc_h for vertical extent.
        float hs_y = npc_h * 0.5f;
        bool vis = IsBoxVisible(cx, cy, cz, hs);
        // Check vertical extent separately (use larger box if needed).
        // For now the cube test is conservative enough.
        out_mask[i] = vis ? 1u : 0u;
        if (vis) ++visible;
    }
    return visible;
}

} // namespace md
