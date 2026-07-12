#pragma once
// Hierarchy:: — keeps ParentRef/ChildrenRef (components/hierarchy.h)
// consistent on both sides, and guards against dangling references when
// either side of the relation is destroyed. See hierarchy.h for scope and
// confirmed use cases (carry unconscious NPC / lead captive).
#include <monkey_dust/ecs/md_entity.h>

namespace Hierarchy {

// Call once at startup (before any SetParent call). Wires on_destroy hooks
// so destroying a parent clears ParentRef on all its former children, and
// destroying a child removes it from its former parent's ChildrenRef —
// without this, a destroyed entity leaves a stale MdEntity reference on
// the other side of the relation.
void RegisterDestroyHooks();

// Detaches child from any existing parent, then attaches it to parent
// (creates/updates ParentRef on child, ChildrenRef on parent). Returns
// false if parent's ChildrenRef is already full (HIERARCHY_MAX_CHILDREN) —
// child is left unparented in that case, caller decides how to handle it
// (e.g. refuse to pick up a second body).
bool SetParent(MdEntity child, MdEntity parent);

// Detaches child from its current parent, if any. No-op if child has no
// ParentRef.
void ClearParent(MdEntity child);

} // namespace Hierarchy
