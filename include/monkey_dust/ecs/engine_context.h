#pragma once
#include <cstdint>

namespace md {

// Узагальнений контекст рушія — НЕ містить gameplay полів.
// Game успадковує цей struct у власному GameState (game/src/game_state.h).
// POD, read-only між тіками (10 TPS).
struct EngineContext {
    float    delta_time  = 0.0f;   // секунди з минулого кадру
    float    fps         = 0.0f;   // поточний FPS
    float    now_s       = 0.0f;   // реальний час від старту (PathCache TTL і т.д.)
    uint32_t logic_tick  = 0;      // лічильник тіків логіки
    uint32_t frame_index = 0;      // лічильник рендер-кадрів
};

} // namespace md
