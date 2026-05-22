#pragma once
#include <cstdint>

// NpcSoundEvent — MD ActionTriggerSound pattern.
// BT node ActionTriggerSound fires one of these events; game side binds to AudioSystem.
// Values intentionally match MD SoundType indices where documented.
enum class NpcSoundEvent : uint8_t {
    None              =  0,
    SuspectWarning    =  1,  // NPC notices something suspicious
    EngageEnemy       =  2,  // NPC commits to attacking
    ChargeToAttack    =  3,  // MD ALIEN_CHARGE_TO_ATTACK:3
    Investigate       =  4,  // NPC inspects area
    LostContact       =  5,  // NPC loses track of target
    SearchStart       =  6,  // NPC begins a systematic search
    SearchEnd         =  7,  // NPC abandons search
    Alert             =  8,  // NPC reaches full alert
    StartSearching    =  9,  // MD ALIEN_STARTS_SEARCHING:9
    // VBfA-R VB-5: morale speech triggers ─────────────────────────────────────
    MoraleTaunt       = 10,  // NPC taunts enemy (high morale, outnumbers)
    MoraleBoost       = 11,  // NPC rallies allies (leader speech)
    MoraleFlee        = 12,  // NPC cries retreat (low morale)
    MoraleVictory     = 13,  // NPC celebrates kill / victory shout
    MoraleWounded     = 14,  // NPC reacts to own injury (pain speech)
    MoraleAllyDown    = 15,  // NPC reacts to ally being killed
    MoraleEnemyDown   = 16,  // NPC reacts to enemy kill (battle cry)
    MoraleSurrender   = 17,  // NPC surrenders (very low morale)
    MoraleHoldLine    = 18,  // NPC commands squad to hold position
    MoraleCharge      = 19,  // NPC orders or joins a charge
};

// Optional game-side callback for sound events.
// Register with NpcSoundBus::SetCallback before running BTs that use ActionTriggerSound.
using NpcSoundCb = void (*)(NpcSoundEvent, uint32_t entity_id);

class NpcSoundBus {
public:
    static NpcSoundBus& Get() noexcept {
        static NpcSoundBus inst;
        return inst;
    }
    void SetCallback(NpcSoundCb cb) noexcept { cb_ = cb; }
    void Fire(NpcSoundEvent ev, uint32_t entity_id) noexcept {
        if (cb_) cb_(ev, entity_id);
    }
    void ClearCallback() noexcept { cb_ = nullptr; }
private:
    NpcSoundCb cb_ = nullptr;
};
