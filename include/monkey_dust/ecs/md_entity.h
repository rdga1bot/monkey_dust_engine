#pragma once
#include <entt/entt.hpp>

// MdEntity — task #8 (EnTT->flecs strangler-fig migration), part B1 step 1.
//
// For now a plain alias for entt::entity: zero behavior change. Every
// existing entt::entity call site (entt::null comparisons, entt::to_integral,
// Registry::Get() calls, storage as entt::entity locals) keeps compiling
// byte-for-byte identically, because MdEntity IS entt::entity, not a
// distinct wrapper type — a function taking entt::entity satisfies a
// BTActionFunc/BTConditionFunc parameter typed MdEntity with zero changes.
//
// This is intentionally NOT yet an opaque wrapper. The seam is introduced
// first (rename the BT leaf-function signature type used across engine/
// game/tests to MdEntity), then hardened into a real wrapper class in a
// later step — at which point the compiler will flag every remaining raw
// entt:: reference in a file using MdEntity, giving a precise,
// compiler-verified checklist instead of a hand-audited one.
using MdEntity = entt::entity;
