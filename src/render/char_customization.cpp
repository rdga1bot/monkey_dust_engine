// char_customization.cpp — Variant A: Kenshi bone-scale + face morph wiring.
// Port of SetBoneScalesFromDef() + kFaceMorphMap from editor_char_preview_sdlgpu.h.
// No game/ or tools/ includes. No malloc/new. No C++20. No assert().
#include <monkey_dust/render/char_customization.h>
#include <monkey_dust/render/skin_mesh.h>
#include <cstring>
#include <cmath>

// ── CharCustomization_ComputeScales ──────────────────────────────────────────
// Direct port of SetBoneScalesFromDef() bone formulas.
// All bone indices are confirmed from md_human.glb (30 bones):
//   0=Bip01(root)  1=Pelvis
//   2=L Thigh  3=L Calf  4=L Foot  5=L Toe0  6=L Toe0Nub
//   7=R Thigh  8=R Calf  9=R Foot 10=R Toe0 11=R Toe0Nub
//  12=Spine  13=Spine1  14=Spine2
//  15=L Clavicle 16=L UpperArm 17=L Forearm 18=L Hand  19=Prop1
//  20=Neck   21=Head    22=HeadNub 23=Jaw    24=JawNub
//  25=R Clavicle 26=R UpperArm 27=R Forearm 28=R Hand  29=Prop2

void CharCustomization_ComputeScales(const float body[CHARCC_BODY_N],
                                      const float face[CHARCC_FACE_N],
                                      int         bone_count,
                                      CharScales& out)
{
    // Initialise to identity
    for (int i = 0; i < MAX_SKIN_BONES; ++i) {
        out.bone[i][0] = 1.f; out.bone[i][1] = 1.f; out.bone[i][2] = 1.f;
        out.pos [i][0] = 1.f; out.pos [i][1] = 1.f; out.pos [i][2] = 1.f;
    }

    if (bone_count <= 0) return;

    // ── Helper lambdas ────────────────────────────────────────────────────────
    auto cl = [](float x) -> float {
        return x < 0.1f ? 0.1f : (x > 4.f ? 4.f : x);
    };
    // comp: blended scale: 1.0 + (x-1.0)*k (compresses deviation by factor k)
    auto comp = [&](float x, float k) -> float {
        return cl(1.f + (x - 1.f) * k);
    };
    // setBS: set bone vertex scales
    auto setBS = [&](int i, float wy, float wx, float wz) {
        if (i < 0 || i >= MAX_SKIN_BONES) return;
        out.bone[i][0] = wy; out.bone[i][1] = wx; out.bone[i][2] = wz;
    };

    // ── Body slider values → normalised scale factors ─────────────────────────
    // slider=100 → factor=1.0 → no deformation (mesh baked at slider=100).
    float H   = cl(body[2]  / 100.f);  // Height
    float Fr  = cl(body[3]  / 100.f);  // Frame
    // body[4] Posture: neutral=35 in [0,99]; map to 0-1 where 35=neutral
    float Po  = cl(body[4]  /  35.f);  // Posture (0=slouch, >1=upright)
    float SS  = cl(body[5]  / 100.f);  // Shoulder set (clavicle height)
    float NP  = cl(body[6]  / 100.f);  // Neck position (neck attach height)
    float LL  = cl(body[7]  / 100.f);  // Leg length
    float Sh  = cl(body[8]  / 100.f);  // Shoulders
    float Ab  = cl(body[9]  / 100.f);  // Arm bulk
    float Wa  = cl(body[10] / 100.f);  // Waist
    float Hn  = cl(body[11] / 100.f);  // Hands
    float Ch  = cl(body[12] / 100.f);  // Chest
    float St  = cl(body[13] / 100.f);  // Stomach
    float Hips= cl(body[15] / 100.f);  // Hips
    float LgB = cl(body[16] / 100.f);  // Legs bulk
    float Ft  = cl(body[17] / 100.f);  // Feet
    float LgS = 1.f;                   // LegShape — not in our body[], default neutral

    // ── Lower body ────────────────────────────────────────────────────────────
    // overall_XZ = (LgShape*LgBulk + (Hips-1)/3) * Frame
    float overall_XZ = cl((LgS * LgB + (Hips - 1.f) / 3.f) * Fr);
    float leg_Y = cl((H + LL - 1.f) * 0.95f);

    // Pelvis [1]
    setBS(1, H, comp(Hips,0.6f)*Fr, comp(Hips,0.6f)*Fr);

    // Thighs [2,7]: setBoneSize + setBonePositionalSize Y (lateral)
    setBS(2, leg_Y, overall_XZ, overall_XZ);
    setBS(7, leg_Y, overall_XZ, overall_XZ);
    float thigh_pos = cl(Fr * (2.f - H) * Hips);
    out.pos[2][1] = thigh_pos;
    out.pos[7][1] = thigh_pos;

    // Calves [3,8]: XZ quadratic
    float calf_XZ = cl((2.f - LgS) * LgB * Fr);
    setBS(3, leg_Y * calf_XZ, calf_XZ*calf_XZ, calf_XZ*calf_XZ);
    setBS(8, leg_Y * calf_XZ, calf_XZ*calf_XZ, calf_XZ*calf_XZ);

    // Feet [4,9]
    float FtH = cl(Ft * H);
    setBS(4, cl(LL * FtH), cl(FtH*FtH), cl(FtH*FtH));
    setBS(9, cl(LL * FtH), cl(FtH*FtH), cl(FtH*FtH));

    // Toes [5,10]
    float FtH2 = FtH * FtH;
    for (int ji = 5; ji <= 10; ji += 5) {
        out.bone[ji][0] = FtH2; out.bone[ji][1] = FtH2; out.bone[ji][2] = FtH2;
    }
    out.pos[5][2]  = FtH;
    out.pos[10][2] = FtH;

    // ── Torso ─────────────────────────────────────────────────────────────────
    float HipsC = comp(Hips, 0.6f);

    // Spine [12]: (comp(Hips,0.6)*Fr, H, comp(Hips,0.6)*St*Fr)
    setBS(12, H, HipsC*Fr, HipsC*St*Fr);

    // Spine1 [13]: compress Waist/Stomach through comp() to avoid 2× torso at slider max
    setBS(13, H, comp(Wa,0.7f)*Fr, comp(St,0.65f)*Fr * comp(Po, 0.1f));

    // Spine2 [14]
    setBS(14, H, comp(Ch,0.45f)*Fr, comp(Ch,0.9f)*Fr * comp(Po, 0.08f));

    // ── Arms ──────────────────────────────────────────────────────────────────
    // Clavicles [15,25]: Shoulder set adjusts their vertical attachment
    float ShY = comp(Sh, 0.3f)*Fr;
    for (int ji = 15; ji <= 25; ji += 10) {
        out.bone[ji][0] = Sh*Fr; out.bone[ji][1] = ShY; out.bone[ji][2] = Sh*Fr;
    }
    // Shoulder set: scale clavicle positional Y offset (raises/lowers shoulder line)
    float ss_pos = comp(SS, 0.5f);
    out.pos[15][1] = ss_pos;
    out.pos[25][1] = ss_pos;

    // UpperArms [16,26]
    // Ab controls XZ thickness; arm Y (length) uses only H*Fr — not Ab (bulk != length)
    float AbFr = Ab * Fr;
    float AbZ  = comp(Ab, 1.5f) * Fr;
    for (int ji = 16; ji <= 26; ji += 10) {
        out.bone[ji][0] = AbFr*AbFr; out.bone[ji][1] = H*Fr; out.bone[ji][2] = AbZ*AbFr;
    }
    float arm_pos = cl(Sh * H);
    out.pos[16][0] = arm_pos;
    out.pos[26][0] = arm_pos;

    // Forearms [17,27]
    for (int ji = 17; ji <= 27; ji += 10) {
        out.bone[ji][0] = AbFr*AbFr; out.bone[ji][1] = H*Fr; out.bone[ji][2] = AbFr*AbFr;
    }

    // Hands [18,28]
    for (int ji = 18; ji <= 28; ji += 10) {
        out.bone[ji][0] = AbFr*Hn*Hn; out.bone[ji][1] = H*Hn*Hn; out.bone[ji][2] = AbFr*Hn*Hn;
    }

    // ── Head/Neck ─────────────────────────────────────────────────────────────
    float Nc  = cl(face[2]  / 100.f);  // Neck (overall neck scale — face[2])
    float Nw  = cl(face[3]  / 100.f);  // Neck width
    float Nl  = cl(face[4]  / 100.f);  // Neck length
    float jaw = cl(face[17] / 100.f);  // Jaw

    // Neck [20]: Nc scales overall neck, Nl scales Y, NP shifts attach height
    setBS(20, Nl * Nc, Nw*Fr*Nc, jaw*Fr*Nc);
    out.pos[20][1] = comp(NP, 0.4f);  // Neck position: vertical attach offset

    // Head [21]
    float Hd  = cl(face[0] / 100.f);  // Head size
    float FrH = comp(Fr, 0.25f);
    float Hsp = cl(face[1] / 100.f);  // Head shape
    setBS(21, FrH*Hd, FrH*Hd*Hsp, FrH*Hd);

    // Jaw [23]
    if (23 < MAX_SKIN_BONES) {
        out.bone[23][0] = FrH*Hd;
        out.bone[23][1] = FrH*Hd*Hsp*jaw;
        out.bone[23][2] = FrH*Hd;
    }
}

// ── CharCustomization_ApplyMorphs ────────────────────────────────────────────
// Port of kFaceMorphMap from editor_char_preview_sdlgpu.h.
// Each entry maps face[idx] → morph target name(s).
// Weight: if val > def → positive morph weight proportional to (val-def)/(hi-def).
//         if val < def and neg != nullptr → negative morph weight ∝ (def-val)/(def-lo).
//         otherwise → 0.

struct FaceMorphEntry { int idx; const char* pos; const char* neg; };

static const FaceMorphEntry kFaceMorphMap[] = {
    {  5, "big_eyes",           nullptr             },  // Eye size
    {  6, "narrow_eyes",        nullptr             },  // Eye shape
    {  7, "close_eyes",         nullptr             },  // Eye spacing
    {  8, "high_eyes",          nullptr             },  // Eye height
    {  9, "wide_nose",          nullptr             },  // Nose width
    { 10, "long_nose",          nullptr             },  // Nose length
    { 11, "arch_nose",          nullptr             },  // Nose depth
    { 12, "tiltup_nose",        "tiltdown_nose"     },  // Nose tip
    { 13, "wide_cheekbones",    "narrow_cheekbones" },  // Cheekbone
    { 14, "shallow_eyes",       nullptr             },  // Eyes depth
    { 15, "tiltup_brow",        "tiltdown_brow"     },  // Brow
    { 16, "high_brow",          "low_brow"          },  // Brow height
    { 18, "wide_mouth",         nullptr             },  // Mouth width
    { 20, "big_mouth",          nullptr             },  // Lips
    { 21, "overbite",           "underbite"         },  // Chin
    { 22, "tiltup_eyes",        "tiltdown_eyes"     },  // Eyes tilt
    { 23, "high_nose",          nullptr             },  // Nose pos.
};
static constexpr int kFaceMorphMapN = 17;

void CharCustomization_ApplyMorphs(const float face[CHARCC_FACE_N],
                                    const float face_def[CHARCC_FACE_N],
                                    const float face_lo[CHARCC_FACE_N],
                                    const float face_hi[CHARCC_FACE_N],
                                    SkinMesh&   mesh)
{
    for (int m = 0; m < kFaceMorphMapN; ++m) {
        const FaceMorphEntry& e = kFaceMorphMap[m];
        int fi = e.idx;
        if (fi < 0 || fi >= CHARCC_FACE_N) continue;

        float val = face[fi];
        float def = face_def[fi];
        float lo  = face_lo[fi];
        float hi  = face_hi[fi];

        if (val >= def) {
            // Positive side morph
            float range = hi - def;
            float w = (range > 0.001f) ? (val - def) / range : 0.f;
            if (w < 0.f) w = 0.f;
            if (w > 1.f) w = 1.f;
            if (e.pos) {
                // Find morph by name and set weight
                for (int i = 0; i < mesh.MorphCount(); ++i) {
                    if (strcmp(mesh.MorphName(i), e.pos) == 0) {
                        mesh.morph_weights[i] = w;
                        break;
                    }
                }
            }
            // Zero out the negative-side morph if it exists
            if (e.neg) {
                for (int i = 0; i < mesh.MorphCount(); ++i) {
                    if (strcmp(mesh.MorphName(i), e.neg) == 0) {
                        mesh.morph_weights[i] = 0.f;
                        break;
                    }
                }
            }
        } else {
            // Below neutral: zero positive side, apply negative if available
            if (e.pos) {
                for (int i = 0; i < mesh.MorphCount(); ++i) {
                    if (strcmp(mesh.MorphName(i), e.pos) == 0) {
                        mesh.morph_weights[i] = 0.f;
                        break;
                    }
                }
            }
            if (e.neg) {
                float range = def - lo;
                float w = (range > 0.001f) ? (def - val) / range : 0.f;
                if (w < 0.f) w = 0.f;
                if (w > 1.f) w = 1.f;
                for (int i = 0; i < mesh.MorphCount(); ++i) {
                    if (strcmp(mesh.MorphName(i), e.neg) == 0) {
                        mesh.morph_weights[i] = w;
                        break;
                    }
                }
            }
        }
    }
}
