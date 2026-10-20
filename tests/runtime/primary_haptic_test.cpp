// Routed haptics: host_primary_haptic must pick the controller sink when the
// pad is primary and the system-vibrator sink otherwise. The fakes record
// both calls, so a swapped branch or a missing delegation fails loudly.
//
// Config-adaptive: the stub build has no backend (HostWindow::open always
// fails), so it skips; the raylib tier runs it against the fake backend.

#include "host_window.h"

#include "support/fake_backend.h"

#include <cassert>
#include <cstdio>

namespace {

gbarecomp::testing::FakeBackend* g_fake = nullptr;

gbarecomp::WindowBackend* make_fake_backend() {
    g_fake = new gbarecomp::testing::FakeBackend();
    return g_fake;
}

}  // namespace

int main() {
    using gbarecomp::HostWindow;
    if (!HostWindow::is_available()) {
        std::printf("primary_haptic: SKIP (no window backend in this build)\n");
        return 0;
    }

    HostWindow window(&make_fake_backend);
    assert(window.open());
    assert(g_fake != nullptr);

    // Touchscreen primary (the default): the system vibrator sink runs.
    g_fake->scripted_input.primary = false;
    assert(!window.controller_is_primary());
    gbarecomp::host_primary_haptic(0.7f, 120u);
    assert(g_fake->scripted_input.haptic_calls == 1);
    assert(g_fake->scripted_input.rumble_calls == 0);
    assert(g_fake->scripted_input.last_haptic_ms == 120);
    assert(g_fake->scripted_input.last_haptic_strength == 0.7f);

    // Pad primary: the controller sink runs, with the same strength on both
    // motors (a fixed impact profile for v1).
    g_fake->scripted_input.primary = true;
    assert(window.controller_is_primary());
    gbarecomp::host_primary_haptic(0.4f, 90u);
    assert(g_fake->scripted_input.rumble_calls == 1);
    assert(g_fake->scripted_input.haptic_calls == 1);
    assert(g_fake->scripted_input.last_low == 0.4f);
    assert(g_fake->scripted_input.last_high == 0.4f);
    assert(g_fake->scripted_input.last_duration_ms == 90u);

    // Zero duration is a no-op at the facade (no sink calls).
    gbarecomp::host_primary_haptic(1.0f, 0u);
    assert(g_fake->scripted_input.rumble_calls == 1);
    assert(g_fake->scripted_input.haptic_calls == 1);

    // Close clears the installed window, so the free function is a safe no-op.
    window.close();
    gbarecomp::host_primary_haptic(1.0f, 120u);

    std::printf("primary_haptic: PASS\n");
    return 0;
}
