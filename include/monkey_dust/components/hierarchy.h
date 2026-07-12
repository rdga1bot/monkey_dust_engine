#pragma once
// ParentRef/ChildrenRef — minimal EnTT-native entity hierarchy
// (ARCHITECTURE_IDEAS.md #9 / ARCHITECTURE_PLAN_7_9.md).
//
// Scoped for DYNAMIC entities only (NPC/items/buildings) — NEVER attach to
// terrain chunks, which live in their own flat array
// (SceneRender::terrain_chunks), not the EnTT registry. Mixing the two is
// exactly the mistake this was designed to avoid.
//
// Confirmed real future consumers (not speculative — see
// ARCHITECTURE_PLAN_7_9.md discussion):
//   - Carrying an unconscious/wounded NPC (Kenshi's "sling over shoulder,
//     lay on bed" mechanic — IS_KNOCKED_OUT already exists as a state flag,
//     the physical carry itself does not exist yet).
//   - Leading captives/slaves (same parent-child shape as carrying).
//
// Explicitly NOT for (confirmed NOT to need this):
//   - Weapon-in-hand: a bone-socket lookup at render time against the
//     wielder's own skeleton (like SkinMesh::LastBoneWorldY), not an
//     entity-to-entity relation.
//   - Squad formation: a logical target-offset from the leader computed in
//     AI (already solved by Squad::leader, components/squad.h), not a
//     transform hierarchy.
//   - Cart + pack animal: a physics constraint/joint (Jolt), not a rigid
//     parent-child relation.
#include <monkey_dust/ecs/md_entity.h>

static constexpr int HIERARCHY_MAX_CHILDREN = 4;

struct ParentRef {
    MdEntity parent = MdEntity::Null();
};

struct ChildrenRef {
    MdEntity children[HIERARCHY_MAX_CHILDREN] = {};
    int          count                             = 0;
};
