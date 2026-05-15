#pragma once
#include <cstdint>
#include <cstring>

// ── EditorPanelRegistry ───────────────────────────────────────────────────────
// Lightweight panel registry — panels self-register with an ID, short name,
// and a draw callback. No ImGui dependency in this header.
//
// MAX_PANELS=16 covers current 14 panels + 2 headroom.
// Draw callbacks take no arguments; panels read global EditorCore state directly.
//
// Usage:
//   EditorPanelRegistry::Get().Register(0, "Hierarchy", DrawHierarchy);
//   if (EditorPanelRegistry::Get().IsRegistered(0))
//       EditorPanelRegistry::Get().Draw(0);
//   EditorPanelRegistry::Get().Clear();  // in tests

using EditorPanelDrawFn = void(*)();

struct EditorPanelEntry {
    uint8_t           id;
    char              name[32];   // short display name (max 31 chars + NUL)
    EditorPanelDrawFn draw;       // nullptr if panel is registered but has no draw fn
};

class EditorPanelRegistry {
public:
    static constexpr int MAX_PANELS = 16;

    static EditorPanelRegistry& Get() {
        static EditorPanelRegistry inst;
        return inst;
    }

    // Register a panel by numeric ID. Re-registration updates name + draw fn.
    // Silently drops if MAX_PANELS is reached (no assert per project rules).
    void Register(uint8_t id, const char* name, EditorPanelDrawFn draw) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (panels_[i].id == id) {
                strncpy(panels_[i].name, name ? name : "", sizeof(panels_[i].name) - 1);
                panels_[i].name[sizeof(panels_[i].name) - 1] = '\0';
                panels_[i].draw = draw;
                return;
            }
        }
        if (count_ >= MAX_PANELS) return;
        EditorPanelEntry& e = panels_[count_++];
        e.id   = id;
        e.draw = draw;
        strncpy(e.name, name ? name : "", sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
    }

    // Call the draw function for panel id. No-op if not registered or draw is nullptr.
    void Draw(uint8_t id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].id == id && panels_[i].draw)
                { panels_[i].draw(); return; }
    }

    // Returns true if panel id has been registered.
    bool IsRegistered(uint8_t id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].id == id) return true;
        return false;
    }

    // Returns the name of panel id, or nullptr if not found.
    const char* GetName(uint8_t id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (panels_[i].id == id) return panels_[i].name;
        return nullptr;
    }

    int Count() const noexcept { return count_; }

    // Clear all registrations. Use in tests between test cases.
    void Clear() noexcept { count_ = 0; }

private:
    EditorPanelRegistry() : count_(0) {}

    int              count_ = 0;
    EditorPanelEntry panels_[MAX_PANELS];
};
