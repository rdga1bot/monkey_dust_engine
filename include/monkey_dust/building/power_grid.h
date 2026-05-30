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
        RegisterWithFuel(building_id, power_output, 0.f, 0.f);
    }

    // B-1: Register generator with fuel consumption (fuel_rate > 0 = fuel-consuming).
    // fuel_rate: units per hour. initial_fuel: starting fuel amount (same units).
    void RegisterWithFuel(int building_id, float power_output,
                          float fuel_rate, float initial_fuel) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (ids_[i] == building_id) {
                outputs_   [i] = power_output;
                fuel_rates_[i] = fuel_rate;
                if (fuel_rate > 0.f && fuel_amounts_[i] <= 0.f)
                    fuel_amounts_[i] = initial_fuel;
                Recalc(); return;
            }
        }
        if (count_ >= MAX_POWER_CONSUMERS) return;
        ids_         [count_] = building_id;
        outputs_     [count_] = power_output;
        fuel_rates_  [count_] = fuel_rate;
        fuel_amounts_[count_] = initial_fuel;
        if (power_output == 0.f) battery_max_ += BATTERY_BANK_CAPACITY;
        ++count_;
        Recalc();
    }

    // B-1: Add fuel to a registered building. Returns false if building not found.
    bool AddFuel(int building_id, float amount) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (ids_[i] != building_id) continue;
            fuel_amounts_[i] += amount;
            return true;
        }
        return false;
    }

    float GetFuel(int building_id) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (ids_[i] == building_id) return fuel_amounts_[i];
        return 0.f;
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
        // B-1: consume fuel for generators; drop output to 0 if empty
        const float dt_hours = delta_s / 3600.f;
        for (int i = 0; i < count_; ++i) {
            if (fuel_rates_[i] <= 0.f) continue;
            fuel_amounts_[i] -= fuel_rates_[i] * dt_hours;
            if (fuel_amounts_[i] <= 0.f) { fuel_amounts_[i] = 0.f; outputs_[i] = 0.f; }
        }
        Recalc();

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

    int   ids_         [MAX_POWER_CONSUMERS] = {};
    float outputs_     [MAX_POWER_CONSUMERS] = {};
    float fuel_rates_  [MAX_POWER_CONSUMERS] = {};  // B-1: units/hour; 0=no fuel needed
    float fuel_amounts_[MAX_POWER_CONSUMERS] = {};  // B-1: current fuel
    int   count_          = 0;
    float total_power_    = 0.f;
    float deficit_timer_s_= 0.f;
    float battery_charge_ = 0.f;
    float battery_max_    = 0.f;
};
