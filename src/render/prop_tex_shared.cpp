#include <monkey_dust/render/prop_tex_shared.h>
#include <monkey_dust/render/asset_cache.h>
#include <cstdio>

PropTexShared& PropTexShared::Get() {
    static PropTexShared s;
    return s;
}

bool PropTexShared::Init() {
    if (ready) return true;

    const char* rock_path = "tmp_/kenshi/data/foliage/Textures/GenericRockTexture_DIF.dds";
    const char* veg_path  = "tmp_/kenshi/data/foliage/Textures/Trees&VegAtlas01.dds";

    GpuSamplerDesc sd = GpuSamplerDesc::Default();
    tex_rock = AssetCache::Get().GetOrLoadDDS(rock_path, sd);
    tex_veg  = AssetCache::Get().GetOrLoadDDS(veg_path,  sd);

    if (!tex_rock) fprintf(stderr, "[PropTexShared] rock texture load failed: %s\n", rock_path);
    if (!tex_veg)  fprintf(stderr, "[PropTexShared] veg texture load failed: %s\n",  veg_path);

    ready = tex_rock && tex_veg;
    return ready;
}
