#include "window_backend.h"

#include <cassert>
#include <cstdio>

struct StubBackend : gbarecomp::WindowBackend {
    bool open_called = false;
    bool open(const gbarecomp::WindowBackendOpenParams&) override { open_called = true; return true; }
};

int main() {
    StubBackend b;
    gbarecomp::WindowBackend& r = b;
    (void)r;

    assert(!b.is_open()); // default remains 0 for us? base stub open override sets open_called but is_open defaults false
    assert(b.open({}) == true);
    assert(b.open_called);

    r.close();
    r.present(nullptr, true);
    r.push_audio_samples(nullptr, 0);
    gbarecomp::WindowBackend::Events ev;
    r.pump_events(ev);
    // Newly converted/delegated backend methods should all be callable through
    // the interface seam without SDL implementation details.
    r.set_view_margins(1, 2, 3, 4);
    r.configure_touch(false, 0U, 0, false, 30);
    r.set_touch_config_dir(nullptr);
    r.set_host_overlay(nullptr);
    assert(r.px_per_mm_value() == 96.0f / 25.4f);
    r.set_presentation_anchor_top(true);
    r.set_fps_readout(true);
    assert(!r.fps_readout_value());
    r.set_touch_pad_visible(true);
    assert(!r.touch_pad_visible_value());
    r.set_frame_interp(true);
    assert(!r.frame_interp());
    r.set_frame_blend(true);
    assert(!r.frame_blend_value());
    r.reset_frame_blend();
    r.set_audio_enabled(true);
    assert(!r.audio_enabled());
    r.adjust_scale(1);
    r.set_volume(50);
    r.set_fullscreen(1);
    assert(!r.fullscreen_mode());
    assert(r.window_scale() == 1);
    r.set_linear_filter(true);
    assert(!r.linear_filter_enabled());
    r.set_resize_driven_view(true);
    r.set_surface_size(240, 160);
    int w = 0, h = 0;
    r.drawable_size(&w, &h);
    assert(w == 0 && h == 0);
    r.set_title(nullptr);
    r.push_audio_samples(nullptr, 0);

    std::printf("window_backend dispatch tests: PASS\n");
    return 0;
}