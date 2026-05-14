#pragma once
#include <cstdint>

// ── NpcDevelopmentStage ───────────────────────────────────────────────────────
// MD ALIEN_DEVELOPMENT_MANAGER_STAGES — NPC learns player's habits over time.
enum class NpcDevelopmentStage : uint16_t {
    Naive          =   0,
    ThreatAware    =   2,
    GettingSneaky  = 398,
    ReallySneaky   = 478,
};

// ── npc_ability ───────────────────────────────────────────────────────────────
// MD ALIEN_DEVELOPMENT_MANAGER_ABILITIES — bitmask capability flags.
namespace npc_ability {
    static constexpr uint16_t ThreatAware                  =   2;
    static constexpr uint16_t ClosesViaBackstage           =   4;
    static constexpr uint16_t WillKilltrap                 =   8;
    static constexpr uint16_t WillFlank                    =  16;
    static constexpr uint16_t WillFlankFromThreatAware     =  32;
    static constexpr uint16_t WillAmbush                   =  64;
    static constexpr uint16_t SearchLockers                = 128;
    static constexpr uint16_t SearchUnderStuff             = 256;
}

// ── NpcDevelopmentComponent ───────────────────────────────────────────────────
// 8 bytes — tracks NPC's accumulated score + unlocked abilities.
// AddScore() auto-advances stage and unlocks abilities at thresholds.
struct NpcDevelopmentComponent {
    uint16_t            score          = 0;
    uint16_t            abilities_mask = 0;
    NpcDevelopmentStage stage          = NpcDevelopmentStage::Naive;
    uint8_t             _pad[2]        = {};

    void AddScore(uint16_t delta) noexcept {
        uint32_t s = static_cast<uint32_t>(score) + delta;
        if (s > 0xFFFF) s = 0xFFFF;
        score = static_cast<uint16_t>(s);

        if (score >= static_cast<uint16_t>(NpcDevelopmentStage::ReallySneaky)) {
            stage = NpcDevelopmentStage::ReallySneaky;
            abilities_mask = npc_ability::ThreatAware
                           | npc_ability::ClosesViaBackstage
                           | npc_ability::WillKilltrap
                           | npc_ability::WillFlank
                           | npc_ability::WillFlankFromThreatAware
                           | npc_ability::WillAmbush
                           | npc_ability::SearchLockers
                           | npc_ability::SearchUnderStuff;
        } else if (score >= static_cast<uint16_t>(NpcDevelopmentStage::GettingSneaky)) {
            stage = NpcDevelopmentStage::GettingSneaky;
            abilities_mask |= npc_ability::ThreatAware
                            | npc_ability::ClosesViaBackstage
                            | npc_ability::WillKilltrap
                            | npc_ability::WillFlank
                            | npc_ability::SearchLockers;
        } else if (score >= static_cast<uint16_t>(NpcDevelopmentStage::ThreatAware)) {
            stage = NpcDevelopmentStage::ThreatAware;
            abilities_mask |= npc_ability::ThreatAware;
        }
    }

    bool HasAbility(uint16_t bit) const noexcept {
        return (abilities_mask & bit) != 0;
    }
};
static_assert(sizeof(NpcDevelopmentComponent) == 8, "NpcDevelopmentComponent must be 8 bytes");
