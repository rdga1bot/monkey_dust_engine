#include <monkey_dust/world/terrain_quadtree_async.h>
#include <monkey_dust/platform/job_system.h>
#include <cstring>
#include <cstdio>

void TerrainQuadtreeAsyncSelector::Init(const TerrainQuadtree* tree) {
    tree_ = tree;
    SDL_SetAtomicInt(&active_buf_, 0);
    SDL_SetAtomicInt(&in_flight_, 0);
    buf_count_[0] = 0;
    buf_count_[1] = 0;
}

void TerrainQuadtreeAsyncSelector::s_run_job(void* p) {
    JobArgs* a = (JobArgs*)p;
    int n = a->tree->SelectVisible(a->cam_pos, a->frustum_planes, a->lod_distances, a->out_buf);
    *a->out_count = n;
    // Publish LAST, after every write above — the render thread's
    // SDL_GetAtomicInt(&active_buf_) pairs with this SDL_SetAtomicInt to
    // guarantee it never observes buf_/buf_count_ mid-write (see header's
    // doc comment).
    SDL_SetAtomicInt(a->active_buf, a->write_buf_idx);
}

void TerrainQuadtreeAsyncSelector::KickAsync(const float cam_pos[3], const float frustum_planes[16],
                                              const float* lod_distances) {
    if (!tree_) return;
    if (SDL_GetAtomicInt(&in_flight_) > 0) return; // previous traversal still running — skip this frame

    int nd = tree_->MaxDepth() + 1;
    if (nd > kMaxLodEntries) {
        fprintf(stderr, "[TerrainQuadtreeAsyncSelector] MaxDepth+1=%d exceeds kMaxLodEntries=%d, clamping\n",
                nd, kMaxLodEntries);
        nd = kMaxLodEntries;
    }

    // Safe to overwrite job_args_ here: in_flight_==0 means the worker
    // already finished reading the PREVIOUS job's copy (JobSystem
    // decrements in_flight_ only after s_run_job returns).
    job_args_.tree = tree_;
    memcpy(job_args_.cam_pos, cam_pos, sizeof(job_args_.cam_pos));
    memcpy(job_args_.frustum_planes, frustum_planes, sizeof(job_args_.frustum_planes));
    memcpy(job_args_.lod_distances, lod_distances, sizeof(float) * nd);

    int write_buf = 1 - SDL_GetAtomicInt(&active_buf_);
    job_args_.out_buf       = buf_[write_buf];
    job_args_.out_count     = &buf_count_[write_buf];
    job_args_.write_buf_idx = write_buf;
    job_args_.active_buf    = &active_buf_;

    JobSystem::Get().Submit(s_run_job, &job_args_, &in_flight_);
}

const TerrainQuadVisibleNode* TerrainQuadtreeAsyncSelector::ActiveVisible(int& out_count) {
    int idx = SDL_GetAtomicInt(&active_buf_);
    out_count = buf_count_[idx];
    return buf_[idx];
}
