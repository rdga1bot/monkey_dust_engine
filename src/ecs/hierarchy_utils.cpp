#include <monkey_dust/ecs/hierarchy_utils.h>
#include <monkey_dust/components/hierarchy.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/ecs/md_registry.h>

namespace Hierarchy {

static void RemoveFromChildrenList(MdRegistry& reg, MdEntity parent, MdEntity child) {
    if (!reg.Valid(parent) || !reg.AllOf<ChildrenRef>(parent)) return;
    auto& cr = reg.Get<ChildrenRef>(parent);
    for (int i = 0; i < cr.count; ++i) {
        if (cr.children[i] == child) {
            cr.children[i] = cr.children[cr.count - 1];
            --cr.count;
            return;
        }
    }
}

void ClearParent(MdEntity child) {
    auto& reg = MdRegistry::Get();
    if (!reg.AllOf<ParentRef>(child)) return;
    MdEntity parent = reg.Get<ParentRef>(child).parent;
    RemoveFromChildrenList(reg, parent, child);
    reg.Remove<ParentRef>(child);
}

bool SetParent(MdEntity child, MdEntity parent) {
    auto& reg = MdRegistry::Get();
    ClearParent(child);  // detach from any previous parent first

    if (!reg.Handle(parent).has<ChildrenRef>()) reg.Handle(parent).emplace<ChildrenRef>();
    auto& cr = reg.Get<ChildrenRef>(parent);
    if (cr.count >= HIERARCHY_MAX_CHILDREN) return false;
    cr.children[cr.count++] = child;
    reg.Handle(child).set<ParentRef>(ParentRef{parent});
    return true;
}

// Parent destroyed (or ChildrenRef removed) -> every child loses its
// ParentRef. Fires before the component data is actually erased, so
// reading cr here is valid (flecs OnRemove contract — verified empirically,
// same guarantee EnTT's on_destroy made).
static void OnChildrenRefDestroyed(flecs::entity parent, ChildrenRef& cr) {
    auto& reg = MdRegistry::Get();
    for (int i = 0; i < cr.count; ++i) {
        MdEntity child = cr.children[i];
        if (reg.Valid(child) && reg.AllOf<ParentRef>(child)) reg.Remove<ParentRef>(child);
    }
    (void)parent;
}

// Child destroyed (or ParentRef removed) -> remove it from its parent's
// ChildrenRef so the slot doesn't reference a dangling entity.
static void OnParentRefDestroyed(flecs::entity child, ParentRef& pr) {
    RemoveFromChildrenList(MdRegistry::Get(), pr.parent, MdEntity(child.id()));
}

void RegisterDestroyHooks() {
    auto& w = MdRegistry::Get().Raw();
    w.observer<ChildrenRef>().event(flecs::OnRemove).each(OnChildrenRefDestroyed);
    w.observer<ParentRef>().event(flecs::OnRemove).each(OnParentRefDestroyed);
}

} // namespace Hierarchy
