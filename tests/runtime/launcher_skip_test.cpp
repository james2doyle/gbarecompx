// launcher_skip_test.cpp — the "Skip this screen on launch when ready"
// state, pinned headless (launcher_skip.h is raylib-free on purpose).
//
// GbaSetupActivity parity: the same wording, the same default (ON — the
// activity's getBoolean(SKIP_LAUNCHER_ON_BOOT, true)), and the same
// decision shape (skip only when the player asked for it AND a verified
// ROM is in hand — "when ready").
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "launcher_skip.h"

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

using namespace gbarecomp::launcher_skip;

int main() {
    // The activity's exact row wording and default.
    CHECK(std::string(kSkipLabel) == "Skip this screen on launch when ready");
    CHECK(kDefault == true);

    // One-line sidecar state: "1"/"0", whitespace-tolerant, anything else
    // reads as the default (a missing or corrupted file never disables the
    // skip — the activity's prefs behave the same way).
    CHECK(parse_state(serialize_state(true)) == true);
    CHECK(parse_state(serialize_state(false)) == false);
    CHECK(serialize_state(true) == "1");
    CHECK(serialize_state(false) == "0");
    CHECK(parse_state("") == kDefault);
    CHECK(parse_state("garbage") == kDefault);
    CHECK(parse_state("0\n") == false);   // hand-edited trailing newline
    CHECK(parse_state(" 0 ") == false);   // padded

    // The decision: skip only when the player asked for it AND a verified
    // ROM is in hand. Unchecked never skips (that is the whole point of
    // unchecking); checked with no ROM cannot skip ("when ready").
    CHECK(should_skip_screen(true, true) == true);
    CHECK(should_skip_screen(false, true) == false);
    CHECK(should_skip_screen(true, false) == false);
    CHECK(should_skip_screen(false, false) == false);

    std::printf("launcher_skip_test: PASS (label, default, state codec, "
                "skip decision)\n");
    return 0;
}
