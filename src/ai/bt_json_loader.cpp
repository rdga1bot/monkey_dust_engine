#include <monkey_dust/ai/bt_json_loader.h>
#include <monkey_dust/ai/bt_action_registry.h>
#include <monkey_dust/ai/fnv.h>
#include <monkey_dust/ai/squad_signal.h>
#include <monkey_dust/ai/named_branch.h>
#include <monkey_dust/ai/npc_sound.h>
#include <monkey_dust/ai/alien_config.h>
#include <monkey_dust/ai/vent_lock.h>
#include <monkey_dust/ai/bt_lua_script_registry.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/npc_memory.h>
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/ai/suspicious_item_group.h>
#include <monkey_dust/ai/npc_development.h>
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

// ── Batch 7 parse helpers ────────────────────────────────────────────────────
static SuspiciousItemReaction parse_si_reaction(const char* s) {
    if (strcmp(s, "Investigate") == 0) return SuspiciousItemReaction::Investigate;
    if (strcmp(s, "Alarm")       == 0) return SuspiciousItemReaction::Alarm;
    if (strcmp(s, "Ignore")      == 0) return SuspiciousItemReaction::Ignore;
    if (strcmp(s, "Flee")        == 0) return SuspiciousItemReaction::Flee;
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);
        if (v >= 0 && v < 4) return static_cast<SuspiciousItemReaction>(v);
    }
    return SuspiciousItemReaction::Unknown;
}
static AmbushType parse_ambush_type(const char* s) {
    if (strcmp(s, "None")      == 0) return AmbushType::None;
    if (strcmp(s, "Vent")      == 0) return AmbushType::Vent;
    if (strcmp(s, "Ceiling")   == 0) return AmbushType::Ceiling;
    if (strcmp(s, "Direct")    == 0) return AmbushType::Direct;
    if (strcmp(s, "Backstage") == 0) return AmbushType::Backstage;
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);
        if (v >= 0 && v < 5) return static_cast<AmbushType>(v);
    }
    return AmbushType::Unknown;
}
static NoiseType parse_noise_type(const char* s) {
    if (strcmp(s, "None")      == 0) return NoiseType::None;
    if (strcmp(s, "Footstep")  == 0) return NoiseType::Footstep;
    if (strcmp(s, "Weapon")    == 0) return NoiseType::Weapon;
    if (strcmp(s, "Vent")      == 0) return NoiseType::Vent;
    if (strcmp(s, "Explosion") == 0) return NoiseType::Explosion;
    if (strcmp(s, "Voice")     == 0) return NoiseType::Voice;
    if (s[0] >= '0' && s[0] <= '9') {
        int v = atoi(s);
        if (v >= 0 && v < 6) return static_cast<NoiseType>(v);
    }
    return NoiseType::Unknown;
}

// ── Batch 12 parse helpers ───────────────────────────────────────────────────
static AlienActionType parse_alien_action(const char* s) {
    if (!s || !s[0]) return AlienActionType::ThreatAware;
    if (strcmp(s, "ThreatAware")              == 0) return AlienActionType::ThreatAware;
    if (strcmp(s, "WillKilltrap")             == 0) return AlienActionType::WillKilltrap;
    if (strcmp(s, "WillFlankFromThreatAware") == 0) return AlienActionType::WillFlankFromThreatAware;
    if (strcmp(s, "WillAmbush")               == 0) return AlienActionType::WillAmbush;
    int v = atoi(s);
    if (v > 0) return static_cast<AlienActionType>(static_cast<uint8_t>(v));
    return AlienActionType::ThreatAware;
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

// ── Split helper functions to avoid MSVC C1061 block-nesting limit (128) ─────
// Each file is a standalone if-else-if chain returning INVALID when unmatched.
static uint16_t parse_node_core(BehaviorTree& bt, const char* type_str,
                                 const char* obj, const char* obj_end,
                                 md::BTActionRegistry& reg) {
    uint16_t idx = BehaviorTree::INVALID;
#include "bt_json_core.inc"
    return idx;
}
static uint16_t parse_node_ai(BehaviorTree& bt, const char* type_str,
                               const char* obj, const char* obj_end,
                               md::BTActionRegistry& reg) {
    uint16_t idx = BehaviorTree::INVALID;
#include "bt_json_ai.inc"
    return idx;
}
static uint16_t parse_node_ext(BehaviorTree& bt, const char* type_str,
                                const char* obj, const char* obj_end,
                                md::BTActionRegistry& reg) {
    uint16_t idx = BehaviorTree::INVALID;
#include "bt_json_ext.inc"
    return idx;
}

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
    }
    if (idx == BehaviorTree::INVALID) idx = parse_node_core(bt, type_str, obj, obj_end, reg);
    if (idx == BehaviorTree::INVALID) idx = parse_node_ai (bt, type_str, obj, obj_end, reg);
    if (idx == BehaviorTree::INVALID) idx = parse_node_ext(bt, type_str, obj, obj_end, reg);
    if (idx == BehaviorTree::INVALID) {
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
