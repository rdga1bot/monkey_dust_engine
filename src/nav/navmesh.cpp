#include <monkey_dust/nav/navmesh.h>
#include <cstdio>
#include <cstring>
#include <cmath>

// ── Внутрішній magic для бінарного формату ───────────────
static constexpr int NAVMESH_MAGIC   = 'M' << 24 | 'D' << 16 | 'N' << 8 | 'V';
static constexpr int NAVMESH_VERSION = 1;

// ─────────────────────────────────────────────────────────
// Внутрішня перезбудова навмешу з terrain_verts_ + obstacles_.
bool NavMesh::BuildInternal()
{
    if (terrain_nverts_ <= 0 || terrain_ntris_ <= 0) return false;

    Destroy();
    ctx_ = new rcContext(false);

    float cs = rebuild_cs_, ch = rebuild_ch_;

    rcConfig cfg = {};
    cfg.cs                     = cs;
    cfg.ch                     = ch;
    cfg.walkableSlopeAngle     = 45.0f;
    cfg.walkableHeight         = (int)ceilf(1.8f / ch);
    cfg.walkableClimb          = (int)floorf(0.3f / ch);
    cfg.walkableRadius         = (int)ceilf(0.4f / cs);
    cfg.maxEdgeLen             = (int)(12.0f / cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea          = (int)rcSqr(8);
    cfg.mergeRegionArea        = (int)rcSqr(20);
    cfg.maxVertsPerPoly        = 6;
    cfg.detailSampleDist       = 6.0f;
    cfg.detailSampleMaxError   = 1.0f;

    rcCalcBounds(terrain_verts_, terrain_nverts_, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcHeightfield* solid = rcAllocHeightfield();
    if (!rcCreateHeightfield(ctx_, *solid, cfg.width, cfg.height,
                             cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        rcFreeHeightField(solid);
        fprintf(stderr, "[NavMesh] BuildInternal: createHeightfield failed\n");
        return false;
    }

    unsigned char* triAreas = new unsigned char[terrain_ntris_]();
    rcMarkWalkableTriangles(ctx_, cfg.walkableSlopeAngle,
                            terrain_verts_, terrain_nverts_,
                            terrain_tris_,  terrain_ntris_, triAreas);
    rcRasterizeTriangles(ctx_, terrain_verts_, terrain_nverts_,
                         terrain_tris_, triAreas, terrain_ntris_,
                         *solid, cfg.walkableClimb);
    delete[] triAreas;

    rcFilterLowHangingWalkableObstacles(ctx_, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(ctx_, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(ctx_, cfg.walkableHeight, *solid);

    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    rcBuildCompactHeightfield(ctx_, cfg.walkableHeight, cfg.walkableClimb,
                              *solid, *chf);
    rcFreeHeightField(solid);

    // Позначити зони перешкод як непрохідні
    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        if (!obstacles_[i].valid) continue;
        rcMarkBoxArea(ctx_, obstacles_[i].bmin, obstacles_[i].bmax,
                      RC_NULL_AREA, *chf);
    }

    rcErodeWalkableArea(ctx_, cfg.walkableRadius, *chf);
    rcBuildDistanceField(ctx_, *chf);
    rcBuildRegions(ctx_, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea);

    rcContourSet* cset = rcAllocContourSet();
    rcBuildContours(ctx_, *chf, cfg.maxSimplificationError,
                    cfg.maxEdgeLen, *cset);

    rcPolyMesh* pmesh = rcAllocPolyMesh();
    rcBuildPolyMesh(ctx_, *cset, cfg.maxVertsPerPoly, *pmesh);

    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    rcBuildPolyMeshDetail(ctx_, *pmesh, *chf,
                          cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh);

    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);

    for (int i = 0; i < pmesh->npolys; ++i)
        if (pmesh->areas[i] == RC_WALKABLE_AREA)
            pmesh->flags[i] = 0x01;

    dtNavMeshCreateParams params = {};
    params.verts            = pmesh->verts;
    params.vertCount        = pmesh->nverts;
    params.polys            = pmesh->polys;
    params.polyAreas        = pmesh->areas;
    params.polyFlags        = pmesh->flags;
    params.polyCount        = pmesh->npolys;
    params.nvp              = pmesh->nvp;
    params.detailMeshes     = dmesh->meshes;
    params.detailVerts      = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris       = dmesh->tris;
    params.detailTriCount   = dmesh->ntris;
    params.walkableHeight   = 1.8f;
    params.walkableRadius   = 0.4f;
    params.walkableClimb    = 0.3f;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs               = cfg.cs;
    params.ch               = cfg.ch;
    params.buildBvTree      = true;

    unsigned char* navData     = nullptr;
    int            navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        fprintf(stderr, "[NavMesh] BuildInternal: dtCreateNavMeshData failed\n");
        return false;
    }
    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    nav_mesh_ = dtAllocNavMesh();
    if (nav_mesh_->init(navData, navDataSize, DT_TILE_FREE_DATA) != DT_SUCCESS) {
        dtFreeNavMesh(nav_mesh_);
        nav_mesh_ = nullptr;
        fprintf(stderr, "[NavMesh] BuildInternal: dtNavMesh::init failed\n");
        return false;
    }

    nav_query_ = dtAllocNavMeshQuery();
    nav_query_->init(nav_mesh_, 2048);
    return true;
}

// ─────────────────────────────────────────────────────────
bool NavMesh::RebuildTile(float wx, float wz,
                           const float* obs_verts, int nobs_verts,
                           const int*   /*obs_tris*/, int nobs_tris)
{
    if (terrain_nverts_ <= 0) {
        fprintf(stderr, "[NavMesh] RebuildTile: no terrain stored (call Build first)\n");
        return false;
    }

    // Знайти або звільнити слот перешкоди по центру (wx, wz)
    static constexpr float MATCH_R2 = 9.0f;  // 3м радіус пошуку
    int slot = -1;
    for (int i = 0; i < MAX_OBSTACLES; ++i) {
        if (!obstacles_[i].valid) continue;
        float dx = obstacles_[i].cx - wx, dz = obstacles_[i].cz - wz;
        if (dx*dx + dz*dz < MATCH_R2) { slot = i; break; }
    }

    if (obs_verts && nobs_verts > 0 && nobs_tris > 0) {
        // Додати або оновити перешкоду
        if (slot < 0) {
            for (int i = 0; i < MAX_OBSTACLES; ++i) {
                if (!obstacles_[i].valid) { slot = i; break; }
            }
        }
        if (slot < 0) {
            fprintf(stderr, "[NavMesh] RebuildTile: obstacle list full\n");
            return false;
        }
        // Обчислити AABB з переданих вершин
        float bmin[3] = {1e9f, 0.0f, 1e9f};
        float bmax[3] = {-1e9f, 2.5f, -1e9f};
        for (int i = 0; i < nobs_verts; ++i) {
            if (obs_verts[i*3]   < bmin[0]) bmin[0] = obs_verts[i*3];
            if (obs_verts[i*3+2] < bmin[2]) bmin[2] = obs_verts[i*3+2];
            if (obs_verts[i*3]   > bmax[0]) bmax[0] = obs_verts[i*3];
            if (obs_verts[i*3+2] > bmax[2]) bmax[2] = obs_verts[i*3+2];
        }
        obstacles_[slot].bmin[0] = bmin[0]; obstacles_[slot].bmin[1] = 0.0f;
        obstacles_[slot].bmin[2] = bmin[2];
        obstacles_[slot].bmax[0] = bmax[0]; obstacles_[slot].bmax[1] = 2.5f;
        obstacles_[slot].bmax[2] = bmax[2];
        obstacles_[slot].cx    = wx;
        obstacles_[slot].cz    = wz;
        obstacles_[slot].valid = true;
    } else {
        // Видалити перешкоду
        if (slot >= 0) obstacles_[slot].valid = false;
    }

    bool ok = BuildInternal();
    if (!ok)
        fprintf(stderr, "[NavMesh] RebuildTile: rebuild failed at (%.1f, %.1f)\n", wx, wz);
    return ok;
}

// ─────────────────────────────────────────────────────────
bool NavMesh::Build(const float* verts, int nverts,
                    const int*   tris,  int ntris,
                    float cs, float ch)
{
    // Зберігаємо terrain для майбутніх RebuildTile
    if (nverts <= MAX_TERRAIN_VERTS && ntris <= MAX_TERRAIN_TRIS) {
        memcpy(terrain_verts_, verts, nverts * 3 * sizeof(float));
        memcpy(terrain_tris_,  tris,  ntris  * 3 * sizeof(int));
        terrain_nverts_ = nverts;
        terrain_ntris_  = ntris;
        rebuild_cs_     = cs;
        rebuild_ch_     = ch;
    }
    // Скидаємо перешкоди при повному rebuild
    for (auto& o : obstacles_) o.valid = false;

    return BuildInternal();
}

// ─────────────────────────────────────────────────────────
int NavMesh::FindPath(float sx, float sy, float sz,
                      float ex, float ey, float ez,
                      float* out_verts, int max_verts) const
{
    if (!nav_query_ || max_verts <= 0) return 0;

    dtQueryFilter filter;
    filter.setIncludeFlags(0x01);
    filter.setExcludeFlags(0x00);

    const float half_ext[3] = { 1.0f, 2.0f, 1.0f };
    const float start[3]    = { sx, sy, sz };
    const float end[3]      = { ex, ey, ez };

    dtPolyRef start_ref = 0, end_ref = 0;
    float nearest_start[3], nearest_end[3];

    nav_query_->findNearestPoly(start, half_ext, &filter,
                                &start_ref, nearest_start);
    nav_query_->findNearestPoly(end,   half_ext, &filter,
                                &end_ref,   nearest_end);

    if (!start_ref || !end_ref) return 0;

    dtPolyRef poly_path[MAX_POLYS];
    int       poly_count = 0;
    nav_query_->findPath(start_ref, end_ref,
                         nearest_start, nearest_end,
                         &filter, poly_path, &poly_count, MAX_POLYS);
    if (poly_count == 0) return 0;

    float straight[MAX_POLYS * 3];
    int   straight_count = 0;
    unsigned char straight_flags[MAX_POLYS];
    dtPolyRef straight_refs[MAX_POLYS];

    nav_query_->findStraightPath(
        nearest_start, nearest_end,
        poly_path, poly_count,
        straight, straight_flags, straight_refs,
        &straight_count, MAX_POLYS);

    int copy_count = straight_count < max_verts ? straight_count : max_verts;
    memcpy(out_verts, straight, copy_count * 3 * sizeof(float));
    return copy_count;
}

// ─────────────────────────────────────────────────────────
bool NavMesh::SaveToFile(const char* path) const
{
    if (!nav_mesh_) return false;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    // Magic + version
    fwrite(&NAVMESH_MAGIC,   sizeof(int), 1, f);
    fwrite(&NAVMESH_VERSION, sizeof(int), 1, f);

    // Tile count — через const pointer (non-const getTile є private)
    const dtNavMesh* cnm = nav_mesh_;
    int tile_count = 0;
    for (int i = 0; i < cnm->getMaxTiles(); ++i)
        if (cnm->getTile(i)->header) tile_count++;
    fwrite(&tile_count, sizeof(int), 1, f);

    for (int i = 0; i < cnm->getMaxTiles(); ++i) {
        const dtMeshTile* tile = cnm->getTile(i);
        if (!tile->header) continue;
        int data_size = tile->dataSize;
        fwrite(&data_size, sizeof(int), 1, f);
        fwrite(tile->data, 1, data_size, f);
    }

    fclose(f);
    return true;
}

// ─────────────────────────────────────────────────────────
bool NavMesh::LoadFromFile(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    int magic = 0, version = 0;
    fread(&magic,   sizeof(int), 1, f);
    fread(&version, sizeof(int), 1, f);

    if (magic != NAVMESH_MAGIC || version != NAVMESH_VERSION) {
        fclose(f);
        return false;
    }

    int tile_count = 0;
    fread(&tile_count, sizeof(int), 1, f);

    Destroy();
    nav_mesh_ = dtAllocNavMesh();

    dtNavMeshParams params = {};
    params.orig[0] = -500.0f; params.orig[1] = 0.0f; params.orig[2] = -500.0f;
    params.tileWidth  = 32.0f;
    params.tileHeight = 32.0f;
    params.maxTiles   = 128;
    params.maxPolys   = 32768;
    nav_mesh_->init(&params);

    for (int i = 0; i < tile_count; ++i) {
        int data_size = 0;
        fread(&data_size, sizeof(int), 1, f);
        unsigned char* data = (unsigned char*)dtAlloc(data_size, DT_ALLOC_PERM);
        fread(data, 1, data_size, f);
        nav_mesh_->addTile(data, data_size, DT_TILE_FREE_DATA, 0, nullptr);
    }

    fclose(f);

    nav_query_ = dtAllocNavMeshQuery();
    nav_query_->init(nav_mesh_, 2048);
    return true;
}

// ── BuildFromExternal — Recast pipeline on caller-supplied geometry ────────────
// Identical pipeline to BuildInternal() but takes geometry as parameters
// instead of reading from terrain_* members.  Used by BuildTileMap().
bool NavMesh::BuildFromExternal(const float* verts, int nverts,
                                 const int*   tris,  int ntris,
                                 float cs, float ch)
{
    Destroy();
    ctx_ = new rcContext(false);

    rcConfig cfg = {};
    cfg.cs                     = cs;
    cfg.ch                     = ch;
    cfg.walkableSlopeAngle     = 45.0f;
    cfg.walkableHeight         = (int)ceilf(1.8f  / ch);
    // Minimum climb = 1 cell so gentle terrain is walkable regardless of ch.
    cfg.walkableClimb          = (int)floorf(0.6f / ch);
    if (cfg.walkableClimb < 1) cfg.walkableClimb = 1;
    cfg.walkableRadius         = (int)ceilf(0.4f  / cs);
    cfg.maxEdgeLen             = (int)(12.0f / cs);
    cfg.maxSimplificationError = 1.3f;
    cfg.minRegionArea          = 1;               // preserve single-tile corridors
    cfg.mergeRegionArea        = (int)rcSqr(5);
    cfg.maxVertsPerPoly        = 6;
    cfg.detailSampleDist       = 6.0f;
    cfg.detailSampleMaxError   = 1.0f;

    rcCalcBounds(verts, nverts, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcHeightfield* solid = rcAllocHeightfield();
    if (!rcCreateHeightfield(ctx_, *solid, cfg.width, cfg.height,
                             cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        rcFreeHeightField(solid);
        fprintf(stderr, "[NavMesh] BuildFromExternal: createHeightfield failed\n");
        return false;
    }

    unsigned char* triAreas = new unsigned char[ntris]();
    rcMarkWalkableTriangles(ctx_, cfg.walkableSlopeAngle,
                            verts, nverts, tris, ntris, triAreas);
    rcRasterizeTriangles(ctx_, verts, nverts, tris, triAreas, ntris,
                         *solid, cfg.walkableClimb);
    delete[] triAreas;

    rcFilterLowHangingWalkableObstacles(ctx_, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(ctx_, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(ctx_, cfg.walkableHeight, *solid);

    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    rcBuildCompactHeightfield(ctx_, cfg.walkableHeight, cfg.walkableClimb,
                              *solid, *chf);
    rcFreeHeightField(solid);

    rcErodeWalkableArea(ctx_, cfg.walkableRadius, *chf);
    rcBuildDistanceField(ctx_, *chf);
    rcBuildRegions(ctx_, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea);

    rcContourSet* cset = rcAllocContourSet();
    rcBuildContours(ctx_, *chf, cfg.maxSimplificationError,
                    cfg.maxEdgeLen, *cset);

    rcPolyMesh* pmesh = rcAllocPolyMesh();
    rcBuildPolyMesh(ctx_, *cset, cfg.maxVertsPerPoly, *pmesh);

    rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
    rcBuildPolyMeshDetail(ctx_, *pmesh, *chf,
                          cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh);

    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);

    for (int i = 0; i < pmesh->npolys; ++i)
        if (pmesh->areas[i] == RC_WALKABLE_AREA)
            pmesh->flags[i] = 0x01;

    dtNavMeshCreateParams params = {};
    params.verts            = pmesh->verts;
    params.vertCount        = pmesh->nverts;
    params.polys            = pmesh->polys;
    params.polyAreas        = pmesh->areas;
    params.polyFlags        = pmesh->flags;
    params.polyCount        = pmesh->npolys;
    params.nvp              = pmesh->nvp;
    params.detailMeshes     = dmesh->meshes;
    params.detailVerts      = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris       = dmesh->tris;
    params.detailTriCount   = dmesh->ntris;
    params.walkableHeight   = 1.8f;
    params.walkableRadius   = 0.4f;
    params.walkableClimb    = 0.3f;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs               = cs;
    params.ch               = ch;
    params.buildBvTree      = true;

    unsigned char* navData     = nullptr;
    int            navDataSize = 0;
    if (!dtCreateNavMeshData(&params, &navData, &navDataSize)) {
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);
        fprintf(stderr, "[NavMesh] BuildFromExternal: dtCreateNavMeshData failed\n");
        return false;
    }
    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);

    nav_mesh_ = dtAllocNavMesh();
    if (nav_mesh_->init(navData, navDataSize, DT_TILE_FREE_DATA) != DT_SUCCESS) {
        dtFreeNavMesh(nav_mesh_);
        nav_mesh_ = nullptr;
        fprintf(stderr, "[NavMesh] BuildFromExternal: dtNavMesh::init failed\n");
        return false;
    }
    nav_query_ = dtAllocNavMeshQuery();
    nav_query_->init(nav_mesh_, 2048);
    return true;
}

bool NavMesh::BuildTileMap(const float* verts, int nverts,
                            const int*   tris,  int ntris,
                            float cs, float ch)
{
    // Don't store terrain — tile maps rebuild the full mesh on map change.
    terrain_nverts_ = 0;
    terrain_ntris_  = 0;
    rebuild_cs_     = cs;
    rebuild_ch_     = ch;
    for (auto& o : obstacles_) o.valid = false;
    return BuildFromExternal(verts, nverts, tris, ntris, cs, ch);
}

// ─────────────────────────────────────────────────────────
void NavMesh::Destroy()
{
    if (nav_query_) { dtFreeNavMeshQuery(nav_query_); nav_query_ = nullptr; }
    if (nav_mesh_)  { dtFreeNavMesh(nav_mesh_);       nav_mesh_  = nullptr; }
    if (ctx_)       { delete ctx_;                     ctx_       = nullptr; }
}
