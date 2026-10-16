// launcher_style.h — the launch screen's shared visual language (framework).
//
// The desktop raygui launch screen (launcher_raygui.cpp) and the Android
// setup activity (platform/android/java/org/gbarecomp/GbaSetupActivity.java)
// are the same pre-boot screen on two platforms — "exactly one pre-boot
// surface per platform". The activity transcribed the desktop layout
// natively and evolved the full visual language; this header is the
// desktop's transcription of that language back — palette constant-for-
// constant (source-pinned by tools/check_launch_screen_wiring.py), so the
// two surfaces cannot drift silently.
//
// Raylib-free on purpose: the palette and the status decision are testable
// headless (tests/runtime/launcher_style_test.cpp); the backend maps the
// values to Colors.
#pragma once

#include <cstdint>
#include <string>

namespace gbarecomp {
namespace launcher_style {

// The GbaSetupActivity palette (buildContent() + status colors), by role.
constexpr std::uint32_t kBackground = 0x070B14;       // page background
constexpr std::uint32_t kPanel = 0x1A1F2E;            // box-art panel fill
constexpr std::uint32_t kPanelStroke = 0x303A50;      // panel stroke + divider
constexpr std::uint32_t kAccent = 0x4ED8FF;           // action-button fill, busy text
constexpr std::uint32_t kAccentText = 0x08101B;       // action-button label
constexpr std::uint32_t kReadyGreen = 0x78EBAB;       // verified path / "● Verified and ready"
constexpr std::uint32_t kAmber = 0xFFD34E;            // import prompt / attention
constexpr std::uint32_t kTitlePlaceholder = 0xCBD4EE; // no-art placeholder title
constexpr std::uint32_t kMuted = 0x848FAD;            // checkbox / info line
constexpr std::uint32_t kFootnote = 0x6E7894;         // privacy footnote

// The ROM status line: what the player reads most. Ready → the resolved
// path itself, in the verified green (the activity shows the private path;
// the desktop shows the picked path). Not ready → the import prompt, in
// amber — the activity's exact wording. An empty path never renders as an
// empty line, even if a caller forces the ready state.
struct StatusLine {
    std::string text;
    std::uint32_t color;
    float size;
};

inline StatusLine rom_status_line(bool ready, const std::string& rom_path) {
    if (ready && !rom_path.empty())
        return {rom_path, kReadyGreen, 14.0f};
    return {"Choose your clean cartridge dump (.gba).", kAmber, 16.0f};
}

}  // namespace launcher_style
}  // namespace gbarecomp
