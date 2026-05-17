#include <monkey_dust/platform/md_features.h>
#include <monkey_dust/platform/md_ini.h>

uint32_t g_md_features = MDF_PauseOnFocusLoss;  // safe defaults

void MdFeaturesLoad(const MdIniReader& ini) noexcept {
    // Only change a flag if the key is explicitly present in the INI.
    // Missing key → keep current default.
    struct { const char* key; MdFeature flag; } table[] = {
        { "disable_low_health_beep", MDF_DisableLowHealthBeep },
        { "skip_intro_on_keypress",  MDF_SkipIntroOnKeypress  },
        { "show_fps_counter",        MDF_ShowFpsCounter        },
        { "unlimited_framerate",     MDF_UnlimitedFramerate    },
        { "disable_screen_flash",    MDF_DisableScreenFlash    },
        { "auto_pickup_gold",        MDF_AutoPickupGold        },
        { "pause_on_focus_loss",     MDF_PauseOnFocusLoss      },
        { "high_contrast_ui",        MDF_HighContrastUI        },
    };
    for (auto& t : table) {
        const char* v = ini.GetStr("Features", t.key, nullptr);
        if (v) MdSetFeature(t.flag, ini.GetBool("Features", t.key, false));
    }
}
