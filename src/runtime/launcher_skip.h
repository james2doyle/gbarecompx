// launcher_skip.h — the "Skip this screen on launch when ready" state
// (framework).
//
// GbaSetupActivity parity: the same row wording, the same default (ON —
// the activity's getBoolean(SKIP_LAUNCHER_ON_BOOT, true)), and the same
// decision shape — the screen is skipped only when the player asked for it
// AND the game is ready (a validated ROM is in hand). Desktop state is one
// line in a sidecar file beside the ROM cache (rom.cfg), read BEFORE any
// window opens: a skip returns Launch without ever showing the screen.
//
// Raylib-free on purpose: the wording, default, codec, and decision are
// testable headless (tests/runtime/launcher_skip_test.cpp).
#pragma once

#include <cstdint>
#include <string>

namespace gbarecomp {
namespace launcher_skip {

// The activity's exact checkbox wording (GbaSetupActivity).
constexpr const char* kSkipLabel = "Skip this screen on launch when ready";

// The activity's default: the checkbox starts checked.
constexpr bool kDefault = true;

// The sidecar state file (in the launcher cache root, beside rom.cfg —
// named after the activity's pref key for greppability).
constexpr const char* kStateFilename = "skip_launcher_on_boot";

// One-line sidecar codec: "1" / "0", whitespace-tolerant. A missing,
// empty, or unrecognized file reads as the default — a corrupted file
// never silently disables the skip (the activity's prefs behave the same).
inline bool parse_state(const std::string& content) {
    std::size_t b = content.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return kDefault;
    std::size_t e = content.find_last_not_of(" \t\r\n");
    const std::string trimmed = content.substr(b, e - b + 1);
    return trimmed != "0";
}

inline std::string serialize_state(bool value) {
    return value ? "1" : "0";
}

// The skip decision. Unchecked NEVER skips — that is the whole point of
// unchecking (the screen must stay reachable for swapping ROMs). Checked
// without a ready ROM cannot skip either ("when ready").
inline bool should_skip_screen(bool state, bool rom_ready) {
    return state && rom_ready;
}

}  // namespace launcher_skip
}  // namespace gbarecomp
