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
#include <vector>

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
constexpr std::uint32_t kTitleWhite = 0xFFFFFF;       // game title row

// The activity draws config.title (GbaGameConfig); the desktop launch
// screen is this game's, so the title lives here beside the palette.
// Per-game constants until LauncherConfig grows a title field.
constexpr const char* kGameTitle = "Final Fantasy Tactics Advance";

// The privacy footnote — the activity's two facts, desktop wording: the
// player's files are never bundled, and this game boots on the built-in
// clean-room BIOS substitute (no BIOS is ever picked).
constexpr const char* kPrivacyFootnote =
    "Your files stay on this machine and are never bundled. The built-in "
    "clean-room BIOS substitute is used.";

// Word wrap for the placeholder title and the footnote: the activity's
// TextView wraps; raylib's DrawText does not. Greedy fill, word boundaries
// only, never a line wider than max_width (a word longer than the column
// gets a line of its own rather than being split).
using TextMeasurer = float (*)(const char* text, float size);

inline std::vector<std::string> wrap_words(const std::string& text,
                                           float max_width, float size,
                                           TextMeasurer measure) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;  // collapse spaces
        std::size_t start = i;
        while (i < text.size() && text[i] != ' ') ++i;  // one word
        if (start == i) break;
        const std::string word = text.substr(start, i - start);
        if (!line.empty() &&
            measure((line + ' ' + word).c_str(), size) > max_width) {
            lines.push_back(line);
            line = word;
        } else {
            line = line.empty() ? word : line + ' ' + word;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

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
