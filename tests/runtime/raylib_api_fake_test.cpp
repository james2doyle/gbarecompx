// raylib_api_fake_test.cpp — Phase 5.1, §5.2 item 4 (red-first).
//
// Failure injection through a fake RaylibApi: InitWindow fails ->
// Err::Window; audio device unavailable -> claim() refuses; shader load
// fails -> Fx values still stored (capability rule); monitor/refresh edge
// -> 60 Hz fallback; queue-only Delete press -> Menu edge fires once.
//
// Runs against the real RaylibBackend with the injected api, no display.
// Stub config (GBARECOMP_USE_RAYLIB off): RaylibApi does not exist — PASS
// without doing anything.
//
// RED STATE: includes support/fake_raylib_api.h, which does not exist yet.
// The build must fail until the fake lands.
#include <cstdio>
#include <cstdlib>

// Included unconditionally so the RED state (missing header) fails in every
// config, not just raylib-ON: the stub branch below never uses the fake.
#include "support/fake_raylib_api.h"

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                          __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

#if !defined(GBARECOMP_USE_RAYLIB)

int main() {
    std::printf("raylib_api_fake tests: PASS (stub build, nothing to inject)\n");
    return 0;
}

#else

#include "raylib_backend.h"
#include "window_backend.h"

using gbarecomp::Caps;
using gbarecomp::Err;
using gbarecomp::FxSettings;
using gbarecomp::InputSnapshot;
using gbarecomp::OpenParams;
using gbarecomp::RaylibBackend;
using gbarecomp::testing::FakeRaylibApi;

// 1. InitWindow fails -> open() reports Err::Window.
void init_window_failure_is_a_window_error() {
    FakeRaylibApi fake;
    fake.window_ready = false;  // InitWindow never brings the window up
    RaylibBackend b(&fake, nullptr);
    const auto r = b.open(OpenParams{});
    CHECK(!r.ok() && r.code == Err::Window && r.detail != nullptr);
    CHECK(!b.is_open());
}

// 2. Audio device unavailable -> claim() refuses.
void audio_unavailable_refuses_claim() {
    FakeRaylibApi fake;  // audio_ready defaults to false: no device
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    const auto r = b.audio().claim();
    CHECK(!r.ok() && r.code == Err::AudioDevice);
    CHECK(!b.caps().audio);
    b.close();
}

// 3. Shader load fails -> Fx values are still stored (capability rule:
// values are stored and reported even when unsupported).
void shader_failure_still_stores_fx_values() {
    FakeRaylibApi fake;
    fake.shader_valid = false;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    FxSettings s = b.fx().settings();
    s.lcd = true;
    s.lcd_boost = 1.25f;
    b.fx().apply(s);
    CHECK(b.fx().settings().lcd);
    CHECK(b.fx().settings().lcd_boost == 1.25f);
    b.close();
}

// 4. Monitor/refresh edge (no monitor, 0 Hz) -> 60 Hz fallback.
void monitor_edge_falls_back_to_60hz() {
    FakeRaylibApi fake;
    fake.monitor_count = 0;
    fake.refresh_hz = 0;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    const float hz = b.video().refresh_hz();
    CHECK(hz > 59.9f && hz < 60.1f);
    CHECK(!b.caps().vsync_report);
    b.close();
}

// 5. Queue-only Delete press (down==false, queue holds Delete) forms the
// Menu edge exactly once — the Slice 0 queue-drain regression guard.
void queue_only_delete_press_fires_menu_edge_once() {
    FakeRaylibApi fake;
    RaylibBackend b(&fake, nullptr);
    CHECK(b.open(OpenParams{}).ok());
    fake.key_down_held = false;
    fake.key_pressed_held = false;
    fake.press_queue_once = 261;  // KEY_DELETE, no level anywhere
    auto ev1 = b.pump();
    CHECK(ev1.toggle_menu);
    auto ev2 = b.pump();
    CHECK(!ev2.toggle_menu);
    b.close();
}

int main() {
    init_window_failure_is_a_window_error();
    audio_unavailable_refuses_claim();
    shader_failure_still_stores_fx_values();
    monitor_edge_falls_back_to_60hz();
    queue_only_delete_press_fires_menu_edge_once();

    std::printf("raylib_api_fake tests: PASS\n");
    return 0;
}

#endif  // defined(GBARECOMP_USE_RAYLIB)
