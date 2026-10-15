// Guards the seam contract that the migration depends on.
//
// The dispatch test proves each method is reachable through a WindowBackend&.
// It does NOT prove the method is *virtual* — a same-named non-virtual method
// also compiles and links while silently defeating every future raylib backend.
// RecordingBackend below overrides the entire surface and verifies each call
// actually lands, which is the check that would have caught the missing
// `: public WindowBackend` base clause.
#include "host_window.h"
#include "window_backend.h"

#include <cassert>
#include <cstdio>
#include <type_traits>

using gbarecomp::HostWindow;
using gbarecomp::WindowBackend;

// The Events refactor made HostWindow::Events an alias of the seam type.
// If these drift apart, pump() can no longer return what backends produce.
static_assert(std::is_same_v<HostWindow::Events, WindowBackend::Events>,
              "HostWindow::Events must alias WindowBackend::Events");

namespace {

struct RecordingBackend : WindowBackend {
    mutable int hits = 0;
    // Echoed values so we can prove the getter path too, not just the setter.
    bool linear = false;
    bool pad_present = true;
    bool rumble = true;
    int scale = 1;
    int vol = 100;
    int fs = 0;

    bool open(int, int, int, const char*, const char*, bool, bool, bool, bool) override {
        ++hits; return true;
    }
    void close() override { ++hits; }
    bool is_open() const override { return true; }

    void set_title(const char*) override { ++hits; }
    bool assist_tools_enabled() const override { ++hits; return true; }
    int fast_forward_multiplier() const override { ++hits; return 4; }
    void set_volume(int pct) override { ++hits; vol = pct; }
    int volume_percent() const override { ++hits; return vol; }
    void adjust_scale(int d) override { ++hits; scale += d; }
    int window_scale() const override { ++hits; return scale; }
    void set_linear_filter(bool e) override { ++hits; linear = e; }
    bool linear_filter_enabled() const override { ++hits; return linear; }
    void set_resize_driven_view(bool) override { ++hits; }
    void set_fullscreen(int m) override { ++hits; fs = m; }
    int fullscreen_mode() const override { ++hits; return fs; }
    bool set_surface_size(int, int) override { ++hits; return true; }
    bool drawable_size(int* w, int* h) const override { ++hits; *w = 320; *h = 240; return true; }
    void load_input_config(const char*, bool, int) override { ++hits; }

    void set_frame_interp(bool) override { ++hits; }
    bool frame_interp() const override { ++hits; return true; }
    void set_frame_blend(bool) override { ++hits; }
    bool frame_blend_value() const override { ++hits; return true; }
    void reset_frame_blend() override { ++hits; }
    void interp_display_tick(const std::uint8_t*, std::uint64_t) override { ++hits; }

    void set_audio_enabled(bool) override { ++hits; }
    bool audio_enabled() const override { ++hits; return true; }
    bool copy_presented_frame(std::vector<std::uint8_t>&, int&, int&) const override {
        ++hits; return true;
    }

    void set_view_margins(std::uint32_t, std::uint32_t, std::uint32_t,
                          std::uint32_t) override { ++hits; }
    void configure_touch(bool, std::uint32_t, int, bool, int) override { ++hits; }
    void set_touch_config_dir(const char*) override { ++hits; }
    void set_touch_pad_visible(bool) override { ++hits; }
    bool touch_pad_visible_value() const override { ++hits; return true; }
    void set_host_overlay(void (*)(gbarecomp::HostOverlay*)) override { ++hits; }
    float px_per_mm_value() const override { ++hits; return 42.0f; }
    void set_presentation_anchor_top(bool) override { ++hits; }

    void set_fps_readout(bool) override { ++hits; }
    bool fps_readout_value() const override { ++hits; return true; }
    void haptic_pulse(int, float) override { ++hits; }
    bool gamepad_connected() const override { ++hits; return pad_present; }
    bool gamepad_supports_rumble() const override { ++hits; return rumble; }
    void gamepad_rumble(float, float, std::uint32_t) override { ++hits; }
    void set_runtime_ui(::RecompRuntimeUi*) override { ++hits; }
    bool wait_for_foreground() override { ++hits; return true; }

    WindowBackend::Events pump() override { ++hits; return WindowBackend::Events{}; }
    void pump_events(WindowBackend::Events&) override { ++hits; }
    void present(const std::uint8_t*, bool) override { ++hits; }
    void push_audio_samples(const std::int16_t*, std::size_t) override { ++hits; }
};

}  // namespace

int main() {
    RecordingBackend impl;
    WindowBackend& b = impl;

    // Every call must land on the override, so `hits` must equal the number
    // of invocations. A hidden (non-virtual) method would not increment.
    int expected = 0;
    auto bump = [&] { ++expected; };

    assert(b.open(3, 240, 160, "t", nullptr, false, false, false, false)); bump();
    b.close(); bump();
    (void)b.is_open(); bump();
    b.set_title("t"); bump();
    (void)b.assist_tools_enabled(); bump();
    assert(b.fast_forward_multiplier() == 4); bump();
    b.load_input_config("d", true, 4); bump();
    b.set_volume(50); bump();
    assert(b.volume_percent() == 50); bump();
    b.adjust_scale(2); bump();
    assert(b.window_scale() == 3); bump();
    b.set_linear_filter(true); bump();
    assert(b.linear_filter_enabled()); bump();
    b.set_resize_driven_view(true); bump();
    b.set_fullscreen(2); bump();
    assert(b.fullscreen_mode() == 2); bump();
    assert(b.set_surface_size(320, 240)); bump();
    int w = 0, h = 0;
    assert(b.drawable_size(&w, &h) && w == 320 && h == 240); bump();
    b.set_frame_interp(true); bump();
    (void)b.frame_interp(); bump();
    b.set_frame_blend(true); bump();
    (void)b.frame_blend_value(); bump();
    b.reset_frame_blend(); bump();
    b.interp_display_tick(nullptr, 0); bump();
    b.set_audio_enabled(true); bump();
    (void)b.audio_enabled(); bump();
    int out_w = 0, out_h = 0;
    std::vector<std::uint8_t> frame;
    assert(b.copy_presented_frame(frame, out_w, out_h)); bump();
    b.set_view_margins(1, 2, 3, 4); bump();
    b.configure_touch(false, 0U, 0, false, 30); bump();
    b.set_touch_config_dir("d"); bump();
    b.set_touch_pad_visible(true); bump();
    assert(b.touch_pad_visible_value()); bump();
    b.set_host_overlay(nullptr); bump();
    assert(b.px_per_mm_value() == 42.0f); bump();
    b.set_presentation_anchor_top(true); bump();
    b.set_fps_readout(true); bump();
    assert(b.fps_readout_value()); bump();
    b.haptic_pulse(10, 0.5f); bump();
    assert(b.gamepad_connected()); bump();
    assert(b.gamepad_supports_rumble()); bump();
    b.gamepad_rumble(0.5f, 0.25f, 120); bump();
    b.set_runtime_ui(nullptr); bump();
    assert(b.wait_for_foreground()); bump();
    (void)b.pump(); bump();
    WindowBackend::Events ev;
    b.pump_events(ev); bump();
    b.present(nullptr, true); bump();
    b.push_audio_samples(nullptr, 0); bump();

    assert(impl.hits == expected);

    // Defaults must be conservative: a backend that has not wired these must
    // never claim a pad or rumble it cannot deliver.
    struct BareBackend : gbarecomp::WindowBackend {};
    BareBackend bare;
    assert(!bare.gamepad_connected());
    assert(!bare.gamepad_supports_rumble());
    bare.gamepad_rumble(1.0f, 1.0f, 500);  // must be a silent no-op

    // Toggling the recorded state must change the reported answer.
    impl.pad_present = false;
    assert(!b.gamepad_connected());
    impl.pad_present = true;

    // Events defaults are part of the contract the runtime reads.
    WindowBackend::Events e2;
    assert(!e2.quit);
    assert(e2.keyinput == 0x03FF);
    assert(e2.save_slot == 0 && e2.load_slot == 0);
    assert(!e2.fast_forward && !e2.rewind);
    assert(!e2.enter_background && !e2.terminating);

    std::printf("window_backend_virtual_dispatch tests: PASS\n");
    return 0;
}