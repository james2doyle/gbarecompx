// launcher.cpp — boot skip-rule classification (framework).
//
// Pure argv parsing, no raylib. Mirrors the old recomp-ui seam's headless
// classification: --rom, --bios, --no-window, --frames, --load-state, --tcp
// each skip the screen; bare invocation shows it.

#include "launcher.h"

#include <cstring>
#include <string>
#include <vector>

namespace gbarecomp {

namespace {

bool is_skip_flag(const std::string& s) {
    return s == "--rom" || s == "--bios" || s == "--no-window" ||
           s == "--frames" || s == "--load-state" || s == "--tcp";
}

}  // namespace

bool launcher_should_skip_screen(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string s(argv[i]);
        if (is_skip_flag(s)) return true;
    }
    return false;
}

}  // namespace gbarecomp
