// launcher_style_test.cpp — the launch screen's shared visual language,
// pinned headless (launcher_style.h is raylib-free on purpose).
//
// The spec is the Android setup activity (GbaSetupActivity): the two
// pre-boot surfaces are the same screen on two platforms, and its
// buildContent() colors/wording are the reference. First tier: the palette
// constants and the ROM status-line decision — the line the player reads
// most (resolved path when verified, the import prompt when not).
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "launcher_style.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

using namespace gbarecomp::launcher_style;

namespace {

// Fake measurer: 0.5 * size per character — deterministic, and wide enough
// that the 29-character title overflows a 300px column and must wrap.
float fake_measure(const char* s, float size) {
    return size * 0.5f * static_cast<float>(std::strlen(s));
}

}  // namespace

int main() {
    // The palette, constant-for-constant against the Android activity.
    CHECK(kBackground == 0x070B14);
    CHECK(kPanel == 0x1A1F2E);
    CHECK(kPanelStroke == 0x303A50);
    CHECK(kAccent == 0x4ED8FF);
    CHECK(kAccentText == 0x08101B);
    CHECK(kReadyGreen == 0x78EBAB);
    CHECK(kAmber == 0xFFD34E);
    CHECK(kTitlePlaceholder == 0xCBD4EE);
    CHECK(kMuted == 0x848FAD);
    CHECK(kFootnote == 0x6E7894);
    CHECK(kTitleWhite == 0xFFFFFF);

    // The title row and the privacy footnote are the activity's content,
    // transcribed (config.title → the game's title; the footnote keeps the
    // two facts the activity states: files stay local, BIOS is built in).
    CHECK(std::string(kGameTitle) == "Final Fantasy Tactics Advance");
    CHECK(std::string(kPrivacyFootnote).find("never bundled") !=
          std::string::npos);
    CHECK(std::string(kPrivacyFootnote).find("clean-room BIOS") !=
          std::string::npos);

    // wrap_words: the placeholder and footnote must wrap at WORD boundaries
    // (the activity's TextView wraps; raylib's DrawText does not).
    {
        const auto lines =
            wrap_words(kGameTitle, 300.0f, 28.0f, fake_measure);
        CHECK(!lines.empty());
        float total_words = 0;
        for (const auto& l : lines) {
            CHECK(!l.empty());
            CHECK(l.front() != ' ' && l.back() != ' ');  // no orphan spaces
            CHECK(fake_measure(l.c_str(), 28.0f) <= 300.0f);  // each fits
            for (char c : l)
                if (c == ' ') total_words += 1;
        }
        // Word count preserved: "Final Fantasy Tactics Advance" = 3 words
        // → 2 inner spaces across all lines.
        CHECK(total_words == 2);
    }
    {
        // A short line never wraps; empty text wraps to nothing.
        const auto one = wrap_words("Play", 300.0f, 28.0f, fake_measure);
        CHECK(one.size() == 1 && one[0] == "Play");
        const auto none = wrap_words("", 300.0f, 28.0f, fake_measure);
        CHECK(none.empty());
    }

    // Action buttons: the activity's visual language — cyan fills, navy
    // bold-caps labels, PLAY the big action (58dp→62px), SELECT ROM smaller
    // (48dp→48px), stacked in the right column with a clear gap.
    {
        const ButtonLayout lay = action_button_layout(720, 480);
        CHECK(lay.play.w == 220.0f && lay.play.h == 62.0f);
        CHECK(lay.select.w == 220.0f && lay.select.h == 48.0f);
        CHECK(lay.play.x == lay.select.x);
        CHECK(lay.play.x > 720 / 2);            // right column
        CHECK(lay.select.y >= lay.play.y + lay.play.h + 8.0f);  // clear gap
        CHECK(lay.play.y > 105.0f);             // below the status line
        CHECK(lay.select.y + lay.select.h < 480.0f - 60.0f);  // footnote space
    }

    // Labels are ALL CAPS, matching the activity's action buttons.
    CHECK(std::string(kPlayLabel) == "PLAY");
    CHECK(std::string(kSelectLabel) == "SELECT ROM");
    CHECK(std::string(kRomDialogTitle) == "Select ROM");

    // Visual decision: normal = cyan/navy; focused = ring visible (keyboard
    // and gamepad focus must be SEEABLE — the touch-only activity has no
    // analog); disabled = the dimmed fill at the activity's 0.35 alpha, and
    // the ring survives dimming (focus visibility matters most when the
    // action is unavailable).
    {
        const ButtonVisual normal = action_button_visual(true, false, false);
        CHECK(normal.fill == kAccent);
        CHECK(normal.text == kAccentText);
        CHECK(normal.ring == 0);
        const ButtonVisual focused = action_button_visual(true, true, false);
        CHECK(focused.fill == kAccent);
        CHECK(focused.ring != 0);
        const ButtonVisual disabled = action_button_visual(false, false, false);
        CHECK(disabled.fill == kAccentDisabled);
        CHECK(disabled.text == kAccentText);
        CHECK(disabled.ring == 0);
        const ButtonVisual disabled_focused =
            action_button_visual(false, true, false);
        CHECK(disabled_focused.fill == kAccentDisabled);
        CHECK(disabled_focused.ring != 0);
        // Hover changes nothing (the activity is flat cyan; the pointer is
        // the hover affordance).
        const ButtonVisual hovered = action_button_visual(true, false, true);
        CHECK(hovered.fill == kAccent && hovered.ring == 0);
    }

    // Hit-test: inside/outside, and the boundary itself is inside.
    {
        const Rect r{10.0f, 20.0f, 200.0f, 50.0f};
        CHECK(rect_contains(r, 10.0f, 20.0f));
        CHECK(rect_contains(r, 209.9f, 69.9f));
        CHECK(rect_contains(r, 110.0f, 45.0f));
        CHECK(!rect_contains(r, 9.9f, 45.0f));
        CHECK(!rect_contains(r, 210.1f, 45.0f));
        CHECK(!rect_contains(r, 110.0f, 70.1f));
    }

    // Not ready: the import prompt, in amber — the activity's exact wording
    // (refreshRomStatus' not-ready branch).
    {
        const StatusLine s = rom_status_line(false, "");
        CHECK(s.text == "Choose your clean cartridge dump (.gba).");
        CHECK(s.color == kAmber);
        CHECK(s.size > 0);
    }

    // Ready: the resolved path itself, in the verified green — the player
    // sees WHERE the ROM lives (the activity shows the private path; the
    // desktop shows the picked path).
    {
        const StatusLine s = rom_status_line(true, "/games/ffta.gba");
        CHECK(s.text == "/games/ffta.gba");
        CHECK(s.color == kReadyGreen);
    }

    // Empty path can never be "ready" text: even a forced ready state with
    // no path falls back to the prompt rather than drawing an empty line.
    {
        const StatusLine s = rom_status_line(true, "");
        CHECK(!s.text.empty());
    }

    std::printf("launcher_style_test: PASS (palette pinned, ROM status "
                "decision)\n");
    return 0;
}
