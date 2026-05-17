#pragma once
#include <cstdint>

// ── MdFeature — zelda3-inspired accessibility/QoL feature flags ───────────────
// Loaded from monkey_dust.ini [Features] section via MdIniReader.
// Runtime toggle via g_md_features uint32 bitmask.
//
// INI usage (data/monkey_dust.ini):
//   [Features]
//   disable_low_health_beep = true
//   skip_intro_on_keypress  = true
//   show_fps_counter        = false
//
// C++ usage:
//   if (MdHasFeature(MDF_DisableLowHealthBeep)) { /* skip beep */ }
//   MdSetFeature(MDF_ShowFpsCounter, true);
//
// Initialization (call after MdIniLoad):
//   MdFeaturesLoad(ini_reader);

enum MdFeature : uint32_t {
    MDF_None                 = 0,
    MDF_DisableLowHealthBeep = 1u << 0,  // suppress low-HP audio alert
    MDF_SkipIntroOnKeypress  = 1u << 1,  // any key skips cutscene/intro
    MDF_ShowFpsCounter       = 1u << 2,  // overlay FPS in corner
    MDF_UnlimitedFramerate   = 1u << 3,  // remove 60 FPS cap
    MDF_DisableScreenFlash   = 1u << 4,  // epilepsy safety — no palette flash
    MDF_AutoPickupGold       = 1u << 5,  // auto-collect currency items
    MDF_PauseOnFocusLoss     = 1u << 6,  // auto-pause when window loses focus
    MDF_HighContrastUI       = 1u << 7,  // thicker UI outlines for visibility
};

// Global bitmask — read from INI, writable at runtime.
extern uint32_t g_md_features;

inline bool     MdHasFeature(MdFeature f)         { return (g_md_features & (uint32_t)f) != 0; }
inline void     MdSetFeature(MdFeature f, bool on) {
    if (on) g_md_features |=  (uint32_t)f;
    else    g_md_features &= ~(uint32_t)f;
}

// Forward declaration — full definition requires md_ini.h.
class MdIniReader;

// Parse [Features] section from a loaded MdIniReader.
// Call after MdIniLoad(). Sets g_md_features bits accordingly.
void MdFeaturesLoad(const MdIniReader& ini) noexcept;
