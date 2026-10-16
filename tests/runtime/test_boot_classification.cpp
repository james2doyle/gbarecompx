// test_boot_classification.cpp — boot screen skip-rule classification.
//
// Constraint 7: --rom, --bios, --no-window, --frames, --load-state, --tcp
// each skip the screen; bare invocation with no sidecars shows it.
// Pure argv classification, no raylib, no engine link.

#include <cstdio>
#include <cstring>
#include <vector>

#include "launcher.h"

namespace {

int failures = 0;

void check(bool skip, const std::vector<std::string>& args, const char* label) {
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
    bool got = gbarecomp::launcher_should_skip_screen(
        static_cast<int>(av.size()), av.data());
    if (got != skip) {
        std::printf("boot FAIL: %s -> got %d, wanted %d\n", label, got, skip);
        ++failures;
    } else {
        std::printf("boot: %s OK\n", label);
    }
}

}  // namespace

int main() {
    // Each Constraint-7 flag skips the screen.
    check(true,  {"FFTARecomp", "--rom",      "/path/to/rom.gba"}, "--rom skips");
    check(true,  {"FFTARecomp", "--bios",     "/path/to/bios.bin"}, "--bios skips");
    check(true,  {"FFTARecomp", "--no-window"},                  "--no-window skips");
    check(true,  {"FFTARecomp", "--frames",  "2400"},            "--frames skips");
    check(true,  {"FFTARecomp", "--load-state", "/tmp/s"},       "--load-state skips");
    check(true,  {"FFTARecomp", "--tcp",     "1234"},           "--tcp skips");

    // Flag without a following value still skips.
    check(true,  {"FFTARecomp", "--rom"},                        "--rom (no value) skips");
    check(true,  {"FFTARecomp", "--bios"},                       "--bios (no value) skips");

    // Bare invocation shows the screen.
    check(false, {"FFTARecomp"},                                 "bare invocation shows");

    // Non-skip flags do not trigger skip.
    check(false, {"FFTARecomp", "--scale", "3"},                 "--scale shows");
    check(false, {"FFTARecomp", "--volume", "70"},               "--volume shows");
    check(false, {"FFTARecomp", "--help"},                       "--help shows");

    if (failures == 0) std::printf("boot: all classification cases pass\n");
    return failures == 0 ? 0 : 1;
}
