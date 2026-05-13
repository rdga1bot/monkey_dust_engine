#pragma once
#include <monkey_dust/ai/fnv.h>
#include <cstdint>

// ── Pattern 1: MotivationType ─────────────────────────────────────────────────
// CATHODE MOTIVATION_TYPE analog — determines which BT branch is active.
// DirectorSystem/FlowGraph write this; BT MotivationCheck nodes read it.
enum class MotivationType : uint8_t {
    None           =  0,
    Idle           =  1,  // default wander/patrol
    Stalk          =  2,  // STALK_MOTIVATION
    Attack         =  3,  // ATTACK_MOTIVATION
    ThreatAware    =  4,  // THREAT_AWARE_MOTIVATION
    Search         =  5,  // SEARCH_SYSTEMATIC_MOTIVATION
    Flee           =  6,  // retreat from threat
    Script         =  7,  // CINEMATIC_MOTIVATION
    Despawn        =  8,  // DESPAWN_MOTIVATION
    Suspicious     =  9,  // SUSPECT_TARGET_RESPONSE_MOTIVATION
    BackstageStalk = 10,  // BACKSTAGE_STALK_MOTIVATION
    Ambush         = 11,  // AMBUSH_MOTIVATION
    Breakout       = 12,  // BREAKOUT_MOTIVATION
    PlayerHiding   = 13,  // PLAYER_HIDE_MOTIVATION
    COUNT
};

// ── Pattern 5: AgentTimerSlot ─────────────────────────────────────────────────
// CATHODE LOGIC_CHARACTER_TIMER_TYPE analog — 26 named per-agent timer slots.
// BT TimerStart/TimerCheck nodes index by slot enum cast to uint8_t.
enum class AgentTimerSlot : uint8_t {
    SuspectTargetResponse =  0,
    HeightenedSenses      =  1,  // 25 s — raised alertness after combat
    ThreatAwareTimeout    =  2,
    ThreatAwareDuration   =  3,
    SearchTimeout         =  4,
    BackstageStalkTimeout =  5,
    AmbushTimeout         =  6,
    AttackBan             =  7,  // cooldown between attacks
    MeleeAttackBan        =  8,
    VentBan               =  9,
    NpcStayInCoverShoot   = 10,
    NpcJustLeftCombat     = 11,
    AttackKeepChasing     = 12,
    DelayReturnToSpawn    = 13,
    TargetInCrawlspace    = 14,
    DurationSinceSearch   = 15,
    BackstagePickKilltrap = 16,
    FlankedVentAttack     = 17,
    ThreatAwareVisual     = 18,
    ResponseToBackstage   = 19,
    VentAttract           = 20,
    SeenPlayerAimWeapon   = 21,
    SearchBan             = 22,
    ObserveTarget         = 23,
    RepeatedPathfindFail  = 24,
    Generic               = 25,
    COUNT
};
static constexpr int MAX_AGENT_TIMERS = static_cast<int>(AgentTimerSlot::COUNT);  // 26

// ── Pattern 4: LogicCharacterFlags ───────────────────────────────────────────
// CATHODE LOGIC_CHARACTER_FLAGS analog — 40-bit flat bitmask per agent.
// Index by lcf::* constants (uint8_t bit position 0-63).
// Replaces the old uint32_t flags field. O(1) test/set/clear, trivially serializable.
namespace lcf {
    static constexpr uint8_t DONE_BREAKOUT          =  0;
    static constexpr uint8_t SHOULD_RESET           =  1;
    static constexpr uint8_t DO_ASSAULT_CHECKS      =  2;
    static constexpr uint8_t IS_IN_VENT             =  3;
    static constexpr uint8_t BANNED_FROM_VENT       =  4;
    static constexpr uint8_t IS_SITTING             =  5;
    static constexpr uint8_t DONE_ESCALATION        =  6;
    static constexpr uint8_t HAS_DONE_GRAPPLE_BREAK =  7;
    static constexpr uint8_t HAS_RECEIVED_DOT       =  8;
    static constexpr uint8_t SHOULD_BREAKOUT        =  9;
    static constexpr uint8_t SHOULD_ATTACK          = 10;
    static constexpr uint8_t SHOULD_HIT_AND_RUN     = 11;
    static constexpr uint8_t DONE_HIT_AND_RUN       = 12;
    static constexpr uint8_t PLAYER_HIDING          = 13;
    static constexpr uint8_t ATTACK_HIDING_PLAYER   = 14;
    static constexpr uint8_t ALIEN_KNOWS_VENT       = 15;
    static constexpr uint8_t IS_CORPSE_TRAP         = 16;
    static constexpr uint8_t SHOULD_DESPAWN         = 17;
    static constexpr uint8_t ATTACK_IN_THRESHOLD    = 18;
    static constexpr uint8_t LOCK_BACKSTAGE_STALK   = 19;
    static constexpr uint8_t TOTALLY_BLIND          = 20;
    static constexpr uint8_t PLAYER_WON_QTE         = 21;
    static constexpr uint8_t NPC_IS_INERT           = 22;
    static constexpr uint8_t NPC_IS_DUMMY           = 23;
    static constexpr uint8_t SHOULD_AMBUSH          = 24;
    static constexpr uint8_t NEVER_AGGRESSIVE       = 25;
    static constexpr uint8_t MUTE_DIALOGUE          = 26;
    static constexpr uint8_t DOING_THREAT_ANIM      = 27;
    static constexpr uint8_t DONE_THREAT_AWARE      = 28;
    static constexpr uint8_t BLOCK_AMBUSH           = 29;
    static constexpr uint8_t PREVENT_GRAPPLES       = 30;
    static constexpr uint8_t PREVENT_ALL_ATTACKS    = 31;
    static constexpr uint8_t ALLOW_FLANK_VENT       = 32;
    static constexpr uint8_t IGNORE_VENT_BEHAV      = 33;
    static constexpr uint8_t AIMED_STANCE           = 34;
    static constexpr uint8_t AIMED_LOW_STANCE       = 35;
    static constexpr uint8_t CLOSE_TO_BACKSTAGE     = 36;
    static constexpr uint8_t IS_IN_EXPLOITABLE_AREA = 37;
    static constexpr uint8_t IS_ON_LADDER           = 38;
    static constexpr uint8_t HAS_PATH_FAIL          = 39;
}

struct LogicCharacterFlags {
    uint64_t bits = 0;
    bool test  (uint8_t idx) const noexcept { return (bits >> idx) & 1ull; }
    void set   (uint8_t idx)       noexcept { bits |=  (1ull << idx); }
    void clear (uint8_t idx)       noexcept { bits &= ~(1ull << idx); }
    void assign(uint8_t idx, bool v) noexcept { v ? set(idx) : clear(idx); }
};

// ── Pattern 6: AgentGauges ────────────────────────────────────────────────────
// CATHODE RETREAT_GAUGE / STUN_DAMAGE_GAUGE analog — float [0..1] per agent.
// GaugeCheck BT node fires when val >= threshold; GaugeSet resets to 0.
enum class GaugeType : uint8_t { Retreat = 0, StunDamage = 1, COUNT };
static constexpr int MAX_AGENT_GAUGES = static_cast<int>(GaugeType::COUNT);

struct AgentGauges {
    float val[MAX_AGENT_GAUGES] = {};
    float get(GaugeType t) const noexcept { return val[static_cast<int>(t)]; }
    void  set(GaugeType t, float v) noexcept { val[static_cast<int>(t)] = v; }
    void  add(GaugeType t, float d) noexcept { val[static_cast<int>(t)] += d; }
};

// ── Pattern 8: EntityStateFlag ────────────────────────────────────────────────
// CATHODE EntityState bitmask analog — 23 lifecycle flags per entity.
// entity_state field in AgentState stores the current OR of active flags.
enum class EntityStateFlag : uint32_t {
    None         = 0x000000,
    Activate     = 0x000001,
    Spawn        = 0x000002,
    Start        = 0x000004,
    Pause        = 0x000008,
    Attach       = 0x000010,
    Enable       = 0x000080,
    Simulate     = 0x000100,
    Lock         = 0x000200,
    Show         = 0x000400,
    Suspend      = 0x000800,
    ProxyEnable  = 0x001000,
    Floating     = 0x002000,
    LightOn      = 0x004000,
    InstallProxy = 0x008000,
    Suspended    = 0x080000,
    Ghosted      = 0x100000,
    Invisible    = 0x200000,
    Frozen       = 0x400000,
};

inline EntityStateFlag operator|(EntityStateFlag a, EntityStateFlag b) noexcept {
    return static_cast<EntityStateFlag>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool esf_test(uint32_t state, EntityStateFlag f) noexcept {
    return (state & static_cast<uint32_t>(f)) != 0u;
}

// ── C15: AwarenessState ───────────────────────────────────────────────────────
// CATHODE DecoratorAwarenessState analog — cognitive state (knows where threat is?).
// BT AwarenessCheck node gates branches by exact state equality.
// DirectorSystem writes; BT reads.
enum class AwarenessState : uint8_t {
    Dead          = 0,  // DEAD:0
    Unaware       = 1,  // default patrol
    Suspicious    = 2,  // heard/saw something indirect
    Investigating = 3,  // moving to stimulus
    SearchingArea = 4,  // SEARCHING_AREA:4 — area sweep without lock-on
    Alert         = 5,  // ALERT — on-edge, weapon raised
    Aware         = 6,  // AWARE:6 — full combat lock-on
};

// ── C16: AlertnessState ───────────────────────────────────────────────────────
// CATHODE ConditionHasAlertnessState analog — behavioural alertness level.
// Orthogonal to AwarenessState: Android can be ALERT but not Aware.
// DirectorSystem/damage handler writes; BT reads.
enum class AlertnessState : uint8_t {
    Relaxed = 0,  // normal patrol / idle
    Alert   = 1,  // ALERT:1 — just received stimulus, reacting
    Combat  = 2,  // actively engaging
    Fleeing = 3,  // retreating / panicked
};

// ── C17: NpcMood ──────────────────────────────────────────────────────────────
// CATHODE DecoratorMood / MoodSet analog — affective state driving animation blending.
// DirectorSystem / FlowGraph writes; BT MoodCheck gates branches; AnimSystem reads.
enum class NpcMood : uint8_t {
    Neutral    = 0,
    Curious    = 1,
    Cautious   = 2,
    Suspicious = 3,  // SUSPICIOUS:3 — search-mode animations
    Panicked   = 4,  // PANICKED:4 — panic run / flinch
    Calm       = 5,  // post-deescalation
};

// ── C19: WithdrawState ────────────────────────────────────────────────────────
// CATHODE ActionSetWithdrawState / ConditionWithdrawState analog — 3-stage retreat FSM.
// Prevents attack continuation after cinematic/script hand-off.
// Invariant: actionMeleeAttack must call SetWithdraw(NotWithdrawing) before attacking.
enum class WithdrawState : uint8_t {
    NotWithdrawing  = 0,  // NOT_WITHDRAWING:0
    NeedsToWithdraw = 1,  // NEEDS_TO_WITHDRAW:1 — intent set, not yet moving
    Withdrawing     = 2,  // WITHDRAWING:2 — actively retreating
};

// ── AgentBlackboard entry ─────────────────────────────────────────────────────
// CATHODE EntityInterface analog: typed parameter slot keyed by FNV-1a hash.
// type: 0=bool  1=int  2=float  3=vec3  4=enum
struct BlackboardEntry {
    uint32_t key;   // md::fnv1a("field_name") — compile-time or runtime
    uint8_t  type;
    uint8_t  _pad[3];
    union {
        bool     b;
        int32_t  i;
        float    f;
        float    v[3];
        uint32_t e;
    } val;
};
static_assert(sizeof(BlackboardEntry) == 20, "BlackboardEntry must be 20 bytes");

// ── AgentState ────────────────────────────────────────────────────────────────
// M18 component. Pairs with BTComponent on every AI entity.
//
// timers:        absolute deadline in game-ms; 0 = inactive. Indexed by AgentTimerSlot.
// lcflags:       Pattern 4 — 40-bit LogicCharacterFlags bitmask (replaces old uint32 flags).
// frame_flags:   C13 — single-tick signal bits between BT branches; cleared each logic tick.
// motivation:    Pattern 1 — current CATHODE-style motivation; BT MotivationCheck reads this.
// entity_state:  Pattern 8 — EntityStateFlag lifecycle bitmask.
// gauges:        Pattern 6 — retreat/stun float gauges [0..1].
// awareness:     C15 — cognitive state (Unaware→Aware); DirectorSystem writes.
// alertness:     C16 — behavioural alertness (Relaxed→Fleeing); damage handler writes.
// mood:          C17 — affective state for anim blending; DirectorSystem/FlowGraph writes.
// withdraw_state:C19 — 3-stage retreat FSM; clears to NotWithdrawing before melee attack.
// bb:            CATHODE-style blackboard; MAX_BB_ENTRIES=24.
static constexpr int MAX_BB_ENTRIES = 24;

struct AgentState {
    uint64_t            timers[MAX_AGENT_TIMERS];  // ms deadlines; 0 = inactive
    LogicCharacterFlags lcflags;                    // Pattern 4: 40-bit flag bitmask
    uint64_t            frame_flags;               // C13: cleared each logic tick
    uint32_t            entity_state;               // Pattern 8: EntityStateFlag OR
    AgentGauges         gauges;                     // Pattern 6: retreat/stun gauges
    MotivationType      motivation;                 // Pattern 1: active motivation
    AwarenessState      awareness;                  // C15: cognitive threat awareness
    AlertnessState      alertness;                  // C16: behavioural alertness level
    NpcMood             mood;                       // C17: affective state / anim layer
    WithdrawState       withdraw_state;             // C19: 3-stage retreat FSM
    int                 bb_count;
    BlackboardEntry     bb[MAX_BB_ENTRIES];
};

// ── Blackboard helpers ────────────────────────────────────────────────────────

inline BlackboardEntry* bb_find(AgentState& s, uint32_t key) noexcept {
    for (int i = 0; i < s.bb_count; ++i)
        if (s.bb[i].key == key) return &s.bb[i];
    return nullptr;
}

inline const BlackboardEntry* bb_find(const AgentState& s, uint32_t key) noexcept {
    for (int i = 0; i < s.bb_count; ++i)
        if (s.bb[i].key == key) return &s.bb[i];
    return nullptr;
}

inline BlackboardEntry* bb_insert(AgentState& s, uint32_t key, uint8_t type) noexcept {
    BlackboardEntry* e = bb_find(s, key);
    if (e) return e;
    if (s.bb_count >= MAX_BB_ENTRIES) return nullptr;
    e = &s.bb[s.bb_count++];
    e->key  = key;
    e->type = type;
    return e;
}

inline void bb_set_float(AgentState& s, uint32_t key, float val) noexcept {
    if (BlackboardEntry* e = bb_insert(s, key, 2)) { e->val.f = val; }
}

inline void bb_set_bool(AgentState& s, uint32_t key, bool val) noexcept {
    if (BlackboardEntry* e = bb_insert(s, key, 0)) { e->val.b = val; }
}

inline void bb_set_int(AgentState& s, uint32_t key, int32_t val) noexcept {
    if (BlackboardEntry* e = bb_insert(s, key, 1)) { e->val.i = val; }
}

inline float bb_get_float(const AgentState& s, uint32_t key, float def = 0.f) noexcept {
    for (int i = 0; i < s.bb_count; ++i)
        if (s.bb[i].key == key) return s.bb[i].val.f;
    return def;
}

inline bool bb_get_bool(const AgentState& s, uint32_t key, bool def = false) noexcept {
    for (int i = 0; i < s.bb_count; ++i)
        if (s.bb[i].key == key) return s.bb[i].val.b;
    return def;
}
