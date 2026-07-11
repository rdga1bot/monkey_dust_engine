#pragma once
#include <monkey_dust/ecs/md_entity.h>
#include <cstdint>
#include <cstring>
#include <entt/entt.hpp>

// ── EditorPanelRegistry ───────────────────────────────────────────────────────
// Lightweight panel registry — panels self-register with an ID, short name,
// draw callback, and optional entity selection callbacks.
// No ImGui dependency in this header.
//
// MAX_PANELS=16 covers current 14 panels + 2 headroom.
//
// Usage:
//   EditorPanelRegistry::Get().Register(0, "Hierarchy", DrawHierarchy,
//                                       OnEntitySelected, OnEntityDeselected);
//   EditorPanelRegistry::Get().NotifyEntitySelected(e);   // from editor pick
//   EditorPanelRegistry::Get().NotifyEntityDeselected();  // on click-away
//   EditorPanelRegistry::Get().Clear();  // in tests

using EditorPanelDrawFn     = void(*)(void);
using EditorPanelSelectFn   = void(*)(MdEntity);
using EditorPanelDeselectFn = void(*)(void);

struct EditorPanelEntry {
    uint8_t              id;
    char                 name[32];     // short display name (max 31 chars + NUL)
    EditorPanelDrawFn    draw;         // nullptr if no draw
    EditorPanelSelectFn  on_select;    // called when an entity is selected (nullptr = ignore)
    EditorPanelDeselectFn on_deselect; // called when selection is cleared (nullptr = ignore)
};

class EditorPanelRegistry {
public:
    static constexpr int MAX_PANELS = 16;

    static EditorPanelRegistry& Get() {
        static EditorPanelRegistry inst;
        return inst;
    }

    // Register a panel. Re-registration updates all fields.
    // Silently drops if MAX_PANELS is reached (no assert per project rules).
    void Register(uint8_t id, const char* name, EditorPanelDrawFn draw,
                  EditorPanelSelectFn on_select   = nullptr,
                  EditorPanelDeselectFn on_deselect = nullptr) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (panels_[i].id == id) {
                strncpy(panels_[i].name, name ? name : "", sizeof(panels_[i].name) - 1);
                panels_[i].name[sizeof(panels_[i].name) - 1] = '\0';
                panels_[i].draw       = draw;
                panels_[i].on_select  = on_select;
                panels_[i].on_deselect= on_deselect;
                return;
            }
        }
        if (count_ >= MAX_PANELS) return;
        EditorPanelEntry& e = panels_[count_++];
        e.id         = id;
        e.draw       = draw;
        e.on_select  = on_select;
        e.on_deselect= on_deselect;
        strncpy(e.name, name ? name : "", sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
    }

    // Call the draw function for panel id. No-op if not registered or draw is nullptr.
    void Draw(uint8_t id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].id == id && panels_[i].draw)
                { panels_[i].draw(); return; }
    }

    // Notify all panels that entity e is now selected.
    void NotifyEntitySelected(MdEntity e) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].on_select) panels_[i].on_select(e);
    }

    // Notify all panels that selection is cleared.
    void NotifyEntityDeselected() const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].on_deselect) panels_[i].on_deselect();
    }

    bool IsRegistered(uint8_t id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].id == id) return true;
        return false;
    }

    const char* GetName(uint8_t id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].id == id) return panels_[i].name;
        return nullptr;
    }

    int Count() const noexcept { return count_; }

    void Clear() noexcept { count_ = 0; }

private:
    EditorPanelRegistry() : count_(0) {}

    int              count_ = 0;
    EditorPanelEntry panels_[MAX_PANELS];
};
