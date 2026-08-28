#include <monkey_dust/render/chunk_lod_world.h>
#ifdef MD_SDL_GPU
#include <monkey_dust/platform/md_log.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace {
// Named distinctly from chunk_lod_renderer.cpp's identical structs -- this
// project's Unity Build concatenates translation units, and two anonymous
// namespaces both declaring `struct ChunkLodUBO` in the same TU collide.
struct ChunkLodWorldUBO {
    float vp[16];
    float origin_xyz_pad[4];
};
struct ChunkLodWorldVertex { float x, y, z, nx, ny, nz; };  // matches tools/chunklod_bake's export layout exactly
constexpr float kChunkSizeM = 460.8f;                // matches engine/include/monkey_dust/world/chunk_def.h
}  // namespace

bool ChunkLodWorld::Init(SDL_GPUDevice* /*dev*/, const char* bake_dir, int atlas_zones) {
    std::strncpy(bake_dir_, bake_dir, sizeof(bake_dir_) - 1);
    atlas_zones_ = atlas_zones;

    GpuPipeline::Desc pd;
    pd.layout.count = 2;
    pd.layout.attribs[0] = { 0, 0,  GpuAttribFmt::F3 };
    pd.layout.attribs[1] = { 1, 12, GpuAttribFmt::F3 };
    pd.layout.stride = sizeof(ChunkLodWorldVertex);
    pd.raster.depth_test  = true;
    pd.raster.depth_write = true;
    pd.raster.cull_back   = true;
    pd.has_depth_target   = true;
    pd.vert_uniform_bufs  = 1;
    pd.vert_path = "shaders/chunk_lod.vert";
    pd.frag_path = "shaders/terrain_gbuffer_mini.frag";  // reused unmodified, same as ChunkLodRenderer
    pd.color_format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    if (!pipeline_.Create(pd)) {
        MD_LOG(MD_LOG_WARNING, "[ChunkLodWorld] pipeline create failed");
        return false;
    }
    ready_ = true;
    return true;
}

void ChunkLodWorld::Shutdown(SDL_GPUDevice* /*dev*/) {
    for (auto& s : slots_) {
        if (s.loaded) { s.vbo.Shutdown(); s.ibo.Shutdown(); s.loaded = false; }
    }
    pipeline_.Destroy();
    ready_ = false;
}

int ChunkLodWorld::FindSlot(int zx, int zy) const {
    for (int i = 0; i < kMaxLoadedZones; ++i) {
        if (slots_[i].loaded && slots_[i].zx == zx && slots_[i].zy == zy) return i;
    }
    return -1;
}

int ChunkLodWorld::SelectLod(float dist_to_zone_center) {
    // Same log2(distance) falloff as chunklod.cpp's real compute_lod()
    // (tmp_/chunklod_reference/chunklod.cpp:2145-2161), applied at
    // whole-zone granularity: kLod0MaxDist is the distance within which
    // the finest baked level is used; each doubling beyond that drops
    // one more level, clamped to the levels we actually baked.
    constexpr float kLod0MaxDist = 300.0f;
    if (dist_to_zone_center <= kLod0MaxDist) return 0;
    int lod = 1 + (int)std::floor(std::log2(dist_to_zone_center / kLod0MaxDist));
    return lod < kNumLodLevels - 1 ? lod : kNumLodLevels - 1;
}

bool ChunkLodWorld::LoadZoneMesh(SDL_GPUDevice* /*dev*/, int zx, int zy, int lod, ZoneSlot& slot) {
    char path[600];
    std::snprintf(path, sizeof(path), "%s/zone_%d_%d_lod%d.mesh", bake_dir_, zx, zy, lod);
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    uint32_t vcount = 0, icount = 0;
    bool ok = std::fread(&vcount, sizeof(vcount), 1, f) == 1 &&
              std::fread(&icount, sizeof(icount), 1, f) == 1;
    // Sanity-bound counts read from the bake file before resize() -- a
    // truncated/corrupted zone_*.mesh (e.g. bake interrupted mid-write,
    // disk corruption) could otherwise hand resize() a huge garbage
    // vcount/icount, attempting a multi-GB allocation and crashing the
    // game process. 16M is far above any real zone (finest LOD is a few
    // thousand verts, see chunklod_bake's own Phase 2 gate results) but
    // well below what would risk the uint32_t byte-size cast below
    // overflowing (16M * sizeof(ChunkLodWorldVertex)=24B ~= 384MB).
    constexpr uint32_t kMaxSaneCount = 16u * 1024u * 1024u;
    if (ok && (vcount > kMaxSaneCount || icount > kMaxSaneCount)) {
        MD_LOG(MD_LOG_WARNING,
               "[ChunkLodWorld] zone %d,%d: implausible vcount=%u/icount=%u -- "
               "rejecting (corrupted or truncated bake file?)",
               zx, zy, vcount, icount);
        ok = false;
    }
    std::vector<ChunkLodWorldVertex> verts;
    std::vector<uint32_t> indices;
    if (ok) {
        verts.resize(vcount);
        indices.resize(icount);
        ok = std::fread(verts.data(), sizeof(ChunkLodWorldVertex), vcount, f) == vcount &&
             std::fread(indices.data(), sizeof(uint32_t), icount, f) == icount;
    }
    std::fclose(f);
    if (!ok || vcount == 0 || icount == 0) return false;

    slot.vbo.Init(0x8892u /*GL_ARRAY_BUFFER*/, verts.data(), (uint32_t)(verts.size() * sizeof(ChunkLodWorldVertex)));
    slot.ibo.Init(0x8893u /*GL_ELEMENT_ARRAY_BUFFER*/, indices.data(), (uint32_t)(indices.size() * sizeof(uint32_t)));
    slot.index_count = icount;
    slot.zx = zx; slot.zy = zy; slot.lod = lod; slot.loaded = true;
    return true;
}

namespace {
float DistanceToZoneCenter(float cam_x, float cam_z, int zx, int zy) {
    float cx = ((float)zx + 0.5f) * kChunkSizeM;
    float cz = ((float)zy + 0.5f) * kChunkSizeM;
    float dx = cam_x - cx, dz = cam_z - cz;
    return std::sqrt(dx * dx + dz * dz);
}
}  // namespace

void ChunkLodWorld::UpdateStreaming(SDL_GPUDevice* dev, float cam_x, float cam_z, int radius_zones) {
    if (!ready_) return;

    // kMaxLoadedZones=81 only covers a (2*radius+1)^2 square up to
    // radius_zones==4 (9x9=81). A caller passing radius_zones>=5 (11x11=121)
    // would silently under-render -- the load loop below simply can't find a
    // free slot for zones beyond the 81st, with no other symptom. Clamp and
    // warn once instead of failing silently.
    const int kMaxSafeRadius = 4;
    if (radius_zones > kMaxSafeRadius) {
        MD_LOG(MD_LOG_WARNING,
               "[ChunkLodWorld] radius_zones=%d exceeds kMaxLoadedZones capacity "
               "(max safe radius=%d for %d slots) -- clamping",
               radius_zones, kMaxSafeRadius, kMaxLoadedZones);
        radius_zones = kMaxSafeRadius;
    }

    int center_zx = (int)std::floor(cam_x / kChunkSizeM);
    int center_zy = (int)std::floor(cam_z / kChunkSizeM);
    if (center_zx != last_center_zx_ || center_zy != last_center_zy_) {
        last_center_zx_ = center_zx; last_center_zy_ = center_zy;

        // Unload slots now outside the (Chebyshev) radius.
        for (auto& s : slots_) {
            if (!s.loaded) continue;
            if (std::abs(s.zx - center_zx) > radius_zones || std::abs(s.zy - center_zy) > radius_zones) {
                s.vbo.Shutdown(); s.ibo.Shutdown(); s.loaded = false;
            }
        }
        // Load zones now inside the radius that aren't loaded yet, at
        // whatever LOD their real distance from the camera calls for.
        for (int dz = -radius_zones; dz <= radius_zones; ++dz) {
            for (int dx = -radius_zones; dx <= radius_zones; ++dx) {
                int zx = center_zx + dx, zy = center_zy + dz;
                if (zx < 0 || zy < 0 || zx >= atlas_zones_ || zy >= atlas_zones_) continue;
                if (FindSlot(zx, zy) >= 0) continue;
                bool placed = false;
                for (auto& s : slots_) {
                    if (!s.loaded) {
                        int lod = SelectLod(DistanceToZoneCenter(cam_x, cam_z, zx, zy));
                        placed = LoadZoneMesh(dev, zx, zy, lod, s);
                        break;
                    }
                }
                if (!placed) {
                    MD_LOG(MD_LOG_WARNING,
                           "[ChunkLodWorld] no free slot for zone %d,%d (all %d slots full) -- zone will not render",
                           zx, zy, kMaxLoadedZones);
                }
            }
        }
    }

    // Фаза 2+3, MERGED (Фаза 4 fix, docs/TERRAIN_CHUNKLOD_PORT_PLAN.md's
    // reopened plan): compute every loaded zone's FINAL LOD -- real
    // per-zone distance-LOD (Фаза 2) relaxed toward neighbor consistency
    // to close the cross-zone seam (Фаза 3, same idea as B2's neighbor-
    // balancing post-pass for the CDLOD quadtree, task #564) -- into a
    // local scratch array BEFORE touching any slot state, and commit
    // (reload) only once at the end.
    //
    // The ORIGINAL two-separate-mutating-passes version (one loop that
    // reloaded straight to the pure-distance answer, immediately followed
    // by a second loop that reloaded AGAIN to a neighbor-clamped answer)
    // oscillated forever at a stationary camera: the distance pass
    // compared its fresh answer against s.lod, but s.lod was whatever the
    // neighbor-clamp pass had forced it to on the PREVIOUS call -- so the
    // distance pass reverted that clamp every single call, and the
    // neighbor-clamp pass immediately re-forced it, two reloads per
    // affected zone every call, forever. Confirmed both by direct code
    // reading (the distance pass's `desired_lod != s.lod` check has no
    // way to distinguish "camera moved" from "neighbor-clamp changed
    // s.lod last frame") and by md.chunklod_stats(): reload_count climbed
    // ~130-144 per call at a fully stationary camera with zone_count and
    // triangle_count both perfectly stable -- this, not extra triangles
    // or bad shading, was the real cause of the measured 5.3x GPU-time
    // regression vs TerrainQuadtreeRenderer (constant disk I/O + GPU
    // buffer teardown/recreation every frame instead of a one-time cost).
    //
    // Computing the final answer once into `desired[]` (a pure function
    // of camera distance + neighbor relaxation, never of the PREVIOUS
    // frame's committed s.lod) fixes this: on an unchanged camera,
    // `desired[]` is identical frame to frame, matches every slot's
    // current s.lod, and the commit loop below reloads nothing.
    int desired[kMaxLoadedZones];
    for (int i = 0; i < kMaxLoadedZones; ++i) {
        desired[i] = slots_[i].loaded
            ? SelectLod(DistanceToZoneCenter(cam_x, cam_z, slots_[i].zx, slots_[i].zy))
            : -1;
    }
    // Two relaxation passes: enough to propagate a change across the
    // largest supported streaming radius (radius_zones<=4 -> a 9x9 grid
    // needs at most 4 hops centre-to-edge in the worst case, but real
    // camera-distance LOD assignments are already spatially smooth --
    // observed empirically to converge in 2 passes for every configuration
    // tested, not proven to converge for a pathological hand-crafted one).
    static constexpr int kDx[4] = {1, -1, 0, 0};
    static constexpr int kDz[4] = {0, 0, 1, -1};
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < kMaxLoadedZones; ++i) {
            if (!slots_[i].loaded) continue;
            int min_neighbor = desired[i];
            for (int k = 0; k < 4; ++k) {
                int ni = FindSlot(slots_[i].zx + kDx[k], slots_[i].zy + kDz[k]);
                if (ni >= 0) min_neighbor = std::min(min_neighbor, desired[ni]);
            }
            desired[i] = std::min(desired[i], min_neighbor + 1);
        }
    }
    for (int i = 0; i < kMaxLoadedZones; ++i) {
        ZoneSlot& s = slots_[i];
        if (!s.loaded || desired[i] == s.lod) continue;
        int zx = s.zx, zy = s.zy, new_lod = desired[i];
        s.vbo.Shutdown(); s.ibo.Shutdown(); s.loaded = false;
        LoadZoneMesh(dev, zx, zy, new_lod, s);
        ++lod_reload_count_;
    }
}

void ChunkLodWorld::Draw(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cmd, const float* vp16) {
    if (!ready_) return;
    SDL_BindGPUGraphicsPipeline(rp, pipeline_.SDLPipeline());
    for (auto& s : slots_) {
        if (!s.loaded) continue;
        ChunkLodWorldUBO ubo{};
        std::memcpy(ubo.vp, vp16, 64);
        ubo.origin_xyz_pad[0] = (float)s.zx * kChunkSizeM;
        ubo.origin_xyz_pad[1] = 0.0f;
        ubo.origin_xyz_pad[2] = (float)s.zy * kChunkSizeM;
        ubo.origin_xyz_pad[3] = 0.0f;
        SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));

        SDL_GPUBufferBinding vb{ s.vbo.SDLBuffer(), 0u };
        SDL_BindGPUVertexBuffers(rp, 0, &vb, 1);
        SDL_GPUBufferBinding ib{ s.ibo.SDLBuffer(), 0u };
        SDL_BindGPUIndexBuffer(rp, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(rp, s.index_count, 1, 0, 0, 0);
    }
}

int ChunkLodWorld::LoadedZoneCount() const {
    int n = 0;
    for (auto& s : slots_) if (s.loaded) ++n;
    return n;
}

int64_t ChunkLodWorld::LoadedTriangleCount() const {
    int64_t n = 0;
    for (auto& s : slots_) if (s.loaded) n += s.index_count / 3;
    return n;
}

void ChunkLodWorld::LodHistogram(int out_counts[kNumLodLevels]) const {
    for (int i = 0; i < kNumLodLevels; ++i) out_counts[i] = 0;
    for (auto& s : slots_) {
        if (s.loaded && s.lod >= 0 && s.lod < kNumLodLevels) ++out_counts[s.lod];
    }
}
#endif  // MD_SDL_GPU
