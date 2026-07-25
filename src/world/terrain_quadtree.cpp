#include <monkey_dust/world/terrain_quadtree.h>
#include <cmath>

void TerrainQuadtree::Init(float world_size_m, int max_depth) {
    root_.origin_x = -world_size_m * 0.5f;
    root_.origin_z = -world_size_m * 0.5f;
    root_.size     = world_size_m;
    root_.depth    = 0;
    max_depth_     = max_depth;
}

namespace {

// Generous fixed Y range for the frustum AABB test — real Kenshi elevation
// is a few hundred metres; this node has no dependency on TerrainAtlas (or
// anything outside terrain_quadtree.{h,cpp}) to look up a real per-node
// height bound, and a coarse box-vs-4-planes cull doesn't need one to be
// correct, only conservative.
constexpr float kAabbMinY = -500.f;
constexpr float kAabbMaxY = 4000.f;

bool AabbInFrustum(float ox, float oz, float size, const float fp[16]) {
    for (int p = 0; p < 4; ++p) {
        const float* pl = fp + p * 4;
        // Positive vertex: the AABB corner furthest along the plane normal.
        // If even that corner is outside (dot < 0), the whole box is.
        float px = (pl[0] >= 0.f) ? (ox + size) : ox;
        float pz = (pl[2] >= 0.f) ? (oz + size) : oz;
        float py = (pl[1] >= 0.f) ? kAabbMaxY   : kAabbMinY;
        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.f) return false;
    }
    return true;
}

void SelectRec(const TerrainQuadNode& node, int max_depth,
                const float cam[3], const float fp[16],
                const float* lod_dist,
                TerrainQuadVisibleNode* out, int& count) {
    if (count >= TerrainQuadtree::MAX_VISIBLE_NODES) return;
    if (!AabbInFrustum(node.origin_x, node.origin_z, node.size, fp)) return;

    float cx = node.origin_x + node.size * 0.5f;
    float cz = node.origin_z + node.size * 0.5f;
    float dx = cam[0] - cx;
    float dz = cam[2] - cz;
    float dist = sqrtf(dx * dx + dz * dz);

    bool at_max_depth = node.depth >= max_depth;
    if (at_max_depth || dist > lod_dist[node.depth]) {
        out[count++] = { node.origin_x, node.origin_z, node.size, node.depth };
        return;
    }

    float half = node.size * 0.5f;
    for (int gz = 0; gz < 2; ++gz) {
        for (int gx = 0; gx < 2; ++gx) {
            TerrainQuadNode child;
            child.origin_x = node.origin_x + (float)gx * half;
            child.origin_z = node.origin_z + (float)gz * half;
            child.size     = half;
            child.depth    = node.depth + 1;
            SelectRec(child, max_depth, cam, fp, lod_dist, out, count);
        }
    }
}

} // namespace

int TerrainQuadtree::SelectVisible(const float cam_pos[3], const float frustum_planes[16],
                                    const float* lod_distances,
                                    TerrainQuadVisibleNode* out) const {
    int count = 0;
    SelectRec(root_, max_depth_, cam_pos, frustum_planes, lod_distances, out, count);
    return count;
}
