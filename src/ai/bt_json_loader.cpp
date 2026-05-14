#include <monkey_dust/ai/bt_json_loader.h>
#include <monkey_dust/ai/bt_action_registry.h>
#include <monkey_dust/platform/md_log.h>
#include <monkey_dust/platform/md_fs.h>
#include <cstring>

// ── String helpers ────────────────────────────────────────────────────────────
// All helpers operate on [start, end) slices — no null termination required.

static const char* find_object_end(const char* p) {
    // p points to '{'  — returns pointer to matching '}'
    int  depth  = 0;
    bool in_str = false;
    while (*p) {
        if (!in_str) {
            if      (*p == '{') ++depth;
            else if (*p == '}') { if (--depth == 0) return p; }
            else if (*p == '"') in_str = true;
        } else {
            if (*p == '\\' && *(p + 1)) ++p;  // skip escaped char
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
        {"ALIEN_KNOWS_VENT",       lcf::ALIEN_KNOWS_VENT},
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

// ── Fallback functions for unregistered actions/conditions ────────────────────
static bool         s_cond_false  (md::EngineContext&, entt::entity) { return false; }
static BTStatus     s_act_failure (md::EngineContext&, entt::entity) { return BTStatus::Failure; }

// ── Recursive node parser ─────────────────────────────────────────────────────
// obj:     points AFTER '{' of the node object
// obj_end: points TO   '}' of the node object
// Returns the allocated node index, or BehaviorTree::INVALID on error.
static uint16_t parse_node(BehaviorTree& bt, const char* obj, const char* obj_end) {
    char type_str[32] = "";
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
        // Deferred resolution: pointer set to nullptr → BT VM returns Failure (safe)
        char tree_name[32] = "";
        read_str_r(obj, obj_end, "\"tree\"", tree_name, sizeof(tree_name));
        if (tree_name[0])
            MD_LOG(MD_LOG_INFO, "[BTJsonLoader] ReferencedBehavior '%s' deferred — resolve manually", tree_name);
        idx = bt.addReference(nullptr);

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
        int   sense = read_int_r  (obj, obj_end, "\"sense\"",     0);
        float thr   = read_float_r(obj, obj_end, "\"threshold\"", 0.5f);
        idx = bt.addSenseCheck(static_cast<uint8_t>(sense & 1), thr);

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
                    const char* child_end = find_object_end(cp);
                    if (!child_end) break;
                    uint16_t ci = parse_node(bt, cp + 1, child_end);
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
    const char* end = json + strlen(json);

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
            const char* root_end = find_object_end(v);
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
