#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <cstdint>

// SquadComponent — groups NPCs into a coordinated squad (Kenshi Phase 2, Task 2.3).
// squad_id maps to SquadSignalBus channels (MAX_SQUADS=8).
// formation selects a hardcoded spacing pattern; leader=MdEntity::Null() → no assigned leader.


enum class SquadFormation : uint8_t {
    None   = 0,
    Line   = 1,  // horizontal line behind leader
    Wedge  = 2,  // V-shape
    Circle = 3,  // surround target
};

static constexpr int SQUAD_MAX_MEMBERS = 20;

struct SquadComponent {
    MdEntity members[SQUAD_MAX_MEMBERS];
    int          member_count = 0;
    MdEntity leader       = MdEntity::Null();
    SquadFormation formation  = SquadFormation::None;
    uint8_t      squad_id     = 0;  // index into SquadSignalBus (0..MAX_SQUADS-1)
    uint8_t      _pad[2]      = {};
};
