// menu_settings.h — menu settings persistence (pure; no backend, no window).
//
// The in-game menu applies every row live, but nothing writes them back, so
// every tweak dies with the process. This header is the file half: parse
// config.ini lines into MenuSettings, and save a MenuSettings back with the
// same drop-and-rewrite shape as save_touch_config — EXCEPT [video], which
// the launcher also writes: there known keys merge in place and unknown
// lines are preserved byte-for-byte.
//
// Sections: [Menu] owns every framework row (window chrome, audio, graphics
// policy, view mode). The LCD group additionally lives in [video] under the
// exact keys Fx::load_config already reads, so no loader changes there.
// A section the launcher owns ([KeyMap], [Launcher], [Touch]) is never
// touched by the writer.
//
// Value vocabulary mirrors the menu rows: toggles accept 1/true/on/yes
// (and the 0/false/off/no mirrors); garbage leaves the field untouched.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ini_section.h"

namespace gbarecomp {

// ViewMode order matches the menu's View row (Native / Adaptive / 16:9).
// Stored as int so hosts without the menu enum can still round-trip it.
struct MenuSettings {
    // [Menu] window chrome.
    int fullscreen = 0;       // 0..2 (windowed, borderless, fullscreen)
    int scale = 3;            // 1..8
    bool linear_filter = false;
    bool show_fps = false;
    // View row: 0 Native, 1 Adaptive, 2 Fixed. When Fixed, fixed_width is
    // the actual width the mode resolves to (the CLI's --view-width or the
    // saved value; 0 = unset, falls back to the legacy 16:9 width).
    int view_mode = 0;
    int fixed_width = 0;
    // [Menu] audio.
    bool audio_enabled = true;
    int volume = 100;         // 0..100
    // [Menu] save-state slot (the menu's State rows: 1..9).
    int state_slot = 1;
    // [Menu] graphics policy.
    bool frame_interp = false;
    float frame_blend = 0.0f;  // 0 or 1 (the menu Toggle semantics)
    // [video] LCD group (same keys Fx::load_config reads).
    bool lcd = false;
    float lcd_boost = 1.0f;
    float lcd_dither = 1.0f;
    float lcd_deband = 0.0f;
};

namespace menu_settings_detail {

inline bool ci_key(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b)))
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

inline bool parse_toggle(const char* val, bool* out) {
    if (!val || !out) return false;
    if (ci_key(val, "1") || ci_key(val, "true") || ci_key(val, "on") ||
        ci_key(val, "yes")) {
        *out = true;
        return true;
    }
    if (ci_key(val, "0") || ci_key(val, "false") || ci_key(val, "off") ||
        ci_key(val, "no")) {
        *out = false;
        return true;
    }
    return false;
}

inline bool parse_clamped_int(const char* val, int lo, int hi, int* out) {
    if (!val || !out) return false;
    char* end = nullptr;
    const long n = std::strtol(val, &end, 10);
    if (end == val || *end != '\0') return false;
    *out = std::clamp(static_cast<int>(n), lo, hi);
    return true;
}

inline bool parse_clamped_float(const char* val, float lo, float hi,
                                float* out) {
    if (!val || !out) return false;
    char* end = nullptr;
    const float f = std::strtof(val, &end);
    if (end == val || *end != '\0') return false;
    *out = std::clamp(f, lo, hi);
    return true;
}

}  // namespace menu_settings_detail

// One config line (section-agnostic). Returns true when the key is ours.
// Unknown keys return false; garbage values leave the field untouched.
inline bool apply_menu_settings_line(MenuSettings& m, const char* key,
                                     const char* val) {
    using namespace menu_settings_detail;
    if (!key || !val) return false;
    if (ci_key(key, "fullscreen")) return parse_clamped_int(val, 0, 2, &m.fullscreen);
    if (ci_key(key, "scale")) return parse_clamped_int(val, 1, 8, &m.scale);
    if (ci_key(key, "linear_filter")) return parse_toggle(val, &m.linear_filter);
    if (ci_key(key, "show_fps")) return parse_toggle(val, &m.show_fps);
    if (ci_key(key, "view_width"))
        return parse_clamped_int(val, 240, 1024, &m.fixed_width);
    if (ci_key(key, "view")) {
        if (ci_key(val, "native") || ci_key(val, "0")) { m.view_mode = 0; return true; }
        if (ci_key(val, "adaptive") || ci_key(val, "1")) { m.view_mode = 1; return true; }
        if (ci_key(val, "fixed") || ci_key(val, "16:9") ||
            ci_key(val, "16x9") || ci_key(val, "2")) {
            m.view_mode = 2;
            return true;
        }
        return true;  // claimed, garbage keeps the old value
    }
    if (ci_key(key, "audio_enabled") || ci_key(key, "enabled"))
        return parse_toggle(val, &m.audio_enabled);
    if (ci_key(key, "volume")) return parse_clamped_int(val, 0, 100, &m.volume);
    if (ci_key(key, "state_slot"))
        return parse_clamped_int(val, 1, 9, &m.state_slot);
    if (ci_key(key, "frame_interp")) return parse_toggle(val, &m.frame_interp);
    if (ci_key(key, "frame_blend")) {
        bool b = false;
        if (!parse_toggle(val, &b)) return true;  // claimed, garbage keeps value
        m.frame_blend = b ? 1.0f : 0.0f;
        return true;
    }
    if (ci_key(key, "lcd") || ci_key(key, "lcd_shader"))
        return parse_toggle(val, &m.lcd);
    if (ci_key(key, "lcd_boost")) return parse_clamped_float(val, 1.0f, 3.0f, &m.lcd_boost);
    if (ci_key(key, "lcd_dither")) return parse_clamped_float(val, 0.0f, 2.0f, &m.lcd_dither);
    if (ci_key(key, "lcd_deband")) return parse_clamped_float(val, 0.0f, 16.0f, &m.lcd_deband);
    return false;
}

namespace menu_settings_detail {

// A `key = value` line whose key (trimmed, case-insensitive) is one of the
// LCD keys we merge into [video] (the rest of that section is preserved).
inline bool is_lcd_key_line(const std::string& line) {
    std::string::size_type eq = line.find('=');
    if (eq == std::string::npos) return false;
    std::string key = line.substr(0, eq);
    std::string::size_type b = key.find_first_not_of(" \t");
    std::string::size_type e = key.find_last_not_of(" \t");
    if (b == std::string::npos) return false;
    key = key.substr(b, e - b + 1);
    std::string lower;
    for (char c : key) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower == "lcd_shader" || lower == "lcd_boost" ||
           lower == "lcd_dither" || lower == "lcd_deband";
}

}  // namespace menu_settings_detail

// Load all three sections ([Menu] + [video] LCD keys) into m. Missing file
// or missing keys leave defaults/garbage-tolerant values in place.
inline void load_menu_settings(const char* dir, MenuSettings& m) {
    if (!dir || !*dir) return;
    const std::string path = std::string(dir) + "/config.ini";
    ini_scan_section(path.c_str(), "Menu",
                     [&](const char* key, const char* val) {
                         apply_menu_settings_line(m, key, val);
                     });
    // LCD group keeps the historical key spelling the Fx loader reads
    // (apply_ accepts both "lcd" and "lcd_shader").
    ini_scan_section(path.c_str(), "video",
                     [&](const char* key, const char* val) {
                         apply_menu_settings_line(m, key, val);
                     });
}

// Save m: [Menu] is drop-and-rewritten (ours alone); [video] merges our LCD
// keys in place so launcher-owned lines in that section survive. Unknown
// sections pass through byte-for-byte (modulo CR stripping).
inline void save_menu_settings(const char* dir, const MenuSettings& m) {
    if (!dir || !*dir) return;
    const std::string path = std::string(dir) + "/config.ini";
    std::vector<std::string> kept;
    // Foreign [video] lines (launcher-owned keys) — re-emitted inside the
    // ONE [video] section this writer produces. The 2026-10-16 writer kept
    // the old [video] header AND appended a new one, so every save added a
    // duplicate header (the device reached ~50); headers are now merged.
    std::vector<std::string> video_extra;
    {
        std::ifstream f(path);
        std::string line;
        std::string section;
        while (std::getline(f, line)) {
            while (!line.empty() && line.back() == '\r') line.pop_back();
            std::string::size_type b = line.find_first_not_of(" \t");
            const bool is_header = b != std::string::npos && line[b] == '[';
            if (is_header) {
                std::string::size_type e = line.find(']', b);
                section = (e == std::string::npos)
                              ? ""
                              : line.substr(b + 1, e - b - 1);
                std::string lower;
                for (char c : section)
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                section = lower;
            }
            const bool in_menu = section == "menu";
            const bool in_video = section == "video";
            if (in_menu) continue;  // ours alone: dropped, rewritten below
            if (in_video && is_header)
                continue;  // every [video] header merges into the one below
            if (in_video &&
                menu_settings_detail::is_lcd_key_line(line))
                continue;  // our LCD key: merged below, rest preserved
            if (in_video) {
                video_extra.push_back(line);  // foreign key: stays in [video]
                continue;
            }
            kept.push_back(line);
        }
    }
    while (!kept.empty() && kept.back().empty()) kept.pop_back();
    while (!video_extra.empty() && video_extra.back().empty())
        video_extra.pop_back();
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (const auto& l : kept) f << l << "\n";
    if (!kept.empty()) f << "\n";
    f << "[Menu]\n";
    f << "fullscreen = " << m.fullscreen << "\n";
    f << "scale = " << m.scale << "\n";
    f << "linear_filter = " << (m.linear_filter ? 1 : 0) << "\n";
    f << "show_fps = " << (m.show_fps ? 1 : 0) << "\n";
    const char* view = m.view_mode == 1
                           ? "adaptive"
                           : (m.view_mode == 2 ? "fixed" : "native");
    f << "view = " << view << "\n";
    if (m.view_mode == 2 && m.fixed_width >= 240)
        f << "view_width = " << m.fixed_width << "\n";
    f << "audio_enabled = " << (m.audio_enabled ? 1 : 0) << "\n";
    f << "volume = " << m.volume << "\n";
    f << "state_slot = " << m.state_slot << "\n";
    f << "frame_interp = " << (m.frame_interp ? 1 : 0) << "\n";
    f << "frame_blend = " << (m.frame_blend != 0.0f ? 1 : 0) << "\n";
    f << "\n[video]\n";
    // Foreign (launcher-owned) lines stay first; our LCD keys land last so
    // last-wins readers always take ours.
    for (const auto& l : video_extra) f << l << "\n";
    f << "lcd_shader = " << (m.lcd ? 1 : 0) << "\n";
    f << "lcd_boost = " << m.lcd_boost << "\n";
    f << "lcd_dither = " << m.lcd_dither << "\n";
    f << "lcd_deband = " << m.lcd_deband << "\n";
}

}  // namespace gbarecomp
