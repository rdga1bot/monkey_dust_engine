#pragma once
// PowerGrid — per-settlement power balance tracker.
// Kenshi RE: "power output" field in building records; positive=generator, negative=consumer.
// When total_power < 0 for DEFICIT_TIMEOUT_S seconds, powered buildings shut off.

#include <cstdint>
#include <cstring>

static constexpr int   MAX_POWER_CONSUMERS = 64;
static constexpr float POWER_DEFICIT_TIMEOUT_S = 10.f;

// Generator output constants (Kenshi RE approximate):
static constexpr float GENERATOR_I_OUTPUT   =  10.f;
static constexpr float GENERATOR_II_OUTPUT  =  25.f;
static constexpr float GENERATOR_III_OUTPUT =  50.f;
static constexpr float GENERATOR_IV_OUTPUT  = 100.f;
static constexpr float BATTERY_BANK_CAPACITY= 1000.f;

class PowerGrid {
public:
    static PowerGrid& Get() { static PowerGrid s; return s; }

    void Reset() noexcept {
        memset(outputs_, 0, sizeof(outputs_));
        count_          = 0;
        total_power_    = 0.f;
        deficit_timer_s_= 0.f;
        battery_charge_ = 0.f;
        battery_max_    = 0.f;
    }

    // Register a building's power contribution (call on build complete or load).
    // power_output > 0 = generator; < 0 = consumer.
    void Register(int building_id, float power_output) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (ids_[i] == building_id) { outputs_[i] = power_output; Recalc(); return; }
        }
        if (count_ >= MAX_POWER_CONSUMERS) return;
        ids_    [count_] = building_id;
        outputs_[count_] = power_output;
        // battery banks add to battery_max_
        if (power_output == 0.f) battery_max_ += BATTERY_BANK_CAPACITY;
        ++count_;
        Recalc();
    }

    // Unregister a building (demolished or destroyed).
    void Unregister(int building_id) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (ids_[i] != building_id) continue;
            ids_    [i] = ids_    [count_ - 1];
            outputs_[i] = outputs_[count_ - 1];
            --count_;
            Recalc();
            return;
        }
    }

    // Call from WorldSimulation or logic tick with delta_s.
    // Returns true if settlement is powered (false = deficit).
    bool Tick(float delta_s) noexcept {
        if (total_power_ >= 0.f) {
            // Surplus: charge battery
            float charge = total_power_ * delta_s;
            battery_charge_ += charge;
            if (battery_charge_ > battery_max_) battery_charge_ = battery_max_;
            deficit_timer_s_ = 0.f;
            return true;
        }
        // Deficit: drain battery first
        float drain = (-total_power_) * delta_s;
        if (battery_charge_ >= drain) {
            battery_charge_ -= drain;
            return true;
        }
        battery_charge_ = 0.f;
        deficit_timer_s_ += delta_s;
        return deficit_timer_s_ < POWER_DEFICIT_TIMEOUT_S;
    }

    float TotalPower()     const noexcept { return total_power_; }
    float BatteryCharge()  const noexcept { return battery_charge_; }
    float BatteryMax()     const noexcept { return battery_max_; }
    float DeficitTimer()   const noexcept { return deficit_timer_s_; }
    bool  IsPowered()      const noexcept { return deficit_timer_s_ < POWER_DEFICIT_TIMEOUT_S; }
    int   ConsumerCount()  const noexcept { return count_; }

private:
    PowerGrid() { memset(ids_, 0, sizeof(ids_)); memset(outputs_, 0, sizeof(outputs_)); }

    void Recalc() noexcept {
        total_power_ = 0.f;
        for (int i = 0; i < count_; ++i) total_power_ += outputs_[i];
    }

    int   ids_    [MAX_POWER_CONSUMERS] = {};
    float outputs_[MAX_POWER_CONSUMERS] = {};
    int   count_          = 0;
    float total_power_    = 0.f;
    float deficit_timer_s_= 0.f;
    float battery_charge_ = 0.f;
    float battery_max_    = 0.f;
};
