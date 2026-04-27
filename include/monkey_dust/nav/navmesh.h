#pragma once
#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"

// ─────────────────────────────────────────────────────────
// NavMesh — обгортка Recast/Detour.
//
// Будується ОДИН РАЗ при завантаженні чанку.
// Зберігається як бінарний файл поряд з чанком (.navmesh).
// НЕ перебудовувати без потреби (дорого!).
//
// rcConfig налаштований для нашого терену:
//   cs=0.3f   — горизонтальна роздільність (30 см)
//   ch=0.2f   — вертикальна роздільність (20 см)
//   walkableSlopeAngle=45° — максимальний кут схилу
//   agentHeight=1.8f, agentRadius=0.4f, agentMaxClimb=0.3f
// ─────────────────────────────────────────────────────────

static constexpr int MAX_POLYS = 256;  // для dtNavMeshQuery пошуку

class NavMesh {
public:
    NavMesh()  = default;
    ~NavMesh() { Destroy(); }

    NavMesh(const NavMesh&)            = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // Збудувати NavMesh з геометрії терену.
    // verts: [x0,y0,z0, x1,y1,z1, ...], nverts — кількість вершин.
    // tris:  [i0,i1,i2, ...],            ntris  — кількість трикутників.
    // cs — горизонтальна роздільність (0.3f = точно, 1.0f = швидко для flat)
    bool Build(const float* verts, int nverts,
               const int*   tris,  int ntris,
               float cs = 0.3f, float ch = 0.2f);

    // Завантажити з файлу (бінарний формат Detour).
    bool LoadFromFile(const char* path);

    // Зберегти у файл.
    bool SaveToFile(const char* path) const;

    // Знайти шлях від start до end.
    // out_verts: [x0,y0,z0, x1,y1,z1, ...] (polypath центри)
    // Повертає кількість точок або 0 при невдачі.
    int FindPath(float sx, float sy, float sz,
                 float ex, float ey, float ez,
                 float* out_verts, int max_verts) const;

    // Перебудувати навмеш з новою перешкодою (або без — obs_verts==nullptr).
    // Зберігає поточний список перешкод і перезбирає весь меш.
    // Викликати після Place/Demolish будівлі.
    bool RebuildTile(float wx, float wz,
                     const float* obs_verts, int nobs_verts,
                     const int*   obs_tris,  int nobs_tris);

    // Build from a large tile-map walkable mesh.
    // Unlike Build(), does not store geometry for RebuildTile (tile maps
    // require a full rebuild on map change, not incremental tile updates).
    // cs=0.5f, ch=0.2f good default for 1-world-unit tiles.
    bool BuildTileMap(const float* verts, int nverts,
                      const int*   tris,  int ntris,
                      float cs = 0.5f, float ch = 0.2f);

    bool IsValid() const { return nav_mesh_ != nullptr; }
    dtNavMesh* Raw() { return nav_mesh_; }

private:
    void Destroy();
    bool BuildInternal();  // перезбудова з terrain_verts_ + obstacles_
    bool BuildFromExternal(const float* v, int nv, const int* t, int nt,
                           float cs, float ch);

    rcContext*      ctx_       = nullptr;
    dtNavMesh*      nav_mesh_  = nullptr;
    dtNavMeshQuery* nav_query_ = nullptr;

    // Зберігаємо для RebuildTile (встановлюється в Build())
    static constexpr int MAX_TERRAIN_VERTS = 8;
    static constexpr int MAX_TERRAIN_TRIS  = 8;
    static constexpr int MAX_OBSTACLES     = 64;

    float terrain_verts_[MAX_TERRAIN_VERTS * 3] = {};
    int   terrain_tris_ [MAX_TERRAIN_TRIS  * 3] = {};
    int   terrain_nverts_ = 0;
    int   terrain_ntris_  = 0;
    float rebuild_cs_     = 1.0f;
    float rebuild_ch_     = 0.2f;

    struct ObstacleDef {
        float bmin[3];
        float bmax[3];
        float cx, cz;
        bool  valid = false;
    };
    ObstacleDef obstacles_[MAX_OBSTACLES] = {};
};
