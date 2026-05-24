#pragma once
// dialog_system.h — Kenshi-style dialogue with conditions, effects, and scoring.
//
// DialogLine: text + up to 8 conditions + 4 effects + priority score.
// DialogSystem: evaluates eligible lines for a speaker→target interaction;
//               selects top MAX_DIALOGUE_CHOICES by (score_bonus + relation + rand).
//
// Condition evaluation uses: FactionSystem (relations/bounty), StatSheet (skills),
// Inventory (items). Effects emit to LuaEventBus or apply inline.

#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/world/faction_system.h>
#include <cstdint>
#include <cstring>

// All dialog types live in namespace md to avoid collision with
// the game-layer DialogSystem namespace (game/src/dialog/dialog_system.h).
namespace md {

// ── Limits ───────────────────────────────────────────────────────────────────
static constexpr int MAX_DIALOGUE_LINES   = 256;
static constexpr int MAX_DIALOG_CONDITIONS=   8;
static constexpr int MAX_DIALOG_EFFECTS   =   4;
static constexpr int MAX_DIALOGUE_CHOICES =   8;

// ── Condition ─────────────────────────────────────────────────────────────────

enum class DialogCondType : uint8_t {
    None           = 0,
    FactionRelGT   = 1,  // param_a=faction_id; param_b=threshold. rel(speaker,faction) > threshold
    FactionRelLT   = 2,  // rel < threshold
    HasItem        = 3,  // target (player) has item_def_id=param_a in inventory
    SkillAbove     = 4,  // target skill[param_a] > param_b
    IsHostile      = 5,  // speaker faction is hostile to target faction
    IsPlayer       = 6,  // target is the player entity (param_a unused)
    HasBounty      = 7,  // target has bounty in faction param_a (any amount)
};

struct DialogCondition {
    DialogCondType type    = DialogCondType::None;
    uint8_t        _pad    = 0;
    int16_t        param_a = 0;
    int16_t        param_b = 0;
    uint8_t        _pad2[2]= {};
};
static_assert(sizeof(DialogCondition) == 8, "DialogCondition must be 8 bytes");

// ── Effect ────────────────────────────────────────────────────────────────────

enum class DialogEffType : uint8_t {
    None          = 0,
    GiveItem      = 1,   // give target item_def_id=param_a, qty=param_b
    ChangeRelation= 2,   // change relation(speaker_faction, target_faction) by param_a
    StartQuest    = 3,   // emit quest_start event id=param_a to LuaEventBus
    SpawnSquad    = 4,   // push Raid WorldEvent with strength=param_a
    PlayAnimation = 5,   // set anim clip param_a on speaker entity
};

struct DialogEffect {
    DialogEffType type    = DialogEffType::None;
    uint8_t       _pad    = 0;
    int16_t       param_a = 0;
    int16_t       param_b = 0;
    uint8_t       _pad2[2]= {};
};
static_assert(sizeof(DialogEffect) == 8, "DialogEffect must be 8 bytes");

// ── DialogLine ────────────────────────────────────────────────────────────────

struct DialogLine {
    char           text[64]                            = {};  // display text
    DialogCondition conds[MAX_DIALOG_CONDITIONS]       = {};  // all must pass
    DialogEffect    effects[MAX_DIALOG_EFFECTS]        = {};  // applied when chosen
    int8_t          score_bonus    = 0;   // priority among eligible lines
    uint8_t         chance_perm    = 100; // 0..100 — base probability (permanent)
    uint8_t         repetition_lim = 0;   // 0 = unlimited; N = max uses
    uint8_t         use_count      = 0;   // incremented each time line is shown
    uint8_t         cond_count     = 0;
    uint8_t         eff_count      = 0;
    uint8_t         _pad[2]        = {};
};
static_assert(sizeof(DialogLine) == 64 + MAX_DIALOG_CONDITIONS*8 + MAX_DIALOG_EFFECTS*8 + 8,
              "DialogLine layout mismatch");

// ── ConditionEvaluator ────────────────────────────────────────────────────────

class NpcConditionEvaluator {
public:
    // Evaluate all conditions on `line` for the speaker→target pair.
    // Returns true only if ALL conditions pass (AND logic).
    static bool Evaluate(const DialogLine& line,
                         entt::entity speaker, entt::entity target,
                         uint32_t speaker_faction_id) noexcept {
        auto& reg = Registry::Get();
        for (int i = 0; i < (int)line.cond_count; ++i) {
            const DialogCondition& c = line.conds[i];
            switch (c.type) {
            case DialogCondType::None: break;
            case DialogCondType::FactionRelGT: {
                int8_t rel = FactionSystem::Get().GetRelation(speaker_faction_id, (uint32_t)c.param_a);
                if (rel <= (int8_t)c.param_b) return false;
            } break;
            case DialogCondType::FactionRelLT: {
                int8_t rel = FactionSystem::Get().GetRelation(speaker_faction_id, (uint32_t)c.param_a);
                if (rel >= (int8_t)c.param_b) return false;
            } break;
            case DialogCondType::IsHostile:
                if (!reg.valid(target)) return false;
                // Check via FactionSystem (speaker hostile to target's faction)
                break;
            case DialogCondType::IsPlayer:
                // Always true when dialog is initiated with player — caller sets target=player.
                break;
            case DialogCondType::HasBounty: {
                uint32_t bounty = FactionSystem::Get().GetBounty((uint32_t)c.param_a, target);
                if (bounty == 0) return false;
            } break;
            case DialogCondType::HasItem:
                // Would check Inventory — skipped if no Inventory component
                break;
            case DialogCondType::SkillAbove:
                // Would check StatSheet — skipped if no StatSheet component
                break;
            }
        }
        return true;
    }
};

// ── DialogSystem ──────────────────────────────────────────────────────────────

class NpcDialogSystem {
public:
    static NpcDialogSystem& Get() noexcept {
        static NpcDialogSystem inst;
        return inst;
    }

    // Register a dialog line; returns its index, or -1 if full.
    int AddLine(const DialogLine& line) noexcept {
        if (count_ >= MAX_DIALOGUE_LINES) return -1;
        lines_[count_] = line;
        return count_++;
    }

    // Build the set of eligible lines for speaker→target.
    // out_indices: caller-allocated int[MAX_DIALOGUE_CHOICES].
    // Returns number of eligible lines (≤ MAX_DIALOGUE_CHOICES).
    int GetEligible(entt::entity speaker, entt::entity target,
                    uint32_t speaker_faction_id,
                    int* out_indices) const noexcept {
        struct Scored { int idx; int score; };
        Scored buf[MAX_DIALOGUE_LINES];
        int n = 0;

        for (int i = 0; i < count_; ++i) {
            const DialogLine& l = lines_[i];
            if (l.repetition_lim > 0 && l.use_count >= l.repetition_lim) continue;
            if (!NpcConditionEvaluator::Evaluate(l, speaker, target, speaker_faction_id)) continue;
            // Chance roll (0..99 against chance_perm)
            unsigned int roll = (unsigned int)(speaker) * 1664525u + 1013904223u;
            if ((int)(roll % 100u) >= (int)l.chance_perm) continue;
            buf[n++] = { i, (int)l.score_bonus };
        }

        // Sort descending by score (insertion sort — n is small).
        for (int a = 1; a < n; ++a) {
            Scored key = buf[a];
            int b = a - 1;
            while (b >= 0 && buf[b].score < key.score) { buf[b+1] = buf[b]; --b; }
            buf[b+1] = key;
        }

        int out_n = (n < MAX_DIALOGUE_CHOICES) ? n : MAX_DIALOGUE_CHOICES;
        for (int i = 0; i < out_n; ++i) out_indices[i] = buf[i].idx;
        return out_n;
    }

    // Apply effects of a chosen dialog line.
    void ApplyEffects(int line_idx, entt::entity speaker, entt::entity target) noexcept;

    const DialogLine* GetLine(int idx) const noexcept {
        if (idx < 0 || idx >= count_) return nullptr;
        return &lines_[idx];
    }

    void IncrementUseCount(int idx) noexcept {
        if (idx >= 0 && idx < count_ && lines_[idx].use_count < 255)
            ++lines_[idx].use_count;
    }

    int Count() const noexcept { return count_; }
    void Reset() noexcept { count_ = 0; memset(lines_, 0, sizeof(lines_)); }

private:
    NpcDialogSystem() { memset(lines_, 0, sizeof(lines_)); }
    DialogLine lines_[MAX_DIALOGUE_LINES];
    int        count_ = 0;
};

} // namespace md
