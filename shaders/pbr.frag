#version 430 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

// PBR texture atlas (512×512 max)
uniform sampler2D albedoTex;        // RGB albedo
uniform sampler2D metallicRoughTex; // R=metallic, G=roughness
uniform sampler2D normalTex;        // tangent-space normal (optional)
uniform sampler2D brdfLUT;          // split-sum LUT: R=scale, G=bias

// Lighting
uniform vec3  sunDir;         // normalized, pointing TOWARD sun (away from surface)
uniform vec3  sunColor;
uniform vec3  ambientColor;
uniform vec3  cameraPos;

// Fog
uniform vec3  fogColor;
uniform float fogNear;
uniform float fogFar;

// CSM
uniform sampler2D shadowMaps[3];
uniform mat4      lightViewProj[3];
uniform float     cascadeSplits[3]; // {20, 60, 150}

out vec4 fragColor;

float sampleCSM(vec3 worldPos, float viewDepth) {
    int cascade = 2;
    if      (viewDepth < cascadeSplits[0]) cascade = 0;
    else if (viewDepth < cascadeSplits[1]) cascade = 1;

    vec4 lc   = lightViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;
    proj = proj * 0.5 + 0.5;

    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float bias   = 0.002;
    vec2  texel  = 1.0 / vec2(1024.0);
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float depth = texture(shadowMaps[cascade], proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias > depth) ? 0.0 : 1.0;
        }
    return shadow / 9.0;
}

const float PI = 3.14159265359;

float D_GGX(float NdotH, float a2) {
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

float G_SmithSchlick(float NdotV, float NdotL, float roughness) {
    float k  = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float g1 = NdotV / (NdotV * (1.0 - k) + k);
    float g2 = NdotL / (NdotL * (1.0 - k) + k);
    return g1 * g2;
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 F_SchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec4  albedoSample = texture(albedoTex, vUV);
    vec3  albedo     = pow(albedoSample.rgb, vec3(2.2)); // sRGB → linear
    vec2  mr         = texture(metallicRoughTex, vUV).rg;
    float metallic   = clamp(mr.r, 0.0, 1.0);
    float roughness  = clamp(mr.g, 0.04, 1.0);
    float a2         = roughness * roughness * roughness * roughness;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPos - vWorldPos);
    vec3 L = normalize(sunDir);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Cook-Torrance direct lighting
    float D = D_GGX(NdotH, a2);
    float G = G_SmithSchlick(NdotV, NdotL, roughness);
    vec3  F = F_Schlick(VdotH, F0);

    vec3 kD       = (1.0 - F) * (1.0 - metallic);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 direct   = (kD * albedo / PI + specular) * sunColor * NdotL;

    // Ambient IBL (split-sum approximation)
    vec3 Fenv     = F_SchlickRoughness(NdotV, F0, roughness);
    vec2 brdf     = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 specIBL  = ambientColor * (Fenv * brdf.x + brdf.y);
    vec3 diffIBL  = ambientColor * albedo * (1.0 - Fenv) * (1.0 - metallic);
    vec3 ambient  = diffIBL + specIBL;

    float viewDepth = length(vWorldPos - cameraPos);
    float shadowFactor = sampleCSM(vWorldPos, viewDepth);
    vec3 color = direct * shadowFactor + ambient;

    // Reinhard tone-map + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    // Fog
    float dist      = viewDepth;
    float fogFactor = clamp((fogFar - dist) / (fogFar - fogNear), 0.0, 1.0);
    color = mix(fogColor, color, fogFactor);

    fragColor = vec4(color, albedoSample.a);
}
