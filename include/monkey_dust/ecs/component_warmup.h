#pragma once

namespace md {

// ── WarmUpEngineComponents ────────────────────────────────────────────────────
// Call ONCE at startup, right after ECS init, BEFORE any JobGraph batch runs
// (main.cpp's InitGame, alongside RegisterCoreComponents()).
//
// Why: flecs lazily registers a component's type the first time it's ever
// touched (emplace/set/get_mut/try_get_mut/add) anywhere in the process.
// That registration is a structural world operation (allocates a new low
// entity id for the component descriptor) — flecs disallows it while the
// world is in world.readonly_begin(true) (multithreaded) mode, which is
// exactly the mode job_graph.cpp wraps EVERY batch in, unconditionally.
// If a component's FIRST-EVER use in the whole process happens to occur
// from inside a JobGraph batch, flecs aborts (ecs_new_low_id's
// EcsWorldMultiThreaded check) in debug builds — and silently does
// something unspecified in release builds, where NDEBUG compiles the
// guarding ecs_assert/ecs_check away.
//
// Root-caused via tests/behavior/test_skill_xp_staged_grant.cpp's
// FreshEntity_FirstBatch_NoOpsCleanly_NoPartialCorruption test (SkillXpAccum
// was the first component in the whole test binary to be touched from
// inside a staged batch) — see task #248. Touching every real component
// type here once, on the main (non-staged) world, makes any batch's lazy
// registration a no-op regardless of which component is used first.
void WarmUpEngineComponents();

}  // namespace md
