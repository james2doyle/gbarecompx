#pragma once

// raylib KeyCode mapping: ini key names -> raylib key codes.
//
// WHY THIS IS PURE (and why it must stay that way)
// -------------------------------------------------
// keybinds.ini stores SDL scancode NAMES ("X", "RShift", "Return") and
// config.ini [KeyMap] stores SDL keycode names with Ctrl+/Alt+/Shift+ prefixes
// ("Alt+Return", "Shift+P"). The SDL backend resolves those with
// SDL_GetScancodeFromName / SDL_GetKeyFromName. raylib 6 has NO name->key
// function (verified against raylib.h: only IsKeyDown/IsKeyPressed/GetKeyName
// exist), so the backend needs its own table.
//
// This header is that table, with no raylib.h and no SDL.h, so it compiles and
// tests anywhere. The integer values MUST equal raylib.h's KeyboardKey enum;
// raylib_backend.cpp static_asserts every one of them when GBARECOMP_USE_RAYLIB
// is on, so drift fails the raylib build instead of silently remapping a key.
//
// Name vocabulary mirrors scancode_from_name() in host_window.cpp (which itself
// mirrors recomp-ui's keybinds.c): SDL_GetScancodeFromName first, then the same
// lowercase aliases. A file either side writes round-trips identically.

#include "input_translate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gbarecomp {

// raylib KeyboardKey values, copied from raylib.h (KeyboardKey enum). Do NOT
// edit by hand without checking raylib.h: raylib_backend.cpp static_asserts
// each of these against KEY_* when built with raylib.
constexpr int kRayKeyNone = -1;
constexpr int kRayKeySpace = 32;
constexpr int kRayKeyBackslash = 92;
constexpr int kRayKey0 = 48;
constexpr int kRayKey1 = 49;
constexpr int kRayKey2 = 50;
constexpr int kRayKey3 = 51;
constexpr int kRayKey4 = 52;
constexpr int kRayKey5 = 53;
constexpr int kRayKey6 = 54;
constexpr int kRayKey7 = 55;
constexpr int kRayKey8 = 56;
constexpr int kRayKey9 = 57;
constexpr int kRayKeyA = 65;
constexpr int kRayKeyB = 66;
constexpr int kRayKeyC = 67;
constexpr int kRayKeyD = 68;
constexpr int kRayKeyE = 69;
constexpr int kRayKeyF = 70;
constexpr int kRayKeyG = 71;
constexpr int kRayKeyH = 72;
constexpr int kRayKeyI = 73;
constexpr int kRayKeyJ = 74;
constexpr int kRayKeyK = 75;
constexpr int kRayKeyL = 76;
constexpr int kRayKeyM = 77;
constexpr int kRayKeyN = 78;
constexpr int kRayKeyO = 79;
constexpr int kRayKeyP = 80;
constexpr int kRayKeyQ = 81;
constexpr int kRayKeyR = 82;
constexpr int kRayKeyS = 83;
constexpr int kRayKeyT = 84;
constexpr int kRayKeyU = 85;
constexpr int kRayKeyV = 86;
constexpr int kRayKeyW = 87;
constexpr int kRayKeyX = 88;
constexpr int kRayKeyY = 89;
constexpr int kRayKeyZ = 90;
constexpr int kRayKeyEscape = 256;
constexpr int kRayKeyEnter = 257;
constexpr int kRayKeyTab = 258;
constexpr int kRayKeyBackspace = 259;
constexpr int kRayKeyDelete = 261;
constexpr int kRayKeyRight = 262;
constexpr int kRayKeyLeft = 263;
constexpr int kRayKeyDown = 264;
constexpr int kRayKeyUp = 265;
constexpr int kRayKeyF1 = 290;
constexpr int kRayKeyF2 = 291;
constexpr int kRayKeyF3 = 292;
constexpr int kRayKeyF4 = 293;
constexpr int kRayKeyF5 = 294;
constexpr int kRayKeyF6 = 295;
constexpr int kRayKeyF7 = 296;
constexpr int kRayKeyF8 = 297;
constexpr int kRayKeyF9 = 298;
constexpr int kRayKeyF10 = 299;
constexpr int kRayKeyF11 = 300;
constexpr int kRayKeyF12 = 301;
constexpr int kRayKeyLeftShift = 340;
constexpr int kRayKeyLeftControl = 341;
constexpr int kRayKeyLeftAlt = 342;
constexpr int kRayKeyLeftSuper = 343;
constexpr int kRayKeyRightShift = 344;
constexpr int kRayKeyRightControl = 345;
constexpr int kRayKeyRightAlt = 346;
constexpr int kRayKeyRightSuper = 347;

// Map an ini key name to a raylib KeyCode. Case-insensitive, with the same
// aliases the SDL side accepts ("return"/"enter", "esc", "lshift", ...).
// Returns kRayKeyNone for "None", empty, or unknown -- matching the SDL
// backend's "unknown => unbound" rule.
inline int raylib_key_from_name(const char* name) {
    if (!name || !*name) return kRayKeyNone;
    // Single ASCII letters and digits first: the common case, no table.
    if (!name[1]) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') return kRayKeyA + (c - 'a');
        if (c >= 'A' && c <= 'Z') return kRayKeyA + (c - 'A');
        if (c >= '0' && c <= '9') return kRayKey0 + (c - '0');
        if (c == ' ') return kRayKeySpace;
    }
    std::string b;
    for (const char* p = name; *p; ++p)
        b += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    if (b == "enter" || b == "return" || b == "kp_enter") return kRayKeyEnter;
    if (b == "tab") return kRayKeyTab;
    if (b == "space") return kRayKeySpace;
    if (b == "escape" || b == "esc") return kRayKeyEscape;
    if (b == "backspace") return kRayKeyBackspace;
    if (b == "delete" || b == "del") return kRayKeyDelete;
    if (b == "backslash") return kRayKeyBackslash;
    if (b == "right" || b == "rightarrow") return kRayKeyRight;
    if (b == "left" || b == "leftarrow") return kRayKeyLeft;
    if (b == "down" || b == "downarrow") return kRayKeyDown;
    if (b == "up" || b == "uparrow") return kRayKeyUp;
    if (b == "lshift" || b == "shift") return kRayKeyLeftShift;
    if (b == "rshift") return kRayKeyRightShift;
    if (b == "lctrl" || b == "ctrl" || b == "lcontrol") return kRayKeyLeftControl;
    if (b == "rctrl" || b == "rcontrol") return kRayKeyRightControl;
    if (b == "lalt" || b == "alt") return kRayKeyLeftAlt;
    if (b == "ralt") return kRayKeyRightAlt;
    // SDL_GetScancodeName spells modifiers with a space ("Right Shift"), and
    // the shipped keybinds.ini documents those spellings in its own header --
    // including the default `select = Right Shift`. Without these, every
    // default user loses Select on the raylib backend (found live: Select
    // parsed to unbound while SDL accepted it).
    if (b == "left shift") return kRayKeyLeftShift;
    if (b == "right shift") return kRayKeyRightShift;
    if (b == "left ctrl" || b == "left control") return kRayKeyLeftControl;
    if (b == "right ctrl" || b == "right control") return kRayKeyRightControl;
    if (b == "left alt") return kRayKeyLeftAlt;
    if (b == "right alt") return kRayKeyRightAlt;
    if (b == "left gui" || b == "left super") return kRayKeyLeftSuper;
    if (b == "right gui" || b == "right super") return kRayKeyRightSuper;
    if (b == "none") return kRayKeyNone;
    // F1..F12. Two extra chars max ("f12"), so compare directly.
    if ((b[0] == 'f') && b.size() >= 2 && b.size() <= 3) {
        const int n = std::atoi(b.c_str() + 1);
        if (n >= 1 && n <= 12) return kRayKeyF1 + (n - 1);
    }
    return kRayKeyNone;
}

// Parse a [KeyMap] value ("Ctrl+R", "Alt+Return", "F", "" = unbound) into a
// HotkeyBinding with a raylib KeyCode. Mirrors parse_hotkey() in
// host_window.cpp, which mirrors the format recomp-ui's hotkey editor writes.
inline HotkeyBinding parse_raylib_hotkey(const char* value) {
    HotkeyBinding hb;
    if (!value || !*value) return hb;
    std::string v = value;
    unsigned mods = kModNone;
    for (;;) {
        if (v.rfind("Ctrl+", 0) == 0) {
            mods |= kModCtrl; v.erase(0, 5);
        } else if (v.rfind("Alt+", 0) == 0) {
            mods |= kModAlt; v.erase(0, 4);
        } else if (v.rfind("Shift+", 0) == 0) {
            mods |= kModShift; v.erase(0, 6);
        } else {
            break;
        }
    }
    const int key = raylib_key_from_name(v.c_str());
    if (key == kRayKeyNone) return HotkeyBinding{};
    hb.key = key;
    hb.mods = mods;
    return hb;
}

// config.ini [KeyMap] key names, in HostHotkey order. Shared vocabulary with
// the SDL backend (kHotkeyNames in host_window.cpp) so both backends read the
// same file.
inline const char* raylib_hotkey_name(int h) {
    switch (h) {
        case HK_FULLSCREEN: return "Fullscreen";
        case HK_PAUSE: return "Pause";
        case HK_TURBO: return "Turbo";
        case HK_WINDOW_BIGGER: return "WindowBigger";
        case HK_WINDOW_SMALLER: return "WindowSmaller";
        case HK_VOLUME_UP: return "VolumeUp";
        case HK_VOLUME_DOWN: return "VolumeDown";
        case HK_DISPLAY_PERF: return "DisplayPerf";
        case HK_SOLAR_BRIGHTER: return "SolarBrighter";
        case HK_SOLAR_DIMMER: return "SolarDimmer";
        case HK_SOLAR_LIVE: return "SolarLive";
        case HK_MENU: return "Menu";
        default: return "";
    }
}

// Same defaults recomp-ui's hotkey panel displays for the GBA catalog
// (kHotkeyDefaults in host_window.cpp).
inline const char* raylib_hotkey_default(int h) {
    switch (h) {
        case HK_FULLSCREEN: return "Alt+Return";
        case HK_PAUSE: return "Shift+P";
        case HK_TURBO: return "Tab";
        case HK_DISPLAY_PERF: return "F";
        case HK_MENU: return "Delete";
        default: return "";
    }
}

// keybinds.ini [player1] key name -> GBA KEYINPUT bit. Same vocabulary as
// kBindKeys in host_window.cpp; x/y/l2/r2/l3/r3 are ignored (no GBA
// equivalent). Returns -1 when `key` is not a GBA button name.
inline int raylib_bind_bit(const char* key) {
    if (!key || !*key) return -1;
    std::string b;
    for (const char* p = key; *p; ++p)
        b += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    if (b == "a") return 0;
    if (b == "b") return 1;
    if (b == "select") return 2;
    if (b == "start") return 3;
    if (b == "right") return 4;
    if (b == "left") return 5;
    if (b == "up") return 6;
    if (b == "down") return 7;
    if (b == "r") return 8;
    if (b == "l") return 9;
    return -1;
}

// Default GBA bindings in raylib KeyCodes. Same layout as kDefaultBinds in
// host_window.cpp (which matches recomp-ui's keybinds.c): A=X, B=Z, Select=
// RShift, Start=Return, d-pad=arrows, R=V, L=C.
inline void raylib_default_gba_keys(HostKey out[10]) {
    out[0] = kRayKeyX;
    out[1] = kRayKeyZ;
    out[2] = kRayKeyRightShift;
    out[3] = kRayKeyEnter;
    out[4] = kRayKeyRight;
    out[5] = kRayKeyLeft;
    out[6] = kRayKeyUp;
    out[7] = kRayKeyDown;
    out[8] = kRayKeyV;
    out[9] = kRayKeyC;
}

// Seed an InputBindings with the built-in defaults, before ini overrides.
inline void raylib_default_bindings(InputBindings& b) {    raylib_default_gba_keys(b.gba_key);
    for (int i = 0; i < 9; ++i) b.slot_key[i] = kRayKeyF1 + i;
    b.escape_key = kRayKeyEscape;
    for (int h = 0; h < HK_COUNT; ++h)
        b.hotkeys[h] = parse_raylib_hotkey(raylib_hotkey_default(h));
    b.assist_key[0] = kRayKey1;
    b.assist_key[1] = kRayKey2;
    // Same encoding as the SDL backend's defaults: trigger axes, positive
    // half. The axis numbering (4/5) is the canonical PadAxis order, which
    // both SDL_CONTROLLER_AXIS_* and raylib GAMEPAD_AXIS_* share.
    b.assist_pad[0] = assist_pad_axis(kPadAxisLeftTrigger, true);
    b.assist_pad[1] = assist_pad_axis(kPadAxisRightTrigger, true);
    b.assist_axis_threshold = 16000;
}

// Convert an on-disk [Launcher] assist key (an SDL scancode INTEGER, e.g. 30
// for SDL_SCANCODE_1) to a raylib KeyCode. The launcher writes SDL scancodes
// because the SDL backend was first; the raylib backend must interpret the
// same file, not a parallel one.
//
// Covers the keys a player can plausibly bind (letters, digits, function keys,
// arrows, space/tab/enter/escape/backspace, modifiers). Unknown codes return
// kRayKeyNone (unbound) rather than a wrong key -- a missed binding the player
// can rebind beats a silently remapped one.
inline int sdl_scancode_to_raylib_key(int sc) {
    // SDL scancodes 4..29 are A..Z in order; raylib letters are ASCII 65..90.
    if (sc >= 4 && sc <= 29) return kRayKeyA + (sc - 4);
    // SDL scancodes 30..39 are 1..9,0 in order; raylib digits are ASCII.
    if (sc >= 30 && sc <= 37) return kRayKey1 + (sc - 30);
    if (sc == 38) return kRayKey9;   // SDL_SCANCODE_9
    if (sc == 39) return kRayKey0;   // SDL_SCANCODE_0
    switch (sc) {
        case 40: return kRayKeyEnter;       // SDL_SCANCODE_RETURN
        case 41: return kRayKeyEscape;      // SDL_SCANCODE_ESCAPE
        case 42: return kRayKeyBackspace;   // SDL_SCANCODE_BACKSPACE
        case 43: return kRayKeyTab;         // SDL_SCANCODE_TAB
        case 44: return kRayKeySpace;       // SDL_SCANCODE_SPACE
        case 49: return kRayKeyBackslash;   // SDL_SCANCODE_BACKSLASH
        case 58: return kRayKeyF1;          // SDL_SCANCODE_F1
        case 59: return kRayKeyF2;
        case 60: return kRayKeyF3;
        case 61: return kRayKeyF4;
        case 62: return kRayKeyF5;
        case 63: return kRayKeyF6;
        case 64: return kRayKeyF7;
        case 65: return kRayKeyF8;
        case 66: return kRayKeyF9;
        case 67: return kRayKeyF10;
        case 68: return kRayKeyF11;
        case 69: return kRayKeyF12;
        case 76: return kRayKeyDelete;   // SDL_SCANCODE_DELETE
        case 79: return kRayKeyRight;       // SDL_SCANCODE_RIGHT
        case 80: return kRayKeyLeft;        // SDL_SCANCODE_LEFT
        case 81: return kRayKeyDown;        // SDL_SCANCODE_DOWN
        case 82: return kRayKeyUp;          // SDL_SCANCODE_UP
        case 224: return kRayKeyLeftControl;   // SDL_SCANCODE_LCTRL
        case 225: return kRayKeyLeftShift;     // SDL_SCANCODE_LSHIFT
        case 226: return kRayKeyLeftAlt;       // SDL_SCANCODE_LALT
        case 228: return kRayKeyRightControl;  // SDL_SCANCODE_RCTRL
        case 229: return kRayKeyRightShift;    // SDL_SCANCODE_RSHIFT
        case 230: return kRayKeyRightAlt;      // SDL_SCANCODE_RALT
        default: return kRayKeyNone;
    }
}

// ini line application, one function per section. Pure, so the file->bindings
// wiring is testable without a window: feed the exact lines of a real
// keybinds.ini and assert the resulting bindings. load_input_config() calls
// these; it does no parsing of its own.

// [player1]: GBA button name -> key name. Returns false for non-GBA names
// (x/y/l2/...), which are ignored, not misrouted.
inline bool apply_keybinds_line(InputBindings& b, const char* key,
                                const char* val) {
    const int bit = raylib_bind_bit(key);
    if (bit < 0 || bit >= 10) return false;
    b.gba_key[bit] = raylib_key_from_name(val);
    return true;
}

// [KeyMap]: hotkey name -> "Ctrl+/Alt+/Shift+Key" value. Unknown names are
// ignored so a future hotkey never breaks an old backend.
inline bool apply_keymap_line(InputBindings& b, const char* key,
                              const char* val) {
    for (int h = 0; h < HK_COUNT; ++h) {
        std::string want = raylib_hotkey_name(h);
        std::string got = key ? key : "";
        if (want.size() != got.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < want.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(want[i])) !=
                std::tolower(static_cast<unsigned char>(got[i]))) {
                same = false;
                break;
            }
        if (!same) continue;
        b.hotkeys[h] = parse_raylib_hotkey(val);
        return true;
    }
    return false;
}

// [Launcher]: assist tools flag, fast-forward multiplier, numeric SDL
// scancodes for assist keys (converted, not reinterpreted), portable
// button/axis codes for assist pads.
inline void apply_launcher_line(InputBindings& b, bool& assist_tools,
                                int& ff_multiplier, const char* key,
                                const char* val) {
    auto eq = [](const char* a, const char* lit) {
        if (!a) return false;
        while (*a && *lit) {
            if (std::tolower(static_cast<unsigned char>(*a)) !=
                std::tolower(static_cast<unsigned char>(*lit)))
                return false;
            ++a;
            ++lit;
        }
        return *a == *lit;
    };
    if (eq(key, "assist_tools"))
        assist_tools = std::atoi(val) != 0;
    else if (eq(key, "assist_fast_forward_multiplier"))
        ff_multiplier = std::clamp(std::atoi(val), 2, 10);
    else if (eq(key, "assist_rewind_key"))
        b.assist_key[0] = sdl_scancode_to_raylib_key(std::atoi(val));
    else if (eq(key, "assist_fast_key"))
        b.assist_key[1] = sdl_scancode_to_raylib_key(std::atoi(val));
    else if (eq(key, "assist_rewind_pad"))
        b.assist_pad[0] = std::atoi(val);
    else if (eq(key, "assist_fast_pad"))
        b.assist_pad[1] = std::atoi(val);
}

}  // namespace gbarecomp
