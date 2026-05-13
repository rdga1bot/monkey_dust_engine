#pragma once

// ProjectileSystem — moves ProjectileComponent entities and handles hit detection.
// Call Tick() from the logic tick (10 TPS); dt = LOGIC_TICK_S.
namespace md {

class ProjectileSystem {
public:
    static ProjectileSystem& Get() { static ProjectileSystem i; return i; }
    void Tick(float dt);

private:
    ProjectileSystem() = default;
};

} // namespace md
