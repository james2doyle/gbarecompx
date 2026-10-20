// Pure input translation. No window, no SDL, no raylib, no display -- which is
// the point: the SDL backend's behaviour is the reference, and this pins it in a
// form any backend can be checked against.

#include "input_translate.h"

#include <cstdio>
#include <cstring>

using namespace gbarecomp;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (ok) {
        std::printf("  ok    %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++failures;
    }
}

// Key ids stand in for SDL_Keycode / raylib KeyCode. Only identity matters.
enum : HostKey {
    kNone = kHostKeyNone,
    kA = 100, kB, kSelect, kStart, kRight, kLeft, kUp, kDown, kR, kL,
    kF1, kF2, kF3, kF4, kF5, kF6, kF7, kF8, kF9, kDelete,
    kTab, kP, kReturn, kF, kAssistRewind, kAssistFast,
};

// Mirrors the SDL backend's kDefaultBinds / kHotkeyDefaults.
InputBindings default_bindings() {
    InputBindings b;
    b.gba_key[0] = kA;       // A
    b.gba_key[1] = kB;       // B
    b.gba_key[2] = kSelect;  // Select
    b.gba_key[3] = kStart;   // Start
    b.gba_key[4] = kRight;
    b.gba_key[5] = kLeft;
    b.gba_key[6] = kUp;
    b.gba_key[7] = kDown;
    b.gba_key[8] = kR;
    b.gba_key[9] = kL;
    for (int i = 0; i < 9; ++i) b.slot_key[i] = kF1 + i;
    b.escape_key = 27;                       // SDLK_ESCAPE's role
    b.hotkeys[HK_FULLSCREEN] = {kReturn, kModAlt};
    b.hotkeys[HK_PAUSE] = {kP, kModShift};
    b.hotkeys[HK_TURBO] = {kTab, kModNone};
    b.hotkeys[HK_DISPLAY_PERF] = {kF, kModNone};
    return b;
}

InputSnapshot snap(std::uint32_t now_ms = 0) {
    InputSnapshot s;
    s.now_ms = now_ms;
    return s;
}

constexpr std::uint16_t kBitA = 1u << 0;
constexpr std::uint16_t kBitB = 1u << 1;

}  // namespace

int main() {
    const InputBindings B = default_bindings();
    InputEdges edges;

    // ---- KEYINPUT is active-low, all-released baseline --------------------
    {
        InputEdges e;
        const auto ev = translate_input(snap(), B, e);
        check(ev.keyinput == 0x03FFu, "baseline keyinput is all-released (0x03FF)");
        check(!ev.quit && ev.save_slot == 0 && ev.load_slot == 0,
              "baseline raises no edges");
    }

    // ---- a held GBA key clears its bit ------------------------------------
    {
        InputEdges e;
        auto s = snap();
        s.hold(kA);
        const auto ev = translate_input(s, B, e);
        check(ev.keyinput == static_cast<std::uint16_t>(0x03FFu & ~kBitA),
              "held A clears bit 0");
    }

    // ---- keyboard and gamepad are additive ---------------------------------
    {
        InputEdges e;
        auto s = snap();
        s.hold(kA);
        s.pad_connected = true;
        s.pad_gba_bits = kBitB;          // as gamepad_map.h would produce
        const auto ev = translate_input(s, B, e);
        check(ev.keyinput ==
                  static_cast<std::uint16_t>(0x03FFu & ~(kBitA | kBitB)),
              "keyboard A + pad B both apply");
    }

    // ---- touch bits apply, and can be suppressed ---------------------------
    {
        InputEdges e;
        auto s = snap();
        s.touch_gba_bits = kBitA;
        check(translate_input(s, B, e).keyinput ==
                  static_cast<std::uint16_t>(0x03FFu & ~kBitA),
              "touch pad bits apply");

        InputEdges e2;
        auto s2 = snap();
        s2.touch_gba_bits = kBitA;
        s2.suppress_game_input = true;
        const auto sup = translate_input(s2, B, e2);
        check(sup.keyinput == 0x03FFu,
              "settings menu suppresses all game input");
        check(sup.menu_keyinput == static_cast<std::uint16_t>(0x03FFu & ~kBitA),
              "suppressed input still carries the raw menu levels");
    }

    // ---- quit is an EDGE, not a level -------------------------------------
    // Escape held for many pumps must request quit exactly once, or the game
    // would tear down on every frame the player holds it.
    {
        InputEdges e;
        auto s = snap();
        s.hold(27);
        check(translate_input(s, B, e).quit, "Escape requests quit on press");
        check(!translate_input(s, B, e).quit, "held Escape does not re-request");
        s.hold(kNone);                     // (no-op; key list rebuilt below)
        InputSnapshot up = snap();
        check(!translate_input(up, B, e).quit, "released Escape stays quiet");
        InputSnapshot again = snap();
        again.hold(27);
        check(translate_input(again, B, e).quit, "second press requests again");
    }

    // ---- window close request ---------------------------------------------
    {
        InputEdges e;
        auto s = snap();
        s.close_requested = true;
        check(translate_input(s, B, e).quit, "close request asks to quit");
        check(!translate_input(s, B, e).quit, "close request does not repeat");
    }

    // ---- F1..F9 slots: load plain, save with Shift -------------------------
    {
        InputEdges e;
        auto s = snap();
        s.hold(kF3);
        auto ev = translate_input(s, B, e);
        check(ev.load_slot == 3 && ev.save_slot == 0, "F3 loads slot 3");
        check(!translate_input(s, B, e).load_slot, "held F3 does not reload");

        // SDL delivers ONE KEYDOWN for a physical press, so a press that
        // starts plain and later gains Shift is still just a load. Getting this
        // wrong is invisible locally but would mis-save a player's state.
        s.mods = kModShift;
        ev = translate_input(s, B, e);
        check(ev.save_slot == 0 && ev.load_slot == 0,
              "adding Shift mid-hold fires nothing");
        s.mods = 0;
        check(translate_input(s, B, e).load_slot == 0,
              "releasing Shift mid-hold does not re-fire load");

        // ...but a press that STARTS with Shift held is a save.
        InputEdges e2;
        auto s2 = snap();
        s2.hold(kF5);
        s2.mods = kModShift;
        const auto ev2 = translate_input(s2, B, e2);
        check(ev2.save_slot == 5 && ev2.load_slot == 0,
              "Shift+F5 saves slot 5");
    }

    // ---- modifier-exact hotkeys: P and Shift+P are distinct ----------------
    {
        InputEdges e;
        auto s = snap();
        s.hold(kP);
        check(!translate_input(s, B, e).toggle_pause,
              "P alone does not trigger Shift+P binding");
        s.mods = kModShift;
        check(translate_input(s, B, e).toggle_pause, "Shift+P pauses");
        check(!translate_input(s, B, e).toggle_pause, "held Shift+P fires once");

        InputEdges e2;
        auto s2 = snap();
        s2.hold(kReturn);
        s2.mods = kModAlt;
        check(translate_input(s2, B, e2).toggle_fullscreen, "Alt+Return fullscreen");

        InputEdges e3;
        auto s3 = snap();
        s3.hold(kReturn);                   // right key, wrong modifier
        check(!translate_input(s3, B, e3).toggle_fullscreen,
              "Return alone does not fire Alt+Return");
    }

    // ---- menu hotkey is edge-triggered -----------------------------------
    {
        InputBindings b = B;
        b.hotkeys[HK_MENU] = {kDelete, kModNone};
        InputEdges e;
        auto s = snap();
        s.hold(kDelete);
        check(translate_input(s, b, e).toggle_menu, "Delete toggles the menu");
        check(!translate_input(s, b, e).toggle_menu, "held Delete fires once");
        InputSnapshot up = snap();
        check(!translate_input(up, b, e).toggle_menu, "release clears it");
    }

    // ---- guide or Y opens the settings overlay ----------------------------
    {
        check(pad_opens_menu(true, false), "Guide opens the menu");
        check(pad_opens_menu(false, true), "Y opens the menu");
        check(pad_opens_menu(true, true), "Guide+Y still opens it");
        check(!pad_opens_menu(false, false), "neither held means no menu");
    }

    // ---- primary modality is last-used, touchscreen by default -----------
    {
        check(!pad_is_primary(0, 0), "no activity keeps touchscreen primary");
        check(pad_is_primary(0, 10), "newer pad activity wins");
        check(!pad_is_primary(10, 0), "newer touch activity wins");
        check(!pad_is_primary(10, 10), "simultaneous activity keeps touch");
        check(pad_is_primary(0xFFFFFFF0u, 0x00000010u),
              "wrap-safe pad stamp comparison");
    }

    // ---- turbo is LEVEL triggered ------------------------------------------
    {
        InputEdges e;
        auto s = snap();
        s.hold(kTab);
        check(translate_input(s, B, e).fast_forward, "Tab fast-forwards");
        check(translate_input(s, B, e).fast_forward, "and keeps fast-forwarding");
        InputSnapshot up = snap();
        check(!translate_input(up, B, e).fast_forward, "release stops it");
    }

    // ---- assist fast-forward binding is additive with turbo ----------------
    {
        InputBindings b = B;
        b.assist_key[1] = kAssistFast;
        InputEdges e;
        auto s = snap();
        s.hold(kAssistFast);
        check(translate_input(s, B, e).fast_forward == false,
              "sanity: unbound key in a copy does not fire");
        InputEdges e2;
        check(translate_input(s, b, e2).fast_forward,
              "assist fast-forward key works");
    }

    // ---- rewind fires immediately then every 500ms ------------------------
    {
        InputEdges e;
        auto s = snap(1000);
        s.hold(kAssistRewind);
        InputBindings b = B;
        b.assist_key[0] = kAssistRewind;

        check(translate_input(s, b, e).rewind, "rewind fires on press");
        s.now_ms = 1100;
        check(!translate_input(s, b, e).rewind, "does not re-fire before 500ms");
        s.now_ms = 1500;
        check(translate_input(s, b, e).rewind, "re-fires at 500ms");

        InputSnapshot up;
        up.now_ms = 1600;
        check(!translate_input(up, b, e).rewind, "release stops rewind");
        up.now_ms = 5000;
        s.now_ms = 5000;
        s.held_key_count = 0;
        s.hold(kAssistRewind);
        check(translate_input(s, b, e).rewind, "press after release fires at once");
    }

    // ---- assist pad bindings (the on-disk encoding) ------------------------
    {
        InputBindings b = B;
        b.assist_key[0] = kNone;
        b.assist_pad[0] = 1 + kPadRightBumper;      // digital, value-1 = code
        InputEdges e;
        auto s = snap(0);
        s.pad_connected = true;
        s.hold_pad_button(kPadRightBumper);
        check(translate_input(s, b, e).rewind, "pad button drives rewind");

        // Analog half-axis: the right trigger, positive half.
        InputBindings a = B;
        a.assist_key[0] = kNone;
        a.assist_pad[0] = assist_pad_axis(kPadAxisRightTrigger, true);
        InputEdges e2;
        auto s2 = snap(0);
        s2.pad_connected = true;
        s2.hold_pad_axis(kPadAxisRightTrigger, 20000);   // over threshold
        check(translate_input(s2, a, e2).rewind, "trigger half-axis drives rewind");

        InputEdges e3;
        auto s3 = snap(0);
        s3.pad_connected = true;
        s3.hold_pad_axis(kPadAxisRightTrigger, 5000);    // under threshold
        check(!translate_input(s3, a, e3).rewind, "half-pressed trigger does not");

        InputEdges e4;
        auto s4 = snap(0);
        s4.pad_connected = true;
        s4.hold_pad_axis(kPadAxisRightTrigger, -20000);  // negative half
        check(!translate_input(s4, a, e4).rewind,
              "negative half does not fire a positive binding");
    }

    // ---- mouse gyro, gated on touch being enabled --------------------------
    {
        InputEdges e;
        auto s = snap();
        s.mouse_left = true;
        s.mouse_dx = 7;
        auto ev = translate_input(s, B, e);
        check(ev.mouse_gyro_active && ev.gyro_delta_x == 7,
              "left-drag reports horizontal gyro");

        InputEdges e2;
        auto s2 = snap();
        s2.mouse_left = true;
        s2.mouse_dx = 7;
        s2.touch_enabled = true;
        auto ev2 = translate_input(s2, B, e2);
        check(!ev2.mouse_gyro_active && ev2.gyro_delta_x == 0,
              "touch-enabled suppresses the mouse gyro stand-in");

        InputEdges e3;
        auto s3 = snap();
        s3.mouse_dx = 5;                        // moving, not dragging
        auto ev3 = translate_input(s3, B, e3);
        check(!ev3.mouse_gyro_active && ev3.gyro_delta_x == 0,
              "motion without the button held is ignored");
    }

    // ---- no gyro source: rate must stay exactly zero, never faked ----------
    {
        InputEdges e;
        check(translate_input(snap(), B, e).gyro_rate_z == 0.0f,
              "gyro_rate_z is zero with no gyro hardware");
    }

    // ---- lifecycle passthrough --------------------------------------------
    {
        InputEdges e;
        auto s = snap();
        s.enter_background = true;
        s.terminating = true;
        const auto ev = translate_input(s, B, e);
        check(ev.enter_background && ev.terminating,
              "lifecycle flags pass through unchanged");
    }

    // ---- helper edge cases -------------------------------------------------
    {
        InputSnapshot s;
        s.hold(kNone);
        check(s.held_key_count == 0, "kHostKeyNone is never held");
        check(!s.is_held(kNone), "is_held(kHostKeyNone) is false");

        // Overflow guard: more keys than the snapshot can hold must not wrap.
        InputSnapshot big;
        for (int i = 0; i < InputSnapshot::kMaxHeldKeys + 10; ++i)
            big.hold(1000 + i);
        check(big.held_key_count == InputSnapshot::kMaxHeldKeys,
              "held-key list clamps instead of overflowing");

        // Out-of-range pad codes from a corrupt config must be ignored.
        InputBindings b = B;
        b.assist_pad[0] = 1 + 9999;
        InputEdges e;
        check(!translate_input(snap(0), b, e).rewind,
              "out-of-range pad button code is ignored");
        b.assist_pad[0] = assist_pad_axis(9999, true);
        InputEdges e2;
        check(!translate_input(snap(0), b, e2).rewind,
              "out-of-range pad axis code is ignored");

        HotkeyBinding hb{100, kModCtrl};
        check(hotkey_mods_ok(hb, kModCtrl), "exact modifier match passes");
        check(!hotkey_mods_ok(hb, kModCtrl | kModShift),
              "extra modifier breaks the match");
    }

    std::printf("input_translate tests: %s (%d failure(s))\n",
                failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}