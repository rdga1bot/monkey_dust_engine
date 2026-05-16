
    // ── Batch 8: SI lifecycle ─────────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionSuspiciousItemValid") == 0) {
        idx = bt.addConditionSuspiciousItemValid();

    } else if (strcmp(type_str, "ActionConsumeSuspiciousItem") == 0) {
        idx = bt.addActionConsumeSuspiciousItem();

    } else if (strcmp(type_str, "ActionForceMoveToSI") == 0) {
        idx = bt.addActionForceMoveToSI();

    // ── Batch 9 (Step 5): Inter-NPC relationship nodes ─────────────────────
    } else if (strcmp(type_str, "RelationshipTrustCheck") == 0) {
        int thr  = read_int_r(obj, obj_end, "\"threshold\"", 128);
        int mode = read_int_r(obj, obj_end, "\"mode\"", 0);
        if (thr < 0) thr = 0; if (thr > 255) thr = 255;
        idx = bt.addRelationshipTrustCheck(static_cast<uint8_t>(thr), static_cast<uint8_t>(mode & 1));

    } else if (strcmp(type_str, "RelationshipFearCheck") == 0) {
        int thr  = read_int_r(obj, obj_end, "\"threshold\"", 64);
        int mode = read_int_r(obj, obj_end, "\"mode\"", 0);
        if (thr < 0) thr = 0; if (thr > 255) thr = 255;
        idx = bt.addRelationshipFearCheck(static_cast<uint8_t>(thr), static_cast<uint8_t>(mode & 1));

    // ── Batch 10: MD_z.md BEHAVIOR XML patterns ───────────────────────────────
    } else if (strcmp(type_str, "ConditionIsAnySenseActivationAbove") == 0) {
        char qual_s[32] = "Lower";
        read_str_r(obj, obj_end, "\"threshold\"", qual_s, sizeof(qual_s));
        idx = bt.addConditionIsAnySenseActivationAbove(parse_sense_qualifier(qual_s));

    } else if (strcmp(type_str, "ActionMoveThroughTarget") == 0) {
        int speed = read_int_r(obj, obj_end, "\"speed\"", 2);  // default Fast=2
        if (speed < 0 || speed > 3) speed = 2;
        idx = bt.addActionMoveThroughTarget(static_cast<LocomotionTargetSpeed>(speed));

    } else if (strcmp(type_str, "ActionAdjustMenace") == 0) {
        float delta = read_float_r(obj, obj_end, "\"delta\"", 0.f);
        int   mode  = read_int_r  (obj, obj_end, "\"mode\"",  0);
        if (mode < 0 || mode > 2) mode = 0;
        if (delta < 0.f) delta = 0.f;
        idx = bt.addActionAdjustMenace(delta, static_cast<uint8_t>(mode));

    // ── Batch 11: BEHAVIOR XML patterns (P2+P8+P10) ──────────────────────────
    } else if (strcmp(type_str, "ConditionAngleToTarget") == 0) {
        float angle = read_float_r(obj, obj_end, "\"angle\"", 45.f);
        if (angle < 0.f) angle = 0.f;
        idx = bt.addConditionAngleToTarget(angle);

    } else if (strcmp(type_str, "ConditionShouldSuspend") == 0) {
        idx = bt.addConditionShouldSuspend();

    } else if (strcmp(type_str, "ActionSuspendSelf") == 0) {
        idx = bt.addActionSuspendSelf();

    } else if (strcmp(type_str, "ConditionTargetDistLOS") == 0) {
        float dist = read_float_r(obj, obj_end, "\"distance\"", 10.f);
        if (dist < 0.f) dist = 0.f;
        idx = bt.addConditionTargetDistLOS(dist);

    // ── Batch 12: P3 / P6 / P7 ───────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionTargetRoutingDistance") == 0) {
        float dist = read_float_r(obj, obj_end, "\"distance\"", 10.f);
        if (dist < 0.f) dist = 0.f;
        idx = bt.addConditionTargetRoutingDistance(dist);

    } else if (strcmp(type_str, "ConditionAlienIsAllowed") == 0) {
        char action_s[40] = "ThreatAware";
        read_str_r(obj, obj_end, "\"action\"", action_s, sizeof(action_s));
        // Integer fallback when field is a number
        if (action_s[0] >= '0' && action_s[0] <= '9') {
            int v = atoi(action_s);
            idx = bt.addConditionAlienIsAllowed(static_cast<AlienActionType>(v));
        } else {
            idx = bt.addConditionAlienIsAllowed(parse_alien_action(action_s));
        }

    } else if (strcmp(type_str, "DecoratorLockVent") == 0) {
        int vid = read_int_r(obj, obj_end, "\"vent_id\"", 0);
        if (vid < 0) vid = 0;
        if (vid >= VentLockTable::MAX_VENTS) vid = VentLockTable::MAX_VENTS - 1;
        idx = bt.addDecoratorLockVent(static_cast<uint8_t>(vid));

    // ── Batch 15 ──────────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionIsEnemyOfTarget") == 0) {
        idx = bt.addConditionIsEnemyOfTarget();

    } else if (strcmp(type_str, "ActionForceIdle") == 0) {
        idx = bt.addActionForceIdle();

    } else if (strcmp(type_str, "ConditionHasValidRouteToTarget") == 0) {
        idx = bt.addConditionHasValidRouteToTarget();

    // ── Batch 17 ─────────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionIsCharacterClass") == 0) {
        // JSON: "character_class": "Alien" | "Player" | "Android" | int
        char cls_s[32] = "Unknown";
        read_str_r(obj, obj_end, "\"character_class\"", cls_s, sizeof(cls_s));
        CharacterClass cls = CharacterClass::Unknown;
        if      (strcmp(cls_s, "Player")       == 0) cls = CharacterClass::Player;
        else if (strcmp(cls_s, "Alien")        == 0) cls = CharacterClass::Alien;
        else if (strcmp(cls_s, "Android")      == 0) cls = CharacterClass::Android;
        else if (strcmp(cls_s, "Civilian")     == 0) cls = CharacterClass::Civilian;
        else if (strcmp(cls_s, "Security")     == 0) cls = CharacterClass::Security;
        else if (strcmp(cls_s, "Innocent")     == 0) cls = CharacterClass::Innocent;
        else if (strcmp(cls_s, "AndroidHeavy") == 0) cls = CharacterClass::AndroidHeavy;
        else if (cls_s[0] >= '0' && cls_s[0] <= '9')
            cls = static_cast<CharacterClass>(static_cast<uint8_t>(atoi(cls_s)));
        idx = bt.addConditionIsCharacterClass(cls);

    } else if (strcmp(type_str, "ConditionIsInCover") == 0) {
        idx = bt.addConditionIsInCover();

    } else if (strcmp(type_str, "ConditionShouldUseCover") == 0) {
        idx = bt.addConditionShouldUseCover();

    } else if (strcmp(type_str, "ActionMoveToCover") == 0) {
        idx = bt.addActionMoveToCover();

    } else if (strcmp(type_str, "ActionIdleInCover") == 0) {
        idx = bt.addActionIdleInCover();

    // ── Batch 18 ─────────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "BehaviourMoodSetCheck") == 0) {
        char s[40] = "Neutral";
        read_str_r(obj, obj_end, "\"mood_set\"", s, sizeof(s));
        BehaviourMoodSet v = BehaviourMoodSet::Neutral;
        if      (strcmp(s, "Neutral")                    == 0) v = BehaviourMoodSet::Neutral;
        else if (strcmp(s, "ThreatEscalationAggressive") == 0) v = BehaviourMoodSet::ThreatEscalationAggressive;
        else if (strcmp(s, "ThreatEscalationPanicked")   == 0) v = BehaviourMoodSet::ThreatEscalationPanicked;
        else if (strcmp(s, "Aggressive")                 == 0) v = BehaviourMoodSet::Aggressive;
        else if (strcmp(s, "Panicked")                   == 0) v = BehaviourMoodSet::Panicked;
        else if (strcmp(s, "ThreatEscalationEscalated")  == 0) v = BehaviourMoodSet::ThreatEscalationEscalated;
        else if (strcmp(s, "Escalated")                  == 0) v = BehaviourMoodSet::Escalated;
        else if (s[0] >= '0' && s[0] <= '9') v = static_cast<BehaviourMoodSet>(static_cast<uint8_t>(atoi(s)));
        idx = bt.addBehaviourMoodSetCheck(v);

    } else if (strcmp(type_str, "SetBehaviourMoodSet") == 0) {
        char s[40] = "Neutral";
        read_str_r(obj, obj_end, "\"mood_set\"", s, sizeof(s));
        BehaviourMoodSet v = BehaviourMoodSet::Neutral;
        if      (strcmp(s, "ThreatEscalationAggressive") == 0) v = BehaviourMoodSet::ThreatEscalationAggressive;
        else if (strcmp(s, "ThreatEscalationPanicked")   == 0) v = BehaviourMoodSet::ThreatEscalationPanicked;
        else if (strcmp(s, "Aggressive")                 == 0) v = BehaviourMoodSet::Aggressive;
        else if (strcmp(s, "Panicked")                   == 0) v = BehaviourMoodSet::Panicked;
        else if (strcmp(s, "ThreatEscalationEscalated")  == 0) v = BehaviourMoodSet::ThreatEscalationEscalated;
        else if (strcmp(s, "Escalated")                  == 0) v = BehaviourMoodSet::Escalated;
        else if (s[0] >= '0' && s[0] <= '9') v = static_cast<BehaviourMoodSet>(static_cast<uint8_t>(atoi(s)));
        idx = bt.addSetBehaviourMoodSet(v);

    } else if (strcmp(type_str, "ViewconeTypeCheck") == 0) {
        char s[32] = "Rectangle";
        read_str_r(obj, obj_end, "\"viewcone_type\"", s, sizeof(s));
        ViewconeType v = ViewconeType::Rectangle;
        if      (strcmp(s, "Ellipse")   == 0) v = ViewconeType::Ellipse;
        else if (s[0] >= '0' && s[0] <= '9') v = static_cast<ViewconeType>(static_cast<uint8_t>(atoi(s)));
        idx = bt.addViewconeTypeCheck(v);

    } else if (strcmp(type_str, "SetViewconeType") == 0) {
        char s[32] = "Rectangle";
        read_str_r(obj, obj_end, "\"viewcone_type\"", s, sizeof(s));
        ViewconeType v = ViewconeType::Rectangle;
        if      (strcmp(s, "Ellipse")   == 0) v = ViewconeType::Ellipse;
        else if (s[0] >= '0' && s[0] <= '9') v = static_cast<ViewconeType>(static_cast<uint8_t>(atoi(s)));
        idx = bt.addSetViewconeType(v);

    } else if (strcmp(type_str, "SensoryTypeCheck") == 0) {
        char s[32] = "Visual";
        read_str_r(obj, obj_end, "\"sensory_type\"", s, sizeof(s));
        SensoryType v = SensoryType::Visual;
        if      (strcmp(s, "WeaponSound")   == 0) v = SensoryType::WeaponSound;
        else if (strcmp(s, "MovementSound") == 0) v = SensoryType::MovementSound;
        else if (s[0] >= '0' && s[0] <= '9') v = static_cast<SensoryType>(static_cast<uint8_t>(atoi(s)));
        idx = bt.addSensoryTypeCheck(v);

    } else if (strcmp(type_str, "SetSensoryType") == 0) {
        char s[32] = "Visual";
        read_str_r(obj, obj_end, "\"sensory_type\"", s, sizeof(s));
        SensoryType v = SensoryType::Visual;
        if      (strcmp(s, "WeaponSound")   == 0) v = SensoryType::WeaponSound;
        else if (strcmp(s, "MovementSound") == 0) v = SensoryType::MovementSound;
        else if (s[0] >= '0' && s[0] <= '9') v = static_cast<SensoryType>(static_cast<uint8_t>(atoi(s)));
        idx = bt.addSetSensoryType(v);

    // ── Batch 19: Weapon System ───────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionCurrentWeaponIsEquipped") == 0) {
        idx = bt.addConditionCurrentWeaponIsEquipped();
    } else if (strcmp(type_str, "ConditionCurrentWeaponNeedsReloading") == 0) {
        idx = bt.addConditionCurrentWeaponNeedsReloading();
    } else if (strcmp(type_str, "ConditionHasMeleeAttackAvailable") == 0) {
        idx = bt.addConditionHasMeleeAttackAvailable();
    } else if (strcmp(type_str, "ActionWeaponEquip") == 0) {
        idx = bt.addActionWeaponEquip();
    } else if (strcmp(type_str, "ActionRangedShoot") == 0) {
        idx = bt.addActionRangedShoot();
    } else if (strcmp(type_str, "ConditionHasObjective") == 0) {
        idx = bt.addConditionHasObjective();
    } else if (strcmp(type_str, "ConditionBehaviourMoodSetAbove") == 0) {
        char s[40] = "Neutral";
        read_str_r(obj, obj_end, "\"mood_set\"", s, sizeof(s));
        BehaviourMoodSet v = BehaviourMoodSet::Neutral;
        if      (strcmp(s, "ThreatEscalationAggressive") == 0) v = BehaviourMoodSet::ThreatEscalationAggressive;
        else if (strcmp(s, "ThreatEscalationPanicked")   == 0) v = BehaviourMoodSet::ThreatEscalationPanicked;
        else if (strcmp(s, "Aggressive")                 == 0) v = BehaviourMoodSet::Aggressive;
        else if (strcmp(s, "Panicked")                   == 0) v = BehaviourMoodSet::Panicked;
        else if (strcmp(s, "ThreatEscalationEscalated")  == 0) v = BehaviourMoodSet::ThreatEscalationEscalated;
        else if (strcmp(s, "Escalated")                  == 0) v = BehaviourMoodSet::Escalated;
        else if (s[0] >= '0' && s[0] <= '9') v = static_cast<BehaviourMoodSet>(static_cast<uint8_t>(atoi(s)));
        idx = bt.addConditionBehaviourMoodSetAbove(v);

    // ── Batch 20 ─────────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "ActionSuccess") == 0) {
        idx = bt.addActionSuccess();
    } else if (strcmp(type_str, "ActionFail") == 0) {
        idx = bt.addActionFail();
    } else if (strcmp(type_str, "DecoratorTimer") == 0) {
        char slot[32] = "0";
        read_str_r(obj, obj_end, "\"timer_slot\"", slot, sizeof(slot));
        int dur = read_int_r(obj, obj_end, "\"duration_ms\"", 3000);
        idx = bt.addDecoratorTimer(parse_timer_slot(slot), static_cast<uint32_t>(dur < 0 ? 0 : dur));
    } else if (strcmp(type_str, "ActionIdle") == 0) {
        idx = bt.addActionIdle();
    } else if (strcmp(type_str, "ConditionTargetIsPlayer") == 0) {
        idx = bt.addTargetFlagCheck(lcf::IS_PLAYER, true);
    } else if (strcmp(type_str, "ConditionTargetIsWithinDistance") == 0) {
        float dist = read_float_r(obj, obj_end, "\"distance\"", 5.f);
        if (dist < 0.f) dist = 0.f;
        idx = bt.addConditionTargetIsWithinDistance(dist);
    } else if (strcmp(type_str, "ConditionHasAWeapon") == 0) {
        idx = bt.addConditionHasAWeapon();
    } else if (strcmp(type_str, "ConditionShouldProcessSuspiciousItem") == 0) {
        idx = bt.addConditionShouldProcessSuspiciousItem();

    // ── Batch 21 ─────────────────────────────────────────────────────────────
    } else if (strcmp(type_str, "ActionMoveToTarget") == 0) {
        int speed = read_int_r(obj, obj_end, "\"speed\"", 2);  // default Fast=2
        if (speed < 0 || speed > 3) speed = 2;
        idx = bt.addActionMoveToTarget(static_cast<LocomotionTargetSpeed>(speed));
    } else if (strcmp(type_str, "ActionMakeAggressive") == 0) {
        idx = bt.addActionMakeAggressive();
    } else if (strcmp(type_str, "ConditionAllowedToAttackTarget") == 0) {
        idx = bt.addConditionAllowedToAttackTarget();
    } else if (strcmp(type_str, "ActionDead") == 0) {
        idx = bt.addActionDead();
    } else if (strcmp(type_str, "ActionMeleeAttack") == 0) {
        int attack_type = read_int_r(obj, obj_end, "\"attack_type\"", 0);
        if (attack_type < 0 || attack_type > 3) attack_type = 0;
        idx = bt.addActionMeleeAttack(static_cast<uint8_t>(attack_type));
    } else if (strcmp(type_str, "ConditionIsCurrentCoverValid") == 0) {
        idx = bt.addConditionIsCurrentCoverValid();

    // ── Batch 22 — new types ──────────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionLastTimeSensed") == 0) {
        uint8_t si     = read_sense_idx_r(obj, obj_end, 0u);
        int     max_ms = read_int_r(obj, obj_end, "\"max_elapsed_ms\"", 2000);
        idx = bt.addConditionLastTimeSensed(si, static_cast<uint32_t>(max_ms < 0 ? 0 : max_ms));
    } else if (strcmp(type_str, "ActionPerformRole") == 0) {
        char role_s[32] = "";
        read_str_r(obj, obj_end, "\"role\"", role_s, sizeof(role_s));
        idx = bt.addActionPerformRole(parse_role(role_s));
    } else if (strcmp(type_str, "ConditionWasSenseThresholdLastIncreaseActivationAbove") == 0) {
        uint8_t si = read_sense_idx_r(obj, obj_end, 0u);
        char thr[16] = "Activated";
        read_str_r(obj, obj_end, "\"threshold\"", thr, sizeof(thr));
        idx = bt.addConditionWasSenseThresholdLastIncreaseActivationAbove(
            si, parse_sense_qualifier(thr));
    } else if (strcmp(type_str, "ConditionTargetsWeaponHasAmmo") == 0) {
        idx = bt.addConditionTargetsWeaponHasAmmo();
    } else if (strcmp(type_str, "ConditionTargetsWeaponHasProperty") == 0) {
        int wtype = read_int_r(obj, obj_end, "\"weapon_type\"", 0);
        if (wtype < 0 || wtype > 255) wtype = 0;
        idx = bt.addConditionTargetsWeaponHasProperty(static_cast<uint8_t>(wtype));
    } else if (strcmp(type_str, "ConditionHasSearchedMostRecentSensedPosition") == 0) {
        idx = bt.addConditionHasSearchedMostRecentSensedPosition();
    // ── Batch 22 — JSON aliases (no new BTNodeType) ───────────────────────────
    } else if (strcmp(type_str, "ConditionIsPerformingRoleOrCouldPerformRole") == 0) {
        char role_s[32] = "";
        read_str_r(obj, obj_end, "\"role\"", role_s, sizeof(role_s));
        idx = bt.addRoleCheck(parse_role(role_s), true);  // could_perform mode
    } else if (strcmp(type_str, "ActionNotifySquad") == 0) {
        char sig[24] = "None";
        read_str_r(obj, obj_end, "\"signal\"", sig, sizeof(sig));
        idx = bt.addActionSquadNotify(parse_squad_signal(sig));
    } else if (strcmp(type_str, "ConditionTargetLogicCharacterFlags") == 0) {
        char f[64] = "";
        read_str_r(obj, obj_end, "\"flag\"", f, sizeof(f));
        bool cs = read_bool_r(obj, obj_end, "\"check_set\"", true);
        int bit = parse_colon_index(f);
        if (bit < 0) bit = static_cast<int>(parse_flag(f));
        idx = bt.addTargetFlagCheck(static_cast<uint8_t>(bit), cs);
    } else if (strcmp(type_str, "ConditionWithdrawState") == 0) {
        char s[16] = "";
        read_str_r(obj, obj_end, "\"state\"", s, sizeof(s));
        idx = bt.addWithdrawCheck(parse_withdraw(s));
    } else if (strcmp(type_str, "ActionStartTimerRandom") == 0) {
        char slot[32] = "0";
        read_str_r(obj, obj_end, "\"timer_slot\"", slot, sizeof(slot));
        int min_ms = read_int_r(obj, obj_end, "\"min_ms\"", 0);
        int max_ms = read_int_r(obj, obj_end, "\"max_ms\"", 3000);
        idx = bt.addActionTimerRandom(parse_timer_slot(slot),
                                      static_cast<uint32_t>(min_ms < 0 ? 0 : min_ms),
                                      static_cast<uint32_t>(max_ms < 0 ? 0 : max_ms));
    } else if (strcmp(type_str, "ConditionAngleToTargetLessThan") == 0) {
        float angle = read_float_r(obj, obj_end, "\"angle\"", 45.f);
        if (angle < 0.f) angle = 0.f;
        idx = bt.addConditionAngleToTarget(angle);

    // ── Batch 23 — new types ──────────────────────────────────────────────────
    } else if (strcmp(type_str, "ActionIdleTimeFacingTarget") == 0) {
        int dur = read_int_r(obj, obj_end, "\"duration_ms\"", 2000);
        idx = bt.addActionIdleTimeFacingTarget(static_cast<uint32_t>(dur < 0 ? 0 : dur));
    } else if (strcmp(type_str, "ActionIdleTimeFacingTargetMostRecentSensedPosition") == 0) {
        int dur = read_int_r(obj, obj_end, "\"duration_ms\"", 2000);
        idx = bt.addActionIdleTimeFacingTargetMostRecentSensedPosition(static_cast<uint32_t>(dur < 0 ? 0 : dur));
    } else if (strcmp(type_str, "ActionIdleTimeFacingSuspiciousItem") == 0) {
        int dur = read_int_r(obj, obj_end, "\"duration_ms\"", 2000);
        idx = bt.addActionIdleTimeFacingSuspiciousItem(static_cast<uint32_t>(dur < 0 ? 0 : dur));
    } else if (strcmp(type_str, "ActionRangedAim") == 0) {
        idx = bt.addActionRangedAim();
    } else if (strcmp(type_str, "ActionSuspiciousItemReaction") == 0) {
        char s[24] = "Investigate";
        read_str_r(obj, obj_end, "\"reaction\"", s, sizeof(s));
        idx = bt.addActionSuspiciousItemReaction(parse_si_reaction(s));
    } else if (strcmp(type_str, "ActionSuspectTargetResponse") == 0) {
        idx = bt.addActionSuspectTargetResponse();
    // ── Batch 23 — alias ─────────────────────────────────────────────────────
    } else if (strcmp(type_str, "ActionSuspiciousItemMoveTo") == 0) {
        idx = bt.addActionForceMoveToSI();
    // ── Batch 23 — 20 STUB nodes (return Success/Failure, no-op) ─────────────
    } else if (strcmp(type_str, "ActionAssert") == 0
            || strcmp(type_str, "ActionBackstageAreaSweep") == 0
            || strcmp(type_str, "ActionSuspend") == 0
            || strcmp(type_str, "ActionStartConversation") == 0
            || strcmp(type_str, "ActionStopConversation") == 0
            || strcmp(type_str, "ActionGamepadRumble") == 0
            || strcmp(type_str, "ActionGamepadRumbleStop") == 0
            || strcmp(type_str, "ActionFlameThrowerOn") == 0
            || strcmp(type_str, "ActionKilltrap") == 0
            || strcmp(type_str, "ActionCorpseTrap") == 0
            || strcmp(type_str, "ActionScriptedWithdrawal") == 0
            || strcmp(type_str, "ActionPrimaryDamageControl") == 0
            || strcmp(type_str, "ActionDebugPrint") == 0) {
        idx = bt.addActionSuccess();  // STUB: action no-op returns Success
    } else if (strcmp(type_str, "ConditionAlienIsDead") == 0
            || strcmp(type_str, "ConditionHasConversation") == 0
            || strcmp(type_str, "ConditionIsInConversation") == 0
            || strcmp(type_str, "ConditionGamepad") == 0
            || strcmp(type_str, "ConditionFlameThrowerOn") == 0
            || strcmp(type_str, "ConditionIsScriptedWithdrawal") == 0
            || strcmp(type_str, "ConditionIsPrimaryDamageControl") == 0) {
        idx = bt.addActionFail();  // STUB: condition with no backend returns Failure

    // ── Batch 24 — new types ──────────────────────────────────────────────────
    } else if (strcmp(type_str, "ActionRequestCover") == 0) {
        idx = bt.addActionRequestCover();
    } else if (strcmp(type_str, "ConditionHasValidCoverToChangeTo") == 0) {
        idx = bt.addConditionHasValidCoverToChangeTo();
    // ── Batch 24 — JSON-only STUBs (no ObjectiveComponent / CombatArea system) ─
    } else if (strcmp(type_str, "ActionMoveToObjective") == 0
            || strcmp(type_str, "ActionMoveToCurrentObjective") == 0) {
        idx = bt.addActionFail();  // STUB: no ObjectiveComponent
    } else if (strcmp(type_str, "ConditionObjectiveIsWithinDistance") == 0
            || strcmp(type_str, "ConditionTargetIsInCombatArea") == 0
            || strcmp(type_str, "ConditionIsInCombatArea") == 0) {
        idx = bt.addActionFail();  // STUB: no CombatArea system

    // ── Batch 25 — Lua script integration ────────────────────────────────────
    } else if (strcmp(type_str, "ConditionHasScript") == 0) {
        idx = bt.addConditionHasScript();
    } else if (strcmp(type_str, "ActionScript") == 0) {
        char script[48] = "";
        read_str_r(obj, obj_end, "\"script\"", script, sizeof(script));
        uint32_t hash = BTLuaScriptRegistry::Get().Register(script);
        idx = bt.addActionScript(hash);

    // ── Batch 26 — P2 simple nodes ────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionLastTimeSearchedWithinTime") == 0) {
        int ms = read_int_r(obj, obj_end, "\"time_ms\"", 3000);
        idx = bt.addConditionLastTimeSearchedWithinTime(static_cast<uint32_t>(ms < 0 ? 0 : ms));
    } else if (strcmp(type_str, "ConditionAllowedToSearch") == 0) {
        idx = bt.addConditionAllowedToSearch();
    } else if (strcmp(type_str, "ConditionHasDoneSuspectResponseMoveTo") == 0) {
        idx = bt.addConditionHasDoneSuspectResponseMoveTo();
    } else if (strcmp(type_str, "ConditionHasDoneSuspectResponseWithinTime") == 0) {
        int ms = read_int_r(obj, obj_end, "\"time_ms\"", 3000);
        idx = bt.addConditionHasDoneSuspectResponseWithinTime(static_cast<uint32_t>(ms < 0 ? 0 : ms));
    } else if (strcmp(type_str, "ConditionHasKilltrap") == 0) {
        idx = bt.addConditionHasKilltrap();
    } else if (strcmp(type_str, "ConditionHasMeleeAttackAvailableOrIsAttacking") == 0) {
        idx = bt.addConditionHasMeleeAttackAvailableOrIsAttacking();
    } else if (strcmp(type_str, "ConditionIsBranchActive") == 0) {
        char branch[64] = "";
        read_str_r(obj, obj_end, "\"branch\"", branch, sizeof(branch));
        idx = bt.addConditionIsBranchActive(md::fnv1a(branch));
    } else if (strcmp(type_str, "ConditionIsRequestingCover") == 0) {
        idx = bt.addConditionIsRequestingCover();
    } else if (strcmp(type_str, "ConditionAllowedToPursueTarget") == 0) {
        idx = bt.addConditionAllowedToPursueTarget();
    // ── Batch 26 — JSON-only alias ────────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionAnotherAlienIsAttackingThisTarget") == 0) {
        idx = bt.addConditionAnotherAlienIsAttacking();  // alias: no per-target tracking

    // ── Batch 27 — P2 medium action nodes ─────────────────────────────────────
    } else if (strcmp(type_str, "ActionBreakout") == 0) {
        idx = bt.addActionBreakout();
    } else if (strcmp(type_str, "ActionMoveToMostRecentSensedPosition") == 0) {
        idx = bt.addActionMoveToMostRecentSensedPosition();
    } else if (strcmp(type_str, "ActionMoveToNearestStandingPointToTarget") == 0) {
        idx = bt.addActionMoveToNearestStandingPointToTarget();
    } else if (strcmp(type_str, "ActionMoveToAttackTarget") == 0) {
        idx = bt.addActionMoveToAttackTarget();
    } else if (strcmp(type_str, "ActionChangeCover") == 0) {
        idx = bt.addActionChangeCover();
    // ── Batch 27 — aliases / P3 STUBs ────────────────────────────────────────
    } else if (strcmp(type_str, "ConditionHasValidRouteToNearTarget") == 0) {
        idx = bt.addConditionHasValidRouteToTarget();  // alias: near-target = same path check
    } else if (strcmp(type_str, "ConditionAngleNPCToTargetsAimLessThan") == 0
            || strcmp(type_str, "ConditionTargetIsOnlyAccessibleCrouching") == 0
            || strcmp(type_str, "ConditionHasFlankedVentCloseToPlayer") == 0
            || strcmp(type_str, "ConditionHasVentCloseToAlien") == 0
            || strcmp(type_str, "ConditionIsCoverExposed") == 0
            || strcmp(type_str, "ConditionIsInTargetsWeaponRange") == 0) {
        idx = bt.addActionFail();  // STUB: no AimComponent/VentRegistry/weapon_range
