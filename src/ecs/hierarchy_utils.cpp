#include <monkey_dust/ecs/hierarchy_utils.h>
#include <monkey_dust/components/hierarchy.h>
#include <monkey_dust/ecs/registry.h>

namespace Hierarchy {

static void RemoveFromChildrenList(entt::registry& reg, entt::entity parent, entt::entity child) {
    if (!reg.valid(parent) || !reg.all_of<ChildrenRef>(parent)) return;
    auto& cr = reg.get<ChildrenRef>(parent);
    for (int i = 0; i < cr.count; ++i) {
        if (cr.children[i] == child) {
            cr.children[i] = cr.children[cr.count - 1];
            --cr.count;
            return;
        }
    }
}

void ClearParent(entt::entity child) {
    auto& reg = Registry::Get();
    if (!reg.all_of<ParentRef>(child)) return;
    entt::entity parent = reg.get<ParentRef>(child).parent;
    RemoveFromChildrenList(reg, parent, child);
    reg.remove<ParentRef>(child);
}

bool SetParent(entt::entity child, entt::entity parent) {
    auto& reg = Registry::Get();
    ClearParent(child);  // detach from any previous parent first

    auto& cr = reg.get_or_emplace<ChildrenRef>(parent);
    if (cr.count >= HIERARCHY_MAX_CHILDREN) return false;
    cr.children[cr.count++] = child;
    reg.emplace_or_replace<ParentRef>(child, ParentRef{parent});
    return true;
}

// Parent destroyed (or ChildrenRef removed) -> every child loses its
// ParentRef. Fires before the component data is actually erased, so
// reading cr here is valid (EnTT on_destroy contract).
static void OnChildrenRefDestroyed(entt::registry& r, entt::entity parent) {
    auto& cr = r.get<ChildrenRef>(parent);
    for (int i = 0; i < cr.count; ++i) {
        entt::entity child = cr.children[i];
        if (r.valid(child) && r.all_of<ParentRef>(child)) r.remove<ParentRef>(child);
    }
}

// Child destroyed (or ParentRef removed) -> remove it from its parent's
// ChildrenRef so the slot doesn't reference a dangling entity.
static void OnParentRefDestroyed(entt::registry& r, entt::entity child) {
    entt::entity parent = r.get<ParentRef>(child).parent;
    RemoveFromChildrenList(r, parent, child);
}

void RegisterDestroyHooks() {
    auto& reg = Registry::Get();
    reg.on_destroy<ChildrenRef>().connect<&OnChildrenRefDestroyed>();
    reg.on_destroy<ParentRef>().connect<&OnParentRefDestroyed>();
}

} // namespace Hierarchy
