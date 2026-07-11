#pragma once
// DirtyTracker<T> — change-detection over EnTT (ARCHITECTURE_IDEAS.md #3).
// Wraps entt::registry's on_construct<T>/on_update<T> signals into a
// per-frame "which entities of T changed" set, so a system can skip work
// for entities whose T is unchanged since the last ForEachDirty() + Clear().
//
// CRITICAL CAVEAT (found while wiring this up — read before using):
// on_update<T> fires ONLY when a write goes through registry.patch<T>(e) or
// registry.emplace_or_replace<T>(e, ...). It does NOT fire when code takes a
// reference via get<T>(e) or a view.each() callback and mutates it directly
// — which is the prevailing write pattern for hot components in this
// codebase (e.g. WorldTransform is mutated by direct reference throughout
// game/src/logic_tick.cpp's TickPhysics/TickNavigation). Retrofitting
// DirtyTracker onto an already-by-reference-mutated component means finding
// and converting EVERY write site to patch()/emplace_or_replace() — miss one
// and that write silently never marks dirty, which is a hard-to-notice
// staleness bug, not a crash. Only wire this onto a component whose writes
// already funnel through one or few controlled call sites, or onto new code
// written from the start to call MarkDirty()/patch() explicitly.
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/ecs/md_registry.h>
#include <entt/entt.hpp>

template <typename T>
class DirtyTracker {
public:
    // Call once at startup (or lazily via EnsureConnected()) to start tracking T.
    static void Connect() {
        auto& self = Instance();
        if (self.connected_) return;
        auto& reg = MdRegistry::Get().Raw();
        reg.on_construct<T>().template connect<&DirtyTracker<T>::OnChanged>();
        reg.on_update<T>().template connect<&DirtyTracker<T>::OnChanged>();
        self.connected_ = true;
    }

    // Explicit mark for code paths that mutate T by direct reference and
    // can't rely on patch()/emplace_or_replace() firing on_update.
    static void MarkDirty(MdEntity e) { Instance().Add(e); }

    static bool IsDirty(MdEntity e) {
        auto& self = Instance();
        for (int i = 0; i < self.count_; ++i) if (self.dirty_[i] == e) return true;
        return false;
    }

    static int Count() { return Instance().count_; }
    static MdEntity At(int i) { return Instance().dirty_[i]; }

    // Call after a system has consumed this tick's dirty set.
    static void Clear() { Instance().count_ = 0; }

    static constexpr int MAX_DIRTY = 1024;

private:
    static DirtyTracker& Instance() { static DirtyTracker inst; return inst; }

    // Signature dictated by EnTT's on_construct/on_update signal contract —
    // stays entt::registry&/entt::entity, not MdRegistry&/MdEntity (same
    // reasoning as BTSystem::ConnectRegistry — see md_entity.h's header
    // comment).
    static void OnChanged(entt::registry&, entt::entity e) { Instance().Add(MdEntity(e)); }

    void Add(MdEntity e) {
        if (count_ >= MAX_DIRTY) return;  // budget hit — caller falls back to full scan
        for (int i = 0; i < count_; ++i) if (dirty_[i] == e) return;  // already marked this tick
        dirty_[count_++] = e;
    }

    MdEntity dirty_[MAX_DIRTY] = {};
    int      count_            = 0;
    bool     connected_        = false;
};
