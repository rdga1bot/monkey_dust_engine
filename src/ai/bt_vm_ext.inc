        // ── Batch 7: SuspiciousItemReaction / AmbushType / NoiseType ─────────
        case BTNodeType::SIReactionCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->si_reaction == static_cast<SuspiciousItemReaction>(nd.data & 0xFFu))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::SetSIReaction: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            as->si_reaction = static_cast<SuspiciousItemReaction>(nd.data & 0xFFu);
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        case BTNodeType::AmbushTypeCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->ambush_type == static_cast<AmbushType>(nd.data & 0xFFu))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::SetAmbushType: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            as->ambush_type = static_cast<AmbushType>(nd.data & 0xFFu);
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        case BTNodeType::NoiseTypeCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->last_noise_type == static_cast<NoiseType>(nd.data & 0xFFu))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::SetNoiseType: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            as->last_noise_type = static_cast<NoiseType>(nd.data & 0xFFu);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        // ── Batch 8: SI lifecycle ─────────────────────────────────────────────
        case BTNodeType::ConditionSuspiciousItemValid: {
            NpcMemoryComponent* nm = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (!nm || nm->event_count == 0) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (nm->events[0].investigation_stage != SuspiciousItemStage::SearchArea)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::ActionConsumeSuspiciousItem: {
            NpcMemoryComponent* nm = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (nm && nm->event_count > 0) {
                for (uint8_t i = 0; i < nm->event_count - 1u; ++i)
                    nm->events[i] = nm->events[i + 1];
                nm->event_count--;
            }
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        case BTNodeType::ActionForceMoveToSI: {
            NpcMemoryComponent* nm = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (!nm || nm->event_count == 0) { result = BTStatus::Failure; pc = nd.parent; continue; }
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) { as->frame_flags |= (1ull << ff::COULD_DO_SUSPECT_TARGET_RESPONSE_MOVETO); }
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        // ── Batch 9 (Step 5): Inter-NPC relationship nodes ────────────────────
        case BTNodeType::RelationshipTrustCheck:
        case BTNodeType::RelationshipFearCheck: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* ben = bb_find(*ab, TARGET_ENTITY_BB_KEY);
            if (!ben) { result = BTStatus::Failure; pc = nd.parent; continue; }
            entt::entity tgt = static_cast<entt::entity>(ben->val.e);
            if (!Registry::Get().valid(tgt)) { result = BTStatus::Failure; pc = nd.parent; continue; }
            NpcRelationshipComponent* rel = Registry::Get().try_get<NpcRelationshipComponent>(e);
            if (!rel) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t threshold = static_cast<uint8_t>((nd.data >> 8) & 0xFFu);
            uint8_t mode      = static_cast<uint8_t>(nd.data & 0x1u);
            uint8_t val = (nd.type == BTNodeType::RelationshipTrustCheck)
                          ? rel->GetTrust(tgt) : rel->GetFear(tgt);
            bool ok = (mode == 0) ? (val >= threshold) : (val < threshold);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // ── Batch 10: MD_z.md BEHAVIOR XML patterns ──────────────────────────────
        case BTNodeType::ConditionIsAnySenseActivationAbove: {
            SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto q = static_cast<SenseThresholdQualifier>(nd.data & 0xFFu);
            bool found = false;
            for (uint8_t si = 0; si < MAX_SENSES && !found; ++si) {
                float act = sc->activation[si];
                switch (q) {
                case SenseThresholdQualifier::Trace:     found = act > 0.f;               break;
                case SenseThresholdQualifier::Lower:     found = act >= sc->threshold_lo;  break;
                case SenseThresholdQualifier::Activated: found = act >= sc->threshold_hi;  break;
                case SenseThresholdQualifier::Upper:     found = act >= 1.0f;              break;
                }
            }
            result = found ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ActionMoveThroughTarget: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                as->frame_flags  |= (1ull << ff::SHOULD_MOVE_THROUGH_TARGET);
                as->target_speed  = static_cast<LocomotionTargetSpeed>(nd.data & 0x3u);
            }
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        case BTNodeType::ActionAdjustMenace: {
            uint8_t mode        = static_cast<uint8_t>(nd.data & 0x3u);
            float   delta_fixed = static_cast<float>((nd.data >> 2) & 0xFFu);
            DirectorSystem::Get().AdjustMenace(delta_fixed / 10.f, mode);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        // ── Batch 11: BEHAVIOR XML patterns (P2+P8+P10) ──────────────────────
        case BTNodeType::ConditionAngleToTarget: {
            const WorldTransform* wt = Registry::Get().try_get<WorldTransform>(e);
            const SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            const AgentState*     as = Registry::Get().try_get<AgentState>(e);
            if (!wt || !sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            // NPC forward from rot_y (right-hand Y-up: fwd = (sin, 0, cos))
            float fwd_x = sinf(wt->rot_y);
            float fwd_z = cosf(wt->rot_y);
            float dx = sc->last_known_x - wt->x;
            float dz = sc->last_known_z - wt->z;
            float len_sq = dx * dx + dz * dz;
            if (len_sq < 1e-6f) { result = BTStatus::Success; pc = nd.parent; continue; }
            float inv_len = 1.f / sqrtf(len_sq);
            float dot = (fwd_x * dx + fwd_z * dz) * inv_len;
            float angle_deg = acosf(dot < -1.f ? -1.f : dot > 1.f ? 1.f : dot) * (180.f / 3.14159265f);
            result = (angle_deg <= static_cast<float>(nd.data)) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionShouldSuspend: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = as->lcflags.test(lcf::IS_SUSPENDED) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ActionSuspendSelf: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->lcflags.set(lcf::IS_SUSPENDED);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        case BTNodeType::ConditionTargetDistLOS: {
            const WorldTransform* wt = Registry::Get().try_get<WorldTransform>(e);
            const SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!wt || !sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            float dx = sc->last_known_x - wt->x;
            float dz = sc->last_known_z - wt->z;
            float dist_sq = dx * dx + dz * dz;
            float max_dist = static_cast<float>(nd.data) / 10.f;
            bool dist_ok = dist_sq <= max_dist * max_dist;
            bool los_ok  = sc->activation[static_cast<int>(SenseType::Visual)] >= sc->threshold_lo;
            result = (dist_ok && los_ok) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // ── Batch 12 VM cases ─────────────────────────────────────────────────

        // P3: ConditionTargetRoutingDistance — nav distance via PathCache, Euclidean fallback
        case BTNodeType::ConditionTargetRoutingDistance: {
            const WorldTransform* wt = Registry::Get().try_get<WorldTransform>(e);
            const SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!wt || !sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            float max_dist = static_cast<float>(nd.data) / 10.f;
            uint32_t ks = PathCache::PosKey(wt->x, wt->z);
            uint32_t ke = PathCache::PosKey(sc->last_known_x, sc->last_known_z);
            float    now_s = static_cast<float>(nowMs) / 1000.f;
            float    nav_dist;
            {
                float tmp_verts[MAX_PATH_LEN * 3];
                int   path_len = 0;
                if (PathCache::Get().Get(ks, ke, now_s, tmp_verts, path_len) && path_len > 1) {
                    nav_dist = 0.f;
                    for (int pi = 0; pi + 1 < path_len; ++pi) {
                        float pdx = tmp_verts[(pi + 1) * 3 + 0] - tmp_verts[pi * 3 + 0];
                        float pdz = tmp_verts[(pi + 1) * 3 + 2] - tmp_verts[pi * 3 + 2];
                        nav_dist += sqrtf(pdx * pdx + pdz * pdz);
                    }
                } else {
                    float dx = sc->last_known_x - wt->x;
                    float dz = sc->last_known_z - wt->z;
                    nav_dist = sqrtf(dx * dx + dz * dz);
                }
            }
            result = (nav_dist <= max_dist) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // P6: ConditionAlienIsAllowed — check AlienConfigPreset::allowed_actions bitmask
        case BTNodeType::ConditionAlienIsAllowed: {
            auto action = static_cast<AlienActionType>(nd.data & 0xFFu);
            const AlienConfigPreset& preset = AlienConfigPreset::Get(
                DirectorSystem::Get().GetActiveConfig());
            uint8_t bit = static_cast<uint8_t>(action);
            bool allowed = (preset.allowed_actions & (uint16_t(1u) << bit)) != 0u;
            result = allowed ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // P7: DecoratorLockVent — exclusive vent access gate
        case BTNodeType::DecoratorLockVent: {
            if (result == BTStatus::Running) {
                uint8_t vent_id = static_cast<uint8_t>(nd.data & 0x7u);
                if (!VentLockTable::Get().TryLock(vent_id, e)) {
                    result = BTStatus::Failure; pc = nd.parent; continue;
                }
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart]; continue;
                }
                VentLockTable::Get().Release(e);
                result = BTStatus::Success; pc = nd.parent; continue;
            }
            // returning from child — release lock and propagate result
            VentLockTable::Get().Release(e);
            pc = nd.parent; continue;
        }

        // ── Batch 15: HIGH-priority patterns from MD engine analysis ─────────
        // ConditionIsEnemyOfTarget — AllianceMatrix::IsEnemy(self.alliance_group, target.alliance_group)
        case BTNodeType::ConditionIsEnemyOfTarget: {
            AgentBlackboard*   ab = Registry::Get().try_get<AgentBlackboard>(e);
            const AgentState*  as = Registry::Get().try_get<AgentState>(e);
            if (!ab || !as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* en = bb_find(*ab, TARGET_ENTITY_BB_KEY);
            if (!en) { result = BTStatus::Failure; pc = nd.parent; continue; }
            entt::entity target    = static_cast<entt::entity>(en->val.e);
            const AgentState* tas  = Registry::Get().try_get<AgentState>(target);
            if (!tas) { result = BTStatus::Failure; pc = nd.parent; continue; }
            bool enemy = AllianceMatrix::Get().IsEnemy(
                static_cast<AllianceGroup>(as->alliance_group),
                static_cast<AllianceGroup>(tas->alliance_group));
            result = enemy ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // ActionForceIdle — always Running (skip this BT tick, re-enter next tick)
        case BTNodeType::ActionForceIdle: {
            result = BTStatus::Running; pc = nd.parent; continue;
        }

        // ── Batch 17 ─────────────────────────────────────────────────────────
        // ConditionIsCharacterClass — Success if as->character_class == (CharacterClass)data
        case BTNodeType::ConditionIsCharacterClass: {
            const AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->character_class == static_cast<CharacterClass>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        // ConditionIsInCover — Success if lcf::IS_IN_COVER set
        case BTNodeType::ConditionIsInCover: {
            const AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = as->lcflags.test(lcf::IS_IN_COVER)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        // ConditionShouldUseCover — Success if aggro_level >= Warning (3)
        case BTNodeType::ConditionShouldUseCover: {
            const AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (static_cast<uint8_t>(as->aggro_level) >=
                      static_cast<uint8_t>(NpcAggroLevel::Warning))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        // ActionMoveToCover — set ff::SHOULD_MOVE_TO_COVER; always Success
        case BTNodeType::ActionMoveToCover: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->frame_flags |= (1ull << ff::SHOULD_MOVE_TO_COVER);
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        // ActionIdleInCover — mark as in cover + set combat state; always Running
        case BTNodeType::ActionIdleInCover: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                as->lcflags.set(lcf::IS_IN_COVER);
                as->combat_state = NpcCombatState::EnteredCover;
            }
            result = BTStatus::Running; pc = nd.parent; continue;
        }

        // ── Batch 18 ─────────────────────────────────────────────────────────
        // BehaviourMoodSetCheck — Success if as->behaviour_mood_set == (BehaviourMoodSet)data
        case BTNodeType::BehaviourMoodSetCheck: {
            const AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->behaviour_mood_set == static_cast<BehaviourMoodSet>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        // SetBehaviourMoodSet — write as->behaviour_mood_set; always Success
        case BTNodeType::SetBehaviourMoodSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->behaviour_mood_set = static_cast<BehaviourMoodSet>(nd.data);
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        // ViewconeTypeCheck — Success if as->viewcone_type == (ViewconeType)data
        case BTNodeType::ViewconeTypeCheck: {
            const AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->viewcone_type == static_cast<ViewconeType>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        // SetViewconeType — write as->viewcone_type; always Success
        case BTNodeType::SetViewconeType: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->viewcone_type = static_cast<ViewconeType>(nd.data);
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        // SensoryTypeCheck — Success if as->last_sensory_type == (SensoryType)data
        case BTNodeType::SensoryTypeCheck: {
            const AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->last_sensory_type == static_cast<SensoryType>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        // SetSensoryType — write as->last_sensory_type; always Success
        case BTNodeType::SetSensoryType: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->last_sensory_type = static_cast<SensoryType>(nd.data);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        // ConditionHasValidRouteToTarget — PathCache::Get from self to target position
        case BTNodeType::ConditionHasValidRouteToTarget: {
            AgentBlackboard*      ab = Registry::Get().try_get<AgentBlackboard>(e);
            const WorldTransform* wt = Registry::Get().try_get<WorldTransform>(e);
            if (!ab || !wt) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* en = bb_find(*ab, TARGET_ENTITY_BB_KEY);
            if (!en) { result = BTStatus::Failure; pc = nd.parent; continue; }
            entt::entity          target = static_cast<entt::entity>(en->val.e);
            const WorldTransform* twt   = Registry::Get().try_get<WorldTransform>(target);
            if (!twt) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint32_t ks = PathCache::PosKey(wt->x,  wt->z);
            uint32_t ke = PathCache::PosKey(twt->x, twt->z);
            float  dummy_pts[3] = {};
            int    dummy_len    = 0;
            bool   has_path = PathCache::Get().Get(ks, ke,
                                  static_cast<float>(nowMs) * 0.001f,
                                  dummy_pts, dummy_len);
            result = has_path ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // ── Batch 19: Weapon System ──────────────────────────────────────────
        case BTNodeType::ConditionCurrentWeaponIsEquipped: {
            WeaponComponent* wc = Registry::Get().try_get<WeaponComponent>(e);
            if (!wc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = wc->is_equipped ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::ConditionCurrentWeaponNeedsReloading: {
            WeaponComponent* wc = Registry::Get().try_get<WeaponComponent>(e);
            if (!wc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = wc->needs_reload ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::ConditionHasMeleeAttackAvailable: {
            WeaponComponent* wc = Registry::Get().try_get<WeaponComponent>(e);
            if (!wc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = wc->melee_available ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::ActionWeaponEquip: {
            WeaponComponent* wc = Registry::Get().try_get<WeaponComponent>(e);
            if (wc) wc->is_equipped = true;
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        case BTNodeType::ActionRangedShoot: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->frame_flags |= (1ull << ff::SHOULD_RANGED_SHOOT);
            result = BTStatus::Success; pc = nd.parent; continue;
        }
        case BTNodeType::ConditionHasObjective: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = bb_get_bool(*ab, HAS_OBJECTIVE_BB_KEY)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
        case BTNodeType::ConditionBehaviourMoodSetAbove: {
            const AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (static_cast<uint8_t>(as->behaviour_mood_set) >
                      static_cast<uint8_t>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }
