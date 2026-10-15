#include "color_settings.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

int main() {
    using gbarecomp::runtime::ColorSettings;
    using gbarecomp::runtime::resolve_color_settings;
    using gbarecomp::runtime::ScreenKind;

    // standard default
    {
        ColorSettings s = resolve_color_settings("raw");
        assert(s.screen == ScreenKind::Raw);
    }
    {
        ColorSettings s = resolve_color_settings("backlit");
        assert(s.screen == ScreenKind::Backlit);
    }
    // env overrides toml
    setenv("GBARECOMP_SCREEN", "classic", 1);
    {
        ColorSettings s = resolve_color_settings("unlit");
        assert(s.screen == ScreenKind::Classic);
    }
    unsetenv("GBARECOMP_SCREEN");
    // unrecognized env ignored
    setenv("GBARECOMP_SCREEN", "nope", 1);
    {
        ColorSettings s = resolve_color_settings("frontlit");
        assert(s.screen == ScreenKind::Frontlit);
    }
    unsetenv("GBARECOMP_SCREEN");
    // invalid toml allowed passthrough? uninitialized fallback Raw
    {
        ColorSettings s = resolve_color_settings("invalid");
        assert(s.screen == ScreenKind::Raw);
    }

    std::printf("color_settings tests: PASS\n");
    return 0;
}