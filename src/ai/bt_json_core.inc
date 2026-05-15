    } else if (strcmp(type_str, "SequenceStateless") == 0) {  // C11
        idx = bt.addSequenceStateless();
    } else if (strcmp(type_str, "SequenceIgnoreChildFail") == 0) {  // Batch 16
        idx = bt.addSequenceIgnoreChildFail();
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
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
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
