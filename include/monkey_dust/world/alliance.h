#pragma once
#include <cstdint>
#include <flecs.h>

// ── AllianceGroup ─────────────────────────────────────────────────────────────
// MD ALLIANCE_GROUP — faction membership categories.
enum class AllianceGroup : uint8_t {
    Unaligned    = 0,
    Player       = 1,
    Alien        = 2,
    Android      = 3,
    Seegson      = 4,
    WeylandYutani= 5,
    Hostile      = 6,
    Neutral      = 7,
    Friendly     = 8,
};

// ── AllianceStance ────────────────────────────────────────────────────────────
enum class AllianceStance : uint8_t {
    Friendly = 0,
    Neutral  = 1,
    Hostile  = 2,
};

// ── AllianceMatrix ────────────────────────────────────────────────────────────
// Singleton stance table over the 9 AllianceGroup values. Default: Hostile
// between (Player,Alien), (Player,Android), (Alien,Seegson),
// (Alien,WeylandYutani); else Neutral.
//
// Task #8 B4 pilot: backed by flecs relation pairs (HostileWith/FriendlyWith)
// on 9 private per-group entities instead of a 9×9 array — absence of either
// pair means Neutral (the default), so only non-Neutral stances cost storage.
// Public API (GetStance/SetStance/IsEnemy) is unchanged; every caller in the
// codebase only ever goes through it, never touched internals directly, so
// this is a pure implementation swap (verified via grep before migrating).
// Uses its OWN private flecs::world, not MdRegistry's global one — alliance
// groups are a fixed enum-indexed lookup table, not gameplay entities, and
// keeping them isolated avoids any interaction with MdRegistry::Clear().
class AllianceMatrix {
public:
    static AllianceMatrix& Get() noexcept {
        static AllianceMatrix inst;
        return inst;
    }

    AllianceStance GetStance(AllianceGroup a, AllianceGroup b) const noexcept {
        flecs::entity ea = group_[idx(a)];
        flecs::entity eb = group_[idx(b)];
        if (ea.has<HostileWith>(eb))  return AllianceStance::Hostile;
        if (ea.has<FriendlyWith>(eb)) return AllianceStance::Friendly;
        return AllianceStance::Neutral;
    }

    void SetStance(AllianceGroup a, AllianceGroup b, AllianceStance s) noexcept {
        flecs::entity ea = group_[idx(a)];
        flecs::entity eb = group_[idx(b)];
        ea.remove<HostileWith>(eb);  eb.remove<HostileWith>(ea);
        ea.remove<FriendlyWith>(eb); eb.remove<FriendlyWith>(ea);
        if (s == AllianceStance::Hostile)  { ea.add<HostileWith>(eb);  eb.add<HostileWith>(ea); }
        if (s == AllianceStance::Friendly) { ea.add<FriendlyWith>(eb); eb.add<FriendlyWith>(ea); }
    }

    bool IsEnemy(AllianceGroup a, AllianceGroup b) const noexcept {
        return GetStance(a, b) == AllianceStance::Hostile;
    }

private:
    static constexpr uint8_t N = 9;

    // Relation tags — zero-size, never instantiated as component data.
    struct HostileWith  {};
    struct FriendlyWith {};

    static uint8_t idx(AllianceGroup g) noexcept { return static_cast<uint8_t>(g); }

    AllianceMatrix() noexcept {
        for (uint8_t i = 0; i < N; ++i) group_[i] = world_.entity();

        auto hostile = [&](AllianceGroup a, AllianceGroup b) { SetStance(a, b, AllianceStance::Hostile); };
        hostile(AllianceGroup::Player, AllianceGroup::Alien);
        hostile(AllianceGroup::Player, AllianceGroup::Android);
        hostile(AllianceGroup::Alien,  AllianceGroup::Seegson);
        hostile(AllianceGroup::Alien,  AllianceGroup::WeylandYutani);
        hostile(AllianceGroup::Hostile, AllianceGroup::Player);
        hostile(AllianceGroup::Hostile, AllianceGroup::Friendly);

        auto friendly = [&](AllianceGroup a, AllianceGroup b) { SetStance(a, b, AllianceStance::Friendly); };
        friendly(AllianceGroup::Player,  AllianceGroup::Friendly);
        friendly(AllianceGroup::Seegson, AllianceGroup::WeylandYutani);
    }

    flecs::world  world_;
    flecs::entity group_[N];
};
