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
    // Kenshi XML: no mid= → neutral = (min+max)/2.
    // Posture:      min=0,  max=70  → neutral=35
    // Shoulder set: min=45, max=90  → neutral=67.5
    // Neck position: min=25, max=80 → neutral=52.5
    float Po  = cl(body[4]  /  35.0f);  // Posture
    float SS  = cl(body[5]  /  67.5f);  // Shoulder set — neutral=67.5, not 100
    float NP  = cl(body[6]  /  52.5f);  // Neck position — neutral=52.5, not 100
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
    float LgS = 1.0f;  // Legs shape: neutral (slider not in BODY_N=18 array)

    // ── Lower body ────────────────────────────────────────────────────────────
    // (local_1b4 = "Hips" slider, NOT Height — Hips=1 neutral → legXZ width
    // stays constant across Height, avoiding a compounded width+length shrink/grow.)
    float legXZ    = cl((LgS * LgB + (Hips - 1.f) / 3.f) * Fr);
    float overall_XZ = legXZ;  // used by thighs/calves
    float leg_Y = cl(H + LL - 1.f);

    setBS(1, H, cl(Hips * Fr), cl(Hips * Fr));

    // Thighs [2,7]: setBoneSize + setBonePositionalSize Y (lateral)
    setBS(2, leg_Y * 0.95f, overall_XZ, overall_XZ);
    setBS(7, leg_Y * 0.95f, overall_XZ, overall_XZ);
    // bind_t[2/7][1] (~0.099) is the LATERAL hip-spread offset, not the length axis —
    // scaling it by an H-dependent factor pushed legs apart at low H ("balloon pants").
    // RE's setBonePositionalSize Y-component maps to bind_t[][0] (~0 for thigh bones
    // in this rig), so it has no visible effect here — applied for axis consistency.
    float thigh_pos = cl(Fr * (2.f - H) * Hips);
    out.pos[2][0] = thigh_pos;
    out.pos[7][0] = thigh_pos;

    // Calves [3,8]: XZ quadratic + positional X (bind_t X=0.439)
    float calf_XZ = cl((2.f - LgS) * LgB * Fr);
    setBS(3, leg_Y * calf_XZ * 0.95f, calf_XZ*calf_XZ, calf_XZ*calf_XZ);
    setBS(8, leg_Y * calf_XZ * 0.95f, calf_XZ*calf_XZ, calf_XZ*calf_XZ);
    out.pos[3][0] = leg_Y;
    out.pos[8][0] = leg_Y;

    // Feet [4,9]: positional X (bind_t X=0.461)
    float FtH = cl(Ft * H);
    setBS(4, cl(LL * FtH), cl(FtH*FtH), cl(FtH*FtH));
    setBS(9, cl(LL * FtH), cl(FtH*FtH), cl(FtH*FtH));
    out.pos[4][0] = leg_Y;
    out.pos[9][0] = leg_Y;

    // Toes [5,10]
    float FtH2 = FtH * FtH;
    for (int ji = 5; ji <= 10; ji += 5) {
        out.bone[ji][0] = FtH2; out.bone[ji][1] = FtH2; out.bone[ji][2] = FtH2;
    }
    out.pos[5][2]  = FtH;
    out.pos[10][2] = FtH;

    // ── Torso ─────────────────────────────────────────────────────────────────
    // fVar17=H is comp()'d with 0.6 for spine width; Waist/Stomach used direct.

    // Spine [12]: Vector3(comp(Hips,0.6)*Fr, H, comp(Hips,0.6)*Stomach*Fr)
    setBS(12, H, comp(Hips,0.6f)*Fr, comp(Hips,0.6f)*St*Fr);

    // Spine1 [13]: Vector3(Waist*Fr, H, Stomach*Fr) — no extra comp()
    setBS(13, H, Wa*Fr, St*Fr);
    out.pos[13][0] = H;  // Spine1 from Spine (bind X=0.141)

    // Spine2 [14]: Vector3(comp(Chest,0.45)*Fr, H, comp(Chest,0.9)*Fr)
    setBS(14, H, comp(Ch,0.45f)*Fr, comp(Ch,0.9f)*Fr);
    out.pos[14][0] = H;  // Spine2 from Spine1 (bind X=0.157)

    // ── Arms ──────────────────────────────────────────────────────────────────
    // Engine note: bone[i][j] only scales vertex skinning — it does NOT propagate through
    // the skeleton hierarchy. pos[i][j] is the only way to move a bone relative to parent.
    // We must manually simulate OGRE's parent-scale propagation via pos[].

    float AbFr = Ab * Fr;
    float AbZ  = comp(Ab, 1.5f) * Fr;

    // as UpperArm pre-multiplication = Vector3(AbFr, H, AbZ).
    for (int ji = 15; ji <= 25; ji += 10) {
        out.bone[ji][0] = AbFr; out.bone[ji][1] = H; out.bone[ji][2] = AbZ;
    }
    // Clavicle positional from Spine2: bind_t=(+0.209 up, ±0.021 lat, -0.081 fwd).
    // KenshiLib confirmed: setPosture(posture, neck, shoulders) calls
    //   setBonePositionalSize(Clavicle, Vector3(SS, 1, 1))
    // In OGRE: Clavicle X-pos = Spine2_X_scale(H) × bind_t[0] × SS — hierarchy auto-propagates.
    // Our engine has no auto-propagation → multiply manually: H × SS.
    // [0] = H * SS:               vertical position (OGRE-equivalent: Spine2 height × shoulder set)
    // [1] = comp(Ch,0.45) * Fr:   lateral (chest width propagation, 0.021m component)
    // [2] = comp(Ch,0.9)  * Fr:   depth/forward (chest depth propagation, 0.081m dominant component)
    float ChW = comp(Ch, 0.45f) * Fr;
    float ChD = comp(Ch, 0.9f)  * Fr;
    out.pos[15][0] = H * SS;  out.pos[15][1] = ChW;  out.pos[15][2] = ChD;
    out.pos[25][0] = H * SS;  out.pos[25][1] = ChW;  out.pos[25][2] = ChD;

    for (int ji = 16; ji <= 26; ji += 10) {
        out.bone[ji][0] = AbFr*AbFr; out.bone[ji][1] = H*AbFr; out.bone[ji][2] = AbZ*AbFr;
    }
    // UpperArm positional from Clavicle: Kenshi setBonePositionalSize(UpperArm, (Sh*comp(Ch,0.45),1,1)).
    // setBonePositionalSize is absolute — does NOT compound with parent Clavicle bone scale (AbFr).
    float arm_pos = cl(Sh * comp(Ch, 0.45f));
    out.pos[16][0] = arm_pos;
    out.pos[26][0] = arm_pos;

    // Forearms [17,27]: setBoneSize = Vector3(AbFr, H, AbFr) *= AbFr = (AbFr², H·AbFr, AbFr²).
    for (int ji = 17; ji <= 27; ji += 10) {
        out.bone[ji][0] = AbFr*AbFr; out.bone[ji][1] = H*AbFr; out.bone[ji][2] = AbFr*AbFr;
    }
    // Forearm positional from UpperArm — OGRE auto-propagates UpperArm X-scale (AbFr²).
    // Our engine has no auto-propagation → simulate: pos[17][0] = AbFr².
    out.pos[17][0] = AbFr*AbFr;
    out.pos[27][0] = AbFr*AbFr;

    // → (AbFr·Hn², H·Hn², AbFr·Hn²). Direct port, same local axes as Kenshi.
    for (int ji = 18; ji <= 28; ji += 10) {
        out.bone[ji][0] = AbFr*Hn*Hn; out.bone[ji][1] = H*Hn*Hn; out.bone[ji][2] = AbFr*Hn*Hn;
    }
    // Hand positional from Forearm — same propagation logic as Forearm from UpperArm.
    out.pos[18][0] = AbFr*AbFr;
    out.pos[28][0] = AbFr*AbFr;

    // ── Head/Neck ─────────────────────────────────────────────────────────────
    float Nc  = cl(face[2]  / 100.f);  // Neck (overall neck scale — face[2])
    float Nw  = cl(face[3]  / 100.f);  // Neck width
    float Nl  = cl(face[4]  / 100.f);  // Neck length
    float jaw = cl(face[17] / 100.f);  // Jaw

    // Neck [20]: original Vector3(NeckWidth*Fr, NeckLength/100, "Neck"-slider*Fr) —
    // Z uses the dedicated "Neck" slider (Nc), not "Jaw"; vertical bones swap X/Y in
    // our rig (confirmed via legs/pelvis/head), so length (orig Y) lands in bone[][0].
    setBS(20, Nl, Nw*Fr, Nc*Fr);
    out.pos[20][0] = H;               // Neck from Spine2 (bind X=0.297)
    out.pos[20][1] = comp(NP, 0.4f);  // Neck position: vertical attach offset

    // Head [21]
    float Hd  = cl(face[0] / 100.f);  // Head size
    float FrH = comp(Fr, 0.25f);
    float Hsp = cl(face[1] / 100.f);  // Head shape
    setBS(21, FrH*Hd, FrH*Hd*Hsp, FrH*Hd);
    out.pos[21][0] = H;               // Head from Neck (bind X=0.138)

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
