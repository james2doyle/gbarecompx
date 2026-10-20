#pragma once

// Host-neutral input translation: snapshot in, WindowBackend::Events out.
//
// WHY THIS IS PURE
// -----------------
// The SDL backend reads a PUSH-based event stream (SDL_PollEvent hands us
// KEYDOWN/WINDOWEVENT/FINGERDOWN...). raylib is PULL-based: you ask
// IsKeyDown() / GetGamepadAxisMovement() / GetTouchPosition() and get the
// current level. Those two models do not map onto each other, and most of
// what Events carries is EDGE-triggered -- quit, save/load slot, every system
// hotkey. With polling there is no "KEYDOWN with repeat==0" to filter, so every
// edge has to be recovered by comparing against the previous pump.
//
// That comparison is policy, not platform code, so it lives here where it can
// be tested with no window, no SDL, no raylib and no display. Backends only
// have to fill an InputSnapshot from their own API.
//
// The SDL backend's behaviour is the reference: the defaults below mirror its
// kDefaultBinds / kHotkeyDefaults / hotkey_mods_ok exactly, so switching
// backends does not silently change the player's bindings.

#include "assist_pad_binding.h"
#include "gamepad_map.h"
#include "window_backend.h"

#include <cstddef>
#include <cstdint>

namespace gbarecomp {

// A backend-native key identifier (SDL_Keycode, raylib KeyCode, ...) mapped
// onto one opaque integer, so this header never names either.
using HostKey = int;
constexpr HostKey kHostKeyNone = -1;

// Modifier mask, normalised across platforms. SDL exposes KMOD_*, raylib
// IsKeyDown(KEY_LEFT_SHIFT) booleans; both collapse to this.
enum : unsigned {
    kModNone = 0,
    kModShift = 1u << 0,
    kModCtrl = 1u << 1,
    kModAlt = 1u << 2,
};

// config.ini [KeyMap] binding order. Shared with the SDL backend (which
// previously declared its own copy) so the two cannot drift apart.
enum HostHotkey {
    HK_FULLSCREEN = 0,
    HK_PAUSE,
    HK_TURBO,
    HK_WINDOW_BIGGER,
    HK_WINDOW_SMALLER,
    HK_VOLUME_UP,
    HK_VOLUME_DOWN,
    HK_DISPLAY_PERF,
    HK_SOLAR_BRIGHTER,
    HK_SOLAR_DIMMER,
    HK_SOLAR_LIVE,
    HK_MENU,  // generic settings menu toggle (default Delete, desktop trigger)
    HK_COUNT
};

struct HotkeyBinding {
    HostKey key = kHostKeyNone;
    unsigned mods = kModNone;
};

// Canonical gamepad button/axis ids. These are the SAME codes the on-disk
// Assist binding encoding uses (see assist_pad_binding.h: "value - 1 is a
// backend-native button code"), because SDL and raylib both use the canonical
// Xbox ordering and recomp-ui writes those.
//
// Deliberately NOT assumed: each backend fills pad_button[]/pad_axis[] through
// an explicit native->canonical mapping table rather than casting its own enum,
// so a future upstream reordering cannot silently remap a player's binding.
enum PadButton {
    kPadA = 0, kPadB, kPadX, kPadY,
    kPadLeftBumper, kPadRightBumper, kPadBack, kPadStart,
    kPadLeftThumb, kPadRightThumb,
    kPadDpadUp, kPadDpadRight, kPadDpadDown, kPadDpadLeft,
    kPadLeftTrigger, kPadRightTrigger,
    // The pad's menu button (Xbox GUIDE / home / Android BUTTON_MODE). No
    // GBA meaning: the backends inject the CONFIGURED HK_MENU key from it,
    // so the settings overlay opens from the controller through the menu's
    // own binding (and closes on a second press, via the hotkey edge). Y is
    // the second trigger (pad_opens_menu) for pads whose menu button is
    // missing or swallowed by the OS.
    kPadGuide,
    kPadButtonCount
};

enum PadAxis {
    kPadAxisLeftX = 0, kPadAxisLeftY, kPadAxisRightX, kPadAxisRightY,
    kPadAxisLeftTrigger, kPadAxisRightTrigger,
    kPadAxisCount
};

// Everything a backend can tell us about this pump. No policy lives here.
struct InputSnapshot {
    // Keyboard: the keys that are down right now. A backend fills this by
    // testing IsKeyDown / SDL_GetKeyboardState over its bound keys.
    static constexpr int kMaxHeldKeys = 32;
    int held_key_count = 0;
    HostKey held_keys[kMaxHeldKeys] = {};

    unsigned mods = kModNone;

    bool close_requested = false;   // window close button / system quit

    // Android-style lifecycle. Desktop backends leave these false; the mobile
    // port is the only source of them.
    bool enter_background = false;
    bool terminating = false;

    bool pad_connected = false;
    bool pad_button[kPadButtonCount] = {};
    int pad_axis[kPadAxisCount] = {};      // raw, backend-native units
    // GBA bits derived from the pad by gamepad_map.h's gamepad_to_keyinput(),
    // computed by the backend so the shared + tested mapping is inherited
    // rather than re-derived.
    std::uint16_t pad_gba_bits = 0;

    // Mouse.
    bool mouse_left = false;
    int mouse_dx = 0;               // horizontal delta this pump

    // On-screen/touch controls. touch_enabled gates the mouse-gyro stand-in
    // (it is off whenever touch can drive the game); touch_gba_bits is what the
    // virtual pad currently holds, filled by the backend.
    bool touch_enabled = false;
    std::uint16_t touch_gba_bits = 0;

    // When the runtime settings menu is open the game must not see input.
    bool suppress_game_input = false;

    std::uint32_t now_ms = 0;

    void hold(HostKey k) {
        if (k == kHostKeyNone) return;
        if (held_key_count < kMaxHeldKeys) held_keys[held_key_count++] = k;
    }
    bool is_held(HostKey k) const {
        if (k == kHostKeyNone) return false;
        for (int i = 0; i < held_key_count; ++i)
            if (held_keys[i] == k) return true;
        return false;
    }
    void hold_pad_button(int b) {
        if (b >= 0 && b < kPadButtonCount) pad_button[b] = true;
    }
    void hold_pad_axis(int a, int v) {
        if (a >= 0 && a < kPadAxisCount) pad_axis[a] = v;
    }
};

// Configuration that maps keys onto meanings. Filled from keybinds.ini /
// config.ini by the backend.
struct InputBindings {
    // Indexed by GBA KEYINPUT bit: the key bound to that button.
    HostKey gba_key[10] = {};
    // F1..F9, used for save/load state slots (index 0 == F1).
    HostKey slot_key[9] = {};
    // Hard-quit key. The SDL backend hardcodes Escape; carrying it as a
    // binding keeps that out of this portable header.
    HostKey escape_key = kHostKeyNone;
    HotkeyBinding hotkeys[HK_COUNT] = {};
    // Assist bindings: index 0 rewind, index 1 fast-forward. The pad encoding
    // is the cross-platform contract in assist_pad_binding.h.
    HostKey assist_key[2] = {kHostKeyNone, kHostKeyNone};
    int assist_pad[2] = {0, 0};
    // Analog half-axis threshold in the backend's native units ("about half of
    // full scale"). SDL uses 16000 of 32768; a backend with different units
    // scales this.
    int assist_axis_threshold = 16000;
};

// Prior-pump state. Edge-triggered Events are impossible without it, and the
// only thing that may mutate it is translate_input().
struct InputEdges {
    bool quit_was = false;
    bool hotkey_was[HK_COUNT] = {};
    // Prior held-state of F1..F9. ONE flag per key, not one per action: the
    // save/load choice is made from the modifier state at the moment of the
    // PRESS, matching SDL's single KEYDOWN (repeat==0) per physical press.
    bool slot_was[9] = {};
    bool rewind_was_down = false;
    std::uint32_t rewind_repeat_at = 0;
};

// Half of "Alt+Return" is the modifier set, half is the key. A binding only
// fires when EVERY required modifier is held and every modifier it does NOT
// require is NOT held -- so "P" and "Shift+P" stay distinct bindings, exactly
// as hotkey_mods_ok does in the SDL backend.
inline bool hotkey_mods_ok(const HotkeyBinding& hb, unsigned mods) {
    auto want = [&](unsigned m) { return (hb.mods & m) != 0; };
    auto held = [&](unsigned m) { return (mods & m) != 0; };
    return want(kModCtrl) == held(kModCtrl) &&
           want(kModAlt) == held(kModAlt) &&
           want(kModShift) == held(kModShift);
}

inline bool hotkey_down(const InputSnapshot& s, const InputBindings& b, int h) {
    const HotkeyBinding& hb = b.hotkeys[h];
    return hb.key != kHostKeyNone && s.is_held(hb.key) &&
           hotkey_mods_ok(hb, s.mods);
}

// The settings-overlay trigger, given the two pad buttons that can carry it:
// the pad's menu/guide button and Y. GUIDE is primary; Y covers pads without
// a usable menu button, and it is the free face button (no GBA meaning), so
// the shortcut costs no game input. The backends turn a held trigger into the
// CONFIGURED HK_MENU key, so the menu's own binding decides what "menu" means
// and the hotkey edge gives press-open / press-close.
inline bool pad_opens_menu(bool guide_down, bool y_down) {
    return guide_down || y_down;
}

// Which modality owns the routed haptic: the pad is primary only when its
// most recent activity is strictly newer than the touchscreen's. Stamps are
// monotonic milliseconds; the subtraction is wrap-safe. Defaults (0, 0) and
// simultaneous activity keep the touchscreen primary, matching "touchscreen
// is the default on Android".
inline bool pad_is_primary(std::uint32_t last_touch_ms,
                           std::uint32_t last_pad_ms) {
    return static_cast<std::int32_t>(last_pad_ms - last_touch_ms) > 0;
}

// Assist pad binding evaluation, given portable pad state. The ENCODING of a
// binding lives in assist_pad_binding.h; this is the platform-free evaluation.
inline bool assist_pad_down(const InputBindings& b, int index,
                            const InputSnapshot& s) {
    AssistAxisBinding axis;
    if (decode_assist_pad_axis(b.assist_pad[index], &axis)) {
        if (axis.axis_code < 0 || axis.axis_code >= kPadAxisCount) return false;
        return assist_axis_down(s.pad_axis[axis.axis_code], axis.positive,
                                b.assist_axis_threshold);
    }
    int button = 0;
    if (decode_assist_pad_button(b.assist_pad[index], &button))
        return button >= 0 && button < kPadButtonCount && s.pad_button[button];
    return false;
}

// Rewind repeats every 500 ms while held, so one-second rewind steps walk
// backward at roughly normal speed. Fires immediately on press.
inline bool assist_rewind_fires(bool down, std::uint32_t now_ms,
                                 InputEdges& edges) {
    const bool fire =
        down && (!edges.rewind_was_down ||
                 static_cast<std::int32_t>(now_ms - edges.rewind_repeat_at) >= 0);
    if (fire) edges.rewind_repeat_at = now_ms + 500;
    if (!down) edges.rewind_repeat_at = 0;
    edges.rewind_was_down = down;
    return fire;
}

// The whole translation. Pure apart from mutating `edges` (prior state), which
// is the entire reason edges exist.
inline WindowBackend::Events translate_input(const InputSnapshot& s,
                                             const InputBindings& b,
                                             InputEdges& edges) {
    WindowBackend::Events ev;

    // ---- GBA KEYINPUT (active low: a cleared bit means pressed) ----------
    std::uint16_t keys = 0x03FFu;
    for (int bit = 0; bit < 10; ++bit) {
        if (b.gba_key[bit] != kHostKeyNone && s.is_held(b.gba_key[bit]))
            keys &= static_cast<std::uint16_t>(~(1u << bit));
    }
    keys &= static_cast<std::uint16_t>(~s.pad_gba_bits);
    keys &= static_cast<std::uint16_t>(~s.touch_gba_bits);
    // Raw levels for host UI (the generic menu navigates from these even
    // while game input is suppressed); the masked value is the game's.
    ev.menu_keyinput = keys;
    ev.keyinput = s.suppress_game_input ? 0x03FFu : keys;

    // ---- quit -------------------------------------------------------------
    const bool quit_now = s.close_requested ||
                          (b.escape_key != kHostKeyNone && s.is_held(b.escape_key));
    // close_requested is a level (the window is still closed); a held Escape is
    // an edge. Treat the pair as one edge so Escape does not fire every pump.
    if (quit_now && !edges.quit_was) ev.quit = true;
    edges.quit_was = quit_now;

    // ---- F1..F9 save/load slots ------------------------------------------
    // Edge on the key PRESS, deciding save vs load from the shift state at that
    // instant. Tracking a separate "was load held" and "was save held" per slot
    // looks equivalent but is not: it fires a load when the player releases
    // Shift while still holding F3, which SDL never does (it delivers a single
    // KEYDOWN for that press).
    for (int i = 0; i < 9; ++i) {
        const bool down =
            b.slot_key[i] != kHostKeyNone && s.is_held(b.slot_key[i]);
        if (down && !edges.slot_was[i]) {
            if (s.mods & kModShift) ev.save_slot = i + 1;
            else                    ev.load_slot = i + 1;
        }
        edges.slot_was[i] = down;
    }

    // ---- rebindable system hotkeys ---------------------------------------
    for (int h = 0; h < HK_COUNT; ++h) {
        if (h == HK_TURBO) continue;             // level-triggered, below
        const bool now = hotkey_down(s, b, h);
        const bool edge = now && !edges.hotkey_was[h];
        edges.hotkey_was[h] = now;
        if (!edge) continue;
        switch (h) {
            case HK_FULLSCREEN:     ev.toggle_fullscreen = true; break;
            case HK_PAUSE:          ev.toggle_pause = true;      break;
            case HK_WINDOW_BIGGER:  ev.window_bigger = true;     break;
            case HK_WINDOW_SMALLER: ev.window_smaller = true;    break;
            case HK_VOLUME_UP:      ev.volume_up = true;         break;
            case HK_VOLUME_DOWN:    ev.volume_down = true;       break;
            case HK_DISPLAY_PERF:   ev.toggle_fps = true;        break;
            case HK_SOLAR_BRIGHTER: ev.solar_brighter = true;    break;
            case HK_SOLAR_DIMMER:   ev.solar_dimmer = true;      break;
            case HK_SOLAR_LIVE:     ev.solar_live = true;        break;
            case HK_MENU:           ev.toggle_menu = true;         break;
            default: break;
        }
    }

    // ---- turbo / fast-forward (level) ------------------------------------
    // The historical [KeyMap] Turbo binding stays as a shortcut; the
    // launcher-editable Assist binding is additive.
    ev.fast_forward = hotkey_down(s, b, HK_TURBO) ||
                      (b.assist_key[1] != kHostKeyNone && s.is_held(b.assist_key[1])) ||
                      assist_pad_down(b, 1, s);

    // ---- rewind (repeating) ----------------------------------------------
    const bool rewind_down =
        (b.assist_key[0] != kHostKeyNone && s.is_held(b.assist_key[0])) ||
        assist_pad_down(b, 0, s);
    ev.rewind = assist_rewind_fires(rewind_down, s.now_ms, edges);

    // ---- mouse gyro --------------------------------------------------------
    // SDL used SDL_GetRelativeMouseState; raylib's GetMouseDeltaMovement is
    // the same quantity. Horizontal-only, and never while touch is driving.
    ev.mouse_gyro_active = !s.touch_enabled && s.mouse_left;
    ev.gyro_delta_x = ev.mouse_gyro_active ? s.mouse_dx : 0;
    // gyro_rate_z stays 0: raylib exposes no gyroscope API, so there is no
    // honest value to report and silently faking motion would be worse.

    ev.enter_background = s.enter_background;
    ev.terminating = s.terminating;
    return ev;
}

}  // namespace gbarecomp