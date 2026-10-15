#pragma once
class RecompRuntimeUi;

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Backend seam for HostWindow. The SDL implementation is still inlined in
// host_window.cpp; this header is the first step toward letting a raylib
// implementation share the same logical contract. It intentionally stays free
// of SDL/raylib headers so it can be compiled on any backend target.
namespace gbarecomp {
struct HostOverlay;

struct WindowBackendOpenParams {
    int scale = 3;
    int base_w = 240;
    int base_h = 160;
    const char* title = "gbarecomp";
    const char* screen = nullptr;
    bool linear_filter = false;
    bool sharp_filter = false;
    bool resize_driven_view = false;
    bool freely_resizable_window = false;
};

class WindowBackend {
public:
    virtual ~WindowBackend() = default;

    virtual bool open(const WindowBackendOpenParams& params) { return false; }
    virtual bool open(int scale, int base_w, int base_h, const char* title,
                      const char* screen, bool linear_filter, bool sharp_filter,
                      bool resize_driven_view,
                      bool freely_resizable_window) {
        (void)scale; (void)base_w; (void)base_h; (void)title; (void)screen;
        (void)linear_filter; (void)sharp_filter; (void)resize_driven_view;
        (void)freely_resizable_window; return false;
    }
    virtual void close() {}
    virtual bool is_open() const { return false; }

    virtual void set_title(const char* title) { (void)title; }
    virtual bool assist_tools_enabled() const { return true; }
    virtual int fast_forward_multiplier() const { return 4; }

    virtual void load_input_config(const char* dir, bool assist_tools_default,
                                  int fast_forward_multiplier_default) {
        (void)dir; (void)assist_tools_default; (void)fast_forward_multiplier_default;
    }

    virtual void set_volume(int pct) { (void)pct; }
    virtual int window_scale() const { return 1; }
    virtual void set_linear_filter(bool enabled) { (void)enabled; }
    virtual bool linear_filter_enabled() const { return false; }
    virtual void set_resize_driven_view(bool enabled) { (void)enabled; }

    virtual void set_view_margins(std::uint32_t left, std::uint32_t right,
                                 std::uint32_t top, std::uint32_t bottom) {
        (void)left; (void)right; (void)top; (void)bottom;
    }
    virtual void configure_touch(bool policy, std::uint32_t claims,
                                int pad_default, bool emulate_touch,
                                int pad_idle_hide_seconds) {
        (void)policy; (void)claims; (void)pad_default;
        (void)emulate_touch; (void)pad_idle_hide_seconds;
    }
    virtual void set_touch_config_dir(const char* dir) { (void)dir; }
    virtual void set_host_overlay(void (*overlay)(HostOverlay*)) { (void)overlay; }
    virtual float px_per_mm_value() const { return 96.0f / 25.4f; }
    virtual void set_presentation_anchor_top(bool anchor_top) { (void)anchor_top; }

    virtual void set_fps_readout(bool on) { (void)on; }
    virtual void haptic_pulse(int duration_ms, float strength) { (void)duration_ms; (void)strength; }

    // Gamepad presence. Used to auto-show the on-screen pad on Android when a
    // physical pad is attached. Deliberately conservative default so a backend
    // that has not wired it never claims a pad it cannot deliver.
    virtual bool gamepad_connected() const { return false; }

    // Gamepad rumble. The base GBA has no rumble, so this is a host-side
    // enhancement, not emulation: recompiled game code calls
    // host_gamepad_rumble() and the backend drives the pad's motors. Dual
    // motors (low = heavy, high = light) so profiles can express "tap" vs
    // "impact" rather than collapsing everything into one strength scalar.
    // Capability-gated and a silent no-op when unsupported, so callers do not
    // need to probe first.
    virtual bool gamepad_supports_rumble() const { return false; }
    virtual void gamepad_rumble(float low_frequency, float high_frequency,
                                std::uint32_t duration_ms) {
        (void)low_frequency; (void)high_frequency; (void)duration_ms;
    }
    virtual void set_runtime_ui(::RecompRuntimeUi* ui) { (void)ui; }

    virtual bool fps_readout_value() const { return false; }

    virtual void set_touch_pad_visible(bool visible) { (void)visible; }
    virtual bool touch_pad_visible_value() const { return false; }

    virtual void set_frame_interp(bool enabled) { (void)enabled; }
    virtual bool frame_interp() const { return false; }
    // LCD shader (raylib-only enhancement; SDL inherits these no-ops).
    // The menu/flags drive these exactly like the frame-blend pair above;
    // backends without a shader stay silent and report defaults.
    virtual void set_lcd_shader(bool enabled) { (void)enabled; }
    virtual bool lcd_shader_enabled() const { return false; }
    virtual void set_lcd_boost(float boost) { (void)boost; }
    virtual float lcd_boost_value() const { return 1.0f; }
    virtual void set_lcd_dither(float dither) { (void)dither; }
    virtual float lcd_dither_value() const { return 1.0f; }
    // Adaptive vertical deband (off by default; threshold in display LSB).
    virtual void set_lcd_deband(float deband) { (void)deband; }
    virtual float lcd_deband_value() const { return 0.0f; }
    virtual void set_frame_blend(bool enabled) { (void)enabled; }
    virtual bool frame_blend_value() const { return false; }
    virtual void reset_frame_blend() {}
    virtual void interp_display_tick(const std::uint8_t* rgb888,
                                      std::uint64_t deadline_ns) {
        (void)rgb888; (void)deadline_ns;
    }

    virtual void set_audio_enabled(bool enabled) { (void)enabled; }
    virtual bool audio_enabled() const { return false; }
    virtual void adjust_scale(int delta) { (void)delta; }
    virtual int volume_percent() const { return 100; }

    virtual void set_fullscreen(int mode) { (void)mode; }
    virtual int fullscreen_mode() const { return 0; }

    virtual bool set_surface_size(int base_w, int base_h) { return false; }
    virtual bool drawable_size(int* width, int* height) const { return false; }

    // Full host event contract. Historically declared as HostWindow::Events;
    // it lives here so a backend can fill it directly without depending on
    // HostWindow. HostWindow::Events is an alias of this type.
    struct Events {
        bool     quit = false;
        // GBA KEYINPUT layout. Active-low: 1 = released, 0 = pressed.
        // Bits: 0=A 1=B 2=Sel 3=Sta 4=Right 5=Left 6=Up 7=Down 8=R 9=L.
        uint16_t keyinput = 0x03FF;
        // Edge-triggered save-state slot hotkeys. F1..F9 load slot
        // 1..9; Shift+F1..F9 save slot 1..9. 0 = no request this pump.
        // The caller acts on these at the top of the loop (a clean
        // dispatch boundary), never mid-frame.
        int      save_slot = 0;
        int      load_slot = 0;
        // Optional, edge-triggered solar controls from config.ini [KeyMap].
        // They have no built-in bindings; recomp-ui exposes them only for
        // cartridges that declare a solar sensor.
        bool     solar_brighter = false;
        bool     solar_dimmer = false;
        bool     solar_live = false;
        // Level-triggered: true while the fast-forward (Turbo) binding is
        // held (default Tab). Uncaps the frame limiter for as long as it's
        // down.
        bool     fast_forward = false;
        // Repeating global Assist binding. It fires immediately, then every
        // half-second while held so one-second rewind steps move backward at
        // approximately normal speed.
        bool     rewind = false;
        // Edge-triggered system hotkeys (config.ini [KeyMap] bindings; see
        // load_input_config). The caller owns the semantics: fullscreen and
        // window scale route back into this window, pause gates stepping in
        // the run loop, volume adjusts pushed-sample gain, FPS toggles the
        // title-bar readout.
        bool     toggle_fullscreen = false;
        bool     toggle_pause = false;
        bool     window_bigger = false;
        bool     window_smaller = false;
        bool     volume_up = false;
        bool     volume_down = false;
        bool     toggle_fps = false;
        // Horizontal mouse velocity while the left button is held. The runtime
        // maps this host-neutral delta onto cartridge gyroscope sample units.
        int      gyro_delta_x = 0;
        bool     mouse_gyro_active = false;
        // Controller angular velocity around its face-normal axis, in rad/s.
        // A DualSense supplies this through SDL's standard gyro sensor API.
        float    gyro_rate_z = 0.0f;
        // Mobile lifecycle: the OS is moving the app to the background (or
        // terminating it). The runtime must flush saves NOW; see
        // wait_for_foreground().
        bool     enter_background = false;
        bool     terminating = false;
    };

    virtual void pump_events(Events& out) { (void)out; }
    virtual Events pump() { return Events{}; }
    virtual bool wait_for_foreground() { return true; }

    virtual void present(const std::uint8_t* rgb888, bool fresh) { (void)rgb888; (void)fresh; }
    virtual bool copy_presented_frame(std::vector<std::uint8_t>& out, int& w, int& h) const { (void)out; (void)w; (void)h; return false; }

    virtual void push_audio_samples(const std::int16_t* samples, std::size_t count) { (void)samples; (void)count; }

    // The UID/backends currently expose raster/audio state through HostWindow's
    // platform-independent helpers; subclasses are expected to keep the same
    // logical contracts documented in host_window.h.

protected:
    WindowBackend() = default;
};

} // namespace gbarecomp