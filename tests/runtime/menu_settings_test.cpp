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
    // The [video] merge must produce ONE section too — the device ran with
    // ~50 accumulated [video] headers (one per save) until 2026-10-16: the
    // writer kept the old header and appended a new one every save.
    CHECK(dir.count_occurrences("[video]") == 1);
}

// The [video] merge: foreign (launcher-owned) lines in [video] survive, land
// inside the single merged section, and repeated saves are byte-idempotent.
void video_merge_preserves_foreign_lines_and_is_idempotent() {
    gbarecomp::testing::TmpConfigDir dir;
    dir.write_config("[KeyMap]\nTurbo = Tab\n"
                     "[video]\nlaunchers_own = 1\n");
    MenuSettings m;
    m.lcd = true;
    gbarecomp::save_menu_settings(dir.path().c_str(), m);
    CHECK(dir.count_occurrences("[video]") == 1);
    CHECK(dir.count_occurrences("[Menu]") == 1);
    CHECK(dir.contains("launchers_own = 1"));
    // The preserved line must still be IN [video] (after its header).
    const std::string text = dir.read_config();
    CHECK(text.find("[video]") < text.find("launchers_own = 1"));
    const std::string once = dir.read_config();
    gbarecomp::save_menu_settings(dir.path().c_str(), m);
    const std::string twice = dir.read_config();
    CHECK(once == twice);  // byte-idempotent: saving changes nothing
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

// Sharp scaling + the border pixel grid: parse, clamp, and round-trip.
// sharp_filter keeps the historical [video] key the runtime config parser
// reads (runtime.cpp); the grid keys follow the lcd_* claim-the-key family.
void sharp_and_pixel_grid_keys_parse() {
    MenuSettings m;
    CHECK(gbarecomp::apply_menu_settings_line(m, "sharp_filter", "1"));
    CHECK(m.sharp_filter);
    CHECK(gbarecomp::apply_menu_settings_line(m, "sharp_filter", "off"));
    CHECK(!m.sharp_filter);
    CHECK(gbarecomp::apply_menu_settings_line(m, "sharp_filter", "junk"));
    CHECK(!m.sharp_filter);  // garbage keeps the value
    CHECK(gbarecomp::apply_menu_settings_line(m, "lcd_border", "on"));
    CHECK(m.lcd_border);
    CHECK(gbarecomp::apply_menu_settings_line(m, "lcd_grid_alpha", "0.6"));
    CHECK(m.lcd_grid_alpha == 0.6f);
    CHECK(gbarecomp::apply_menu_settings_line(m, "lcd_grid_alpha", "5"));
    CHECK(m.lcd_grid_alpha == 1.0f);  // clamped high
    CHECK(gbarecomp::apply_menu_settings_line(m, "lcd_grid_alpha", "-1"));
    CHECK(m.lcd_grid_alpha == 0.0f);  // clamped low
    CHECK(gbarecomp::apply_menu_settings_line(m, "edge_smoothing", "1"));
    CHECK(m.edge_smoothing);
    CHECK(gbarecomp::apply_menu_settings_line(m, "edge_smoothing", "junk"));
    CHECK(m.edge_smoothing);  // garbage keeps the value (claims the key)
    CHECK(!gbarecomp::apply_menu_settings_line(m, "nope", "1"));
}

void sharp_and_grid_round_trip() {
    gbarecomp::testing::TmpConfigDir dir;
    MenuSettings m;
    m.sharp_filter = true;
    m.lcd_border = true;
    m.lcd_grid_alpha = 0.65f;
    m.edge_smoothing = true;
    gbarecomp::save_menu_settings(dir.path().c_str(), m);
    MenuSettings back;
    gbarecomp::load_menu_settings(dir.path().c_str(), back);
    CHECK(back.sharp_filter);
    CHECK(back.lcd_border);
    CHECK(back.lcd_grid_alpha == 0.65f);
    CHECK(back.edge_smoothing);
    CHECK(dir.count_occurrences("sharp_filter") == 1);
    CHECK(dir.count_occurrences("lcd_border") == 1);
    CHECK(dir.count_occurrences("lcd_grid_alpha") == 1);
    CHECK(dir.count_occurrences("edge_smoothing") == 1);
}

}  // namespace

// The save-state slot: parses under [Menu], clamps to the 1..9 slot
// vocabulary, garbage keeps the value, and the round-trip survives the
// drop-and-rewrite without duplicates.
void state_slot_parses_clamps_and_round_trips() {
    MenuSettings m;
    CHECK(gbarecomp::apply_menu_settings_line(m, "state_slot", "7"));
    CHECK(m.state_slot == 7);
    CHECK(gbarecomp::apply_menu_settings_line(m, "state_slot", "99"));
    CHECK(m.state_slot == 9);  // clamped high
    CHECK(gbarecomp::apply_menu_settings_line(m, "state_slot", "0"));
    CHECK(m.state_slot == 1);  // clamped low
    CHECK(!gbarecomp::apply_menu_settings_line(m, "state_slot", "junk"));
    CHECK(m.state_slot == 1);  // garbage keeps the value
    gbarecomp::testing::TmpConfigDir dir;
    m.state_slot = 4;
    gbarecomp::save_menu_settings(dir.path().c_str(), m);
    MenuSettings back;
    gbarecomp::load_menu_settings(dir.path().c_str(), back);
    CHECK(back.state_slot == 4);
    CHECK(dir.count_occurrences("state_slot") == 1);
}

int main() {
    display_keys_parse_and_clamp();
    audio_and_graphics_keys_parse();
    sharp_and_pixel_grid_keys_parse();
    sharp_and_grid_round_trip();
    sharp_and_grid_round_trip();
    save_rewrite_round_trips_without_dupes();
    video_merge_preserves_foreign_lines_and_is_idempotent();
    foreign_sections_survive();
    state_slot_parses_clamps_and_round_trips();
    std::printf("menu_settings tests: PASS\n");
    return 0;
}
