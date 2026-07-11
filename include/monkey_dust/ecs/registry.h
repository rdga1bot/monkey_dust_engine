#pragma once
#include <flecs.h>

// Глобальний singleton flecs::world (task #8 B3.4 — was entt::registry
// through B1-B3.3). КРИТИЧНО: не передавати по значенню, не мутувати під
// час query ітерації. При потребі змінити entities під час ітерації —
// збирати в temp vector, застосовувати після завершення query.each().

class Registry {
public:
    static flecs::world& Get() {
        static flecs::world w;
        return w;
    }
    Registry() = delete;
};
