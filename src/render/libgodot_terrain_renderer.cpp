#include <monkey_dust/render/libgodot_terrain_renderer.h>

#ifdef MD_USE_LIBGODOT

#include <monkey_dust/world/terrain_gen.h>
#include <monkey_dust/world/terrain_quadtree.h>
#include <monkey_dust/world/biome_def.h>
#include <monkey_dust/render/md_camera.h>

#include "scene/resources/mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/image_texture.h"
#include "servers/rendering/rendering_server.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <algorithm>
#include <unistd.h>

// Витягнуто з game/src/main_libgodot.cpp (Крок 1a-1d, tasks #530-533) --
// нуль поведінкових змін, лише файлова межа + публічні імена
// (LibgodotTerrain_Init/Update/Shutdown замінюють колишні
// InitTerrainStreaming/UpdateTerrainStreaming/ResetTerrainStreamingStatics).
// Кожен коментар нижче -- оригінальний, з місця виявлення в
// main_libgodot.cpp, не переписаний.

static const int   kNodeQuads = 16;              // matches terrain_quadtree_mesh.h's kPatchQuads
static const int   kNodeGrid  = kNodeQuads + 1;  // 17
static const float kTerrainChunkSize = 460.8f;   // real Kenshi zone size (CHUNK_SIZE)
static const int   kTerrainMaxDepth  = 3;        // chunk/16/2^3 = 3.6m = TERRAIN_STEP
static const float kTerrainDetailMul = 3.0f;     // CDLOD-style detail multiplier (TerrainQuadtree.h default)

static const char* kTerrainNodeShaderSrc = R"(
shader_type spatial;
render_mode unshaded, cull_disabled;
uniform sampler2D height_tex;
uniform sampler2D tex_colour : source_color;
uniform sampler2D tex_ground_baked : source_color;
uniform sampler2DArray tex_ground_array : source_color;
uniform vec2  world_origin_atlas;
uniform float world_extent = 29491.2;
uniform float patch_size_m = 128.0;
uniform float skirt_depth = 5.0;
uniform float cliff_layer = 0.0;
uniform vec2  cliff_tiling = vec2(1.0, 1.0);
uniform vec3  sun_dir_ws = vec3(0.4, 0.8, 0.3);
uniform float sun_intensity = 0.9;
uniform vec3  ambient_col = vec3(0.35, 0.35, 0.38);
// Fog (task #533): real GraphicsSettings defaults (engine/include/
// monkey_dust/tools/graphics_settings.h) -- fog_near=1200m/fog_far=2800m/
// fog_color matches the sky clear colour. TS_ApplyLighting's real
// formula (shaders/terrain_shading_common.glsl): linear ramp between
// fog_near and fog_far, clamped, mixed AFTER lighting.
uniform float fog_near = 1200.0;
uniform float fog_far  = 2800.0;
uniform vec3  fog_color = vec3(0.38, 0.58, 0.82);
varying vec2  v_overlay_uv;
varying vec3  v_world_pos;
void vertex() {
    // Real per-vertex normal via forward-difference (task #532; ddx/ddy
    // don't work here -- LOD terrain normals need forward-diff sampling
    // of the height texture, same lesson already learned on the SDL_GPU
    // path). texel = one height_tex texel step (kNodeQuads=16 quads/side).
    float texel = 1.0 / 16.0;
    float hC = texture(height_tex, UV).r;
    float hX = texture(height_tex, UV + vec2(texel, 0.0)).r;
    float hZ = texture(height_tex, UV + vec2(0.0, texel)).r;
    VERTEX.y = hC;
    if (COLOR.r > 0.5) {
        VERTEX.y -= skirt_depth; // border skirt vertex -- hangs down to close LOD/neighbour seams
    }
    float world_step = texel * patch_size_m;
    vec3 dx = vec3(world_step, hX - hC, 0.0);
    vec3 dz = vec3(0.0, hZ - hC, world_step);
    NORMAL = normalize(cross(dz, dx));

    vec2 world_xz_atlas = world_origin_atlas + UV * patch_size_m;
    v_overlay_uv = world_xz_atlas / world_extent;
    // task #533 bug fix: world_xz_atlas is in ATLAS-shifted space
    // (world_origin_atlas = scene origin + kFeaturesToAtlasShift, see
    // that constant's own doc comment) -- but CAMERA_POSITION_WORLD and
    // every other real position in this scene (props/NPC/ECS/instance
    // transforms) are in the UNSHIFTED centre-origin scene space. Using
    // the atlas-shifted value here put v_world_pos ~20.8km away from
    // CAMERA_POSITION_WORLD for every fragment, permanently saturating
    // fog_t to 1.0 -- live-verified: the whole terrain turned solid
    // fog_color. Subtract the same shift back out (ATLAS_SHIFT below
    // must match kFeaturesToAtlasShift exactly -- it's a compile-time
    // constant here since the shader has no access to that C++ symbol).
    const float ATLAS_SHIFT = 14745.6;
    v_world_pos = vec3(world_xz_atlas.x - ATLAS_SHIFT, hC, world_xz_atlas.y - ATLAS_SHIFT);
}
void fragment() {
    vec3 overlay = texture(tex_colour, v_overlay_uv).rgb * 1.2;
    vec3 dds     = texture(tex_ground_baked, v_overlay_uv).rgb;
    vec3 tint    = mix(vec3(1.0), overlay, 0.5);
    vec3 albedo  = dds * tint * 1.3;

    vec3  N     = normalize(NORMAL);

    // Cliff-blending (task #532, reduced scope): real per-biome cliff
    // texture layer + real per-biome UV tiling (TerrainGen_ResolveBiome),
    // triplanar-style X/Z side blend by slope steepness. Deliberately
    // deferred vs the SDL_GPU reference (docs/LIBGODOT_MIGRATION_GROUP1_
    // TERRAIN_SCOPE.md): no near/far anti-alias tiling split (task #354),
    // no per-zone grass/dirt/road detail blend (needs tex_overlay_mask,
    // a separate real asset not yet loaded here) -- real follow-up.
    const float CLIFF_MIN = 0.50, CLIFF_MAX = 1.00, CLIFF_BLEND = 0.15;
    const float CLIFF_UV_SCALE = 1.0 / 5000.0;
    float steepness = 1.0 - N.y;
    float cliff_w = smoothstep(CLIFF_MIN - CLIFF_BLEND, CLIFF_MIN, steepness)
                  * smoothstep(CLIFF_MAX + CLIFF_BLEND, CLIFF_MAX, steepness);
    if (cliff_w > 0.001) {
        vec2 uvSideX = vec2(v_world_pos.z, v_world_pos.y) * CLIFF_UV_SCALE * cliff_tiling;
        vec2 uvSideZ = vec2(v_world_pos.x, v_world_pos.y) * CLIFF_UV_SCALE * cliff_tiling;
        vec2 Nxz = N.xz;
        float NxzLen = length(Nxz);
        vec2 uvblend = vec2(0.5, 0.5);
        if (NxzLen > 1e-4) {
            uvblend = abs(Nxz / NxzLen) - 0.2;
            uvblend = max(vec2(0.0), pow(uvblend * 7.0, vec2(2.0)));
            uvblend /= max(uvblend.x + uvblend.y, 1e-4);
        }
        vec3 cCliffX = texture(tex_ground_array, vec3(uvSideX, cliff_layer)).rgb;
        vec3 cCliffZ = texture(tex_ground_array, vec3(uvSideZ, cliff_layer)).rgb;
        vec3 cCliff  = cCliffX * uvblend.x + cCliffZ * uvblend.y;
        albedo = mix(albedo, cCliff, cliff_w);
    }

    vec3  sun_n = normalize(sun_dir_ws);
    vec3  amb   = max(ambient_col, vec3(0.30));
    float ndl   = max(dot(N, sun_n), 0.85);
    vec3  light = min(amb + ndl * sun_intensity, vec3(1.0));
    vec3  shaded = albedo * light;

    // Fog (task #533): real TS_ApplyLighting formula -- linear ramp by
    // camera distance, applied AFTER lighting, never baked (a cached/
    // baked fog value would desync the moment the camera moves -- same
    // reasoning the SDL_GPU VT path's own doc comment already spells out).
    float dist  = distance(v_world_pos, CAMERA_POSITION_WORLD);
    float fog_t = clamp((dist - fog_near) / max(fog_far - fog_near, 1.0), 0.0, 1.0);
    shaded = mix(shaded, fog_color, fog_t);

    ALBEDO = shaded;
}
)";

// Loads mip-0 of a real BC3/DXT5 .dds (task #529) -- this libgodot build
// has no registered runtime DDS ImageLoader (only stock PNG/JPG/WebP/etc
// are loadable via Image::load(); DDS support in stock Godot is an
// EDITOR-import-time thing, not available at runtime here), so this
// hand-parses the exact header layout tools/md_bc3_encode.py itself
// writes (verified by reading that script directly, not guessed):
// 4-byte "DDS " magic + 124-byte DDS_HEADER (31 little-endian uint32
// words: [0]=dwSize, [1]=dwFlags, [2]=dwHeight, [3]=dwWidth,
// [4]=dwPitchOrLinearSize -- the exact byte count of mip 0's BC3 data,
// which is all this needs). Returns a null Ref<Image> on any I/O/format
// mismatch (caller treats that as "skip this texture", matching this
// file's existing graceful-missing-asset tolerance elsewhere).
static Ref<Image> LoadDdsBc3Mip0(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("FAILED: LoadDdsBc3Mip0 open %s\n", path); return Ref<Image>(); }
    uint8_t header[128];
    if (fread(header, 1, sizeof(header), f) != sizeof(header) ||
        memcmp(header, "DDS ", 4) != 0) {
        printf("FAILED: LoadDdsBc3Mip0 bad header %s\n", path);
        fclose(f);
        return Ref<Image>();
    }
    auto read_u32 = [&](int word_idx) -> uint32_t {
        uint32_t v;
        memcpy(&v, header + 4 + word_idx * 4, 4);
        return v;  // host is little-endian (x86_64), same as the file
    };
    uint32_t height = read_u32(2), width = read_u32(3), mip0_size = read_u32(4);
    std::vector<uint8_t> raw(mip0_size);
    if (fread(raw.data(), 1, mip0_size, f) != mip0_size) {
        printf("FAILED: LoadDdsBc3Mip0 short read %s\n", path);
        fclose(f);
        return Ref<Image>();
    }
    fclose(f);
    Vector<uint8_t> data;
    data.resize((int)mip0_size);
    memcpy(data.ptrw(), raw.data(), mip0_size);
    Ref<Image> img = Image::create_from_data((int)width, (int)height, false, Image::FORMAT_DXT5, data);
    printf("OK: LoadDdsBc3Mip0 %s -- %ux%u, mip0=%u bytes\n", path, width, height, mip0_size);
    return img;
}

// Coordinate convention fix (task #528, live-verified, not a guess):
// features.dat/features_scatter.txt's raw world coordinates (private/
// md_features_parse.py's `pos / KENSHI_UNITS_PER_METRE`) are CENTRE-
// origin -- Kenshi's own raw engine coordinate space, spanning roughly
// +/-14745.6m in both X and Z. TerrainAtlas_SampleWorld's convention is
// CORNER-origin over [0, 29491.2m) (confirmed via TerrainQuery::
// GetHeight's real formula, engine/include/monkey_dust/world/
// terrain_query.h: `ax = wx - world_off_x_ + zone_ox_*chunk_size_`, and
// WorldRegistry zone grid_x/grid_z, game/data/terrain_config.txt, are
// themselves 0..63 corner-indexed). Confirmed empirically: sampling the
// real prop cluster (~6029,-3944) raw gave h=0.00m (silently clamped --
// negative Z falls outside [0,29491.2)); shifting BOTH x and z by
// +14745.6 (half of ATLAS_ZONES*CHUNK_SIZE=29491.2) gave a real,
// non-clamped 82.87m -- a plausible rocky-terrain elevation, not a
// coincidence (shifting only one axis either stayed clamped at 0 or
// landed on an unrelated, still-plausible-looking value from a
// different part of the map -- only shifting BOTH matches the actual
// ground under this specific cluster). This shift applies ONLY to
// TerrainAtlas_SampleWorld() calls (the atlas's own coordinate space)
// -- NOT to where the patch/props/NPC/ECS entities are actually placed
// in the LibGodot scene, which stays in features.dat's native
// centre-origin space (matching everything else already rendered
// there).
static const float kFeaturesToAtlasShift = 29491.2f * 0.5f;  // 14745.6

// File-scope (not function-local) statics -- MUST be explicitly reset in
// LibgodotTerrain_Shutdown() BEFORE bridge.Shutdown()/window_shutdown(),
// same Ref<>-outliving-RS-usage bug class this file's npc_skin_ref/
// npc_skin/npc_doc/npc_state comments already document (in
// main_libgodot.cpp): their destructors free their own RS RID (mesh/
// material/shader/texture RIDs are NOT independent of the C++ Resource
// wrapper's lifetime), so if these ran as a bare function-local
// (destroyed at function return) the RS instance registered against
// them would immediately dangle; if left to destruct at PROCESS EXIT
// (implicit for true statics) after bridge.Shutdown() already tore down
// the RenderingServer, that's a real heap-corruption crash -- live-
// verified this session ("free(): chunks in smallbin corrupted" after
// "1 RID allocations of type ... Mesh/Material/Shader/Texture ...
// leaked at exit").
// Per-node cache entry (task #530). The shared mesh (s_terrain_node_mesh)
// is reused by every node; only the material (height_tex + per-node
// uniforms) differs, applied via instance_geometry_set_material_override
// -- NOT mesh_surface_set_material, which sets the material on the
// shared MESH's surface and would make every node share the SAME
// material (a real bug this design deliberately avoids).
struct TerrainNodeEntry {
    RID instance;
    Ref<ImageTexture>   height_tex;
    Ref<ShaderMaterial> material;
    int last_seen_frame = -1;
};

static TerrainQuadtree             s_terrain_quadtree;
static bool                        s_terrain_quadtree_inited = false;
static Ref<ArrayMesh>              s_terrain_node_mesh;
static Ref<Shader>                 s_terrain_node_shader;
static Ref<ImageTexture>           s_terrain_colour_tex;
static Ref<ImageTexture>           s_terrain_ground_baked_tex;
static Ref<Texture2DArray>         s_terrain_ground_array_tex;
static bool                        s_terrain_biomes_ready = false;
static std::map<std::tuple<int,int,int>, TerrainNodeEntry> s_terrain_node_cache; // key=(depth,gx,gz)
static int                         s_terrain_frame_counter = 0;

// Builds the real per-biome cliff/ground texture array (task #532) from
// BiomeRegistry::GroundTexPath() -- real Kenshi terrain textures,
// tmp_/kenshi_re/terrain_textures/*.dds (BC3, mostly 2048x2048; a
// symlink into this worktree, same gitignored-RE-data convention as
// world_hmap.r16/md_terrain.dds elsewhere in this module). Texture2DArray
// requires every layer to share dimensions+format -- the ~3/198 real
// files that don't match 2048x2048 fall back to layer 0's image (a
// real texture, just not the intended one for those rare biomes;
// documented gap, not a guess).
static Ref<Texture2DArray> BuildGroundTextureArray() {
    const BiomeRegistry& biomes = BiomeRegistry::Get();
    int tex_count = biomes.GroundTexCount();
    if (tex_count <= 0) {
        printf("FAILED: BiomeRegistry has 0 ground textures -- cliff-blending disabled\n");
        return Ref<Texture2DArray>();
    }
    Vector<Ref<Image>> images;
    int target_w = 0, target_h = 0;
    int loaded = 0, fallback = 0;
    for (int i = 0; i < tex_count; ++i) {
        Ref<Image> img = LoadDdsBc3Mip0(biomes.GroundTexPath(i));
        if (img.is_valid()) {
            if (target_w == 0) { target_w = img->get_width(); target_h = img->get_height(); }
            if (img->get_width() == target_w && img->get_height() == target_h) {
                images.push_back(img);
                ++loaded;
                continue;
            }
        }
        images.push_back(Ref<Image>()); // placeholder, patched below once target size is known
        ++fallback;
    }
    // Second pass: patch placeholders now that target_w/h is known (layer
    // 0 itself could have been the very first successfully-sized image).
    Ref<Image> fallback_img;
    for (int i = 0; i < images.size(); ++i) {
        if (images[i].is_valid()) { fallback_img = images[i]; break; }
    }
    if (!fallback_img.is_valid()) {
        printf("FAILED: no ground texture loaded at a consistent size -- cliff-blending disabled\n");
        return Ref<Texture2DArray>();
    }
    for (int i = 0; i < images.size(); ++i) {
        if (!images[i].is_valid()) images.set(i, fallback_img);
    }
    Ref<Texture2DArray> arr;
    arr.instantiate();
    if (arr->create_from_images(images) != OK) {
        printf("FAILED: Texture2DArray::create_from_images (%d layers)\n", (int)images.size());
        return Ref<Texture2DArray>();
    }
    printf("OK: ground texture array built -- %d layers (%d real, %d fallback-to-layer), %dx%d\n",
           (int)images.size(), loaded, fallback, target_w, target_h);
    return arr;
}

// Plain function pointer (TerrainQuadtree::HeightSampleFn signature, no
// captures allowed) -- applies the task #528 coordinate-convention shift
// (features.dat/scene placement space is centre-origin; TerrainAtlas_
// SampleWorld's own space is corner-origin [0,29491.2)).
static float TerrainNodeHeightSample(float wx, float wz) {
    return TerrainAtlas_SampleWorld(wx + kFeaturesToAtlasShift, wz + kFeaturesToAtlasShift);
}

// ONE shared unit-quad mesh (local X,Z in [0,1], scaled to a node's real
// world size via each instance's own Transform3D) -- built once, reused
// by every visible node regardless of depth/position. Topology: main
// kNodeGrid x kNodeGrid grid (17x17, matches terrain_quadtree_mesh.h's
// kPatchQuads=16 exactly) + a border skirt ring (COLOR.r=1 duplicate of
// each perimeter vertex, hangs down by `skirt_depth` in the vertex
// shader) closing seams against neighbouring tiles of a different LOD
// tier or against the world edge.
static Ref<ArrayMesh> BuildTerrainNodeMesh() {
    const int main_count = kNodeGrid * kNodeGrid;

    // Perimeter walk: bottom edge L->R, right edge B->T, top edge R->L,
    // left edge T->B -- one closed loop, corners counted once.
    std::vector<std::pair<int,int>> perim;
    for (int col = 0; col < kNodeGrid; ++col) perim.push_back({col, 0});
    for (int row = 1; row < kNodeGrid; ++row) perim.push_back({kNodeQuads, row});
    for (int col = kNodeQuads - 1; col >= 0; --col) perim.push_back({col, kNodeQuads});
    for (int row = kNodeQuads - 1; row >= 1; --row) perim.push_back({0, row});
    const int perim_count = (int)perim.size(); // 17+16+16+15 = 64

    PackedVector3Array verts;
    PackedVector3Array normals;
    PackedVector2Array uvs;
    PackedColorArray   colors;
    PackedInt32Array   indices;
    const int total_verts = main_count + perim_count;
    verts.resize(total_verts);
    normals.resize(total_verts);
    uvs.resize(total_verts);
    colors.resize(total_verts);

    for (int row = 0; row < kNodeGrid; ++row) {
        for (int col = 0; col < kNodeGrid; ++col) {
            int idx = row * kNodeGrid + col;
            float u = (float)col / (float)kNodeQuads;
            float v = (float)row / (float)kNodeQuads;
            verts.set(idx, Vector3(u, 0.0f, v));
            normals.set(idx, Vector3(0.0f, 1.0f, 0.0f));
            uvs.set(idx, Vector2(u, v));
            colors.set(idx, Color(0.0f, 0.0f, 0.0f, 1.0f));
        }
    }
    for (int i = 0; i < perim_count; ++i) {
        int col = perim[i].first, row = perim[i].second;
        int idx = main_count + i;
        float u = (float)col / (float)kNodeQuads;
        float v = (float)row / (float)kNodeQuads;
        verts.set(idx, Vector3(u, 0.0f, v));
        normals.set(idx, Vector3(0.0f, 1.0f, 0.0f));
        uvs.set(idx, Vector2(u, v));
        colors.set(idx, Color(1.0f, 0.0f, 0.0f, 1.0f)); // skirt flag
    }

    for (int row = 0; row < kNodeQuads; ++row) {
        for (int col = 0; col < kNodeQuads; ++col) {
            int i0 = row * kNodeGrid + col;
            int i1 = i0 + 1;
            int i2 = i0 + kNodeGrid;
            int i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }
    auto main_index_of = [&](int col, int row) { return row * kNodeGrid + col; };
    for (int i = 0; i < perim_count; ++i) {
        int j = (i + 1) % perim_count;
        int m0 = main_index_of(perim[i].first, perim[i].second);
        int m1 = main_index_of(perim[j].first, perim[j].second);
        int s0 = main_count + i;
        int s1 = main_count + j;
        indices.push_back(m0); indices.push_back(s0); indices.push_back(m1);
        indices.push_back(m1); indices.push_back(s0); indices.push_back(s1);
    }

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = verts;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_TEX_UV] = uvs;
    arrays[Mesh::ARRAY_COLOR]  = colors;
    arrays[Mesh::ARRAY_INDEX]  = indices;

    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    // Local-space AABB: X/Z span [0,1] (scaled per-instance by node size
    // -- Y is NEVER scaled, only translated, so local Y bounds transform
    // 1:1 into world Y). Real Kenshi terrain height is well inside
    // [-100,1000]m (TERRAIN_HEIGHT_SCALE_M=980 is the load-time scale
    // ceiling); generous margin covers skirt_depth too.
    mesh->set_custom_aabb(AABB(Vector3(-0.1f, -100.0f, -0.1f), Vector3(1.2f, 1100.0f, 1.2f)));
    return mesh;
}

// Builds a small per-node real-height texture (kNodeGrid x kNodeGrid,
// FORMAT_RF storing REAL world metres directly -- simpler than task
// #527-529's single-patch [0,1] min/max normalization: since each node
// gets its OWN material now (not a shared one), there's no longer a
// reason to normalize; the vertex shader just does VERTEX.y =
// texture(height_tex,UV).r).
static Ref<ImageTexture> BuildNodeHeightTexture(float ox, float oz, float size) {
    Ref<Image> img = Image::create_empty(kNodeGrid, kNodeGrid, false, Image::FORMAT_RF);
    for (int row = 0; row < kNodeGrid; ++row) {
        for (int col = 0; col < kNodeGrid; ++col) {
            float wx = ox + (float)col / (float)kNodeQuads * size;
            float wz = oz + (float)row / (float)kNodeQuads * size;
            float h = TerrainNodeHeightSample(wx, wz);
            img->set_pixel(col, row, Color(h, h, h, 1.0f));
        }
    }
    return ImageTexture::create_from_image(img);
}

void LibgodotTerrain_Init(const std::string& launch_cwd) {
    if (!TerrainAtlas_Loaded()) {
        // Relative path resolves against launch_cwd, NOT the CWD at this
        // call site -- window_init() (called earlier in main()) chdir()s
        // to the executable's own directory (Godot project detection,
        // see engine/data/libgodot_project_stub/README.md), same reason
        // every other real-data path here is already launch_cwd-prefixed.
        std::string atlas_path = launch_cwd + "/game/data/terrain/world_hmap";
        if (!TerrainAtlas_Load(atlas_path.c_str())) {
            printf("FAILED: TerrainAtlas_Load(%s) -- real heightmap missing\n", atlas_path.c_str());
            return;
        }
    }

    // Whole 64-zone Kenshi world, centre-origin (matches props/NPC/ECS
    // placement space -- see kFeaturesToAtlasShift's own doc comment;
    // the shift is applied ONLY inside TerrainNodeHeightSample, not here).
    const float world_extent = 29491.2f;
    s_terrain_quadtree.Init(-world_extent * 0.5f, -world_extent * 0.5f, world_extent,
                             kTerrainChunkSize, kTerrainMaxDepth, kTerrainDetailMul,
                             TerrainNodeHeightSample);
    s_terrain_quadtree_inited = true;

    s_terrain_node_mesh = BuildTerrainNodeMesh();
    s_terrain_node_shader.instantiate();
    s_terrain_node_shader->set_code(kTerrainNodeShaderSrc);

    std::string colour_path = launch_cwd + "/game/data/textures/md_terrain.png";
    Ref<Image> colour_img;
    colour_img.instantiate();
    if (colour_img->load(colour_path.c_str()) == OK) {
        s_terrain_colour_tex = ImageTexture::create_from_image(colour_img);
        printf("OK: loaded tex_colour %s\n", colour_path.c_str());
    } else {
        printf("FAILED: load tex_colour %s\n", colour_path.c_str());
    }
    std::string baked_path = launch_cwd + "/game/data/textures/md_ground_baked.dds";
    Ref<Image> baked_img = LoadDdsBc3Mip0(baked_path.c_str());
    if (baked_img.is_valid()) {
        s_terrain_ground_baked_tex = ImageTexture::create_from_image(baked_img);
    }

    // Cliff-blending (task #532): BiomeRegistry/TerrainGen_ResolveBiome
    // internally use bare relative paths (game/data/biome_table.txt,
    // game/data/textures/md_biomemap.png -- engine code shared with the
    // SDL_GPU path, where CWD is already the project root; not
    // parameterized on launch_cwd since it has no chdir problem there).
    // chdir() here ONCE, permanently -- nothing after window_init()'s own
    // Godot-project chdir() depends on CWD staying at the executable's
    // own directory (confirmed by every other real-data load in this
    // module already working via launch_cwd-prefixing instead of chdir).
    if (chdir(launch_cwd.c_str()) != 0) {
        printf("FAILED: chdir(%s) -- cliff-blending disabled\n", launch_cwd.c_str());
    } else {
        BiomeRegistry::Get().LoadFromFile("game/data/biome_table.txt");
        if (BiomeRegistry::Get().GroundTexCount() > 0) {
            s_terrain_ground_array_tex = BuildGroundTextureArray();
            s_terrain_biomes_ready = s_terrain_ground_array_tex.is_valid();
        } else {
            printf("FAILED: BiomeRegistry::LoadFromFile(game/data/biome_table.txt) -- cliff-blending disabled\n");
        }
    }

    printf("OK: terrain streaming initialised (world_extent=%.1fm, chunk=%.1fm, max_depth=%d, biomes=%s)\n",
           world_extent, kTerrainChunkSize, kTerrainMaxDepth, s_terrain_biomes_ready ? "ready" : "disabled");
}

// Per-frame streaming update (master plan Крок 1a/1b). Reuses
// TerrainQuadtree::SelectVisible() to find visible nodes around the
// camera, creates/reuses a per-node RS instance+material (cached by
// (depth,gx,gz) so an already-visible node costs nothing to redraw),
// and evicts nodes no longer selected (camera moved away) -- real
// streaming, not a fixed patch set.
void LibgodotTerrain_Update(uint64_t scenario_id,
                            float cam_x, float cam_y, float cam_z,
                            float cam_target_x, float cam_target_y, float cam_target_z) {
    if (!s_terrain_quadtree_inited) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return;
    RID scenario = RID::from_uint64(scenario_id);
    ++s_terrain_frame_counter;

    MdCamera cam;
    cam.pos    = Vec3{cam_x, cam_y, cam_z};
    cam.target = Vec3{cam_target_x, cam_target_y, cam_target_z};
    cam.up     = Vec3{0.0f, 1.0f, 0.0f};
    cam.fovy   = 60.0f;
    const float aspect = 1280.0f / 720.0f;
    float planes[16];
    cam.FrustumPlanes(aspect, planes);
    float cam_pos[3] = {cam_x, cam_y, cam_z};

    static TerrainQuadtree::VisibleNode nodes[TerrainQuadtree::kMaxNodesPublic];
    int count = s_terrain_quadtree.SelectVisible(cam_pos, planes, nodes, TerrainQuadtree::kMaxNodesPublic);

    int depth_hist[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < count; ++i) {
        const TerrainQuadtree::VisibleNode& n = nodes[i];
        int gx = (int)std::lround(n.origin_x / n.size);
        int gz = (int)std::lround(n.origin_z / n.size);
        auto key = std::make_tuple(n.depth, gx, gz);
        auto it = s_terrain_node_cache.find(key);
        if (it == s_terrain_node_cache.end()) {
            TerrainNodeEntry entry;
            entry.height_tex = BuildNodeHeightTexture(n.origin_x, n.origin_z, n.size);
            entry.material.instantiate();
            entry.material->set_shader(s_terrain_node_shader);
            entry.material->set_shader_parameter("height_tex", entry.height_tex);
            entry.material->set_shader_parameter("tex_colour", s_terrain_colour_tex);
            entry.material->set_shader_parameter("tex_ground_baked", s_terrain_ground_baked_tex);
            entry.material->set_shader_parameter(
                "world_origin_atlas",
                Vector2(n.origin_x + kFeaturesToAtlasShift, n.origin_z + kFeaturesToAtlasShift));
            entry.material->set_shader_parameter("patch_size_m", n.size);
            entry.material->set_shader_parameter("skirt_depth", n.skirt_depth);

            // Cliff-blending (task #532): every node lies entirely within
            // ONE zone (TerrainQuadtree::SelectVisible's outer loop
            // recurses PER ZONE, never across a zone boundary) -- so,
            // unlike the SDL_GPU screen-space path, no cross-zone blend
            // is needed here; a single CPU biome lookup at the node's
            // centre, once at node-build time, is exact.
            if (s_terrain_biomes_ready) {
                float cx = n.origin_x + n.size * 0.5f + kFeaturesToAtlasShift;
                float cz = n.origin_z + n.size * 0.5f + kFeaturesToAtlasShift;
                int zx = (int)std::floor(cx / kTerrainChunkSize);
                int zy = (int)std::floor(cz / kTerrainChunkSize);
                zx = std::max(0, std::min(63, zx));
                zy = std::max(0, std::min(63, zy));
                const BiomeDef& b = TerrainGen_ResolveBiome(zx, zy);
                entry.material->set_shader_parameter("tex_ground_array", s_terrain_ground_array_tex);
                entry.material->set_shader_parameter("cliff_layer", (float)b.tex_cliff);
                entry.material->set_shader_parameter("cliff_tiling", Vector2(b.cliff_tiling_x, b.cliff_tiling_y));
            }

            entry.instance = rs->instance_create();
            rs->instance_set_base(entry.instance, s_terrain_node_mesh->get_rid());
            rs->instance_set_scenario(entry.instance, scenario);
            rs->instance_set_visible(entry.instance, true);
            Transform3D xf;
            xf.basis = Basis(Vector3(n.size, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, n.size));
            xf.origin = Vector3(n.origin_x, 0.0f, n.origin_z);
            rs->instance_set_transform(entry.instance, xf);
            rs->instance_geometry_set_material_override(entry.instance, entry.material->get_rid());

            it = s_terrain_node_cache.emplace(key, std::move(entry)).first;
        } else {
            // Reused node -- skirt_depth is the only thing that can
            // legitimately vary frame-to-frame for the SAME (depth,gx,gz)
            // key (relief re-sampled at the same footprint is
            // deterministic, but cheap to refresh regardless).
            it->second.material->set_shader_parameter("skirt_depth", n.skirt_depth);
        }
        it->second.last_seen_frame = s_terrain_frame_counter;
        if (n.depth >= 0 && n.depth < 8) ++depth_hist[n.depth];
    }

    for (auto it = s_terrain_node_cache.begin(); it != s_terrain_node_cache.end(); ) {
        if (it->second.last_seen_frame != s_terrain_frame_counter) {
            rs->free_rid(it->second.instance);
            it = s_terrain_node_cache.erase(it);
        } else {
            ++it;
        }
    }

    if (s_terrain_frame_counter == 1 || s_terrain_frame_counter % 60 == 0) {
        printf("OK: terrain streaming -- %d visible nodes (depth0=%d depth1=%d depth2=%d depth3=%d), cache=%zu\n",
               count, depth_hist[0], depth_hist[1], depth_hist[2], depth_hist[3], s_terrain_node_cache.size());
    }
}

void LibgodotTerrain_Shutdown() {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (rs) {
        for (auto& kv : s_terrain_node_cache) rs->free_rid(kv.second.instance);
    }
    s_terrain_node_cache.clear();
    s_terrain_node_mesh        = Ref<ArrayMesh>();
    s_terrain_node_shader      = Ref<Shader>();
    s_terrain_colour_tex       = Ref<ImageTexture>();
    s_terrain_ground_baked_tex = Ref<ImageTexture>();
    s_terrain_ground_array_tex = Ref<Texture2DArray>();
}

#endif // MD_USE_LIBGODOT
