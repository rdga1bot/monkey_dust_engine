
    // ── MD_z nodes ───────────────────────────────────────────────────────

    // Z1: DecoratorMood — decorator: run child only when mood matches
    if (strcmp(type_str, "DecoratorMood") == 0) {
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
        int ct = read_int_r(obj, obj_end, "\"char_type\"", 0);
        idx = bt.addConditionIsInVent(static_cast<uint8_t>(ct & 0x3));
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

    // ── Batch 7: SuspiciousItemReaction / AmbushType / NoiseType ─────────────
    } else if (strcmp(type_str, "SIReactionCheck") == 0) {
        char s[32] = "Investigate";
        read_str_r(obj, obj_end, "\"reaction\"", s, sizeof(s));
        idx = bt.addSIReactionCheck(parse_si_reaction(s));

    } else if (strcmp(type_str, "SetSIReaction") == 0) {
        char s[32] = "Investigate";
        read_str_r(obj, obj_end, "\"reaction\"", s, sizeof(s));
        idx = bt.addSetSIReaction(parse_si_reaction(s));

    } else if (strcmp(type_str, "AmbushTypeCheck") == 0) {
        char s[32] = "None";
        read_str_r(obj, obj_end, "\"ambush_type\"", s, sizeof(s));
        idx = bt.addAmbushTypeCheck(parse_ambush_type(s));

    } else if (strcmp(type_str, "SetAmbushType") == 0) {
        char s[32] = "None";
        read_str_r(obj, obj_end, "\"ambush_type\"", s, sizeof(s));
        idx = bt.addSetAmbushType(parse_ambush_type(s));

    } else if (strcmp(type_str, "NoiseTypeCheck") == 0) {
        char s[32] = "None";
        read_str_r(obj, obj_end, "\"noise_type\"", s, sizeof(s));
        idx = bt.addNoiseTypeCheck(parse_noise_type(s));

    } else if (strcmp(type_str, "SetNoiseType") == 0) {
        char s[32] = "None";
        read_str_r(obj, obj_end, "\"noise_type\"", s, sizeof(s));
        idx = bt.addSetNoiseType(parse_noise_type(s));
    }
