// test_launcher_contract.cpp — LauncherBackend contract test (framework).
//
// RED-first: verifies the contract shape before any backend exists.
// A mock backend derives from gbarecomp::LauncherBackend; the test drives each
// LauncherResult::Kind and asserts the values main() would dispatch on.

#include <cstdio>
#include <string>

#include "launcher.h"

namespace {

class MockBackend : public gbarecomp::LauncherBackend {
public:
    gbarecomp::LauncherResult result{};

    explicit MockBackend(gbarecomp::LauncherResult r)
        : result(std::move(r)) {}

    gbarecomp::LauncherResult run(const gbarecomp::LauncherConfig&) override {
        return result;
    }
};

int check(gbarecomp::LauncherResult::Kind kind, const char* label) {
    gbarecomp::LauncherResult expected{kind};
    MockBackend backend(expected);
    gbarecomp::LauncherConfig cfg{};
    gbarecomp::LauncherResult got = backend.run(cfg);

    int failures = 0;
    if (static_cast<int>(got.kind) != static_cast<int>(expected.kind)) {
        std::printf("contract FAIL: %s -> expected kind=%d got kind=%d\n", label,
                    static_cast<int>(expected.kind), static_cast<int>(got.kind));
        ++failures;
    } else {
        std::printf("contract: %s -> kind=%d OK\n", label,
                    static_cast<int>(got.kind));
    }
    return failures;
}

}  // namespace

int run_launcher_contract_tests() {
    int failures = 0;
    failures += check(gbarecomp::LauncherResult::Kind::Skip, "skip");
    failures += check(gbarecomp::LauncherResult::Kind::Launch, "launch");
    failures += check(gbarecomp::LauncherResult::Kind::Quit, "quit");

    // Launch carries validated asset paths through unchanged.
    gbarecomp::LauncherResult launch{gbarecomp::LauncherResult::Kind::Launch};
    launch.rom = "/tmp/opencode/game.gba";
    launch.bios = "/tmp/opencode/gba_bios.bin";
    MockBackend backend(launch);
    gbarecomp::LauncherConfig cfg{};
    gbarecomp::LauncherResult got = backend.run(cfg);
    if (got.kind != gbarecomp::LauncherResult::Kind::Launch) {
        std::printf("contract FAIL: launch kind mismatch\n");
        ++failures;
    }
    if (got.rom != "/tmp/opencode/game.gba") {
        std::printf("contract FAIL: rom path mismatch\n");
        ++failures;
    }
    if (got.bios != "/tmp/opencode/gba_bios.bin") {
        std::printf("contract FAIL: bios path mismatch\n");
        ++failures;
    }

    if (failures == 0) {
        std::printf("contract: all kinds pass\n");
    }
    return failures;
}

int main() {
    return run_launcher_contract_tests() == 0 ? 0 : 1;
}
