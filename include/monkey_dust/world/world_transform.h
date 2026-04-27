#pragma once
#include <cstdint>

// Перейменовано в WorldTransform — raylib вже має свій тип Transform.
// Базовий engine-рівень просторовий компонент; використовується
// TransformSoA і SpatialGrid (обидва в engine/).
struct WorldTransform {
    float    x, y, z;
    float    rot_y;      // тільки поворот по Y (top-down RPG)
    uint32_t slot = 0xFFFFFFFFu; // TransformSoA slot index
};
