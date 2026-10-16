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
#include <string>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

using namespace gbarecomp::launcher_style;

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
