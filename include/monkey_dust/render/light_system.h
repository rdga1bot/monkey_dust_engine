#pragma once
#include "raylib.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#ifdef USE_SDL3
#  include "render/MdTexture.h"
#endif

// ─────────────────────────────────────────────────────────
// LightSystem — directional sun + ambient IBL.
//
// BRDF LUT (64×64, RGBA8): precomputed split-sum for specular.
//   X axis = NdotV, Y axis = roughness.
//   R = F0 scale, G = F0 bias (standard Karis split-sum).
//
// Apply(shader): sets sunDir, sunColor, ambientColor uniforms.
// Bind(): binds brdfLUT to texture unit 8 (slot > PBR atlas range).
// ─────────────────────────────────────────────────────────

static constexpr int BRDF_LUT_SIZE = 64;

class LightSystem {
public:
    static LightSystem& Get() {
        static LightSystem inst;
        return inst;
    }

    Vector3 sun_dir     = { 0.577f, -0.577f, 0.577f }; // normalized, sun→surface
    Vector3 sun_color   = { 1.00f, 0.95f, 0.80f };
    Vector3 ambient_color = { 0.15f, 0.18f, 0.22f };

#ifdef USE_SDL3
    MdTexture brdf_lut;
#else
    Texture2D brdf_lut = {};
#endif

    void Init() {
        static uint8_t pixels[BRDF_LUT_SIZE * BRDF_LUT_SIZE * 4];
        GenerateBRDFLUT(pixels, BRDF_LUT_SIZE);
#ifdef USE_SDL3
        brdf_lut = MdLoadTextureFromMemory(pixels, BRDF_LUT_SIZE, BRDF_LUT_SIZE);
#else
        Image img = {};
        img.data    = pixels;
        img.width   = BRDF_LUT_SIZE;
        img.height  = BRDF_LUT_SIZE;
        img.mipmaps = 1;
        img.format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        brdf_lut = LoadTextureFromImage(img);
        // do NOT call UnloadImage — pixels[] is stack, img.data not heap
#endif
    }

    // Sets sunDir, sunColor, ambientColor uniforms on any shader by name.
    void Apply(Shader shader) const {
        int locDir = GetShaderLocation(shader, "sunDir");
        int locSun = GetShaderLocation(shader, "sunColor");
        int locAmb = GetShaderLocation(shader, "ambientColor");
        // sunDir is "to sun" in fragment shader convention (light dir reversed)
        float toSun[3] = { -sun_dir.x, -sun_dir.y, -sun_dir.z };
        if (locDir >= 0) SetShaderValue(shader, locDir, toSun,            SHADER_UNIFORM_VEC3);
        if (locSun >= 0) SetShaderValue(shader, locSun, &sun_color,       SHADER_UNIFORM_VEC3);
        if (locAmb >= 0) SetShaderValue(shader, locAmb, &ambient_color,   SHADER_UNIFORM_VEC3);
    }

    void Shutdown() {
#ifdef USE_SDL3
        MdUnloadTexture(brdf_lut);
#else
        if (brdf_lut.id > 0) { UnloadTexture(brdf_lut); brdf_lut = {}; }
#endif
    }

private:
    LightSystem() = default;

    // Split-sum BRDF LUT via GGX importance sampling (Hammersley, 128 samples).
    static void GenerateBRDFLUT(uint8_t* out, int size) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float NdotV    = (x + 0.5f) / (float)size;
                float roughness = (y + 0.5f) / (float)size;
                float a  = roughness * roughness;
                float Vx = sqrtf(1.0f - NdotV * NdotV);
                float Vz = NdotV;

                float scale = 0.0f, bias = 0.0f;
                const int N = 128;
                for (int i = 0; i < N; ++i) {
                    // Hammersley low-discrepancy sequence
                    float phi  = 6.28318530f * (float)i / (float)N;
                    uint32_t bits = (uint32_t)i;
                    bits = (bits << 16u) | (bits >> 16u);
                    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                    float xi = (float)bits * 2.3283064365e-10f;

                    // GGX importance sample half-vector
                    float cosT = sqrtf((1.0f - xi) / (1.0f + (a*a - 1.0f) * xi + 1e-7f));
                    float sinT = sqrtf(fmaxf(0.0f, 1.0f - cosT * cosT));
                    float Hx   = cosf(phi) * sinT;
                    float Hz   = cosT;

                    float VdotH = Vx * Hx + Vz * Hz;
                    float NdotL = fmaxf(2.0f * VdotH * Hz - Vz, 0.0f);
                    float NdotH = fmaxf(Hz, 0.0f);

                    if (NdotL > 0.0f) {
                        // Smith-Schlick geometry
                        float k    = a * 0.5f;
                        float G1v  = NdotV / (NdotV * (1.0f - k) + k);
                        float G1l  = NdotL / (NdotL * (1.0f - k) + k);
                        float G_vis = G1v * G1l * VdotH / fmaxf(NdotH * NdotV, 1e-5f);
                        float Fc   = powf(1.0f - VdotH, 5.0f);
                        scale += (1.0f - Fc) * G_vis;
                        bias  += Fc           * G_vis;
                    }
                }
                scale /= (float)N;
                bias  /= (float)N;

                int idx = (y * size + x) * 4;
                out[idx + 0] = (uint8_t)(fminf(fmaxf(scale, 0.0f), 1.0f) * 255.0f + 0.5f);
                out[idx + 1] = (uint8_t)(fminf(fmaxf(bias,  0.0f), 1.0f) * 255.0f + 0.5f);
                out[idx + 2] = 0;
                out[idx + 3] = 255;
            }
        }
    }
};
