#pragma once
// Signal<Args...> — minimal fixed-slot pub/sub for NEW code
// (ARCHITECTURE_IDEAS.md #4). Function-pointer + user-data slot, no
// std::function (no heap allocation; matches the fixed-array style already
// used by LuaEventBus::MAX_HANDLERS/SquadSignalBus::MAX_SQUADS).
//
// This does NOT replace LuaEventBus/WorldEventBus/SquadSignalBus — those
// three are used pervasively already and migrating them is a separate,
// larger, riskier effort (see ARCHITECTURE_IDEAS.md #4). Use Signal only
// for NEW event needs, so future code has one place to reach for instead
// of writing yet another one-off bus.
template <typename... Args>
class Signal {
public:
    using Fn = void (*)(void* user, Args...);
    static constexpr int MAX_SLOTS = 16;

    // Returns false if MAX_SLOTS is full — caller should raise MAX_SLOTS
    // if that budget is genuinely too small for its use case.
    bool Connect(Fn fn, void* user = nullptr) {
        for (auto& s : slots_) {
            if (!s.fn) { s.fn = fn; s.user = user; return true; }
        }
        return false;
    }

    void Disconnect(Fn fn, void* user = nullptr) {
        for (auto& s : slots_)
            if (s.fn == fn && s.user == user) { s.fn = nullptr; s.user = nullptr; }
    }

    void Fire(Args... args) const {
        for (auto& s : slots_) if (s.fn) s.fn(s.user, args...);
    }

    int Count() const {
        int n = 0;
        for (auto& s : slots_) if (s.fn) ++n;
        return n;
    }

private:
    struct Slot { Fn fn = nullptr; void* user = nullptr; };
    Slot slots_[MAX_SLOTS] = {};
};
