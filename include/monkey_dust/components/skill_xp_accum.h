#pragma once
#include <monkey_dust/components/stat_sheet.h>

// SkillXpAccum — per-entity fractional train-by-doing XP accumulator.
// Keeps sub-1 remainders so slow XP trickle still accumulates correctly.
//
// Definition lives in engine/ (grant LOGIC — GrantSkillXP/ApplySkillXpGrant —
// stays in game/src/combat/skill_xp.h) so spawn code under tools/ (which must
// not #include from game/ — split-readiness) can pre-seed it alongside
// StatSheet at NPC creation time, avoiding ApplySkillXpGrant's first-grant
// no-op safety-net path (see skill_xp.h's ApplySkillXpGrant doc comment).
struct SkillXpAccum {
    float pending[static_cast<int>(Skill::COUNT)] = {};
};
