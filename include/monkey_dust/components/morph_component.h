#pragma once
#include <cstdint>

// MorphComponent — per-entity vertex morph weights for GPU skinning upload.
// Matches Kenshi editor_data_human.xml slider layout.
// Upload path: AnimationSoA morph target SSBO (binding=6).

static constexpr int MORPH_BODY_COUNT = 19;
static constexpr int MORPH_FACE_COUNT = 21;

struct MorphComponent {
    // Body morphs (index 0–18) — driven by CharacterDef sliders
    float body[MORPH_BODY_COUNT] = {};
    //   0=Height   1=Frame    2=Posture  3=LegLength   4=Shoulders
    //   5=ArmBulk  6=Chest    7=Belly    8=Hips        9=NeckLength
    //  10=HeadSize 11=JawWidth 12=Tall   13=Fat        14=Muscular
    //  15=Longlegs 16=Bighead  17=BroadShdr 18=Bulk

    // Face morphs (index 0–20) — driven by face slider tab
    float face[MORPH_FACE_COUNT] = {};
    //   0=HeadW    1=HeadD    2=FaceW    3=FaceD       4=ChinH
    //   5=ChinW    6=JawW     7=CheekW   8=NoseW       9=NoseH
    //  10=NoseTip 11=EyeW    12=EyeH    13=EyeSep     14=BrowH
    //  15=BrowW   16=LipW    17=LipH    18=EarH        19=EarW  20=EarProt

    bool dirty = true;   // set when weights changed; cleared after GPU upload
};
static_assert(sizeof(MorphComponent) == (MORPH_BODY_COUNT + MORPH_FACE_COUNT) * 4 + 4,
              "MorphComponent size mismatch");
