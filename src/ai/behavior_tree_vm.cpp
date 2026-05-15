#include <monkey_dust/ai/behavior_tree.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/bt_components.h>
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/components/npc_memory.h>
#include <monkey_dust/ai/squad_signal.h>
#include <monkey_dust/ai/suspicious_item_group.h>
#include <monkey_dust/ai/fnv.h>
#include <cstring>

static constexpr uint32_t TARGET_ENTITY_BB_KEY            = md::fnv1a("target_entity");
static constexpr uint32_t NEXT_TARGET_ENTITY_BB_KEY       = md::fnv1a("next_target_entity");
static constexpr uint32_t HAS_SEARCHED_POS_BB_KEY         = md::fnv1a("has_searched_pos");
static constexpr uint32_t DONE_SUSPECT_MOVETO_BB_KEY      = md::fnv1a("done_suspect_moveto");
static constexpr uint32_t DONE_SYSTEMATIC_SEARCH_BB_KEY   = md::fnv1a("done_systematic_search");

// ── Stackless VM tick ─────────────────────────────────────────────────────────
BTStatus BehaviorTree::tick(md::EngineContext& ctx, entt::entity e, uint32_t nowMs) {
    if (!isValid()) return BTStatus::Failure;

    uint16_t pc     = m_root;
    BTStatus result = BTStatus::Running;

    static constexpr int VM_MAX_STEPS = 4096;
    int vm_steps = 0;

    while (true) {
        if (++vm_steps > VM_MAX_STEPS) break;
        if (pc == INVALID) break;

        BTNode&  nd = m_nodes[pc];
        BTState& st = m_state[pc];

        switch (nd.type) {

        case BTNodeType::Selector:
            if (result == BTStatus::Success) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            if (result == BTStatus::Failure) {
                st.currentChild++;
            } else {
                st.currentChild = 0;
            }
            if (st.currentChild >= nd.childCount) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            pc = m_children[nd.childStart + st.currentChild];
            result = BTStatus::Running; continue;

        case BTNodeType::Sequence:
            if (result == BTStatus::Failure) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            if (result == BTStatus::Success) {
                st.currentChild++;
            }
            if (st.currentChild >= nd.childCount) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            pc = m_children[nd.childStart + st.currentChild];
            result = BTStatus::Running; continue;

        case BTNodeType::Condition:
            result = nd.condition(ctx, e) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;

        case BTNodeType::Action:
            result = nd.action(ctx, e);
            if (result == BTStatus::Running) goto exit_loop;
            pc = nd.parent; continue;

        case BTNodeType::Inverter:
            if (result == BTStatus::Running) {
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            result = (result == BTStatus::Success) ? BTStatus::Failure : BTStatus::Success;
            pc = nd.parent; continue;

        case BTNodeType::Repeat:
            if (result == BTStatus::Running) {
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            if (result == BTStatus::Failure) {
                st.counter = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            st.counter++;
            if (nd.data > 0 && st.counter >= nd.data) {
                st.counter = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            if (nd.childCount > 0) {
                uint16_t ci = m_children[nd.childStart];
                m_state[ci] = BTState{};
                pc = ci; result = BTStatus::Running; continue;
            }
            pc = nd.parent; result = BTStatus::Success; continue;

        case BTNodeType::Wait:
            if (st.timer == 0)
                st.timer = nowMs + nd.data;
            if (static_cast<int32_t>(nowMs - st.timer) >= 0) {
                st.timer = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            result = BTStatus::Running;
            goto exit_loop;

        // ── M21 ──────────────────────────────────────────────────────────────

        case BTNodeType::Branch:
            if (result == BTStatus::Running) {
                // Re-evaluated every tick: persistent interrupt gate
                if (nd.childCount == 0 || (nd.condition && !nd.condition(ctx, e))) {
                    pc = nd.parent; result = BTStatus::Failure; continue;
                }
                pc = m_children[nd.childStart];
                result = BTStatus::Running; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is

        case BTNodeType::TimerStart: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t  tid    = static_cast<uint8_t>(nd.data >> 24);
                uint32_t dur_ms = nd.data & 0x00FFFFFFu;
                if (tid < MAX_AGENT_TIMERS) {
                    uint64_t new_end = static_cast<uint64_t>(nowMs) + dur_ms;
                    // C12: only_increase — don't shorten an already-running timer
                    if ((nd.flags & 0x01u) && as->timers[tid] > new_end) {
                        // existing deadline is later; leave it untouched
                    } else {
                        as->timers[tid] = new_end;
                    }
                }
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::TimerCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t  tid      = static_cast<uint8_t>(nd.data & 0x1Fu);
            if (tid >= MAX_AGENT_TIMERS) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint64_t deadline = as->timers[tid];
            if (deadline != 0u && static_cast<uint64_t>(nowMs) >= deadline) {
                as->timers[tid] = 0u;
                result = BTStatus::Success;
            } else {
                result = BTStatus::Failure;
            }
            pc = nd.parent; continue;
        }

        // Pattern 4: bit-index encoding (lcf::* constants, 0-63)
        case BTNodeType::FlagCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
            bool    is_set  = as->lcflags.test(bit_idx);
            result = (nd.flags == 0u ? is_set : !is_set)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::FlagSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
                if (nd.flags == 0u) as->lcflags.set  (bit_idx);
                else                as->lcflags.clear(bit_idx);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::SenseCheck: {
            SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t sense_idx = static_cast<uint8_t>(nd.data >> 24);
            float   threshold = static_cast<float>(nd.data & 0x00FFFFFFu) * 0.001f;
            if (sense_idx >= MAX_SENSES) sense_idx = 0u;
            result = (sc->activation[sense_idx] >= threshold)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // ── AI behavior patterns adaptations ───────────────────────────────────────

        // Pattern 1: check current motivation matches expected
        case BTNodeType::MotivationCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->motivation == static_cast<MotivationType>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Pattern 1: write motivation to agent state
        case BTNodeType::SetMotivation: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->motivation = static_cast<MotivationType>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Pattern 3: delegate to referenced tree
        case BTNodeType::Reference: {
            BehaviorTree* other = static_cast<BehaviorTree*>(nd._padding);
            if (!other) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = other->tick(ctx, e, nowMs);
            if (result == BTStatus::Running) goto exit_loop;
            pc = nd.parent; continue;
        }

        // Pattern 6: gauge threshold check
        case BTNodeType::GaugeCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto    gtype = static_cast<GaugeType>(nd.data >> 24);
            float   thr   = static_cast<float>(nd.data & 0x00FFFFFFu) * 0.001f;
            result = (as->gauges.get(gtype) >= thr)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Pattern 6: gauge write (set to fixed value)
        case BTNodeType::GaugeSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                auto  gtype = static_cast<GaugeType>(nd.data >> 24);
                float val   = static_cast<float>(nd.data & 0x00FFFFFFu) * 0.001f;
                as->gauges.set(gtype, val);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // ── AI-11–20 ─────────────────────────────────────────────────────

        // C11: SequenceStateless — always restarts from child 0 on re-entry
        case BTNodeType::SequenceStateless:
            if (result == BTStatus::Failure) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Failure; continue;
            }
            if (result == BTStatus::Success) {
                st.currentChild++;
            } else { // Running — stateless: reset to 0 every entry
                st.currentChild = 0;
            }
            if (st.currentChild >= nd.childCount) {
                st.currentChild = 0;
                pc = nd.parent; result = BTStatus::Success; continue;
            }
            pc = m_children[nd.childStart + st.currentChild];
            result = BTStatus::Running; continue;

        // C13: FrameFlag — read/write frame_flags (cleared each logic tick)
        case BTNodeType::FrameFlagCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
            bool    is_set  = (as->frame_flags >> bit_idx) & 1ull;
            result = (nd.flags == 0u ? is_set : !is_set)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::FrameFlagSet: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
                if (nd.flags == 0u) as->frame_flags |=  (1ull << bit_idx);
                else                as->frame_flags &= ~(1ull << bit_idx);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // C14: WeightedSelector — one-shot weighted child pick per entry
        case BTNodeType::WeightedSelector:
            if (result == BTStatus::Success || result == BTStatus::Failure) {
                st.currentChild = 0;
                pc = nd.parent; continue; // propagate child result as-is
            }
            // First entry (Running): pick weighted random child
            {
                if (nd.childCount == 0) {
                    pc = nd.parent; result = BTStatus::Failure; continue;
                }
                uint8_t w[4] = {
                    static_cast<uint8_t>( nd.data        & 0xFFu),
                    static_cast<uint8_t>((nd.data >>  8) & 0xFFu),
                    static_cast<uint8_t>((nd.data >> 16) & 0xFFu),
                    static_cast<uint8_t>((nd.data >> 24) & 0xFFu),
                };
                // LCG step seeded by entity id XOR frame_index
                uint32_t rng = static_cast<uint32_t>(static_cast<uint64_t>(e))
                               ^ ctx.frame_index;
                rng = rng * 1664525u + 1013904223u;
                uint8_t  nc    = nd.childCount < 4u ? static_cast<uint8_t>(nd.childCount) : 4u;
                uint32_t total = 0;
                for (uint8_t ii = 0; ii < nc; ++ii) total += w[ii];
                if (total == 0u) total = 1u;
                uint32_t roll    = rng % total;
                uint8_t  chosen  = nc - 1u;
                uint32_t acc     = 0u;
                for (uint8_t ii = 0; ii < nc; ++ii) {
                    acc += w[ii];
                    if (roll < acc) { chosen = ii; break; }
                }
                st.currentChild = chosen;
                pc = m_children[nd.childStart + (chosen % nd.childCount)];
                result = BTStatus::Running; continue;
            }

        // C15: AwarenessCheck
        case BTNodeType::AwarenessCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->awareness == static_cast<AwarenessState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // C16: AlertnessCheck
        case BTNodeType::AlertnessCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->alertness == static_cast<AlertnessState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // C17: MoodCheck
        case BTNodeType::MoodCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->mood == static_cast<NpcMood>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // C18: Role coordination
        case BTNodeType::RoleCheck: {
            auto  role    = static_cast<NpcRole>(nd.data >> 8);
            bool  do_could = (nd.data & 0xFFu) != 0u;
            bool  ok = do_could ? RoleRegistry::Get().could_perform(role)
                                : RoleRegistry::Get().is_performing(role, e);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::RoleClaim: {
            auto     role     = static_cast<NpcRole>(nd.data & 0xFFu);
            uint32_t query_id = nd.data >> 8;
            // Prefer dynamic query_id from DirectorHintComponent when the hint
            // targets this same role — scopes the claim to the Director's session.
            if (const DirectorHintComponent* hint =
                    Registry::Get().try_get<DirectorHintComponent>(e)) {
                if (hint->role_pending && hint->suggested_role == role)
                    query_id = hint->query_id;
            }
            result = RoleRegistry::Get().claim(role, query_id, e)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::RoleRelease: {
            auto role = static_cast<NpcRole>(nd.data);
            RoleRegistry::Get().release(role, e);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // C19: WithdrawState
        case BTNodeType::WithdrawCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->withdraw_state == static_cast<WithdrawState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::SetWithdraw: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->withdraw_state = static_cast<WithdrawState>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Echo NpcMemory nodes
        case BTNodeType::MemoryCheck: {
            NpcMemoryComponent* mem = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (!mem) { result = BTStatus::Failure; pc = nd.parent; continue; }
            bool ok = (nd.data == 0) ? (mem->spatial_count > 0) : (mem->event_count > 0);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::MemoryForget: {
            NpcMemoryComponent* mem = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (mem) mem->ClearAll();
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::AreaSweepCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (static_cast<uint8_t>(as->area_sweep_type) == static_cast<uint8_t>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // MD_gemini: DecoratorPercentage — run child with data% probability
        case BTNodeType::DecoratorPercentage:
            if (result == BTStatus::Running) {
                // LCG seeded by entity XOR frame_index (same pattern as WeightedSelector)
                uint32_t rng = static_cast<uint32_t>(static_cast<uint64_t>(e)) ^ ctx.frame_index;
                rng = rng * 1664525u + 1013904223u;
                uint8_t pct = static_cast<uint8_t>(nd.data & 0xFFu);
                if ((rng % 100u) < pct && nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is

        // MD_gemini: SelectorPercentage — pick one random child, return its result
        case BTNodeType::SelectorPercentage:
            if (result == BTStatus::Success || result == BTStatus::Failure) {
                pc = nd.parent; continue;  // propagate child result
            }
            {
                if (nd.childCount == 0) { result = BTStatus::Failure; pc = nd.parent; continue; }
                uint32_t rng = static_cast<uint32_t>(static_cast<uint64_t>(e)) ^ ctx.frame_index;
                rng = rng * 1664525u + 1013904223u;
                uint16_t chosen = static_cast<uint16_t>(rng % nd.childCount);
                pc = m_children[nd.childStart + chosen];
                result = BTStatus::Running; continue;
            }

        // MD_gemini: SenseTimeCheck — Success if sense was triggered recently
        case BTNodeType::SenseTimeCheck: {
            auto* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t  sense_idx      = static_cast<uint8_t>((nd.data >> 24) & 0xFFu);
            uint32_t max_elapsed_ms = nd.data & 0x00FFFFFFu;
            if (sense_idx >= MAX_SENSES) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint32_t elapsed = nowMs - sc->last_activated_ms[sense_idx];
            result = (elapsed <= max_elapsed_ms) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // MD_gemini: ActionSetDead / ActionDespawn — deferred lifecycle via lcf flags
        case BTNodeType::ActionSetDead: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            as->lcflags.set(lcf::IS_DEAD);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        case BTNodeType::ActionDespawn: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            as->lcflags.set(lcf::SHOULD_DESPAWN);
            result = BTStatus::Success; pc = nd.parent; continue;
        }

        // MD_deepseek: AggroLevelCheck / SetAggroLevel
        case BTNodeType::AggroLevelCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->aggro_level == static_cast<NpcAggroLevel>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::SetAggroLevel: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->aggro_level = static_cast<NpcAggroLevel>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // MD_deepseek: NpcCombatStateCheck / SetNpcCombatState
        case BTNodeType::NpcCombatStateCheck: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (as->combat_state == static_cast<NpcCombatState>(nd.data))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::SetNpcCombatState: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->combat_state = static_cast<NpcCombatState>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // ── MD_z VM cases ─────────────────────────────────────────────────

        // Z1: DecoratorMood — run child only when as->mood matches
        case BTNodeType::DecoratorMood: {
            if (result == BTStatus::Running) {
                AgentState* as = Registry::Get().try_get<AgentState>(e);
                if (!as || as->mood != static_cast<NpcMood>(nd.data)) {
                    result = BTStatus::Failure; pc = nd.parent; continue;
                }
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is
        }

        // Z2: DecoratorAwareness — run child only when as->awareness matches
        case BTNodeType::DecoratorAwareness: {
            if (result == BTStatus::Running) {
                AgentState* as = Registry::Get().try_get<AgentState>(e);
                if (!as || as->awareness != static_cast<AwarenessState>(nd.data)) {
                    result = BTStatus::Failure; pc = nd.parent; continue;
                }
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is
        }

        // Z3: DecoratorTimerAuto — start timer on every entry, propagate child result
        case BTNodeType::DecoratorTimerAuto: {
            if (result == BTStatus::Running) {
                AgentState* as = Registry::Get().try_get<AgentState>(e);
                if (as) {
                    uint8_t  tid    = static_cast<uint8_t>(nd.data >> 24);
                    uint32_t dur_ms = nd.data & 0x00FFFFFFu;
                    if (tid < MAX_AGENT_TIMERS) {
                        uint64_t new_end = static_cast<uint64_t>(nowMs) + dur_ms;
                        if ((nd.flags & 0x01u) && as->timers[tid] > new_end) {
                            // only_increase: leave longer deadline intact
                        } else {
                            as->timers[tid] = new_end;
                        }
                    }
                }
                if (nd.childCount > 0) {
                    pc = m_children[nd.childStart];
                    result = BTStatus::Running; continue;
                }
                result = BTStatus::Success; pc = nd.parent; continue;
            }
            pc = nd.parent; continue;  // propagate child result as-is
        }

        // Z4: ActionTimerRandom — start timer with LCG-random duration [min_ms, max_ms]
        case BTNodeType::ActionTimerRandom: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t  tid   = static_cast<uint8_t>(nd.data >> 24);
                uint32_t max_u = (nd.data >> 12) & 0xFFFu;   // 12 bits, unit=100ms
                uint32_t min_u =  nd.data        & 0xFFFu;
                if (tid < MAX_AGENT_TIMERS) {
                    uint32_t range = (max_u > min_u) ? (max_u - min_u) : 0u;
                    uint32_t rng   = static_cast<uint32_t>(static_cast<uint64_t>(e)) ^ nowMs;
                    rng = rng * 1664525u + 1013904223u;
                    uint32_t dur_u   = min_u + (range > 0u ? rng % range : 0u);
                    uint64_t new_end = static_cast<uint64_t>(nowMs)
                                     + static_cast<uint64_t>(dur_u) * 100u;
                    as->timers[tid] = new_end;
                }
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Z5: ActionSquadNotify — broadcast SquadSignal to entity's squad channel
        case BTNodeType::ActionSquadNotify: {
            SquadMemberComponent* sm = Registry::Get().try_get<SquadMemberComponent>(e);
            if (sm) {
                auto sig = static_cast<SquadSignal>(nd.data & 0xFFu);
                uint32_t raw_id = static_cast<uint32_t>(static_cast<uint64_t>(e) & 0xFFFFFFFFu);
                SquadSignalBus::Get().Set(sm->squad_id, sig, raw_id, nowMs);
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Z6: ConditionSquadSignal — check squad channel for expected signal
        case BTNodeType::ConditionSquadSignal: {
            SquadMemberComponent* sm = Registry::Get().try_get<SquadMemberComponent>(e);
            if (!sm) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto expected = static_cast<SquadSignal>(nd.data & 0xFFu);
            result = (SquadSignalBus::Get().GetSignal(sm->squad_id) == expected)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Z7: ConditionAnySenseWithinTime — any (or specific) sense fired within time_ms
        case BTNodeType::ConditionAnySenseWithinTime: {
            SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint32_t time_ms  = nd.data & 0x0FFFFFFFu;
            uint8_t  sense_idx= static_cast<uint8_t>((nd.data >> 28) & 0xFu);
            bool     specific = (nd.flags != 0u);
            bool     ok       = false;
            if (specific) {
                uint32_t ts = sc->last_activated_ms[sense_idx];
                ok = (ts != 0u && static_cast<uint32_t>(nowMs) - ts <= time_ms);
            } else {
                for (uint8_t ii = 0; ii < MAX_SENSES && !ok; ++ii) {
                    uint32_t ts = sc->last_activated_ms[ii];
                    ok = (ts != 0u && static_cast<uint32_t>(nowMs) - ts <= time_ms);
                }
            }
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Z8: ActionExpireTimer — immediately mark timer slot as expired
        case BTNodeType::ActionExpireTimer: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) {
                uint8_t tid = static_cast<uint8_t>(nd.data & 0x1Fu);
                if (tid < MAX_AGENT_TIMERS)
                    as->timers[tid] = 1u;  // past-deadline: nowMs >= 1 always true
            }
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // Z9: TargetFlagCheck — check lcflags on target entity from blackboard
        case BTNodeType::TargetFlagCheck: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* en = bb_find(*ab, TARGET_ENTITY_BB_KEY);
            if (!en) { result = BTStatus::Failure; pc = nd.parent; continue; }
            entt::entity target = static_cast<entt::entity>(en->val.e);
            AgentState*  as_t   = Registry::Get().try_get<AgentState>(target);
            if (!as_t) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t bit_idx = static_cast<uint8_t>(nd.data & 0x3Fu);
            bool    is_set  = as_t->lcflags.test(bit_idx);
            result = (nd.flags == 0u ? is_set : !is_set)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // Z10: SetLocomotionState — write as->locomotion_state
        case BTNodeType::SetLocomotionState: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (as) as->locomotion_state = static_cast<LocomotionState>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // MD_arch P4: DecoratorNamedBranch — gate child on NamedBranchRegistry
        case BTNodeType::DecoratorNamedBranch: {
            if (result == BTStatus::Running) {
                // first entry: check branch
                bool active   = NamedBranchRegistry::Get().IsActive(nd.data);
                bool inverted = (nd.flags & 1u) != 0u;
                bool gate_ok  = inverted ? !active : active;
                if (!gate_ok) { result = BTStatus::Failure; pc = nd.parent; continue; }
                if (nd.childStart == INVALID || nd.childCount == 0) {
                    result = BTStatus::Success; pc = nd.parent; continue;
                }
                pc = m_children[nd.childStart]; continue;
            }
            // returning from child — propagate as-is
            pc = nd.parent; continue;
        }

        // G1: ActionIdleTime — wait duration_ms like Wait but named for MD clarity
        case BTNodeType::ActionIdleTime: {
            BTState& st = m_state[pc];
            if (st.timer == 0u)
                st.timer = nowMs + nd.data;
            if (static_cast<int32_t>(nowMs - st.timer) >= 0) {
                st.timer = 0u;
                result = BTStatus::Success;
            } else {
                result = BTStatus::Running;
            }
            pc = nd.parent; continue;
        }

        // G2: ConditionHaveTarget — Success if "target_entity" bb key holds a valid entity
        case BTNodeType::ConditionHaveTarget: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* en = bb_find(*ab, TARGET_ENTITY_BB_KEY);
            if (!en) { result = BTStatus::Failure; pc = nd.parent; continue; }
            entt::entity tgt = static_cast<entt::entity>(en->val.e);
            result = Registry::Get().valid(tgt) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // G3: ConditionHaveNextTarget — Success if "next_target_entity" bb key holds valid entity
        case BTNodeType::ConditionHaveNextTarget: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* en = bb_find(*ab, NEXT_TARGET_ENTITY_BB_KEY);
            if (!en) { result = BTStatus::Failure; pc = nd.parent; continue; }
            entt::entity tgt = static_cast<entt::entity>(en->val.e);
            result = Registry::Get().valid(tgt) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // G4: ActionTriggerSound — fire NpcSoundBus event; always Success
        case BTNodeType::ActionTriggerSound: {
            NpcSoundBus::Get().Fire(static_cast<NpcSoundEvent>(nd.data),
                                    static_cast<uint32_t>(static_cast<uint64_t>(e) & 0xFFFFFFFFu));
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // G5: ConditionIsSenseActivationAbove — current activation vs qualifier threshold
        case BTNodeType::ConditionIsSenseActivationAbove: {
            SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t sense_idx = static_cast<uint8_t>((nd.data >> 8) & 0xFu);
            auto    q         = static_cast<SenseThresholdQualifier>(nd.data & 0xFFu);
            float   act       = sc->activation[sense_idx];
            bool    ok        = false;
            switch (q) {
                case SenseThresholdQualifier::Trace:     ok = (act > 0.0f);               break;
                case SenseThresholdQualifier::Lower:     ok = (act >= sc->threshold_lo);  break;
                case SenseThresholdQualifier::Activated: ok = (act >= sc->threshold_hi);  break;
                case SenseThresholdQualifier::Upper:     ok = (act >= 1.0f);              break;
            }
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // G6: ConditionHasAnySenseBeenAbove — any sense ever reached qualifier (last_activated_ms set)
        case BTNodeType::ConditionHasAnySenseBeenAbove: {
            SenseComponent* sc = Registry::Get().try_get<SenseComponent>(e);
            if (!sc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto q = static_cast<SenseThresholdQualifier>(nd.data & 0xFFu);
            bool ok = false;
            for (uint8_t ii = 0; ii < MAX_SENSES && !ok; ++ii) {
                switch (q) {
                    case SenseThresholdQualifier::Trace:
                    case SenseThresholdQualifier::Lower:
                        ok = (sc->activation[ii] > 0.0f); break;
                    case SenseThresholdQualifier::Activated:
                    case SenseThresholdQualifier::Upper:
                        ok = (sc->last_activated_ms[ii] != 0u); break;
                }
            }
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // G7: ActionSuspiciousItemDoneStage — stamp investigation_stage on most-recent event
        case BTNodeType::ActionSuspiciousItemDoneStage: {
            NpcMemoryComponent* nmc = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (!nmc || nmc->event_count == 0) { result = BTStatus::Failure; pc = nd.parent; continue; }
            nmc->events[nmc->event_count - 1].investigation_stage =
                static_cast<SuspiciousItemStage>(nd.data);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // G8: ConditionSuspiciousItemBTPriority — any event intensity >= min_intensity
        case BTNodeType::ConditionSuspiciousItemBTPriority: {
            NpcMemoryComponent* nmc = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (!nmc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t min_i = static_cast<uint8_t>(nd.data & 0xFFu);
            bool    ok    = false;
            for (uint8_t ii = 0; ii < nmc->event_count && !ok; ++ii)
                ok = (nmc->events[ii].intensity >= min_i);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // G9: ConditionLastTimeSquadNotified — Success if squad channel signal matches and is recent
        case BTNodeType::ConditionLastTimeSquadNotified: {
            SquadMemberComponent* sm = Registry::Get().try_get<SquadMemberComponent>(e);
            if (!sm) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto    expected  = static_cast<SquadSignal>((nd.data >> 8) & 0xFFu);
            uint8_t thresh_s  = static_cast<uint8_t>(nd.data & 0xFFu);
            const SquadChannel& ch = SquadSignalBus::Get().GetChannel(sm->squad_id);
            bool ok = (ch.signal == expected) &&
                      (static_cast<uint32_t>(nowMs) - ch.timestamp) / 1000u <= static_cast<uint32_t>(thresh_s);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // G10: DecoratorAggressionEscalation — pass through if aggro_level != None
        case BTNodeType::DecoratorAggressionEscalation: {
            if (result == BTStatus::Running) {
                AgentState* as = Registry::Get().try_get<AgentState>(e);
                if (!as || as->aggro_level == NpcAggroLevel::None) {
                    result = BTStatus::Failure; pc = nd.parent; continue;
                }
                if (nd.childStart == INVALID || nd.childCount == 0) {
                    result = BTStatus::Success; pc = nd.parent; continue;
                }
                pc = m_children[nd.childStart]; continue;
            }
            pc = nd.parent; continue;
        }

        // ── Batch 2: MD BEHAVIOR XML patterns ────────────────────────────────

        case BTNodeType::ConditionIsDead: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = as->lcflags.test(lcf::IS_DEAD) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionIsInVent: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = as->lcflags.test(lcf::IS_IN_VENT) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionCanBreakout: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            bool ok = as->lcflags.test(lcf::SHOULD_BREAKOUT) &&
                     !as->lcflags.test(lcf::DONE_BREAKOUT);
            result = ok ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionIsBackstage: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = as->lcflags.test(lcf::CLOSE_TO_BACKSTAGE) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionIsPartOfNPCGroup: {
            result = Registry::Get().try_get<SquadMemberComponent>(e)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionAnotherAlienIsAttacking: {
            // Stalk = closest role to "attacking/pursuing"; Flanker = second approach vector
            const auto& rr = RoleRegistry::Get();
            bool another_stalk   = !rr.could_perform(NpcRole::Stalk)   &&
                                   !rr.is_performing(NpcRole::Stalk,   e);
            bool another_flanker = !rr.could_perform(NpcRole::Flanker) &&
                                   !rr.is_performing(NpcRole::Flanker, e);
            result = (another_stalk || another_flanker) ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionHasSearchedPos: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = bb_get_bool(*ab, HAS_SEARCHED_POS_BB_KEY)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionHasDoneSuspectMoveTo: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = bb_get_bool(*ab, DONE_SUSPECT_MOVETO_BB_KEY)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ActionSwitchToNextTarget: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (!ab) { result = BTStatus::Failure; pc = nd.parent; continue; }
            const BlackboardEntry* next = bb_find(*ab, NEXT_TARGET_ENTITY_BB_KEY);
            if (!next) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint32_t next_val = next->val.e;
            // Write target_entity = next entity; clear next_target_entity (type=3 = entity slot)
            if (BlackboardEntry* tgt = bb_insert(*ab, TARGET_ENTITY_BB_KEY, 3))
                tgt->val.e = next_val;
            if (BlackboardEntry* nxt = bb_insert(*ab, NEXT_TARGET_ENTITY_BB_KEY, 3))
                nxt->val.e = static_cast<uint32_t>(entt::tombstone);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        case BTNodeType::ActionDoneSystematicSearch: {
            AgentBlackboard* ab = Registry::Get().try_get<AgentBlackboard>(e);
            if (ab) bb_set_bool(*ab, DONE_SYSTEMATIC_SEARCH_BB_KEY, true);
            result = BTStatus::Success;
            pc = nd.parent; continue;
        }

        // ── Batch 4: EventOrder ───────────────────────────────────────────────

        case BTNodeType::ConditionEventAOccuredAfterB: {
            AgentState* as = Registry::Get().try_get<AgentState>(e);
            if (!as) { result = BTStatus::Failure; pc = nd.parent; continue; }
            uint8_t ea = static_cast<uint8_t>((nd.data >> 8) & 0x3u);
            uint8_t eb = static_cast<uint8_t>(nd.data & 0x3u);
            if (ea >= MAX_EVENT_TYPES || eb >= MAX_EVENT_TYPES) {
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            // A must have occurred (ts != 0) and be strictly newer than B
            result = (as->event_ts[ea] != 0u && as->event_ts[ea] > as->event_ts[eb])
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        // ── Batch 5: Squad extensions ─────────────────────────────────────────

        case BTNodeType::ConditionSquadDoingEscalation: {
            SquadMemberComponent* smc = Registry::Get().try_get<SquadMemberComponent>(e);
            if (!smc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (SquadSignalBus::Get().GetSignal(smc->squad_id) == SquadSignal::Escalating)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionSquadDoingSuspiciousWarning: {
            SquadMemberComponent* smc = Registry::Get().try_get<SquadMemberComponent>(e);
            if (!smc) { result = BTStatus::Failure; pc = nd.parent; continue; }
            result = (SquadSignalBus::Get().GetSignal(smc->squad_id) == SquadSignal::SuspiciousWarn)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::DecoratorSquadSearch:
            if (result == BTStatus::Running) {
                SquadMemberComponent* smc = Registry::Get().try_get<SquadMemberComponent>(e);
                if (smc) SquadSignalBus::Get().Set(smc->squad_id,
                                                   SquadSignal::Warning,
                                                   static_cast<uint32_t>(static_cast<uint64_t>(e)),
                                                   nowMs);
                if (nd.childCount > 0) { pc = m_children[nd.childStart]; result = BTStatus::Running; continue; }
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            pc = nd.parent; continue; // propagate child result

        // ── Batch 6: SuspiciousItem Group system ──────────────────────────────

        case BTNodeType::ConditionSuspiciousItemShouldDoStage: {
            NpcMemoryComponent* nm = Registry::Get().try_get<NpcMemoryComponent>(e);
            if (!nm || nm->event_count == 0) { result = BTStatus::Failure; pc = nd.parent; continue; }
            auto expected = static_cast<SuspiciousItemStage>(nd.data & 0xFFu);
            result = (nm->events[0].investigation_stage == expected)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionSuspiciousItemIsWithinDistance: {
            NpcMemoryComponent* nm = Registry::Get().try_get<NpcMemoryComponent>(e);
            SenseComponent*     sc = Registry::Get().try_get<SenseComponent>(e);
            if (!nm || nm->event_count == 0 || !sc) {
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            float max_m  = static_cast<float>(nd.data) * 0.01f;
            float dx     = sc->last_known_x - nm->events[0].x;
            float dz     = sc->last_known_z - nm->events[0].z;
            result = (dx * dx + dz * dz <= max_m * max_m)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionSuspiciousItemFirstGroupMember: {
            SuspiciousItemGroupComponent* gc = Registry::Get().try_get<SuspiciousItemGroupComponent>(e);
            if (!gc || gc->group_id == SuspiciousItemGroupComponent::INVALID_GROUP) {
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            result = SuspiciousItemGroupRegistry::Get().IsFirstMember(
                         gc->group_id, static_cast<uint32_t>(static_cast<uint64_t>(e)))
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionSuspiciousItemGroupAllowedToProgress: {
            SuspiciousItemGroupComponent* gc = Registry::Get().try_get<SuspiciousItemGroupComponent>(e);
            if (!gc || gc->group_id == SuspiciousItemGroupComponent::INVALID_GROUP) {
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            result = SuspiciousItemGroupRegistry::Get().IsAllowedToProgress(gc->group_id)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionSuspiciousItemGroupMembersRoutingTo: {
            SuspiciousItemGroupComponent* gc = Registry::Get().try_get<SuspiciousItemGroupComponent>(e);
            if (!gc || gc->group_id == SuspiciousItemGroupComponent::INVALID_GROUP) {
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            result = SuspiciousItemGroupRegistry::Get().AllRoutingTo(gc->group_id)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

        case BTNodeType::ConditionSuspiciousItemWaitForGroupRouting: {
            SuspiciousItemGroupComponent* gc = Registry::Get().try_get<SuspiciousItemGroupComponent>(e);
            if (!gc || gc->group_id == SuspiciousItemGroupComponent::INVALID_GROUP) {
                result = BTStatus::Failure; pc = nd.parent; continue;
            }
            result = SuspiciousItemGroupRegistry::Get().WaitingForRouting(gc->group_id)
                     ? BTStatus::Success : BTStatus::Failure;
            pc = nd.parent; continue;
        }

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

        default:
            goto exit_loop;
        }
    }

exit_loop:
    return result;
}
