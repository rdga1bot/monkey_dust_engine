#include <monkey_dust/ai/bt_json_loader.h>
#include <monkey_dust/ai/bt_action_registry.h>
#include <monkey_dust/ai/fnv.h>
#include <monkey_dust/ai/squad_signal.h>
#include <monkey_dust/ai/named_branch.h>
#include <monkey_dust/ai/npc_sound.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/npc_memory.h>
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/ai/suspicious_item_group.h>
#include <monkey_dust/platform/md_log.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstring>

// Maximum JSON nesting depth to prevent stack overflow on hostile/malformed input.
static constexpr int MAX_JSON_DEPTH = 32;

// ── String helpers ────────────────────────────────────────────────────────────
// All helpers operate on [start, end) slices — no null termination required.

// p points to '{' — returns pointer to matching '}', or nullptr if not found
// before end or if end is reached without closure.
static const char* find_object_end(const char* p, const char* end) {
    int  depth  = 0;
    bool in_str = false;
    while (p < end && *p) {
        if (!in_str) {
            if      (*p == '{') ++depth;
            else if (*p == '}') { if (--depth == 0) return p; }
            else if (*p == '"') in_str = true;
        } else {
            if (*p == '\\' && (p + 1) < end) ++p;  // skip escaped char
            else if (*p == '"') in_str = false;
        }
        ++p;
    }
    return nullptr;
}

static void read_str_r(const char* s, const char* e, const char* key,
                       char* out, int max) {
    if (max <= 0) return;
    out[0] = '\0';
    size_t klen = strlen(key);
    const char* p = s;
    while (p + klen <= e) {
        if (memcmp(p, key, klen) == 0) {
            const char* v = p + klen;
            while (v < e && (*v == ':' || *v == ' ' || *v == '\t'
                             || *v == '\n' || *v == '\r')) ++v;
            if (v < e && *v == '"') {
                ++v;
                int i = 0;
                while (v < e && *v != '"' && i < max - 1) out[i++] = *v++;
                out[i] = '\0';
                return;
            }
        }
        ++p;
    }
}

static int read_int_r(const char* s, const char* e, const char* key, int def = 0) {
    size_t klen = strlen(key);
    const char* p = s;
    while (p + klen <= e) {
        if (memcmp(p, key, klen) == 0) {
            const char* v = p + klen;
            while (v < e && (*v == ':' || *v == ' ' || *v == '\t')) ++v;
            if (v < e && (*v == '-' || (*v >= '0' && *v <= '9')))
                return atoi(v);
        }
        ++p;
    }
    return def;
}

static float read_float_r(const char* s, const char* e, const char* key, float def = 0.f) {
    size_t klen = strlen(key);
    const char* p = s;
    while (p + klen <= e) {
        if (memcmp(p, key, klen) == 0) {
            const char* v = p + klen;
            while (v < e && (*v == ':' || *v == ' ' || *v == '\t')) ++v;
            if (v < e && (*v == '-' || *v == '.' || (*v >= '0' && *v <= '9')))
                return static_cast<float>(atof(v));
        }
        ++p;
    }
    return def;
}

static bool read_bool_r(const char* s, const char* e, const char* key, bool def = false) {
    size_t klen = strlen(key);
    const char* p = s;
    while (p + klen <= e) {
        if (memcmp(p, key, klen) == 0) {
            const char* v = p + klen;
            while (v < e && (*v == ':' || *v == ' ' || *v == '\t')) ++v;
            if (v + 4 <= e && memcmp(v, "true",  4) == 0) return true;
            if (v + 5 <= e && memcmp(v, "false", 5) == 0) return false;
            return def;
        }
        ++p;
    }
    return def;
}

// Parse "weights": [w0, w1, w2, w3] into out[4]
static void read_weights_r(const char* s, const char* e, uint8_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 25; // default equal split
    const char* kw   = "\"weights\"";
    size_t      klen = strlen(kw);
    const char* p    = s;
    while (p + klen <= e) {
        if (memcmp(p, kw, klen) == 0) {
            const char* arr = p + klen;
            while (arr < e && *arr != '[') ++arr;
            if (arr >= e) return;
            ++arr;
            int idx = 0;
            while (arr < e && *arr != ']' && idx < 4) {
                while (arr < e && (*arr == ' ' || *arr == ',' || *arr == '\t'
                                   || *arr == '\n' || *arr == '\r')) ++arr;
                if (arr < e && *arr >= '0' && *arr <= '9') {
                    out[idx++] = static_cast<uint8_t>(atoi(arr));
                    while (arr < e && *arr >= '0' && *arr <= '9') ++arr;
                } else if (arr < e && *arr != ']') ++arr;
            }
            return;
        }
        ++p;
    }
}

// ── Enum lookup tables ────────────────────────────────────────────────────────

static MotivationType parse_motivation(const char* s) {
    static const struct { const char* n; MotivationType v; } T[] = {
        {"None",           MotivationType::None},
        {"Idle",           MotivationType::Idle},
        {"Stalk",          MotivationType::Stalk},
        {"Attack",         MotivationType::Attack},
        {"ThreatAware",    MotivationType::ThreatAware},
        {"Search",         MotivationType::Search},
        {"Flee",           MotivationType::Flee},
        {"Script",         MotivationType::Script},
        {"Despawn",        MotivationType::Despawn},
        {"Suspicious",     MotivationType::Suspicious},
        {"BackstageStalk", MotivationType::BackstageStalk},
        {"Ambush",         MotivationType::Ambush},
        {"Breakout",       MotivationType::Breakout},
        {"PlayerHiding",   MotivationType::PlayerHiding},
    };
    for (auto& e : T) if (strcmp(s, e.n) == 0) return e.v;
    return MotivationType::None;
}

static uint8_t parse_timer_slot(const char* s) {
    static const struct { const char* n; AgentTimerSlot v; } T[] = {
        {"SuspectTargetResponse", AgentTimerSlot::SuspectTargetResponse},
        {"HeightenedSenses",      AgentTimerSlot::HeightenedSenses},
        {"ThreatAwareTimeout",    AgentTimerSlot::ThreatAwareTimeout},
        {"ThreatAwareDuration",   AgentTimerSlot::ThreatAwareDuration},
        {"SearchTimeout",         AgentTimerSlot::SearchTimeout},
        {"BackstageStalkTimeout", AgentTimerSlot::BackstageStalkTimeout},
        {"AmbushTimeout",         AgentTimerSlot::AmbushTimeout},
        {"AttackBan",             AgentTimerSlot::AttackBan},
        {"MeleeAttackBan",        AgentTimerSlot::MeleeAttackBan},
        {"VentBan",               AgentTimerSlot::VentBan},
        {"NpcStayInCoverShoot",   AgentTimerSlot::NpcStayInCoverShoot},
        {"NpcJustLeftCombat",     AgentTimerSlot::NpcJustLeftCombat},
        {"AttackKeepChasing",     AgentTimerSlot::AttackKeepChasing},
        {"DelayReturnToSpawn",    AgentTimerSlot::DelayReturnToSpawn},
        {"TargetInCrawlspace",    AgentTimerSlot::TargetInCrawlspace},
        {"DurationSinceSearch",   AgentTimerSlot::DurationSinceSearch},
        {"BackstagePickKilltrap", AgentTimerSlot::BackstagePickKilltrap},
        {"FlankedVentAttack",     AgentTimerSlot::FlankedVentAttack},
        {"ThreatAwareVisual",     AgentTimerSlot::ThreatAwareVisual},
        {"ResponseToBackstage",   AgentTimerSlot::ResponseToBackstage},
        {"VentAttract",           AgentTimerSlot::VentAttract},
        {"SeenPlayerAimWeapon",   AgentTimerSlot::SeenPlayerAimWeapon},
        {"SearchBan",             AgentTimerSlot::SearchBan},
        {"ObserveTarget",         AgentTimerSlot::ObserveTarget},
        {"RepeatedPathfindFail",  AgentTimerSlot::RepeatedPathfindFail},
        {"Generic",               AgentTimerSlot::Generic},
    };
    for (auto& e : T) if (strcmp(s, e.n) == 0) return static_cast<uint8_t>(e.v);
    MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] unknown timer_slot: '%s'", s);
    return static_cast<uint8_t>(AgentTimerSlot::Generic);
}

static BranchType parse_branch_type(const char* s) {
    static const struct { const char* n; BranchType v; } T[] = {
        {"Standard",              BranchType::Standard},
        {"Cinematic",             BranchType::Cinematic},
        {"Attack",                BranchType::Attack},
        {"Despawn",               BranchType::Despawn},
        {"AreaSweep",             BranchType::AreaSweep},
        {"BackstageAreaSweep",    BranchType::BackstageAreaSweep},
        {"Shot",                  BranchType::Shot},
        {"SuspectTargetResponse", BranchType::SuspectTargetResponse},
        {"ThreatAware",           BranchType::ThreatAware},
        {"AttackCore",            BranchType::AttackCore},
        {"Breakout",              BranchType::Breakout},
        {"StunDamage",            BranchType::StunDamage},
        {"Suspend",               BranchType::Suspend},
        {"Dead",                  BranchType::Dead},
        {"Script",                BranchType::Script},
        {"Idle",                  BranchType::Idle},
        {"Killtrap",              BranchType::Killtrap},
        {"Panic",                 BranchType::Panic},
    };
    for (auto& e : T) if (strcmp(s, e.n) == 0) return e.v;
    return BranchType::Standard;
}

static ShutdownSpeed parse_shutdown_speed(const char* s) {
    if (strcmp(s, "Critical") == 0) return ShutdownSpeed::Critical;
    if (strcmp(s, "Normal")   == 0) return ShutdownSpeed::Normal;
    return ShutdownSpeed::Graceful;
}

static uint8_t parse_flag(const char* s) {
    // lcf:: bit indices — listed in agent_state.h
    static const struct { const char* n; uint8_t v; } T[] = {
        {"DONE_BREAKOUT",          lcf::DONE_BREAKOUT},
        {"SHOULD_RESET",           lcf::SHOULD_RESET},
        {"DO_ASSAULT_CHECKS",      lcf::DO_ASSAULT_CHECKS},
        {"IS_IN_VENT",             lcf::IS_IN_VENT},
        {"BANNED_FROM_VENT",       lcf::BANNED_FROM_VENT},
        {"IS_SITTING",             lcf::IS_SITTING},
        {"DONE_ESCALATION",        lcf::DONE_ESCALATION},
        {"HAS_DONE_GRAPPLE_BREAK", lcf::HAS_DONE_GRAPPLE_BREAK},
        {"HAS_RECEIVED_DOT",       lcf::HAS_RECEIVED_DOT},
        {"SHOULD_BREAKOUT",        lcf::SHOULD_BREAKOUT},
        {"SHOULD_ATTACK",          lcf::SHOULD_ATTACK},
        {"SHOULD_HIT_AND_RUN",     lcf::SHOULD_HIT_AND_RUN},
        {"DONE_HIT_AND_RUN",       lcf::DONE_HIT_AND_RUN},
        {"PLAYER_HIDING",          lcf::PLAYER_HIDING},
        {"ATTACK_HIDING_PLAYER",   lcf::ATTACK_HIDING_PLAYER},
        {"NPC_KNOWS_VENT",         lcf::NPC_KNOWS_VENT},
        {"IS_CORPSE_TRAP",         lcf::IS_CORPSE_TRAP},
        {"SHOULD_DESPAWN",         lcf::SHOULD_DESPAWN},
        {"ATTACK_IN_THRESHOLD",    lcf::ATTACK_IN_THRESHOLD},
        {"LOCK_BACKSTAGE_STALK",   lcf::LOCK_BACKSTAGE_STALK},
        {"TOTALLY_BLIND",          lcf::TOTALLY_BLIND},
        {"PLAYER_WON_QTE",         lcf::PLAYER_WON_QTE},
        {"NPC_IS_INERT",           lcf::NPC_IS_INERT},
        {"NPC_IS_DUMMY",           lcf::NPC_IS_DUMMY},
        {"SHOULD_AMBUSH",          lcf::SHOULD_AMBUSH},
        {"NEVER_AGGRESSIVE",       lcf::NEVER_AGGRESSIVE},
        {"MUTE_DIALOGUE",          lcf::MUTE_DIALOGUE},
        {"DOING_THREAT_ANIM",      lcf::DOING_THREAT_ANIM},
        {"DONE_THREAT_AWARE",      lcf::DONE_THREAT_AWARE},
        {"BLOCK_AMBUSH",           lcf::BLOCK_AMBUSH},
        {"PREVENT_GRAPPLES",       lcf::PREVENT_GRAPPLES},
        {"PREVENT_ALL_ATTACKS",    lcf::PREVENT_ALL_ATTACKS},
        {"ALLOW_FLANK_VENT",       lcf::ALLOW_FLANK_VENT},
        {"IGNORE_VENT_BEHAV",      lcf::IGNORE_VENT_BEHAV},
        {"AIMED_STANCE",           lcf::AIMED_STANCE},
        {"AIMED_LOW_STANCE",       lcf::AIMED_LOW_STANCE},
        {"CLOSE_TO_BACKSTAGE",     lcf::CLOSE_TO_BACKSTAGE},
        {"IS_IN_EXPLOITABLE_AREA", lcf::IS_IN_EXPLOITABLE_AREA},
        {"IS_ON_LADDER",           lcf::IS_ON_LADDER},
        {"HAS_PATH_FAIL",          lcf::HAS_PATH_FAIL},
    };
    for (auto& e : T) if (strcmp(s, e.n) == 0) return e.v;
    MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] unknown flag: '%s'", s);
    return 0;
}

static GaugeType parse_gauge(const char* s) {
    if (strcmp(s, "StunDamage") == 0) return GaugeType::StunDamage;
    return GaugeType::Retreat;
}

static AwarenessState parse_awareness(const char* s) {
    if (strcmp(s, "Dead")          == 0) return AwarenessState::Dead;
    if (strcmp(s, "Unaware")       == 0) return AwarenessState::Unaware;
    if (strcmp(s, "Suspicious")    == 0) return AwarenessState::Suspicious;
    if (strcmp(s, "Investigating") == 0) return AwarenessState::Investigating;
    if (strcmp(s, "SearchingArea") == 0) return AwarenessState::SearchingArea;
    if (strcmp(s, "Alert")         == 0) return AwarenessState::Alert;
    return AwarenessState::Aware;
}

static AlertnessState parse_alertness(const char* s) {
    if (strcmp(s, "Relaxed") == 0) return AlertnessState::Relaxed;
    if (strcmp(s, "Alert")   == 0) return AlertnessState::Alert;
    if (strcmp(s, "Combat")  == 0) return AlertnessState::Combat;
    return AlertnessState::Fleeing;
}

static NpcMood parse_mood(const char* s) {
    if (strcmp(s, "Neutral")    == 0) return NpcMood::Neutral;
    if (strcmp(s, "Curious")    == 0) return NpcMood::Curious;
    if (strcmp(s, "Cautious")   == 0) return NpcMood::Cautious;
    if (strcmp(s, "Suspicious") == 0) return NpcMood::Suspicious;
    if (strcmp(s, "Panicked")   == 0) return NpcMood::Panicked;
    return NpcMood::Calm;
}

static NpcRole parse_role(const char* s) {
    if (strcmp(s, "Stalk")         == 0) return NpcRole::Stalk;
    if (strcmp(s, "SuspectMoveTo") == 0) return NpcRole::SuspectMoveTo;
    if (strcmp(s, "Flanker")       == 0) return NpcRole::Flanker;
    if (strcmp(s, "SearchLead")    == 0) return NpcRole::SearchLead;
    if (strcmp(s, "AmbushWait")    == 0) return NpcRole::AmbushWait;
    if (strcmp(s, "Backstage")     == 0) return NpcRole::Backstage;
    if (strcmp(s, "CorpseTrap")    == 0) return NpcRole::CorpseTrap;
    return NpcRole::None;
}

static WithdrawState parse_withdraw(const char* s) {
    if (strcmp(s, "NeedsToWithdraw") == 0) return WithdrawState::NeedsToWithdraw;
    if (strcmp(s, "Withdrawing")     == 0) return WithdrawState::Withdrawing;
    return WithdrawState::NotWithdrawing;
}

static AreaSweepType parse_area_sweep_type(const char* s) {
    if (strcmp(s, "FixedRadiusAroundPosition") == 0) return AreaSweepType::FixedRadiusAroundPosition;
    if (strcmp(s, "AroundTarget")              == 0) return AreaSweepType::AroundTarget;
    return AreaSweepType::InAndOutBetweenTargetAndPosition;
}

static NpcAggroLevel parse_aggro_level(const char* s) {
    if (strcmp(s, "StandDown")     == 0) return NpcAggroLevel::StandDown;
    if (strcmp(s, "Interrogative") == 0) return NpcAggroLevel::Interrogative;
    if (strcmp(s, "Warning")       == 0) return NpcAggroLevel::Warning;
    if (strcmp(s, "LastChance")    == 0) return NpcAggroLevel::LastChance;
    if (strcmp(s, "NoLimit")       == 0) return NpcAggroLevel::NoLimit;
    return NpcAggroLevel::None;
}

static NpcCombatState parse_npc_combat_state(const char* s) {
    if (strcmp(s, "Warning")                 == 0) return NpcCombatState::Warning;
    if (strcmp(s, "Attacking")               == 0) return NpcCombatState::Attacking;
    if (strcmp(s, "ReachedObjective")        == 0) return NpcCombatState::ReachedObjective;
    if (strcmp(s, "EnteredCover")            == 0) return NpcCombatState::EnteredCover;
    if (strcmp(s, "LeaveCover")              == 0) return NpcCombatState::LeaveCover;
    if (strcmp(s, "StartRetreating")         == 0) return NpcCombatState::StartRetreating;
    if (strcmp(s, "ReachedRetreat")          == 0) return NpcCombatState::ReachedRetreat;
    if (strcmp(s, "LostSense")               == 0) return NpcCombatState::LostSense;
    if (strcmp(s, "SuspiciousWarning")       == 0) return NpcCombatState::SuspiciousWarning;
    if (strcmp(s, "SuspiciousWarningFailed") == 0) return NpcCombatState::SuspiciousWarningFailed;
    if (strcmp(s, "StartAdvance")            == 0) return NpcCombatState::StartAdvance;
    if (strcmp(s, "DoneAdvance")             == 0) return NpcCombatState::DoneAdvance;
    if (strcmp(s, "Blocking")                == 0) return NpcCombatState::Blocking;
    if (strcmp(s, "HeardBackstageAlien")     == 0) return NpcCombatState::HeardBackstageAlien;
    if (strcmp(s, "AlienSighted")            == 0) return NpcCombatState::AlienSighted;
    return NpcCombatState::None;
}

// ── MD_z enum parsers ────────────────────────────────────────────────────

static SquadSignal parse_squad_signal(const char* s) {
    if (strcmp(s, "Warning")         == 0) return SquadSignal::Warning;
    if (strcmp(s, "EnteredCover")    == 0) return SquadSignal::EnteredCover;
    if (strcmp(s, "StartRetreating") == 0) return SquadSignal::StartRetreating;
    if (strcmp(s, "SuspiciousWarn")  == 0) return SquadSignal::SuspiciousWarn;
    if (strcmp(s, "HearingBSAlien")  == 0) return SquadSignal::HearingBSAlien;
    if (strcmp(s, "Escalating")      == 0) return SquadSignal::Escalating;
    if (strcmp(s, "CombatStart")     == 0) return SquadSignal::CombatStart;
    return SquadSignal::None;
}

static LocomotionState parse_locomotion_state(const char* s) {
    if (strcmp(s, "Running")    == 0) return LocomotionState::Running;
    if (strcmp(s, "Crouching")  == 0) return LocomotionState::Crouching;
    if (strcmp(s, "InVent")     == 0) return LocomotionState::InVent;
    if (strcmp(s, "Aiming")     == 0) return LocomotionState::Aiming;
    if (strcmp(s, "Traversing") == 0) return LocomotionState::Traversing;
    if (strcmp(s, "Idling")     == 0) return LocomotionState::Idling;
    return LocomotionState::Walking;
}

// ── MD_grok enum parsers ─────────────────────────────────────────────────

static NpcSoundEvent parse_npc_sound_event(const char* s) {
    if (strcmp(s, "SuspectWarning")  == 0) return NpcSoundEvent::SuspectWarning;
    if (strcmp(s, "EngageEnemy")     == 0) return NpcSoundEvent::EngageEnemy;
    if (strcmp(s, "ChargeToAttack")  == 0) return NpcSoundEvent::ChargeToAttack;
    if (strcmp(s, "Investigate")     == 0) return NpcSoundEvent::Investigate;
    if (strcmp(s, "LostContact")     == 0) return NpcSoundEvent::LostContact;
    if (strcmp(s, "SearchStart")     == 0) return NpcSoundEvent::SearchStart;
    if (strcmp(s, "SearchEnd")       == 0) return NpcSoundEvent::SearchEnd;
    if (strcmp(s, "Alert")           == 0) return NpcSoundEvent::Alert;
    if (strcmp(s, "StartSearching")  == 0) return NpcSoundEvent::StartSearching;
    return NpcSoundEvent::None;
}

static SenseThresholdQualifier parse_sense_qualifier(const char* s) {
    if (strcmp(s, "Lower")     == 0) return SenseThresholdQualifier::Lower;
    if (strcmp(s, "Activated") == 0) return SenseThresholdQualifier::Activated;
    if (strcmp(s, "Upper")     == 0) return SenseThresholdQualifier::Upper;
    return SenseThresholdQualifier::Trace;
}

static uint8_t parse_sense_type(const char* s) {
    if (strcmp(s, "Visual")     == 0) return 0u;
    if (strcmp(s, "Audio")      == 0) return 1u;
    if (strcmp(s, "Smell")      == 0) return 2u;
    if (strcmp(s, "Vibration")  == 0) return 3u;
    if (strcmp(s, "Touch")      == 0) return 4u;
    if (strcmp(s, "Peripheral") == 0) return 5u;
    if (strcmp(s, "Motion")     == 0) return 6u;
    if (strcmp(s, "Anxiety")    == 0) return 7u;
    if (strcmp(s, "Background") == 0) return 8u;
    return 0u;
}
// Returns 0-8, or def when field absent. Accepts integer or SenseType name string.
static uint8_t read_sense_idx_r(const char* obj, const char* obj_end, uint8_t def = 0u) {
    int v = read_int_r(obj, obj_end, "\"sense\"", -1);
    if (v >= 0) return (v < MAX_SENSES) ? static_cast<uint8_t>(v) : 0u;
    char buf[16] = "";
    read_str_r(obj, obj_end, "\"sense\"", buf, sizeof(buf));
    return buf[0] ? parse_sense_type(buf) : def;
}
// Returns -1 when field absent, 0-8 when present. Used where absence means "any sense".
static int read_sense_optional_r(const char* obj, const char* obj_end) {
    int v = read_int_r(obj, obj_end, "\"sense\"", -1);
    if (v >= 0) return (v < MAX_SENSES) ? v : 0;
    char buf[16] = "";
    read_str_r(obj, obj_end, "\"sense\"", buf, sizeof(buf));
    return buf[0] ? static_cast<int>(parse_sense_type(buf)) : -1;
}

static SuspiciousItemStage parse_suspicious_item_stage(const char* s) {
    if (strcmp(s, "FirstSensed")                == 0) return SuspiciousItemStage::FirstSensed;
    if (strcmp(s, "InitialReaction")            == 0) return SuspiciousItemStage::InitialReaction;
    if (strcmp(s, "WaitForTeamMembersRouting")  == 0) return SuspiciousItemStage::WaitForTeamMembersRouting;
    if (strcmp(s, "MoveCloseTo")               == 0) return SuspiciousItemStage::MoveCloseTo;
    if (strcmp(s, "CloseToReaction")           == 0) return SuspiciousItemStage::CloseToReaction;
    if (strcmp(s, "CloseToWaitForGroupMembers") == 0) return SuspiciousItemStage::CloseToWaitForGroupMembers;
    if (strcmp(s, "SearchArea")                == 0) return SuspiciousItemStage::SearchArea;
    return SuspiciousItemStage::None;
}

static EventType parse_event_type(const char* s) {
    if (strcmp(s, "SensedTarget")          == 0) return EventType::SensedTarget;
    if (strcmp(s, "SensedSuspiciousItem")  == 0) return EventType::SensedSuspiciousItem;
    if (strcmp(s, "TargetHiding")          == 0) return EventType::TargetHiding;
    if (strcmp(s, "SuspectTargetResponse") == 0) return EventType::SuspectTargetResponse;
    // Integer fallback
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);
        return (v >= 0 && v < MAX_EVENT_TYPES) ? static_cast<EventType>(v) : EventType::SensedTarget;
    }
    return EventType::SensedTarget;
}

static EventType read_event_type_r(const char* obj, const char* obj_end, const char* key) {
    char buf[32] = "0";
    read_str_r(obj, obj_end, key, buf, sizeof(buf));
    if (!buf[0]) { int v = read_int_r(obj, obj_end, key, 0); return static_cast<EventType>(v & 0x3u); }
    return parse_event_type(buf);
}

// ── MD XML alias helpers ─────────────────────────────────────────────────

// Extracts the integer after the last ':' in "NAME:N" format (MD enum serialization).
// Returns -1 if no colon found.
static int parse_colon_index(const char* s) {
    if (!s || !*s) return -1;
    const char* c = strrchr(s, ':');
    if (!c) return -1;
    return atoi(c + 1);
}

// Maps MD MotivationType XML names → monkey_dust MotivationType enum.
// MD indices (ATTACK_MOTIVATION:1) do NOT match our values (Attack=3);
// name-based matching is required.
static MotivationType parse_md_motivation(const char* s) {
    if (strncmp(s, "BACKSTAGE_STALK_MOTIVATION", 26) == 0) return MotivationType::BackstageStalk;
    if (strncmp(s, "ATTACK_MOTIVATION",          17) == 0) return MotivationType::Attack;
    if (strncmp(s, "STALK_MOTIVATION",           16) == 0) return MotivationType::Stalk;
    if (strncmp(s, "THREAT_AWARE_MOTIVATION",    23) == 0) return MotivationType::ThreatAware;
    if (strncmp(s, "SEARCH",                      6) == 0) return MotivationType::Search;
    if (strncmp(s, "FLEE_MOTIVATION",            15) == 0) return MotivationType::Flee;
    if (strncmp(s, "SCRIPT_MOTIVATION",          17) == 0) return MotivationType::Script;
    if (strncmp(s, "DESPAWN_MOTIVATION",         18) == 0) return MotivationType::Despawn;
    if (strncmp(s, "SUSPICIOUS_MOTIVATION",      21) == 0) return MotivationType::Suspicious;
    if (strncmp(s, "AMBUSH_MOTIVATION",          17) == 0) return MotivationType::Ambush;
    if (strncmp(s, "BREAKOUT_MOTIVATION",        19) == 0) return MotivationType::Breakout;
    if (strncmp(s, "PLAYER_HIDING_MOTIVATION",   24) == 0) return MotivationType::PlayerHiding;
    if (strncmp(s, "IDLE_MOTIVATION",            15) == 0) return MotivationType::Idle;
    return MotivationType::None;
}

// Maps MD GaugeAmountType (name or ":N" index) → float threshold.
// GAUGE_NONE:0=0.0, GAUGE_TRACE:1=0.01, GAUGE_LOWER:2=0.25, GAUGE_ACTIVATED:3=0.5, GAUGE_UPPER:4=0.9
static float parse_gauge_amount(const char* s) {
    int idx = parse_colon_index(s);
    if (idx >= 0) {
        switch (idx) {
            case 1: return 0.01f;
            case 2: return 0.25f;
            case 3: return 0.5f;
            case 4: return 0.9f;
            default: return 0.0f;
        }
    }
    if (strncmp(s, "GAUGE_UPPER",     11) == 0) return 0.9f;
    if (strncmp(s, "GAUGE_ACTIVATED", 15) == 0) return 0.5f;
    if (strncmp(s, "GAUGE_LOWER",     11) == 0) return 0.25f;
    if (strncmp(s, "GAUGE_TRACE",     11) == 0) return 0.01f;
    return 0.0f;
}

// ── Fallback functions for unregistered actions/conditions ────────────────────
static bool         s_cond_false  (md::EngineContext&, entt::entity) { return false; }
static BTStatus     s_act_failure (md::EngineContext&, entt::entity) { return BTStatus::Failure; }

// ── Recursive node parser ─────────────────────────────────────────────────────
// obj:     points AFTER '{' of the node object
// obj_end: points TO   '}' of the node object
// Returns the allocated node index, or BehaviorTree::INVALID on error.
static uint16_t parse_node(BehaviorTree& bt, const char* obj, const char* obj_end, int depth = 0) {
    if (depth > MAX_JSON_DEPTH) {
        MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] max nesting depth %d exceeded — aborting subtree", MAX_JSON_DEPTH);
        return BehaviorTree::INVALID;
    }
    char type_str[64] = "";
    read_str_r(obj, obj_end, "\"type\"", type_str, sizeof(type_str));
    if (!type_str[0]) {
        MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] node missing 'type' field");
        return BehaviorTree::INVALID;
    }

    auto& reg = md::BTActionRegistry::Get();
    uint16_t idx = BehaviorTree::INVALID;

    // ── Composites ────────────────────────────────────────────────────────────
    if (strcmp(type_str, "SelectorLinear") == 0) {
        idx = bt.addSelector();
    } else if (strcmp(type_str, "SequenceLinear") == 0) {
        idx = bt.addSequence();
    } else if (strcmp(type_str, "SequenceStateless") == 0) {  // C11
        idx = bt.addSequenceStateless();
    } else if (strcmp(type_str, "WeightedSelector") == 0) {   // C14
        uint8_t w[4];
        read_weights_r(obj, obj_end, w);
        idx = bt.addWeightedSelector(w);
    } else if (strcmp(type_str, "Inverter") == 0) {
        idx = bt.addInverter();
    } else if (strcmp(type_str, "Repeat") == 0) {
        int count = read_int_r(obj, obj_end, "\"count\"", 0);
        idx = bt.addRepeat(static_cast<uint32_t>(count));
    } else if (strcmp(type_str, "Wait") == 0) {
        int dur = read_int_r(obj, obj_end, "\"duration_ms\"", 1000);
        idx = bt.addWait(static_cast<uint32_t>(dur));

    // ── DecoratorBranch ─────────────────────────────
    } else if (strcmp(type_str, "DecoratorBranch") == 0) {
        char cond_str[32] = "", branch_str[32] = "Standard", speed_str[16] = "Graceful";
        read_str_r(obj, obj_end, "\"condition\"",    cond_str,   sizeof(cond_str));
        read_str_r(obj, obj_end, "\"branch_type\"",  branch_str, sizeof(branch_str));
        read_str_r(obj, obj_end, "\"shutdown_speed\"",speed_str, sizeof(speed_str));
        BTConditionFunc cond = cond_str[0] ? reg.FindCondition(cond_str) : nullptr;
        if (cond_str[0] && !cond) {
            MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] unregistered condition: '%s' — always false", cond_str);
            cond = s_cond_false;
        }
        idx = bt.addBranch(cond, parse_branch_type(branch_str), parse_shutdown_speed(speed_str));

    // ── ConditionNode (plain predicate leaf) ──────────────────────────────────
    } else if (strcmp(type_str, "ConditionNode") == 0) {
        char cond_str[32] = "";
        read_str_r(obj, obj_end, "\"condition\"", cond_str, sizeof(cond_str));
        BTConditionFunc cond = cond_str[0] ? reg.FindCondition(cond_str) : nullptr;
        if (!cond) {
            if (cond_str[0]) MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] unregistered condition: '%s'", cond_str);
            cond = s_cond_false;
        }
        idx = bt.addCondition(cond);

    // ── ActionNode ────────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "ActionNode") == 0) {
        char act_str[32] = "";
        read_str_r(obj, obj_end, "\"action\"", act_str, sizeof(act_str));
        BTActionFunc act = act_str[0] ? reg.FindAction(act_str) : nullptr;
        if (!act) {
            if (act_str[0]) MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] unregistered action: '%s'", act_str);
            act = s_act_failure;
        }
        idx = bt.addAction(act);

    // ── ReferencedBehavior ───────────────────────
    } else if (strcmp(type_str, "ReferencedBehavior") == 0) {
        // Deferred resolution: store FNV hash in data; BTLoader::ResolveRefs()
        // patches the pointer after all templates are loaded. Until then → Failure.
        char tree_name[32] = "";
        read_str_r(obj, obj_end, "\"tree\"", tree_name, sizeof(tree_name));
        uint32_t h = tree_name[0] ? md::fnv1a_rt(tree_name) : 0u;
        if (tree_name[0])
            MD_LOG(MD_LOG_INFO, "[BTJsonLoader] ReferencedBehavior '%s' (hash=0x%08x) deferred", tree_name, h);
        idx = bt.addReference(nullptr, h);

    // ── Timer nodes ──────────────────────────────────
    } else if (strcmp(type_str, "TimerStart") == 0) {
        char slot_str[32] = "";
        read_str_r(obj, obj_end, "\"timer_slot\"", slot_str, sizeof(slot_str));
        uint8_t slot    = parse_timer_slot(slot_str);
        int     dur     = read_int_r(obj, obj_end, "\"duration_ms\"", 1000);
        bool    only_inc= read_bool_r(obj, obj_end, "\"only_increase\"", false);
        idx = only_inc ? bt.addTimerStartOnlyIncrease(slot, static_cast<uint32_t>(dur))  // C12
                       : bt.addTimerStart(slot, static_cast<uint32_t>(dur));
    } else if (strcmp(type_str, "TimerCheck") == 0) {
        char slot_str[32] = "";
        read_str_r(obj, obj_end, "\"timer_slot\"", slot_str, sizeof(slot_str));
        idx = bt.addTimerCheck(parse_timer_slot(slot_str));

    // ── Motivation ───────────
    } else if (strcmp(type_str, "MotivationCheck") == 0) {
        char m[32] = "";
        read_str_r(obj, obj_end, "\"motivation\"", m, sizeof(m));
        idx = bt.addMotivationCheck(parse_motivation(m));
    } else if (strcmp(type_str, "SetMotivation") == 0) {
        char m[32] = "";
        read_str_r(obj, obj_end, "\"motivation\"", m, sizeof(m));
        idx = bt.addSetMotivation(parse_motivation(m));

    // ── LogicCharacterFlags ────────────
    } else if (strcmp(type_str, "FlagCheck") == 0) {
        char f[32] = "";
        read_str_r(obj, obj_end, "\"flag\"", f, sizeof(f));
        bool cs = read_bool_r(obj, obj_end, "\"check_set\"", true);
        idx = bt.addFlagCheck(parse_flag(f), cs);
    } else if (strcmp(type_str, "FlagSet") == 0) {
        char f[32] = "";
        read_str_r(obj, obj_end, "\"flag\"", f, sizeof(f));
        bool ds = read_bool_r(obj, obj_end, "\"set\"", true);
        idx = bt.addFlagSet(parse_flag(f), ds);

    // ── FrameFlag (C13) ───────────────────────────────────────────────────────
    } else if (strcmp(type_str, "FrameFlagCheck") == 0) {
        char f[32] = "";
        read_str_r(obj, obj_end, "\"flag\"", f, sizeof(f));
        bool cs = read_bool_r(obj, obj_end, "\"check_set\"", true);
        idx = bt.addFrameFlagCheck(parse_flag(f), cs);
    } else if (strcmp(type_str, "FrameFlagSet") == 0) {
        char f[32] = "";
        read_str_r(obj, obj_end, "\"flag\"", f, sizeof(f));
        bool ds = read_bool_r(obj, obj_end, "\"set\"", true);
        idx = bt.addFrameFlagSet(parse_flag(f), ds);

    // ── SenseCheck ────────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "SenseCheck") == 0) {
        uint8_t sense = read_sense_idx_r(obj, obj_end);
        float   thr   = read_float_r(obj, obj_end, "\"threshold\"", 0.5f);
        idx = bt.addSenseCheck(sense, thr);

    // ── Gauges (C6) ───────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "GaugeCheck") == 0) {
        char g[16] = "";
        read_str_r(obj, obj_end, "\"gauge\"", g, sizeof(g));
        float thr = read_float_r(obj, obj_end, "\"threshold\"", 0.5f);
        idx = bt.addGaugeCheck(parse_gauge(g), thr);
    } else if (strcmp(type_str, "GaugeSet") == 0) {
        char g[16] = "";
        read_str_r(obj, obj_end, "\"gauge\"", g, sizeof(g));
        float val = read_float_r(obj, obj_end, "\"value\"", 0.f);
        idx = bt.addGaugeSet(parse_gauge(g), val);

    // ── AwarenessState (C15) ──────────────────────────────────────────────────
    } else if (strcmp(type_str, "AwarenessCheck") == 0) {
        char s[16] = "";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addAwarenessCheck(parse_awareness(s));

    // ── AlertnessState (C16) ──────────────────────────────────────────────────
    } else if (strcmp(type_str, "AlertnessCheck") == 0) {
        char s[16] = "";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addAlertnessCheck(parse_alertness(s));

    // ── NpcMood (C17) ─────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "MoodCheck") == 0) {
        char m[16] = "";
        read_str_r(obj, obj_end, "\"mood\"", m, sizeof(m));
        idx = bt.addMoodCheck(parse_mood(m));

    // ── RoleSystem (C18) ──────────────────────────────────────────────────────
    } else if (strcmp(type_str, "RoleCheck") == 0) {
        char r[16] = "", mode[16] = "performing";
        read_str_r(obj, obj_end, "\"role\"", r,    sizeof(r));
        read_str_r(obj, obj_end, "\"mode\"", mode, sizeof(mode));
        bool could = strcmp(mode, "could_perform") == 0;
        idx = bt.addRoleCheck(parse_role(r), could);
    } else if (strcmp(type_str, "RoleClaim") == 0) {
        char r[16] = "";
        read_str_r(obj, obj_end, "\"role\"", r, sizeof(r));
        int qid = read_int_r(obj, obj_end, "\"query_id\"", 0);
        idx = bt.addRoleClaim(parse_role(r), static_cast<uint32_t>(qid));
    } else if (strcmp(type_str, "RoleRelease") == 0) {
        char r[16] = "";
        read_str_r(obj, obj_end, "\"role\"", r, sizeof(r));
        idx = bt.addRoleRelease(parse_role(r));

    // ── WithdrawState (C19) ───────────────────────────────────────────────────
    } else if (strcmp(type_str, "WithdrawCheck") == 0) {
        char s[16] = "";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addWithdrawCheck(parse_withdraw(s));
    } else if (strcmp(type_str, "SetWithdraw") == 0) {
        char s[16] = "";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addSetWithdraw(parse_withdraw(s));

    } else if (strcmp(type_str, "MemoryCheck") == 0) {
        char mode_str[16] = "spatial";
        read_str_r(obj, obj_end, "\"mode\"", mode_str, sizeof(mode_str));
        uint8_t mode = (strcmp(mode_str, "event") == 0) ? 1u : 0u;
        idx = bt.addMemoryCheck(mode);

    } else if (strcmp(type_str, "MemoryForget") == 0) {
        idx = bt.addMemoryForget();

    } else if (strcmp(type_str, "AreaSweepCheck") == 0) {
        char s[64] = "InAndOutBetweenTargetAndPosition";
        read_str_r(obj, obj_end, "\"sweep_type\"", s, sizeof(s));
        idx = bt.addAreaSweepCheck(parse_area_sweep_type(s));

    } else if (strcmp(type_str, "DecoratorPercentage") == 0) {
        int pct = read_int_r(obj, obj_end, "\"percentage\"", 50);
        if (pct < 0) pct = 0; if (pct > 100) pct = 100;
        idx = bt.addDecoratorPercentage(static_cast<uint8_t>(pct));

    } else if (strcmp(type_str, "SelectorPercentage") == 0) {
        idx = bt.addSelectorPercentage();

    } else if (strcmp(type_str, "SenseTimeCheck") == 0) {
        uint8_t sense_idx      = read_sense_idx_r(obj, obj_end);
        int     max_elapsed_ms = read_int_r(obj, obj_end, "\"max_elapsed_ms\"", 3000);
        if (max_elapsed_ms < 0) max_elapsed_ms = 0;
        idx = bt.addSenseTimeCheck(sense_idx, static_cast<uint32_t>(max_elapsed_ms));

    } else if (strcmp(type_str, "ActionSetDead") == 0) {
        idx = bt.addActionSetDead();

    } else if (strcmp(type_str, "ActionDespawn") == 0) {
        idx = bt.addActionDespawn();

    // ── MD_deepseek ──────────────────────────────────────────────────────
    } else if (strcmp(type_str, "AggroLevelCheck") == 0) {
        char l[32] = "None";
        read_str_r(obj, obj_end, "\"level\"", l, sizeof(l));
        idx = bt.addAggroLevelCheck(parse_aggro_level(l));

    } else if (strcmp(type_str, "SetAggroLevel") == 0) {
        char l[32] = "None";
        read_str_r(obj, obj_end, "\"level\"", l, sizeof(l));
        idx = bt.addSetAggroLevel(parse_aggro_level(l));

    } else if (strcmp(type_str, "NpcCombatStateCheck") == 0) {
        char s[32] = "None";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addNpcCombatStateCheck(parse_npc_combat_state(s));

    } else if (strcmp(type_str, "SetNpcCombatState") == 0) {
        char s[32] = "None";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addSetNpcCombatState(parse_npc_combat_state(s));

    // ── MD_z nodes ───────────────────────────────────────────────────────

    // Z1: DecoratorMood — decorator: run child only when mood matches
    } else if (strcmp(type_str, "DecoratorMood") == 0) {
        char m[24] = "Neutral";
        read_str_r(obj, obj_end, "\"mood\"", m, sizeof(m));
        idx = bt.addDecoratorMood(parse_mood(m));

    // Z2: DecoratorAwareness — decorator: run child only when awareness matches
    } else if (strcmp(type_str, "DecoratorAwareness") == 0) {
        char s[24] = "Unaware";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addDecoratorAwareness(parse_awareness(s));

    // Z3: DecoratorTimerAuto — auto-start timer on every entry, propagate child
    } else if (strcmp(type_str, "DecoratorTimerAuto") == 0) {
        char slot_str[32] = "";
        read_str_r(obj, obj_end, "\"timer_slot\"", slot_str, sizeof(slot_str));
        int  dur      = read_int_r (obj, obj_end, "\"duration_ms\"",  1000);
        bool only_inc = read_bool_r(obj, obj_end, "\"only_increase\"", false);
        idx = bt.addDecoratorTimerAuto(parse_timer_slot(slot_str),
                                        static_cast<uint32_t>(dur), only_inc);

    // Z4: ActionTimerRandom — random duration [min_ms, max_ms]
    } else if (strcmp(type_str, "ActionTimerRandom") == 0) {
        char slot_str[32] = "";
        read_str_r(obj, obj_end, "\"timer_slot\"", slot_str, sizeof(slot_str));
        int min_ms = read_int_r(obj, obj_end, "\"min_ms\"", 500);
        int max_ms = read_int_r(obj, obj_end, "\"max_ms\"", 2000);
        idx = bt.addActionTimerRandom(parse_timer_slot(slot_str),
                                       static_cast<uint32_t>(min_ms),
                                       static_cast<uint32_t>(max_ms));

    // Z5: ActionSquadNotify — broadcast squad signal
    } else if (strcmp(type_str, "ActionSquadNotify") == 0) {
        char sig_str[24] = "None";
        read_str_r(obj, obj_end, "\"signal\"", sig_str, sizeof(sig_str));
        idx = bt.addActionSquadNotify(parse_squad_signal(sig_str));

    // Z6: ConditionSquadSignal — check squad channel
    } else if (strcmp(type_str, "ConditionSquadSignal") == 0) {
        char sig_str[24] = "None";
        read_str_r(obj, obj_end, "\"signal\"", sig_str, sizeof(sig_str));
        idx = bt.addConditionSquadSignal(parse_squad_signal(sig_str));

    // Z7: ConditionAnySenseWithinTime — any/specific sense within time window
    } else if (strcmp(type_str, "ConditionAnySenseWithinTime") == 0) {
        int  time_ms   = read_int_r(obj, obj_end, "\"time_ms\"", 5000);
        int  sense_opt = read_sense_optional_r(obj, obj_end);
        bool specific  = (sense_opt >= 0);
        idx = bt.addConditionAnySenseWithinTime(
                static_cast<uint32_t>(time_ms),
                specific,
                specific ? static_cast<uint8_t>(sense_opt) : 0u);

    // Z8: ActionExpireTimer — force-expire a timer slot
    } else if (strcmp(type_str, "ActionExpireTimer") == 0) {
        char slot_str[32] = "";
        read_str_r(obj, obj_end, "\"timer_slot\"", slot_str, sizeof(slot_str));
        idx = bt.addActionExpireTimer(parse_timer_slot(slot_str));

    // Z9: TargetFlagCheck — check lcflags on target from blackboard "target_entity"
    } else if (strcmp(type_str, "TargetFlagCheck") == 0) {
        char f[32] = "";
        read_str_r(obj, obj_end, "\"flag\"", f, sizeof(f));
        bool cs = read_bool_r(obj, obj_end, "\"check_set\"", true);
        idx = bt.addTargetFlagCheck(parse_flag(f), cs);

    // Z10: SetLocomotionState — write as->locomotion_state
    } else if (strcmp(type_str, "SetLocomotionState") == 0) {
        char s[24] = "Walking";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addSetLocomotionState(parse_locomotion_state(s));

    // MD_arch P4: DecoratorNamedBranch — gate subtree on NamedBranchRegistry
    } else if (strcmp(type_str, "DecoratorNamedBranch") == 0) {
        char name[48] = "";
        read_str_r(obj, obj_end, "\"branch\"", name, sizeof(name));
        bool inv = false;
        { char inv_s[8] = "false";
          read_str_r(obj, obj_end, "\"inverted\"", inv_s, sizeof(inv_s));
          inv = (inv_s[0] == 't' || inv_s[0] == '1'); }
        idx = bt.addDecoratorNamedBranch(name[0] ? md::fnv1a_rt(name) : 0u, inv);

    // G1: ActionIdleTime — wait for duration_ms
    } else if (strcmp(type_str, "ActionIdleTime") == 0) {
        int dur = read_int_r(obj, obj_end, "\"duration_ms\"", 0);
        idx = bt.addActionIdleTime(static_cast<uint32_t>(dur));

    // G2: ConditionHaveTarget — target_entity bb key holds a valid entity
    } else if (strcmp(type_str, "ConditionHaveTarget") == 0) {
        idx = bt.addConditionHaveTarget();

    // G3: ConditionHaveNextTarget — next_target_entity bb key holds a valid entity
    } else if (strcmp(type_str, "ConditionHaveNextTarget") == 0) {
        idx = bt.addConditionHaveNextTarget();

    // G4: ActionTriggerSound — fire NpcSoundBus event
    } else if (strcmp(type_str, "ActionTriggerSound") == 0) {
        char ev_s[24] = "";
        read_str_r(obj, obj_end, "\"sound\"", ev_s, sizeof(ev_s));
        idx = bt.addActionTriggerSound(parse_npc_sound_event(ev_s));

    // G5: ConditionIsSenseActivationAbove — current sense[i] vs qualifier
    } else if (strcmp(type_str, "ConditionIsSenseActivationAbove") == 0) {
        uint8_t sense_i = read_sense_idx_r(obj, obj_end);
        char    qual_s[16] = "Trace";
        read_str_r(obj, obj_end, "\"qualifier\"", qual_s, sizeof(qual_s));
        idx = bt.addConditionIsSenseActivationAbove(sense_i, parse_sense_qualifier(qual_s));

    // G6: ConditionHasAnySenseBeenAbove — any sense ever fired above qualifier
    } else if (strcmp(type_str, "ConditionHasAnySenseBeenAbove") == 0) {
        char qual_s[16] = "Activated";
        read_str_r(obj, obj_end, "\"qualifier\"", qual_s, sizeof(qual_s));
        idx = bt.addConditionHasAnySenseBeenAbove(parse_sense_qualifier(qual_s));

    // G7: ActionSuspiciousItemDoneStage — stamp investigation_stage on latest event
    } else if (strcmp(type_str, "ActionSuspiciousItemDoneStage") == 0) {
        char stage_s[32] = "None";
        read_str_r(obj, obj_end, "\"stage\"", stage_s, sizeof(stage_s));
        idx = bt.addActionSuspiciousItemDoneStage(parse_suspicious_item_stage(stage_s));

    // G8: ConditionSuspiciousItemBTPriority — any event intensity >= min_intensity
    } else if (strcmp(type_str, "ConditionSuspiciousItemBTPriority") == 0) {
        int min_i = read_int_r(obj, obj_end, "\"min_intensity\"", 0);
        idx = bt.addConditionSuspiciousItemBTPriority(static_cast<uint8_t>(min_i));

    // G9: ConditionLastTimeSquadNotified — squad channel signal is recent
    } else if (strcmp(type_str, "ConditionLastTimeSquadNotified") == 0) {
        char sig_s[24]  = "";
        read_str_r(obj, obj_end, "\"signal\"", sig_s, sizeof(sig_s));
        int time_s = read_int_r(obj, obj_end, "\"time_s\"", 0);
        idx = bt.addConditionLastTimeSquadNotified(
            parse_squad_signal(sig_s), static_cast<uint8_t>(time_s));

    // G10: DecoratorAggressionEscalation — pass through if aggro_level != None
    } else if (strcmp(type_str, "DecoratorAggressionEscalation") == 0) {
        idx = bt.addDecoratorAggressionEscalation();

    // ── Batch 1: MD XML aliases ──────────────────────────────────────────
    // These map MD LegendPlugin class names to existing BT VM node types.
    // Attribute format: "NAME:N" — the :N suffix is the authoritative enum index.

    // B1-1: ActionSetLogicCharacterFlags → FlagSet
    // XML: <ActionSetLogicCharacterFlags FlagType="SHOULD_ATTACK:10" Flag="True"/>
    } else if (strcmp(type_str, "ActionSetLogicCharacterFlags") == 0) {
        char ft[64] = "";
        read_str_r(obj, obj_end, "\"flag_type\"", ft, sizeof(ft));
        char val_s[8] = "True";
        read_str_r(obj, obj_end, "\"value\"",    val_s, sizeof(val_s));
        int bit_idx = parse_colon_index(ft);
        if (bit_idx < 0) bit_idx = static_cast<int>(parse_flag(ft));
        bool do_set = !(val_s[0] == 'F' || val_s[0] == 'f');
        idx = bt.addFlagSet(static_cast<uint8_t>(bit_idx < 0 ? 0 : bit_idx), do_set);

    // B1-2: ActionSetFrameFlag → FrameFlagSet
    // XML: <ActionSetFrameFlag FrameFlag="COULD_SEARCH:3" Flag="True"/>
    } else if (strcmp(type_str, "ActionSetFrameFlag") == 0) {
        char ff[64] = "";
        read_str_r(obj, obj_end, "\"flag\"",  ff, sizeof(ff));
        char val_s[8] = "True";
        read_str_r(obj, obj_end, "\"value\"", val_s, sizeof(val_s));
        int bit_idx = parse_colon_index(ff);
        if (bit_idx < 0) bit_idx = static_cast<int>(parse_flag(ff));
        bool do_set = !(val_s[0] == 'F' || val_s[0] == 'f');
        idx = bt.addFrameFlagSet(static_cast<uint8_t>(bit_idx < 0 ? 0 : bit_idx), do_set);

    // B1-3: ConditionIsFrameFlagSet → FrameFlagCheck (always check_set=true)
    // XML: <ConditionIsFrameFlagSet FrameFlag="COULD_SEARCH:3"/>
    } else if (strcmp(type_str, "ConditionIsFrameFlagSet") == 0) {
        char ff[64] = "";
        read_str_r(obj, obj_end, "\"flag\"", ff, sizeof(ff));
        int bit_idx = parse_colon_index(ff);
        if (bit_idx < 0) bit_idx = static_cast<int>(parse_flag(ff));
        idx = bt.addFrameFlagCheck(static_cast<uint8_t>(bit_idx < 0 ? 0 : bit_idx), true);

    // B1-4: ActionSetWithdrawState → SetWithdraw
    // XML: <ActionSetWithdrawState WithdrawState="NEEDS_TO_WITHDRAW:1"/>
    // MD: NOT_WITHDRAWING:0, NEEDS_TO_WITHDRAW:1, WITHDRAWING:2
    } else if (strcmp(type_str, "ActionSetWithdrawState") == 0) {
        char ws[64] = "";
        read_str_r(obj, obj_end, "\"state\"", ws, sizeof(ws));
        int w_idx = parse_colon_index(ws);
        WithdrawState ws_val = (w_idx == 1) ? WithdrawState::NeedsToWithdraw
                             : (w_idx == 2) ? WithdrawState::Withdrawing
                             : WithdrawState::NotWithdrawing;
        idx = bt.addSetWithdraw(ws_val);

    // B1-5: ConditionHasMotivation → MotivationCheck
    // XML: <ConditionHasMotivation MotivationType="ATTACK_MOTIVATION:1"/>
    // MD indices ≠ monkey_dust values — must use name mapping
    } else if (strcmp(type_str, "ConditionHasMotivation") == 0) {
        char m[64] = "";
        read_str_r(obj, obj_end, "\"motivation\"", m, sizeof(m));
        idx = bt.addMotivationCheck(parse_md_motivation(m));

    // B1-6: ConditionIsGaugeAmountAbove → GaugeCheck
    // XML: <ConditionIsGaugeAmountAbove GaugeType="RETREAT_GAUGE:0" GaugeAmount="GAUGE_ACTIVATED:3"/>
    } else if (strcmp(type_str, "ConditionIsGaugeAmountAbove") == 0) {
        char g[64] = "", amt[64] = "";
        read_str_r(obj, obj_end, "\"gauge\"",  g,   sizeof(g));
        read_str_r(obj, obj_end, "\"amount\"", amt, sizeof(amt));
        int g_idx = parse_colon_index(g);
        GaugeType gt = (g_idx == 1) ? GaugeType::StunDamage : GaugeType::Retreat;
        idx = bt.addGaugeCheck(gt, parse_gauge_amount(amt));

    // B1-7: ActionSetGaugeAmount → GaugeSet
    // XML: <ActionSetGaugeAmount GaugeType="RETREAT_GAUGE:0" GaugeAmount="GAUGE_NONE:0"/>
    } else if (strcmp(type_str, "ActionSetGaugeAmount") == 0) {
        char g[64] = "", amt[64] = "";
        read_str_r(obj, obj_end, "\"gauge\"",  g,   sizeof(g));
        read_str_r(obj, obj_end, "\"amount\"", amt, sizeof(amt));
        int g_idx = parse_colon_index(g);
        GaugeType gt = (g_idx == 1) ? GaugeType::StunDamage : GaugeType::Retreat;
        idx = bt.addGaugeSet(gt, parse_gauge_amount(amt));

    // ── Batch 2: MD BEHAVIOR XML zero-param nodes ─────────────────────────────
    } else if (strcmp(type_str, "ConditionIsDead") == 0) {
        idx = bt.addConditionIsDead();
    } else if (strcmp(type_str, "ConditionIsInVent") == 0) {
        idx = bt.addConditionIsInVent();
    } else if (strcmp(type_str, "ConditionCanBreakout") == 0) {
        idx = bt.addConditionCanBreakout();
    } else if (strcmp(type_str, "ConditionIsBackstage") == 0) {
        idx = bt.addConditionIsBackstage();
    } else if (strcmp(type_str, "ConditionIsPartOfNPCGroup") == 0) {
        idx = bt.addConditionIsPartOfNPCGroup();
    } else if (strcmp(type_str, "ConditionAnotherAlienIsAttacking") == 0) {
        idx = bt.addConditionAnotherAlienIsAttacking();
    } else if (strcmp(type_str, "ConditionHasSearchedPos") == 0) {
        idx = bt.addConditionHasSearchedPos();
    } else if (strcmp(type_str, "ConditionHasDoneSuspectMoveTo") == 0) {
        idx = bt.addConditionHasDoneSuspectMoveTo();
    } else if (strcmp(type_str, "ActionSwitchToNextTarget") == 0) {
        idx = bt.addActionSwitchToNextTarget();
    } else if (strcmp(type_str, "ActionDoneSystematicSearch") == 0) {
        idx = bt.addActionDoneSystematicSearch();

    // ── Batch 4: EventOrder ───────────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionEventAOccuredAfterB") == 0) {
        char ea_s[32] = "SensedTarget"; char eb_s[32] = "SensedTarget";
        read_str_r(obj, obj_end, "\"event_a\"", ea_s, sizeof(ea_s));
        read_str_r(obj, obj_end, "\"event_b\"", eb_s, sizeof(eb_s));
        EventType ea = parse_event_type(ea_s);
        EventType eb = parse_event_type(eb_s);
        // Also accept integer fallback via read_int_r if string fields absent
        if (!ea_s[0]) ea = static_cast<EventType>(read_int_r(obj, obj_end, "\"event_a\"", 0) & 0x3u);
        if (!eb_s[0]) eb = static_cast<EventType>(read_int_r(obj, obj_end, "\"event_b\"", 0) & 0x3u);
        idx = bt.addConditionEventAOccuredAfterB(ea, eb);

    // ── Batch 5: Squad extensions ─────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionSquadDoingEscalation") == 0) {
        idx = bt.addConditionSquadDoingEscalation();
    } else if (strcmp(type_str, "ConditionSquadDoingSuspiciousWarning") == 0) {
        idx = bt.addConditionSquadDoingSuspiciousWarning();
    } else if (strcmp(type_str, "DecoratorSquadSearch") == 0) {
        idx = bt.addDecoratorSquadSearch();

    // ── Batch 6: SuspiciousItem Group system ──────────────────────────────────
    } else if (strcmp(type_str, "ConditionSuspiciousItemShouldDoStage") == 0) {
        char stage_s[32] = "None";
        read_str_r(obj, obj_end, "\"stage\"", stage_s, sizeof(stage_s));
        idx = bt.addConditionSuspiciousItemShouldDoStage(parse_suspicious_item_stage(stage_s));
    } else if (strcmp(type_str, "ConditionSuspiciousItemIsWithinDistance") == 0) {
        float dist = read_float_r(obj, obj_end, "\"max_dist_m\"", 3.f);
        idx = bt.addConditionSuspiciousItemIsWithinDistance(dist);
    } else if (strcmp(type_str, "ConditionSuspiciousItemFirstGroupMember") == 0) {
        idx = bt.addConditionSuspiciousItemFirstGroupMember();
    } else if (strcmp(type_str, "ConditionSuspiciousItemGroupAllowedToProgress") == 0) {
        idx = bt.addConditionSuspiciousItemGroupAllowedToProgress();
    } else if (strcmp(type_str, "ConditionSuspiciousItemGroupMembersRoutingTo") == 0) {
        idx = bt.addConditionSuspiciousItemGroupMembersRoutingTo();
    } else if (strcmp(type_str, "ConditionSuspiciousItemWaitForGroupRouting") == 0) {
        idx = bt.addConditionSuspiciousItemWaitForGroupRouting();

    } else {
        MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] unknown node type: '%s' — skipped", type_str);
        return BehaviorTree::INVALID;
    }

    if (idx == BehaviorTree::INVALID) return BehaviorTree::INVALID;

    // ── Recurse into children array ───────────────────────────────────────────
    const char* ch_kw   = "\"children\"";
    size_t      ch_klen = strlen(ch_kw);
    const char* p       = obj;
    while (p + ch_klen <= obj_end) {
        if (memcmp(p, ch_kw, ch_klen) == 0) {
            // Advance to '['
            const char* arr = p + ch_klen;
            while (arr < obj_end && *arr != '[') ++arr;
            if (arr >= obj_end) break;
            ++arr; // skip '['

            // Iterate child objects between '[' and matching ']'
            const char* cp = arr;
            while (cp < obj_end && *cp != ']') {
                // Skip whitespace and commas
                while (cp < obj_end && (*cp == ' '  || *cp == '\t'
                                     || *cp == '\n' || *cp == '\r'
                                     || *cp == ',')) ++cp;
                if (cp >= obj_end || *cp == ']') break;
                if (*cp == '{') {
                    const char* child_end = find_object_end(cp, obj_end);
                    if (!child_end) break;
                    uint16_t ci = parse_node(bt, cp + 1, child_end, depth + 1);
                    if (ci != BehaviorTree::INVALID)
                        bt.addChild(idx, ci);
                    cp = child_end + 1;
                } else {
                    ++cp;
                }
            }
            break;
        }
        ++p;
    }

    return idx;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool BTJsonLoader::ReadName(const char* json, char* out_name, int name_max) {
    if (!json || !out_name || name_max <= 0) return false;
    read_str_r(json, json + strlen(json), "\"name\"", out_name, name_max);
    return out_name[0] != '\0';
}

bool BTJsonLoader::LoadFromString(BehaviorTree& bt, const char* json) {
    if (!json) return false;

    // Reject inputs with embedded control characters (NUL, STX, etc.)
    const char* end = json + strlen(json);
    for (const char* q = json; q < end; ++q) {
        unsigned char c = static_cast<unsigned char>(*q);
        if (c < 0x09 || (c > 0x0D && c < 0x20)) {
            MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] control character 0x%02x at offset %zu — rejected",
                   c, static_cast<size_t>(q - json));
            return false;
        }
    }

    // Find "root": { ... }
    const char* kw   = "\"root\"";
    size_t      klen = strlen(kw);
    const char* p    = json;
    while (p + klen <= end) {
        if (memcmp(p, kw, klen) == 0) {
            const char* v = p + klen;
            while (v < end && *v != '{') ++v;
            if (v >= end) {
                MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] 'root' object not found");
                return false;
            }
            const char* root_end = find_object_end(v, end);
            if (!root_end) {
                MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] 'root' object not closed");
                return false;
            }
            uint16_t root_idx = parse_node(bt, v + 1, root_end);
            if (root_idx == BehaviorTree::INVALID) {
                MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] root node parse failed");
                return false;
            }
            bt.setRoot(root_idx);
            return true;
        }
        ++p;
    }
    MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] 'root' key not found");
    return false;
}

bool BTJsonLoader::LoadFromFile(BehaviorTree& bt, const char* path) {
    char* buf = md::fs_read_alloc(path, nullptr);
    if (!buf) {
        MD_LOG(MD_LOG_WARNING, "[BTJsonLoader] file not found: %s", path);
        return false;
    }
    bool ok = LoadFromString(bt, buf);
    md::fs_free(buf);
    return ok;
}
