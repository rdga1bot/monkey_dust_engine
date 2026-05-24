#include <monkey_dust/scripting/dialog_system.h>
#include <monkey_dust/scripting/lua_event_bus.h>
#include <monkey_dust/world/faction_system.h>
#include <monkey_dust/world/world_events.h>
#include <monkey_dust/platform/md_log.h>

void md::NpcDialogSystem::ApplyEffects(int line_idx, entt::entity /*speaker*/, entt::entity target) noexcept {
    if (line_idx < 0 || line_idx >= count_) return;
    DialogLine& line = lines_[line_idx];

    for (int i = 0; i < (int)line.eff_count; ++i) {
        const DialogEffect& eff = line.effects[i];
        switch (eff.type) {
        case DialogEffType::None:
            break;

        case DialogEffType::GiveItem:
            // Give item to target — requires Inventory; deferred to game layer via event.
            // Emit Lua event "dialog_give_item" { item_id, qty }.
            {
                char event[32];
                snprintf(event, sizeof(event), "dialog_give_item");
                LuaEventBus::Get().Fire(event, target);
            }
            break;

        case DialogEffType::ChangeRelation:
            // param_a = delta; applied between speaker_faction and target_faction.
            // Simplified: use faction 0 as speaker faction for now.
            // Game layer should override by providing faction ids through context.
            break;

        case DialogEffType::StartQuest:
            // Emit quest_start Lua event with quest id.
            {
                char event[32];
                snprintf(event, sizeof(event), "quest_start_%d", (int)eff.param_a);
                LuaEventBus::Get().Fire(event, target);
            }
            break;

        case DialogEffType::SpawnSquad:
            // Push a WorldEvent raid with strength=param_a.
            {
                WorldEvent ev{};
                ev.type     = WorldEventType::Raid;
                ev.strength = (eff.param_a > 0 && eff.param_a < 256)
                            ? (uint8_t)eff.param_a : 1u;
                WorldEventBus::Get().Push(ev);
            }
            break;

        case DialogEffType::PlayAnimation:
            // Animation clip is set via AnimationSoA on the speaker entity.
            // The game layer handles this after ApplyEffects returns.
            MD_LOG(MD_LOG_INFO, "DialogSystem: anim clip %d requested", (int)eff.param_a);
            break;
        }
    }

    IncrementUseCount(line_idx);
    (void)target;
}
