#pragma once
#include <entt/entt.hpp>

// Глобальний singleton entt::registry.
// КРИТИЧНО: не передавати по значенню, не мутувати під час view ітерації.
// При потребі змінити entities під час ітерації — збирати в temp vector,
// застосовувати після завершення view.each().

class Registry {
public:
    static entt::registry& Get() {
        static entt::registry reg;
        return reg;
    }
    Registry() = delete;
};
