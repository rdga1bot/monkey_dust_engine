#pragma once
// char_customization.h — Variant A character customisation.
// Ports Kenshi-style bone-scale + face morph wiring from editor_char_preview_sdlgpu.h
// into a runtime-usable engine API.
// engine/ only: no game/ or tools/ includes.

#include <monkey_dust/render/skin_mesh.h>

static constexpr int CHARCC_BODY_N = 18;
static constexpr int CHARCC_FACE_N = 24;

// Per-bone scale factors computed from body[]/face[] slider values.
// bone[i] = vertex/geometry scale (setBoneSize equivalent).
// pos[i]  = translation scale applied to bind-pose bone offset (setBonePositionalSize).
// rot[i]  = extra local-Y rotation in radians (0=none); composited onto animation quat.
//   Biped rig: Y = sagittal axis. Positive = forward flex (hunch), negative = extension.
// All values default to {1,1,1}/{1,1,1}/0 = identity (no deformation).
struct CharScales {
    float    bone[MAX_SKIN_BONES][3];  // vertex scale per bone (x/y/z)
    float    pos [MAX_SKIN_BONES][3];  // translation scale per bone (x/y/z)
    float    rot [MAX_SKIN_BONES];     // extra rotation around local Y axis (radians)
    uint32_t amputee_mask;             // KEN-MORPH-1: bit i set → bone i amputated (scale *= 0.8)
    // Full quaternion delta override (xyzw). identity = {0,0,0,1}.
    // When qrot_delta[i][3] < 1.0, applied post-multiply in bone local space.
    // Takes precedence over rot[i] for the same bone.
    float    qrot_delta[MAX_SKIN_BONES][4];
};

// Compute CharScales from body[18]+face[24] slider values.
// Port of SetBoneScalesFromDef() from tools/editor/editor_char_preview_sdlgpu.h.
// bone_count: mesh.bone_count (typically 30 for md_human.glb).
// Slider neutral = 100 → all scales = 1.0 (no deformation).
void CharCustomization_ComputeScales(const float body[CHARCC_BODY_N],
                                      const float face[CHARCC_FACE_N],
                                      int         bone_count,
                                      CharScales& out);

// Map face[] slider values to SkinMesh morph target weights.
// face_def[24] = neutral values (= zero morph weight).
// face_lo[24]  = minimum slider values (below def → negative side morph or 0).
// face_hi[24]  = maximum slider values (above def → positive side morph).
// Sets mesh.morph_weights[] in-place; non-matching morphs are left unchanged.
void CharCustomization_ApplyMorphs(const float face[CHARCC_FACE_N],
                                    const float face_def[CHARCC_FACE_N],
                                    const float face_lo[CHARCC_FACE_N],
                                    const float face_hi[CHARCC_FACE_N],
                                    SkinMesh&   mesh);
