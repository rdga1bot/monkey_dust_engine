#pragma once
#include <cstdint>

// NpcSoundEvent — CATHODE ActionTriggerSound pattern.
// BT node ActionTriggerSound fires one of these events; game side binds to AudioSystem.
// Values intentionally match CATHODE SoundType indices where documented.
enum class NpcSoundEvent : uint8_t {
    None            = 0,
    SuspectWarning  = 1,   // NPC notices something suspicious
    EngageEnemy     = 2,   // NPC commits to attacking
    ChargeToAttack  = 3,   // CATHODE ALIEN_CHARGE_TO_ATTACK:3
    Investigate     = 4,   // NPC inspects area
    LostContact     = 5,   // NPC loses track of target
    SearchStart     = 6,   // NPC begins a systematic search
    SearchEnd       = 7,   // NPC abandons search
    Alert           = 8,   // NPC reaches full alert
    StartSearching  = 9,   // CATHODE ALIEN_STARTS_SEARCHING:9
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
