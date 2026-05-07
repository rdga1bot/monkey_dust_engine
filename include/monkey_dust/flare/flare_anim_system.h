#pragma once

namespace md::flare {

// Drives sprite animation for all ECS entities with FlareSpriteAnim.
// Requires FlareRuntime::LoadMod() and SpawnFlareActors() to have run first.
class FlareAnimSystem {
public:
    static FlareAnimSystem& Get();

    // Assign BillboardRenderer atlas slots for every active Flare category.
    // Call once after SpawnFlareActors().
    void Init();

    // Advance all sprite animations by dt_ms.  Updates direction from NavAgent.
    void Tick(float dt_ms);

    // Submit all animated actors to BillboardRenderer.
    // Must be called between BillboardRenderer::BeginFrame() and Render().
    void SubmitBillboards(float tile_world_size);

    void Shutdown();

private:
    FlareAnimSystem() = default;

    static constexpr int MAX_ATLAS = 4;
    char atlas_paths_[MAX_ATLAS][256] = {};
    int  n_atlases_ = 0;
    bool inited_    = false;

    int GetOrCreateSlot(const char* full_path);
};

} // namespace md::flare
