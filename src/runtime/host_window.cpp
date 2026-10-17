// host_window.cpp — the HostWindow surface over the backend seam.
//
// Phase 4b deleted the SDL backend and the flat generation: this file now
// holds only the delegation to the V2 facade plus the no-backend stub (raylib
// OFF = the stub compile-check).

#include "host_window.h"
#include "host_backend_factory.h"
#include "window_backend.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


#if defined(GBARECOMP_USE_RAYLIB)

namespace gbarecomp {

// The open window that owns the platform vibrator (one per process).
HostWindow* g_haptic_window = nullptr;
// Settings-menu requests from game policies (any thread; consumed by pump).
std::atomic<bool> g_settings_menu_requested{false};

HostWindow::HostWindow() = default;

HostWindow::HostWindow(BackendFactory factory) : factory_(factory) {}

HostWindow::~HostWindow() {
    close();
}

bool HostWindow::is_available() { return true; }

bool HostWindow::open(int scale, int base_w, int base_h, const char* title,
                      const char* screen, bool linear_filter, bool sharp_filter,
                      bool resize_driven_view,
                      bool freely_resizable_window) {
    if (open_) return true;
    WindowBackend* b = factory_ ? factory_() : host_make_backend();
    if (!b) return false;
    // The facade's single entry point; mode stays Windowed because the
    // historical flat open never carried one (callers apply fullscreen after).
    OpenParams p;
    p.scale = scale;
    p.base_w = base_w;
    p.base_h = base_h;
    p.title = title;
    p.display = screen;
    p.linear_filter = linear_filter;
    p.sharp_filter = sharp_filter;
    p.resizable = freely_resizable_window;
    p.resize_driven_view = resize_driven_view;
    const Result r = b->open(p);
    if (!r.ok()) {
        std::fprintf(stderr, "host_window: backend open failed (%s)\n",
                     r.detail ? r.detail : "no detail");
        delete b;
        return false;
    }
    impl_ = b;
    open_ = true;
    g_haptic_window = this;
    return true;
}



void HostWindow::push_audio_samples(const int16_t* samples, std::size_t count) {
    if (!open_ || !impl_ || !samples || count == 0) return;
    impl_->audio().push(samples, count);
}



void HostWindow::set_title(const char* title) {
    if (!open_ || !impl_ || !title) return;
    impl_->video().set_title(title);
}



void HostWindow::close() {
    if (!impl_) { open_ = false; return; }
    // Drop the engine-wide haptic hook FIRST so nothing can reach a backend
    // that is mid-teardown.
    if (g_haptic_window == this) g_haptic_window = nullptr;
    impl_->close();
    delete impl_;
    impl_ = nullptr;
    open_ = false;
}



bool HostWindow::set_surface_size(int base_w, int base_h) {
    if (!open_ || !impl_) return false;
    return impl_->video().set_surface_size(base_w, base_h);
}



bool HostWindow::drawable_size(int* width, int* height) const {
    if (!open_ || !impl_ || !width || !height) return false;
    return impl_->video().drawable_size(width, height);
}



void HostWindow::present(const uint8_t* rgb888, bool fresh) {
    if (!open_ || !impl_ || !rgb888) return;
    impl_->video().present(rgb888, fresh);
}



void HostWindow::load_input_config(const char* dir,
                                   bool assist_tools_default,
                                   int fast_forward_multiplier_default) {
    if (!open_ || !impl_ || !dir) return;
    // The seam's two config entries: Input owns keybinds/hotkeys, Fx owns
    // the [video] LCD block.
    impl_->input().load_config(dir, assist_tools_default,
                               fast_forward_multiplier_default);
    impl_->fx().load_config(dir);
}



bool HostWindow::assist_tools_enabled() const {
    if (!open_ || !impl_) return true;
    return impl_->input().assist_tools_enabled();
}

int HostWindow::fast_forward_multiplier() const {
    if (!open_ || !impl_) return 4;
    return impl_->input().fast_forward_multiplier();
}

void HostWindow::set_fullscreen(int mode) {
    if (!open_ || !impl_) return;
    impl_->video().set_fullscreen(mode);
}



int HostWindow::fullscreen() const {
    if (!open_ || !impl_) return 0;
    return impl_->video().fullscreen();
}



void HostWindow::adjust_scale(int delta) {
    if (!open_ || !impl_) return;
    impl_->video().adjust_scale(delta);
}



void HostWindow::set_volume(int pct) {
    if (!open_ || !impl_) return;
    impl_->audio().set_volume(pct);
}



int HostWindow::volume() const {
    if (!open_ || !impl_) return 100;
    return impl_->audio().volume();
}



int HostWindow::window_scale() const {
    if (!open_ || !impl_) return 1;
    return impl_->video().scale();
}



void HostWindow::set_linear_filter(bool enabled) {
    if (!open_ || !impl_) return;
    impl_->video().set_linear_filter(enabled);
}



bool HostWindow::linear_filter() const {
    if (!open_ || !impl_) return false;
    return impl_->video().linear_filter();
}



void HostWindow::set_frame_interp(bool enabled) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.frame_interp = enabled;
    impl_->fx().apply(s);
    // The blend-history reset here is unconditional (historical flat parity);
    // apply() fires it only on a real toggle.
    impl_->fx().reset_frame_blend();
}



bool HostWindow::frame_interp() const {
    if (!open_ || !impl_) return false;
    return impl_->fx().settings().frame_interp;
}



void HostWindow::interp_display_tick(const uint8_t* rgb888,
                                     uint64_t deadline_ns) {
#if defined(GBARECOMP_RUNTIME_UI)
    if (!open_ || !impl_ || !rgb888) return;
    impl_->video().interp_display_tick(rgb888, deadline_ns);
#else
    (void)rgb888;
    (void)deadline_ns;
#endif
}



void HostWindow::set_frame_blend(bool enabled) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.frame_blend = enabled ? 1.0f : 0.0f;
    impl_->fx().apply(s);
    impl_->fx().reset_frame_blend();  // unconditional, as the flat setter was
}



bool HostWindow::frame_blend() const {
    if (!open_ || !impl_) return false;
    return impl_->fx().settings().frame_blend != 0.0f;
}



void HostWindow::reset_frame_blend() {
    if (!open_ || !impl_) return;
    impl_->fx().reset_frame_blend();
}



void HostWindow::set_lcd_shader(bool enabled) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.lcd = enabled;
    impl_->fx().apply(s);
}



bool HostWindow::lcd_shader() const {
    if (!open_ || !impl_) return false;
    return impl_->fx().settings().lcd;
}



void HostWindow::set_lcd_boost(float boost) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.lcd_boost = boost;
    impl_->fx().apply(s);
}



float HostWindow::lcd_boost() const {
    if (!open_ || !impl_) return 1.0f;
    return impl_->fx().settings().lcd_boost;
}



void HostWindow::set_lcd_dither(float dither) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.lcd_dither = dither;
    impl_->fx().apply(s);
}



float HostWindow::lcd_dither() const {
    if (!open_ || !impl_) return 1.0f;
    return impl_->fx().settings().lcd_dither;
}

void HostWindow::set_lcd_border(bool enabled) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.lcd_border = enabled;
    impl_->fx().apply(s);
}

bool HostWindow::lcd_border() const {
    if (!open_ || !impl_) return false;
    return impl_->fx().settings().lcd_border;
}

void HostWindow::set_lcd_grid_alpha(float alpha) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.lcd_grid_alpha = alpha;
    impl_->fx().apply(s);
}

float HostWindow::lcd_grid_alpha() const {
    if (!open_ || !impl_) return 0.4f;
    return impl_->fx().settings().lcd_grid_alpha;
}

void HostWindow::set_sharp_filter(bool enabled) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.sharp = enabled;
    impl_->fx().apply(s);
}

bool HostWindow::sharp_filter() const {
    if (!open_ || !impl_) return false;
    return impl_->fx().settings().sharp;
}

void HostWindow::set_edge_smoothing(bool enabled) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.edge_smoothing = enabled;
    impl_->fx().apply(s);
}

bool HostWindow::edge_smoothing() const {
    if (!open_ || !impl_) return false;
    return impl_->fx().settings().edge_smoothing;
}



void HostWindow::set_lcd_deband(float deband) {
    if (!open_ || !impl_) return;
    FxSettings s = impl_->fx().settings();
    s.lcd_deband = deband;
    impl_->fx().apply(s);
}



float HostWindow::lcd_deband() const {
    if (!open_ || !impl_) return 0.0f;
    return impl_->fx().settings().lcd_deband;
}



bool HostWindow::copy_presented_frame(std::vector<uint8_t>& out, int& w, int& h) const {
    if (!open_ || !impl_) return false;
    return impl_->video().readback(out, w, h);
}



void HostWindow::set_audio_enabled(bool enabled) {
    if (!open_ || !impl_) return;
    impl_->audio().set_enabled(enabled);
}



bool HostWindow::audio_enabled() const {
    if (!open_ || !impl_) return false;
    return impl_->audio().enabled();
}



void HostWindow::set_resize_driven_view(bool enabled) {
    if (!open_ || !impl_) return;
    impl_->video().set_resize_driven_view(enabled);
}



#if defined(GBARECOMP_RUNTIME_UI)
void HostWindow::set_runtime_ui(RecompRuntimeUi* ui) {
    // Compile-compatibility shim: the seam stopped naming recomp-ui types in
    // Phase 4b (the generic menu is the in-game surface; recomp-ui's runtime
    // UI is not ported to the raylib path). App call sites keep compiling.
    (void)ui;
}

#endif

void HostWindow::configure_touch(bool policy, std::uint32_t claims,
                                 int pad_default, bool emulate_touch,
                                 int pad_idle_hide_seconds) {
    if (!open_ || !impl_ ) return;
    TouchConfig cfg;
    cfg.policy = policy;
    cfg.claims = claims;
    cfg.pad_default = pad_default;
    cfg.emulate_touch = emulate_touch;
    cfg.pad_idle_hide_seconds = pad_idle_hide_seconds;
    cfg.pad_scale = touch_pad_scale_;
    impl_->overlay().configure_touch(cfg);
}

void HostWindow::set_touch_pad_scale(float scale) {
    // Applied by the next configure_touch (the runtime sets both together);
    // stored regardless so the order of the two calls cannot drop it.
    if (scale >= 0.5f && scale <= 1.5f) touch_pad_scale_ = scale;
}



void HostWindow::set_touch_config_dir(const char* dir) {
    if (!open_ || !impl_ || !dir) return;
    impl_->overlay().set_config_dir(dir);
}



bool HostWindow::touch_pad_visible() const {
    if (!open_ || !impl_) return false;
    return impl_->overlay().touch_pad_visible();
}



void HostWindow::set_touch_pad_visible(bool visible) {
    if (!open_ || !impl_) return;
    impl_->overlay().set_touch_pad_visible(visible);
}



void HostWindow::set_view_margins(std::uint32_t left, std::uint32_t right,
                                  std::uint32_t top, std::uint32_t bottom) {
    if (!open_ || !impl_) return;
    impl_->video().set_view_margins(left, right, top, bottom);
}



float HostWindow::px_per_mm() const {
    if (!open_ || !impl_) return 96.0f / 25.4f;
    return impl_->video().px_per_mm();
}



void HostWindow::set_presentation_anchor_top(bool anchor_top) {
    if (!open_ || !impl_) return;
    impl_->video().set_presentation_anchor_top(anchor_top);
}



bool HostWindow::wait_for_foreground() {
    if (!open_ || !impl_) return false;
    return impl_->wait_for_foreground();
}



void HostWindow::haptic_pulse(int duration_ms, float strength) {
    if (!open_ || !impl_) return;
    impl_->input().haptic_pulse(duration_ms, strength);
}



void host_haptic_pulse(int duration_ms, float strength) {
    if (g_haptic_window) g_haptic_window->haptic_pulse(duration_ms, strength);
}

bool HostWindow::gamepad_supports_rumble() const {
    if (!open_ || !impl_) return false;
    return impl_->caps().rumble;  // capability is data in V2
}

void HostWindow::gamepad_rumble(float low_frequency, float high_frequency,
                                std::uint32_t duration_ms) {
    if (!open_ || !impl_) return;
    impl_->input().rumble(low_frequency, high_frequency, duration_ms);
}

void host_gamepad_rumble(float low_frequency, float high_frequency,
                         std::uint32_t duration_ms) {
    if (g_haptic_window)
        g_haptic_window->gamepad_rumble(low_frequency, high_frequency,
                                        duration_ms);
}

void host_request_settings_menu() {
    g_settings_menu_requested.store(true);
}

void HostWindow::set_fps_readout(bool on) {
    if (!open_ || !impl_) return;
    impl_->video().set_fps_readout(on);
}

bool HostWindow::fps_readout() const {
    if (!open_ || !impl_) return false;
    return impl_->video().fps_readout();
}



void HostWindow::set_host_overlay(void (*overlay)(HostOverlay*)) {
    if (!open_ || !impl_) return;
    impl_->overlay().set_host_overlay(overlay);
}

void HostWindow::service_events() {
    // raylib services its own event queue (pump()/EndDrawing); nothing to
    // pre-pump. Kept because the public surface is stable.
}

HostWindow::Events HostWindow::pump() {
    Events ev{};
    if (!open_ || !impl_) { ev.quit = true; return ev; }
    return impl_->pump();
}

WindowBackend* HostWindow::backend_v2() const {
    return impl_;
}



}  // namespace gbarecomp

#else  // No window backend at all (raylib OFF): every method a documented
       // closed-window no-op.

namespace gbarecomp {

HostWindow::HostWindow()  = default;
HostWindow::HostWindow(BackendFactory factory) : factory_(factory) {}
HostWindow::~HostWindow() = default;

bool HostWindow::is_available() { return false; }

bool HostWindow::open(int /*scale*/, int /*base_w*/, int /*base_h*/,
                      const char* /*title*/, const char* /*screen*/,
                      bool /*linear_filter*/, bool /*sharp_filter*/,
                      bool /*resize_driven_view*/,
                      bool /*freely_resizable_window*/) {
    std::fprintf(stderr,
                 "host_window: built without a window backend (raylib off); "
                 "--window unavailable\n");
    return false;
}

void HostWindow::close() { open_ = false; }
void HostWindow::set_title(const char*) {}

bool HostWindow::set_surface_size(int /*base_w*/, int /*base_h*/) {
    return false;
}

bool HostWindow::drawable_size(int* /*width*/, int* /*height*/) const {
    return false;
}

void HostWindow::present(const uint8_t* /*rgb888*/, bool /*fresh*/) {}

void HostWindow::load_input_config(const char* /*dir*/,
                                   bool /*assist_tools_default*/,
                                   int /*fast_forward_multiplier_default*/) {}
void HostWindow::set_fullscreen(int /*mode*/) {}
int  HostWindow::fullscreen() const { return 0; }
void HostWindow::adjust_scale(int /*delta*/) {}
void HostWindow::set_volume(int /*pct*/) {}
int  HostWindow::volume() const { return 100; }
int  HostWindow::window_scale() const { return 1; }
void HostWindow::set_linear_filter(bool /*enabled*/) {}
bool HostWindow::linear_filter() const { return false; }
void HostWindow::set_frame_blend(bool /*enabled*/) {}
bool HostWindow::frame_blend() const { return false; }
void HostWindow::reset_frame_blend() {}
void HostWindow::set_lcd_shader(bool /*enabled*/) {}
bool HostWindow::lcd_shader() const { return false; }
void HostWindow::set_lcd_boost(float /*boost*/) {}
float HostWindow::lcd_boost() const { return 1.0f; }
void HostWindow::set_lcd_dither(float /*dither*/) {}
float HostWindow::lcd_dither() const { return 1.0f; }
void HostWindow::set_lcd_deband(float /*deband*/) {}
float HostWindow::lcd_deband() const { return 0.0f; }
void HostWindow::set_lcd_border(bool /*enabled*/) {}
bool HostWindow::lcd_border() const { return false; }
void HostWindow::set_lcd_grid_alpha(float /*alpha*/) {}
float HostWindow::lcd_grid_alpha() const { return 0.4f; }
void HostWindow::set_sharp_filter(bool /*enabled*/) {}
bool HostWindow::sharp_filter() const { return false; }
void HostWindow::set_edge_smoothing(bool /*enabled*/) {}
bool HostWindow::edge_smoothing() const { return false; }
void HostWindow::set_frame_interp(bool /*enabled*/) {}
bool HostWindow::frame_interp() const { return false; }
void HostWindow::set_resize_driven_view(bool /*enabled*/) {}
#if defined(GBARECOMP_RUNTIME_UI)
void HostWindow::set_runtime_ui(RecompRuntimeUi* /*ui*/) {}
#endif
void HostWindow::configure_touch(bool /*policy*/, std::uint32_t /*claims*/,
                                 int /*pad_default*/, bool /*emulate_touch*/,
                                 int /*pad_idle_hide_seconds*/) {}
void HostWindow::set_touch_pad_scale(float /*scale*/) {}
void HostWindow::set_touch_config_dir(const char* /*dir*/) {}
bool HostWindow::touch_pad_visible() const { return false; }
void HostWindow::set_touch_pad_visible(bool /*visible*/) {}
void HostWindow::set_view_margins(std::uint32_t, std::uint32_t, std::uint32_t,
                                  std::uint32_t) {}
void HostWindow::set_host_overlay(void (*)(HostOverlay*)) {}
float HostWindow::px_per_mm() const { return 96.0f / 25.4f; }
void HostWindow::set_presentation_anchor_top(bool) {}
bool HostWindow::wait_for_foreground() { return false; }
void HostWindow::haptic_pulse(int, float) {}
void host_haptic_pulse(int, float) {}
bool HostWindow::gamepad_supports_rumble() const { return false; }
void HostWindow::gamepad_rumble(float, float, std::uint32_t) {}
void host_gamepad_rumble(float, float, std::uint32_t) {}
bool host_gamepad_supports_rumble() { return false; }
void host_request_settings_menu() {}
void HostWindow::set_fps_readout(bool /*on*/) {}
bool HostWindow::fps_readout() const { return false; }
bool HostWindow::assist_tools_enabled() const { return true; }
int HostWindow::fast_forward_multiplier() const { return 4; }

void HostWindow::push_audio_samples(const int16_t* /*samples*/,
                                    std::size_t /*count*/) {}
void HostWindow::set_audio_enabled(bool /*enabled*/) {}
bool HostWindow::audio_enabled() const { return false; }
bool HostWindow::copy_presented_frame(std::vector<uint8_t>& /*out*/,
                                      int& /*w*/, int& /*h*/) const {
    return false;
}
void HostWindow::interp_display_tick(const uint8_t* /*rgb888*/,
                                     uint64_t /*deadline_ns*/) {}

void HostWindow::service_events() {}

HostWindow::Events HostWindow::pump() {
    Events ev{};
    ev.quit = true;
    return ev;
}

WindowBackend* HostWindow::backend_v2() const { return nullptr; }

}  // namespace gbarecomp

#endif
