#pragma once

#include <cstdlib>

#include "color_lut.h"

namespace gbarecomp::runtime {

// Resolve the screen model: the per-game toml_screen value is the default;
// GBARECOMP_SCREEN overrides it at launch. Unset/unrecognized => Raw.
inline ColorSettings resolve_color_settings(const char* toml_screen) {
    ColorSettings s;
    ScreenKind k;
    if (toml_screen && screen_kind_from_name(toml_screen, k)) s.screen = k;
    if (const char* env = std::getenv("GBARECOMP_SCREEN")) {
        if (screen_kind_from_name(env, k)) s.screen = k;
    }
    return s;
}

} // namespace gbarecomp::runtime