#pragma once

struct GraphicsSettings {
    static GraphicsSettings& Get() { static GraphicsSettings s; return s; }

    float fog_near    = 80.f;
    float fog_far     = 150.f;
    float fog_color[3]= {0.18f, 0.20f, 0.25f};
    bool  fog_enabled = true;

    bool  vsync       = true;
    bool  shadows_enabled = true;
    bool  soft_shadows    = true;
    int   shadow_cascades = 3;       // 1/2/3
    float shadow_distance = 150.f;
    bool  ibl_enabled     = true;
    float ibl_intensity   = 1.f;

private:
    GraphicsSettings() = default;
};
