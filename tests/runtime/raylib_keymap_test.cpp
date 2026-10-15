// Key-name mapping for the raylib backend. Pure, no display: the whole point
// is that ini vocabulary can be verified without a window.
//
// The SDL backend is the reference. These tests pin that a keybinds.ini or
// config.ini written for the SDL backend resolves to the SAME physical key on
// the raylib backend -- a silent remap would change the player's controls
// with no error.

#include "raylib_keymap.h"

#include <cstdio>

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

}  // namespace

int main() {
    // ---- single letters and digits -----------------------------------------
    check(raylib_key_from_name("X") == 88, "X -> 88");
    check(raylib_key_from_name("x") == 88, "lowercase x -> 88");
    check(raylib_key_from_name("a") == 65, "a -> 65");
    check(raylib_key_from_name("Z") == 90, "Z -> 90");
    check(raylib_key_from_name("P") == 80, "P -> 80");
    check(raylib_key_from_name("1") == 49, "1 -> 49");
    check(raylib_key_from_name("0") == 48, "0 -> 48");

    // ---- SDL aliases both sides accept --------------------------------------
    check(raylib_key_from_name("Return") == 257, "Return -> ENTER");
    check(raylib_key_from_name("enter") == 257, "enter -> ENTER");
    check(raylib_key_from_name("Tab") == 258, "Tab -> TAB");
    check(raylib_key_from_name("tab") == 258, "tab -> TAB");
    check(raylib_key_from_name("Space") == 32, "Space -> SPACE");
    check(raylib_key_from_name("space") == 32, "space -> SPACE");
    check(raylib_key_from_name("Escape") == 256, "Escape -> ESCAPE");
    check(raylib_key_from_name("esc") == 256, "esc -> ESCAPE");
    check(raylib_key_from_name("Backspace") == 259, "Backspace");
    check(raylib_key_from_name("backspace") == 259, "backspace");
    check(raylib_key_from_name("Backslash") == 92, "Backslash");
    check(raylib_key_from_name("backslash") == 92, "backslash");
    check(raylib_key_from_name("LShift") == 340, "LShift -> LEFT_SHIFT");
    check(raylib_key_from_name("RShift") == 344, "RShift -> RIGHT_SHIFT");
    check(raylib_key_from_name("lshift") == 340, "lshift");
    check(raylib_key_from_name("rshift") == 344, "rshift");
    check(raylib_key_from_name("LCtrl") == 341, "LCtrl -> LEFT_CONTROL");
    check(raylib_key_from_name("RCtrl") == 345, "RCtrl -> RIGHT_CONTROL");
    check(raylib_key_from_name("LAlt") == 342, "LAlt -> LEFT_ALT");
    check(raylib_key_from_name("RAlt") == 346, "RAlt -> RIGHT_ALT");
    // Spaced SDL spellings (SDL_GetScancodeName + the shipped keybinds.ini
    // header). Missing these killed Select for every default user: the
    // default file ships `select = Right Shift`.
    check(raylib_key_from_name("Right Shift") == 344, "Right Shift -> RIGHT_SHIFT");
    check(raylib_key_from_name("Left Shift") == 340, "Left Shift -> LEFT_SHIFT");
    check(raylib_key_from_name("Right Ctrl") == 345, "Right Ctrl");
    check(raylib_key_from_name("Left Ctrl") == 341, "Left Ctrl");
    check(raylib_key_from_name("Right Alt") == 346, "Right Alt");
    check(raylib_key_from_name("Left Alt") == 342, "Left Alt");
    check(raylib_key_from_name("right shift") == 344, "case-insensitive spaced");
    check(raylib_key_from_name("Right") == 262, "Right arrow");
    check(raylib_key_from_name("Left") == 263, "Left arrow");
    check(raylib_key_from_name("Down") == 264, "Down arrow");
    check(raylib_key_from_name("Up") == 265, "Up arrow");
    check(raylib_key_from_name("F1") == 290, "F1");
    check(raylib_key_from_name("F9") == 298, "F9");
    check(raylib_key_from_name("F12") == 301, "F12");

    // ---- unbound -------------------------------------------------------------
    check(raylib_key_from_name(nullptr) == kRayKeyNone, "null -> unbound");
    check(raylib_key_from_name("") == kRayKeyNone, "empty -> unbound");
    check(raylib_key_from_name("None") == kRayKeyNone, "None -> unbound");
    check(raylib_key_from_name("Qwertyuiop") == kRayKeyNone,
          "unknown -> unbound, never a wrong key");

    // ---- hotkey parsing mirrors the SDL format -------------------------------
    {
        const HotkeyBinding hb = parse_raylib_hotkey("Alt+Return");
        check(hb.key == 257 && hb.mods == kModAlt, "Alt+Return parses");
    }
    {
        const HotkeyBinding hb = parse_raylib_hotkey("Shift+P");
        check(hb.key == 80 && hb.mods == kModShift, "Shift+P parses");
    }
    {
        const HotkeyBinding hb = parse_raylib_hotkey("Tab");
        check(hb.key == 258 && hb.mods == kModNone, "bare Tab parses");
    }
    {
        const HotkeyBinding hb = parse_raylib_hotkey("F");
        check(hb.key == 70 && hb.mods == kModNone, "bare F parses");
    }
    {
        const HotkeyBinding hb = parse_raylib_hotkey("Ctrl+Alt+Delete");
        check(hb.key == kRayKeyNone, "unknown key part -> unbound");
    }
    check(parse_raylib_hotkey("").key == kRayKeyNone, "empty hotkey unbound");
    check(parse_raylib_hotkey(nullptr).key == kRayKeyNone, "null hotkey unbound");
    check(parse_raylib_hotkey("Shift+None").key == kRayKeyNone,
          "Shift+None is unbound, not Shift held alone");

    // ---- hotkey names and defaults match the SDL backend ----------------------
    check(std::string(raylib_hotkey_name(HK_FULLSCREEN)) == "Fullscreen",
          "hotkey name Fullscreen");
    check(std::string(raylib_hotkey_name(HK_TURBO)) == "Turbo",
          "hotkey name Turbo");
    check(std::string(raylib_hotkey_default(HK_FULLSCREEN)) == "Alt+Return",
          "default Fullscreen is Alt+Return");
    check(std::string(raylib_hotkey_default(HK_PAUSE)) == "Shift+P",
          "default Pause is Shift+P");
    check(std::string(raylib_hotkey_default(HK_TURBO)) == "Tab",
          "default Turbo is Tab");
    check(std::string(raylib_hotkey_default(HK_DISPLAY_PERF)) == "F",
          "default DisplayPerf is F");
    check(std::string(raylib_hotkey_default(HK_VOLUME_UP)) == "",
          "default VolumeUp is unbound");

    // ---- GBA button names ------------------------------------------------------
    check(raylib_bind_bit("a") == 0, "bind name a -> bit 0");
    check(raylib_bind_bit("A") == 0, "bind name A -> bit 0");
    check(raylib_bind_bit("select") == 2, "bind name select");
    check(raylib_bind_bit("start") == 3, "bind name start");
    check(raylib_bind_bit("l") == 9, "bind name l -> bit 9");
    check(raylib_bind_bit("x") == -1, "x has no GBA equivalent");
    check(raylib_bind_bit("l2") == -1, "l2 has no GBA equivalent");
    check(raylib_bind_bit("bogus") == -1, "unknown bind name ignored");

    // ---- defaults match the SDL backend's layout -------------------------------
    {
        InputBindings b;
        raylib_default_bindings(b);
        check(b.gba_key[0] == 88, "default A is X");
        check(b.gba_key[1] == 90, "default B is Z");
        check(b.gba_key[2] == 344, "default Select is RShift");
        check(b.gba_key[3] == 257, "default Start is Return");
        check(b.gba_key[4] == 262, "default Right is Right");
        check(b.gba_key[5] == 263, "default Left is Left");
        check(b.gba_key[6] == 265, "default Up is Up");
        check(b.gba_key[7] == 264, "default Down is Down");
        check(b.gba_key[8] == 86, "default R is V");
        check(b.gba_key[9] == 67, "default L is C");
        check(b.slot_key[0] == 290 && b.slot_key[8] == 298,
              "default slots are F1..F9");
        check(b.escape_key == 256, "default quit is Escape");
        check(b.hotkeys[HK_TURBO].key == 258, "default Turbo is Tab");
        check(b.assist_key[0] == 49 && b.assist_key[1] == 50,
              "default assist keys are 1 and 2");
        check(b.assist_axis_threshold == 16000,
              "assist axis threshold matches SDL half-scale");
    }

    // ---- SDL scancode integers (on-disk assist keys) ------------------------------
    // Values verified against /usr/include/SDL2/SDL_scancode.h.
    check(sdl_scancode_to_raylib_key(27) == 88, "SDL X (27) -> raylib X");
    check(sdl_scancode_to_raylib_key(29) == 90, "SDL Z (29) -> raylib Z");
    check(sdl_scancode_to_raylib_key(30) == 49, "SDL 1 (30) -> raylib 1");
    check(sdl_scancode_to_raylib_key(31) == 50, "SDL 2 (31) -> raylib 2");
    check(sdl_scancode_to_raylib_key(39) == 48, "SDL 0 (39) -> raylib 0");
    check(sdl_scancode_to_raylib_key(4) == 65, "SDL A (4) -> raylib A");
    check(sdl_scancode_to_raylib_key(40) == 257, "SDL RETURN (40) -> ENTER");
    check(sdl_scancode_to_raylib_key(41) == 256, "SDL ESCAPE (41) -> ESCAPE");
    check(sdl_scancode_to_raylib_key(43) == 258, "SDL TAB (43) -> TAB");
    check(sdl_scancode_to_raylib_key(44) == 32, "SDL SPACE (44) -> SPACE");
    check(sdl_scancode_to_raylib_key(58) == 290, "SDL F1 (58) -> F1");
    check(sdl_scancode_to_raylib_key(79) == 262, "SDL RIGHT (79)");
    check(sdl_scancode_to_raylib_key(82) == 265, "SDL UP (82)");
    check(sdl_scancode_to_raylib_key(225) == 340, "SDL LSHIFT (225)");
    check(sdl_scancode_to_raylib_key(229) == 344, "SDL RSHIFT (229)");
    check(sdl_scancode_to_raylib_key(0) == kRayKeyNone,
          "SDL UNKNOWN (0) -> unbound");
    check(sdl_scancode_to_raylib_key(9999) == kRayKeyNone,
          "out-of-range scancode -> unbound, never a wrong key");

    // ---- whole-file regression: the shipped default keybinds.ini ------------
    // These are the EXACT [player1] lines shipped at the repo root (values
    // are SDL scancode names, incl. the spaced "Right Shift"). Every GBA
    // button must resolve to a bound key. Missing the spaced modifiers
    // killed Select for every default user -- this is the test that would
    // have caught it.
    {
        InputBindings b;
        raylib_default_bindings(b);
        const char* keys[10] = {"a", "b", "select", "start", "right", "left",
                                "up", "down", "r", "l"};
        const char* vals[10] = {"X", "Z", "Right Shift", "Return", "Right",
                                "Left", "Up", "Down", "V", "C"};
        for (int i = 0; i < 10; ++i)
            check(apply_keybinds_line(b, keys[i], vals[i]),
                  "shipped line applies");
        check(b.gba_key[0] == 88, "file A is X");
        check(b.gba_key[1] == 90, "file B is Z");
        check(b.gba_key[2] == 344, "file Select is Right Shift, not unbound");
        check(b.gba_key[3] == 257, "file Start is Return");
        check(b.gba_key[4] == 262 && b.gba_key[5] == 263 &&
                  b.gba_key[6] == 265 && b.gba_key[7] == 264,
              "file d-pad is arrows");
        check(b.gba_key[8] == 86 && b.gba_key[9] == 67, "file R/L are V/C");
        // Non-GBA names are ignored, never misrouted.
        check(!apply_keybinds_line(b, "x", "A"),
              "x ignored (no GBA equivalent)");
        check(!apply_keybinds_line(b, "l2", "B"), "l2 ignored");
        check(!apply_keybinds_line(b, "bogus", "Z"), "unknown name ignored");
        check(b.gba_key[0] == 88, "ignored lines change nothing");
    }

    // ---- player rebinds: Start=D, Select=Q ------------------------------------
    // A real user configuration (single-letter rebinds). Must resolve to
    // the same physical keys the SDL backend reads.
    {
        InputBindings b;
        raylib_default_bindings(b);
        check(apply_keybinds_line(b, "start", "D"), "rebind start applies");
        check(apply_keybinds_line(b, "select", "Q"), "rebind select applies");
        check(b.gba_key[3] == 68, "Start is D");
        check(b.gba_key[2] == 81, "Select is Q");
    }

    // ---- [KeyMap] + [Launcher] lines ----------------------------------------------
    {
        InputBindings b;
        raylib_default_bindings(b);
        check(apply_keymap_line(b, "Turbo", "Tab"), "Turbo line applies");
        check(b.hotkeys[HK_TURBO].key == 258, "Turbo is Tab");
        check(apply_keymap_line(b, "Pause", "Shift+P"), "Pause line applies");
        check(!apply_keymap_line(b, "FutureHotkey", "F1"),
              "unknown hotkey ignored");
        bool tools = true;
        int mult = 4;
        apply_launcher_line(b, tools, mult, "assist_rewind_key", "30");
        check(b.assist_key[0] == 49, "SDL scancode 30 reads as raylib 1");
        apply_launcher_line(b, tools, mult, "assist_fast_forward_multiplier",
                            "99");
        check(mult == 10, "multiplier clamps to 10");
        apply_launcher_line(b, tools, mult, "assist_tools", "0");
        check(!tools, "assist_tools off parses");
    }

    std::printf("raylib_keymap tests: %s (%d failure(s))\n",
                failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
