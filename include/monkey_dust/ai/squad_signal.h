#pragma once
#include <cstdint>

// ── MD_z: SquadSignalBus ─────────────────────────────────────────────────
// NPC squad pub/sub channel — maps MD ActionNotifySquad / ConditionSquadSignal.
// MAX_SQUADS=8 channels; each channel stores the latest signal + sender + timestamp.
// ActionSquadNotify BT node writes to the entity's squad channel.
// ConditionSquadSignal BT node reads and checks the entity's squad channel.
//
// Usage:
//   - Attach SquadMemberComponent {squad_id} to NPC entities.
//   - BT: ActionSquadNotify {signal:"Warning"} writes to SquadSignalBus::channel[squad_id].
//   - BT: ConditionSquadSignal {signal:"Warning"} reads the same channel.
//   - Call SquadSignalBus::Get().ClearAll() between test cases.
//
// Invariants:
//   - squad_id >= MAX_SQUADS → noop / Failure (silently clamped)
//   - SquadSignal::None = 0 is the default/empty state (no pending signal)
//   - sender stored as raw uint32_t (entity id) — no entt dependency in header

// SquadActivity is defined in squad_controller.h (29 states, RE-verified).
// squad_signal.h stays dependency-free — activity stored as uint8_t.
// Cast: static_cast<SquadActivity>(channel.activity) at callsites with squad_controller.h.

enum class SquadSignal : uint8_t {
    None            = 0,
    Warning         = 1,
    EnteredCover    = 2,
    StartRetreating = 3,
    SuspiciousWarn  = 4,
    HearingBSAlien  = 5,
    Escalating      = 6,
    CombatStart     = 7,
};
static constexpr uint8_t SQUAD_SIGNAL_COUNT = 8;


struct SquadMemberComponent {
    uint8_t squad_id;  // 0 – MAX_SQUADS-1; channels NPCs into a shared broadcast group
    uint8_t _pad[7];
};
static_assert(sizeof(SquadMemberComponent) == 8, "SquadMemberComponent must be 8 bytes");

struct SquadChannel {
    SquadSignal signal    = SquadSignal::None;
    uint8_t     activity  = 0;     // SquadActivity (squad_controller.h) cast to uint8_t
    uint8_t     _pad1[2];
    uint32_t    sender    = 0;     // raw entity id of last notifier
    uint32_t    timestamp = 0;     // nowMs when signal was written
    uint8_t     _pad2[4];
};
static_assert(sizeof(SquadChannel) == 16, "SquadChannel must be 16 bytes");

class SquadSignalBus {
public:
    static constexpr uint8_t MAX_SQUADS = 8;

    static SquadSignalBus& Get() noexcept {
        static SquadSignalBus inst;
        return inst;
    }

    void Set(uint8_t squad_id, SquadSignal sig, uint32_t sender, uint32_t now_ms) noexcept {
        if (squad_id >= MAX_SQUADS) return;
        channels_[squad_id].signal    = sig;
        channels_[squad_id].sender    = sender;
        channels_[squad_id].timestamp = now_ms;
    }

    // activity: pass static_cast<uint8_t>(SquadActivity::...) from squad_controller.h
    void SetActivity(uint8_t squad_id, uint8_t act) noexcept {
        if (squad_id >= MAX_SQUADS) return;
        channels_[squad_id].activity = act;
    }

    SquadSignal GetSignal  (uint8_t squad_id) const noexcept {
        if (squad_id >= MAX_SQUADS) return SquadSignal::None;
        return channels_[squad_id].signal;
    }

    uint8_t GetActivity(uint8_t squad_id) const noexcept {
        if (squad_id >= MAX_SQUADS) return 0;
        return channels_[squad_id].activity;
    }

    const SquadChannel& GetChannel(uint8_t squad_id) const noexcept {
        static constexpr SquadChannel empty{};
        if (squad_id >= MAX_SQUADS) return empty;
        return channels_[squad_id];
    }

    void ClearChannel(uint8_t squad_id) noexcept {
        if (squad_id < MAX_SQUADS) channels_[squad_id] = {};
    }

    void ClearAll() noexcept {
        for (auto& c : channels_) c = {};
    }

private:
    SquadChannel channels_[MAX_SQUADS] = {};
};
