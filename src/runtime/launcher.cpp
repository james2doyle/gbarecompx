// launcher.cpp — boot skip-rule classification (framework).
//
// Pure argv parsing, no raylib. Mirrors the old recomp-ui seam's headless
// classification: --rom, --bios, --no-window, --frames, --load-state, --tcp
// each skip the screen; bare invocation shows it. --no-launcher skips
// explicitly (the mobile platform appends it), and --launcher forces the
// screen even for headless-looking runs (play.sh passes --rom, which reads
// as a skip).
//
// ANDROID NOTE: a NativeActivity's main() receives a synthetic argv —
// raylib's android_main passes {"raylib"} — so flags appended by the mobile
// layer (--no-launcher, the staged TOML, debug-args.txt entries such as
// --load-state) are invisible to an argv-based check. main() must classify
// the MUTATED argument vector via the vector overload; classifying the raw
// argv made the launch screen un-skippable on Android (2026-10-16 device
// finding: the screen appeared on every boot and its Select ROM — tinyfd
// with no backend — could only fail).

#include "launcher.h"

#include <cstring>
#include <string>
#include <vector>

namespace gbarecomp {

namespace {

bool is_skip_flag(const std::string& s) {
    return s == "--rom" || s == "--bios" || s == "--no-window" ||
           s == "--frames" || s == "--load-state" || s == "--tcp" ||
           s == "--no-launcher";
}

bool is_force_flag(const std::string& s) { return s == "--launcher"; }

}  // namespace

bool launcher_should_skip_screen(const std::vector<std::string>& args) {
    for (const std::string& s : args)
        if (is_skip_flag(s)) return true;
    return false;
}

bool launcher_wants_screen(const std::vector<std::string>& args) {
    for (const std::string& s : args)
        if (is_force_flag(s)) return true;
    return false;
}

bool launcher_should_skip_screen(int argc, char** argv) {
    std::vector<std::string> args;
    if (argv != nullptr && argc > 0) args.assign(argv, argv + argc);
    return launcher_should_skip_screen(args);
}

}  // namespace gbarecomp
