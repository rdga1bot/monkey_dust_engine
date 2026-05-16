#pragma once
#include <cstdint>

// ── NpcInteractionComponent (M58) ────────────────────────────────────────────
// Marks an NPC entity as interactable via the dialogue system.
// Game code checks player distance each logic tick; when player is within
// interaction_range and presses the interact key, DialogSystem::FindDialogForFaction
// is called with dialog_faction_id to open the conversation.
//
// is_available: set false while a dialog is active to prevent re-trigger.
//   Game code clears it when the dialog closes.
// cooldown_ms: minimum ms between consecutive interactions (spam guard).

struct NpcInteractionComponent {
    uint32_t dialog_faction_id = 0;   // key for DialogSystem::FindDialogForFaction
    float    interaction_range = 2.5f; // metres; player must be within this to interact
    uint32_t cooldown_ms       = 5000; // minimum ms between interactions
    uint32_t last_interact_ms  = 0;    // timestamp of last interaction
    bool     is_available      = true; // false while dialog is open
};
static_assert(sizeof(NpcInteractionComponent) == 20, "NpcInteractionComponent must be 20 bytes");
