// menu_settings_test.cpp — persistence round-trip, red-first.
//
// MenuSettings (every persistable menu row) parses from config.ini sections
// ([Display]/[Audio]/[video]) and saves back with section-drop-and-rewrite
// (the save_touch_config shape): no duplicates, other sections preserved.
// Pure file/std I/O; runs everywhere.
//
// RED STATE: includes menu_settings.h, which does not exist yet.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "support/tmp_config_dir.h"

// Included unconditionally so the RED state fails in every config.
#include "menu_settings.h"

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                          __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

using gbarecomp::MenuSettings;

void display_keys_parse_and_clamp() {
    MenuSettings m;
    CHECK(gbarecomp::apply_menu_settings_line(m, "fullscreen", "2"));
    CHECK(m.fullscreen == 2);
    CHECK(gbarecomp::apply_menu_settings_line(m, "fullscreen", "9"));
    CHECK(m.fullscreen == 2);  // clamped high
    CHECK(gbarecomp::apply_menu_settings_line(m, "scale", "8"));
    CHECK(m.scale == 8);
    CHECK(gbarecomp::apply_menu_settings_line(m, "scale", "99"));
    CHECK(m.scale == 8);
    CHECK(gbarecomp::apply_menu_settings_line(m, "scale", "0"));
    CHECK(m.scale == 1);  // clamped low
    CHECK(gbarecomp::apply_menu_settings_line(m, "linear_filter", "on"));
    CHECK(m.linear_filter);
    CHECK(gbarecomp::apply_menu_settings_line(m, "show_fps", "1"));
    CHECK(m.show_fps);
    CHECK(gbarecomp::apply_menu_settings_line(m, "view", "adaptive"));
    CHECK(m.view_mode == 1);
    CHECK(gbarecomp::apply_menu_settings_line(m, "view", "16:9"));
    CHECK(m.view_mode == 2);
    CHECK(gbarecomp::apply_menu_settings_line(m, "view", "bogus"));
    CHECK(m.view_mode == 2);  // garbage leaves the value untouched
    CHECK(!gbarecomp::apply_menu_settings_line(m, "nope", "1"));
}

void audio_and_graphics_keys_parse() {
    MenuSettings m;
    CHECK(gbarecomp::apply_menu_settings_line(m, "enabled", "0"));
    CHECK(!m.audio_enabled);
    CHECK(gbarecomp::apply_menu_settings_line(m, "volume", "55"));
    CHECK(m.volume == 55);
    CHECK(gbarecomp::apply_menu_settings_line(m, "volume", "150"));
    CHECK(m.volume == 100);
    CHECK(gbarecomp::apply_menu_settings_line(m, "frame_blend", "1"));
    CHECK(m.frame_blend != 0.0f);
    CHECK(gbarecomp::apply_menu_settings_line(m, "frame_interp", "on"));
    CHECK(m.frame_interp);
    CHECK(gbarecomp::apply_menu_settings_line(m, "lcd_shader", "1"));
    CHECK(m.lcd);
}

void save_rewrite_round_trips_without_dupes() {
    gbarecomp::testing::TmpConfigDir dir;
    MenuSettings m;
    m.fullscreen = 1;
    m.scale = 4;
    m.volume = 55;
    m.view_mode = 1;
    gbarecomp::save_menu_settings(dir.path().c_str(), m);
    gbarecomp::save_menu_settings(dir.path().c_str(), m);  // rewrite must not duplicate
    MenuSettings back;
    gbarecomp::load_menu_settings(dir.path().c_str(), back);
    CHECK(back.fullscreen == 1);
    CHECK(back.scale == 4);
    CHECK(back.volume == 55);
    CHECK(back.view_mode == 1);
    CHECK(dir.count_occurrences("fullscreen") == 1);
    CHECK(dir.count_occurrences("[Menu]") == 1);
}

void foreign_sections_survive() {
    gbarecomp::testing::TmpConfigDir dir;
    dir.write_config("[KeyMap]\nTurbo = Tab\n");
    MenuSettings m;
    m.scale = 5;
    gbarecomp::save_menu_settings(dir.path().c_str(), m);
    MenuSettings back;
    gbarecomp::load_menu_settings(dir.path().c_str(), back);
    CHECK(back.scale == 5);
    CHECK(dir.contains("Turbo = Tab"));  // untouched section preserved
}

}  // namespace

int main() {
    display_keys_parse_and_clamp();
    audio_and_graphics_keys_parse();
    save_rewrite_round_trips_without_dupes();
    foreign_sections_survive();
    std::printf("menu_settings tests: PASS\n");
    return 0;
}
