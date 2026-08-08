#include "terrain_gen_internal.h"

// Gradient-limited ramp from h_bnd (at the boundary) out toward the
// UNTOUCHED original values, capping the height CHANGE PER VERTEX at
// max_step instead of using a fixed blend fraction. Fixes a real bug in
// the previous fixed-N=15-linear-blend kernel (2026-07-26, quadtree-LOD
// terrain rewrite Phase 7 investigation — see CLAUDE_STATE.md): that
// kernel's own doc comment assumed a "worst case" raw discontinuity of
// ~22.9m/7.8m step, but real measured Kenshi fullmap zone-pair seams go
// far past that (up to 157.8m/7.2m step, confirmed via
// TerrainQuadtreeRenderer::UploadHeightmapRegion's adjacent-sample scan) —
// the fixed linear schedule only blends the CLOSEST interior vertex 6.25%
// toward the boundary average (t=1/16), leaving ~94% of an arbitrarily
// large discontinuity concentrated in that single first step: a near-
// vertical spike wall in the quadtree's real per-pixel-normal geometry
// (the old per-chunk system's own per-vertex normal baking may have
// visually absorbed this same underlying data without ever showing a
// literal geometric wall — not investigated, out of scope for this fix).
// This ramp instead walks outward one vertex at a time, moving at most
// max_step per vertex toward the true original value and stopping as soon
// as it catches up — self-scaling to the ACTUAL severity of each specific
// boundary instead of assuming one fixed worst case, with N_MAX only a
// safety cap on how far it's allowed to reach.
static void s_gradient_ramp(float* h, int stride, int n_max, float h_bnd, float max_step) {
    float prev = h_bnd;
    for (int k = 1; k <= n_max; ++k) {
        float orig = h[k * stride];
        float delta = orig - prev;
        if (fabsf(delta) <= max_step) break; // already close enough — leave the rest untouched
        prev = prev + (delta > 0.f ? max_step : -max_step);
        h[k * stride] = prev;
    }
}

void TerrainAtlas_SmoothBoundaries() {
    if (!s_atlas_loaded) return;
    // See s_gradient_ramp's doc comment for the bug this kernel replaces.
    // max_step=15m over one ~7.2m atlas step is still a steep (~64% grade,
    // ~32.6°) per-vertex ramp — real, legitimately steep Kenshi cliffs away
    // from a zone SEAM keep their actual shape untouched (this function only
    // ever touches the N_MAX=40 vertices nearest a zone boundary); this only
    // bounds how fast an AUTHORING-ARTIFACT seam is allowed to resolve.
    constexpr int N_MAX = 40;
    constexpr float kMaxStepM = 15.f;

    // X-direction seams: average col=ATLAS_VERTS-1(128) of zone(zx) with col=0 of zone(zx+1),
    // then ramp outward from that shared average on each side. This ensures
    // the shared boundary vertex is identical in both chunks.
    for (int zy = 0; zy < ATLAS_ZONES; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES - 1; ++zx) {
            for (int row = 0; row < ATLAS_VERTS; ++row) {
                float& h_left  = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1, row)]; // col=128 of A
                float& h_right = s_atlas_h[s_atlas_hi(zx+1, zy, 0,             row)]; // col=0   of B
                float h_bnd = (h_left + h_right) * 0.5f;  // average → shared vertex
                h_left  = h_bnd;
                h_right = h_bnd;
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx+1, zy, 0, row)], 1, N_MAX, h_bnd, kMaxStepM);
                // Left side walks toward DECREASING col from ATLAS_VERTS-1 —
                // stride -1 from that same base index.
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx, zy, ATLAS_VERTS-1, row)], -1, N_MAX, h_bnd, kMaxStepM);
            }
        }
    }

    // Z-direction seams: average row=64 of zone(zy) with row=0 of zone(zy+1).
    for (int zy = 0; zy < ATLAS_ZONES - 1; ++zy) {
        for (int zx = 0; zx < ATLAS_ZONES; ++zx) {
            for (int col = 0; col < ATLAS_VERTS; ++col) {
                float& h_bot = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1)]; // row=128 of A
                float& h_top = s_atlas_h[s_atlas_hi(zx, zy+1, col, 0            )]; // row=0   of B
                float h_bnd = (h_bot + h_top) * 0.5f;
                h_bot = h_bnd;
                h_top = h_bnd;
                // row stride within one (zx,zy) zone block is ATLAS_VERTS
                // (row-major, see s_atlas_hi) — same trick as the X-loop,
                // just with that stride instead of 1.
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx, zy+1, col, 0)], ATLAS_VERTS, N_MAX, h_bnd, kMaxStepM);
                s_gradient_ramp(&s_atlas_h[s_atlas_hi(zx, zy, col, ATLAS_VERTS-1)], -ATLAS_VERTS, N_MAX, h_bnd, kMaxStepM);
            }
        }
    }
}

void TerrainAtlas_StitchEdge(int zx, int zy, int dir) {
    if (!s_atlas_loaded) return;
    if (zx < 0 || zy < 0 || zx >= ATLAS_ZONES || zy >= ATLAS_ZONES) return;
    constexpr int N = 15;
    if (dir == 0 && zx < ATLAS_ZONES - 1) {
        for (int row = 0; row < ATLAS_VERTS; ++row) {
            float& h_left  = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1, row)];
            float& h_right = s_atlas_h[s_atlas_hi(zx+1, zy, 0,             row)];
            float h_bnd = (h_left + h_right) * 0.5f;
            h_left = h_bnd; h_right = h_bnd;
            for (int k = 1; k <= N; ++k) {
                float t = (float)k / (N + 1);
                float& hr = s_atlas_h[s_atlas_hi(zx+1, zy, k,             row)];
                hr = (1.f - t) * h_bnd + t * hr;
                float& hl = s_atlas_h[s_atlas_hi(zx,   zy, ATLAS_VERTS-1-k, row)];
                hl = (1.f - t) * h_bnd + t * hl;
            }
        }
    } else if (dir == 1 && zy < ATLAS_ZONES - 1) {
        for (int col = 0; col < ATLAS_VERTS; ++col) {
            float& h_bot = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1)];
            float& h_top = s_atlas_h[s_atlas_hi(zx, zy+1, col, 0            )];
            float h_bnd = (h_bot + h_top) * 0.5f;
            h_bot = h_bnd; h_top = h_bnd;
            for (int k = 1; k <= N; ++k) {
                float t = (float)k / (N + 1);
                float& ht = s_atlas_h[s_atlas_hi(zx, zy+1, col, k)];
                ht = (1.f - t) * h_bnd + t * ht;
                float& hb = s_atlas_h[s_atlas_hi(zx, zy,   col, ATLAS_VERTS-1-k)];
                hb = (1.f - t) * h_bnd + t * hb;
            }
        }
    }
}
