#pragma once
#include <cstdint>

// ── AllianceGroup ─────────────────────────────────────────────────────────────
// CATHODE ALLIANCE_GROUP — faction membership categories.
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
// Singleton 9×9 stance table. Default: Hostile between (Player,Alien),
// (Player,Android), (Alien,Seegson), (Alien,WeylandYutani); else Neutral.
class AllianceMatrix {
public:
    static AllianceMatrix& Get() noexcept {
        static AllianceMatrix inst;
        return inst;
    }

    AllianceStance GetStance(AllianceGroup a, AllianceGroup b) const noexcept {
        return table_[idx(a)][idx(b)];
    }

    void SetStance(AllianceGroup a, AllianceGroup b, AllianceStance s) noexcept {
        table_[idx(a)][idx(b)] = s;
        table_[idx(b)][idx(a)] = s;
    }

    bool IsEnemy(AllianceGroup a, AllianceGroup b) const noexcept {
        return GetStance(a, b) == AllianceStance::Hostile;
    }

private:
    static constexpr uint8_t N = 9;

    static uint8_t idx(AllianceGroup g) noexcept { return static_cast<uint8_t>(g); }

    AllianceMatrix() noexcept {
        for (uint8_t i = 0; i < N; ++i)
            for (uint8_t j = 0; j < N; ++j)
                table_[i][j] = AllianceStance::Neutral;

        auto hostile = [&](AllianceGroup a, AllianceGroup b) {
            table_[idx(a)][idx(b)] = AllianceStance::Hostile;
            table_[idx(b)][idx(a)] = AllianceStance::Hostile;
        };
        hostile(AllianceGroup::Player, AllianceGroup::Alien);
        hostile(AllianceGroup::Player, AllianceGroup::Android);
        hostile(AllianceGroup::Alien,  AllianceGroup::Seegson);
        hostile(AllianceGroup::Alien,  AllianceGroup::WeylandYutani);
        hostile(AllianceGroup::Hostile, AllianceGroup::Player);
        hostile(AllianceGroup::Hostile, AllianceGroup::Friendly);

        auto friendly = [&](AllianceGroup a, AllianceGroup b) {
            table_[idx(a)][idx(b)] = AllianceStance::Friendly;
            table_[idx(b)][idx(a)] = AllianceStance::Friendly;
        };
        friendly(AllianceGroup::Player,  AllianceGroup::Friendly);
        friendly(AllianceGroup::Seegson, AllianceGroup::WeylandYutani);
    }

    AllianceStance table_[N][N];
};
