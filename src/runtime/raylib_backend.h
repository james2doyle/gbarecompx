// raylib_backend.h — raylib implementation of the WindowBackend seam.
//
// DESIGN RULES (keep these):
//   * This header includes ONLY window_backend.h. It must stay includable from
//     code that has neither SDL nor raylib on the include path. All raylib
//     headers live in raylib_backend.cpp behind the pimpl.
//   * Every WindowBackend virtual is declared `override` here. Be precise about
//     what that buys: it catches SIGNATURE DRIFT (a base virtual whose signature
//     changes, or a .cpp definition with no declaration) at compile time. It does
//     NOT catch a brand-new base virtual that this class fails to override --
//     that silently inherits the default. Member-pointer comparison cannot help
//     either (it folds to equal even for a genuine override).
//     Completeness is guarded instead by raylib_backend_test.cpp, which calls
//     every method through a WindowBackend& and asserts the *result*, so a
//     newly-added method that still returns its default fails there.
//   * Pure state (volume, scale, filter flags, ...) is real; the
//     window/renderer/audio paths are implemented and raylib-guarded (stub
//     builds get honest no-ops). Every raylib call goes through RaylibApi
//     (raylib_api.h); time goes through Clock (host_clock.h).
//
// The shared logic (gamepad->GBA mapping, pacing, audio queue policy, INI
// scanning, clamps) comes from the platform-neutral helpers, so a raylib backend
// implements roughly 20 lines of native input reads rather than re-deriving any
// of that policy.

#pragma once

#include "audio_slot.h"
#include "lcd_shader.h"
#include "touch_pad_layout.h"
#include "window_backend.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gbarecomp {

struct Clock;
struct RaylibApi;

// Both generations live on this object during the transition (plan §3.1):
// the flat class forwards into the concern implementations, and the V2
// facade (WindowBackendV2) returns the same objects. Phase 4b deletes the
// flat base.
class RaylibBackend final : public WindowBackend, public WindowBackendV2 {
public:
    // Dependencies are injectable for tests (Phase 2 tail / Phase 5): a fake
    // RaylibApi injects failures, a fake Clock makes host loops deterministic.
    // Nullptr (the default) uses the real raylib surface / real clock.
    explicit RaylibBackend(RaylibApi* api = nullptr, Clock* clock = nullptr);
    ~RaylibBackend() override;

    RaylibBackend(const RaylibBackend&) = delete;
    RaylibBackend& operator=(const RaylibBackend&) = delete;

    // ---- lifecycle -------------------------------------------------------
    bool open(const WindowBackendOpenParams& params) override;
    bool open(int scale, int base_w, int base_h, const char* title,
              const char* screen, bool linear_filter, bool sharp_filter,
              bool resize_driven_view,
              bool freely_resizable_window) override;
    void close() override;
    bool is_open() const override;

    // ---- V2 lifecycle (plan §3.2/§3.3) ------------------------------------
    // The single V2 entry point (AlreadyOpen/Unsupported per §3.3), the
    // capability report, and the mobile lifecycle hooks (desktop no-ops;
    // the Android port is where suspend/resume grow real work).
    Result open(const OpenParams& params) override;
    Caps caps() const override;
    bool suspend() override;
    bool resume() override;

    // ---- V2 concern accessors --------------------------------------------
    // Both generations see one state: the flat methods forward into the
    // concern implementations (VideoImpl etc. in raylib_backend.cpp) and the
    // V2 facade returns the same objects.
    Video& video() override;
    Audio& audio() override;
    Input& input() override;
    Fx& fx() override;
    Overlay& overlay() override;
    Menu& menu() override;

    // ---- window state (implemented: pure state) --------------------------
    void set_title(const char* title) override;
    bool assist_tools_enabled() const override;
    int fast_forward_multiplier() const override;
    void load_input_config(const char* dir, bool assist_tools_default,
                           int fast_forward_multiplier_default) override;
    void set_volume(int pct) override;
    int window_scale() const override;
    void set_linear_filter(bool enabled) override;
    bool linear_filter_enabled() const override;
    void set_resize_driven_view(bool enabled) override;
    void set_view_margins(std::uint32_t left, std::uint32_t right,
                          std::uint32_t top, std::uint32_t bottom) override;
    float px_per_mm_value() const override;
    void set_presentation_anchor_top(bool anchor_top) override;
    void set_host_overlay(void (*overlay)(HostOverlay*)) override;
    void set_fps_readout(bool on) override;
    bool fps_readout_value() const override;
    void set_frame_interp(bool enabled) override;
    bool frame_interp() const override;
    void set_frame_blend(bool enabled) override;
    bool frame_blend_value() const override;
    void reset_frame_blend() override;
    void set_audio_enabled(bool enabled) override;
    bool audio_enabled() const override;
    void adjust_scale(int delta) override;
    int volume_percent() const override;
    void set_fullscreen(int mode) override;
    int fullscreen_mode() const override;
    void set_runtime_ui(::RecompRuntimeUi* ui) override;

    // ---- touch / virtual pad --------------------------------------------
    void configure_touch(bool policy, std::uint32_t claims, int pad_default,
                         bool emulate_touch,
                         int pad_idle_hide_seconds) override;
    void set_touch_config_dir(const char* dir) override;
    void set_touch_pad_visible(bool visible) override;
    bool touch_pad_visible_value() const override;

    // ---- gamepad ---------------------------------------------------------
    bool gamepad_connected() const override;
    bool gamepad_supports_rumble() const override;
    void gamepad_rumble(float low_frequency, float high_frequency,
                        std::uint32_t duration_ms) override;

    // ---- haptics ---------------------------------------------------------
    void haptic_pulse(int duration_ms, float strength) override;

    // ---- frame / geometry ------------------------------------------------
    bool set_surface_size(int base_w, int base_h) override;
    bool drawable_size(int* width, int* height) const override;

    // ---- LCD shader (seam methods; the shader itself stays raylib-only) ----
    // Promoted from backend-specific methods so flags/menu drive them like the
    // frame-blend pair; the SDL backend inherits silent no-ops. Enablement
    // persists like the other display flags; the shader itself is GPU-side, so
    // copy_presented_frame() (CPU staging) is unaffected and verify/frame-hash
    // never see it. Precedence: a set GBARECOMP_LCD_SHADER pins on/off above
    // all callers (operator override); otherwise CLI flags beat the config
    // file, which beats defaults.
    void set_lcd_shader(bool enabled) override;
    bool lcd_shader_enabled() const override;
    void set_lcd_boost(float boost) override;
    float lcd_boost_value() const override;
    void set_lcd_dither(float dither) override;
    float lcd_dither_value() const override;
    void set_lcd_deband(float deband) override;
    float lcd_deband_value() const override;

    // ---- window/renderer/audio surface -----------------------------------
    // raylib-guarded bodies; stub builds return the WindowBackend default so
    // behaviour is honest rather than silently wrong.

    // Draining the raylib input queue into the host-neutral Events struct.
    void pump_events(Events& out) override;
    // Full pump: fills save slots, hotkeys, gyro, assist, gamepad, touch.
    Events pump() override;
    // Suspend/resume. raylib has no blocking "wait for foreground" event;
    // this should poll IsWindowReady/IsGestureAvailable with a sleep.
    bool wait_for_foreground() override;
    // Upload RGB888 -> Texture2D, apply filter/scale, present. This is also
    // the hook point for shader / post-processing passes.
    void present(const std::uint8_t* rgb888, bool fresh) override;
    // Copy the exact bytes handed to the GPU for the `presented` debug cmd.
    bool copy_presented_frame(std::vector<std::uint8_t>& out, int& w,
                              int& h) const override;
    // Convert mono 65536 Hz S16 into raylib's AudioStream and push.
    void push_audio_samples(const std::int16_t* samples,
                            std::size_t count) override;
    // Display-paced interpolation loop (see interp_pacing.h for the policy).
    void interp_display_tick(const std::uint8_t* rgb888,
                             std::uint64_t deadline_ns) override;

private:
    struct Impl;
    // V2 concern implementations (defined in raylib_backend.cpp; nested so
    // they can reach the private static helpers below).
    class VideoImpl;
    class AudioImpl;
    class InputImpl;
    class FxImpl;
    class OverlayImpl;
    class MenuImpl;
    std::unique_ptr<Impl> impl_;

    // raylib's AudioCallback carries no userdata (open upstream limitation:
    // raysan5/raylib#3911), so the callback reaches backend state through a
    // static single-owner slot. The slot REFUSES a second owner rather than
    // letting it steal the callback -- two open backends would otherwise
    // cross-talk through one ring buffer. Class members, not file statics,
    // because Impl is a private type. See audio_slot.h.
    static void audio_callback(void* framesOut, unsigned int frameCount);
    static AudioSlot s_audio_slot;
    // Sharp-target, interp-mode and touch-config helpers. Private statics
    // (not file statics) because Impl is a private type -- same reason as the
    // audio slot above. Signatures mention only Impl and pure helper types,
    // so these stay declared (and the header stays raylib-free) in BOTH
    // builds: stub builds call release_sharp_target/save_touch_config from
    // unguarded setters, and must link. The raylib-dependent BODIES are
    // guarded in the .cpp; stubs get no-op/false equivalents.
    static void release_sharp_target(Impl* impl);
    static bool ensure_sharp_target(Impl* impl, int factor);
    static void refresh_interp_mode(Impl* impl);
    // LCD shader program lifecycle (raylib-guarded bodies; stub no-ops).
    // Private statics, not file statics, because Impl is a private type.
    static void load_lcd_shader(Impl* impl);
    static void unload_lcd_shader(Impl* impl);
    // Standalone dither program + fullscreen post target (own pass, so
    // debanding works with the grid off). Same lifecycle pattern.
    static void load_dither_shader(Impl* impl);
    static void unload_dither_shader(Impl* impl);
    static void load_deband_shader(Impl* impl);
    static void unload_deband_shader(Impl* impl);
    static void release_post_targets(Impl* impl);
    static bool ensure_post_target(Impl* impl, int which, int w, int h);
    // Touch config persistence (pure file I/O, no raylib).
    static void save_touch_config(const Impl* impl);
    // Pad visibility transition (state + persistence + logging), shared by the
    // flat setter and InputImpl::read()'s tap handling (read() has no backend
    // object to call the flat method through).
    static void apply_touch_pad_visible(Impl* impl, bool visible);
    // The [video] config block (Fx concern's loader): ini [video] -> the LCD
    // group, transitioning the shader flag through Fx::apply.
    static void load_video_config(Impl* impl, const char* dir);
    // Draw (and clear) the menu frame stashed by MenuImpl::present. Called by
    // VideoImpl::present after the pad chrome — the menu is the topmost layer
    // and never runs inside a shader/scale pass (plan §3.4).
    static void draw_pending_menu(Impl* impl);
    // Pad geometry inputs from backend state (pure, no raylib).
    static PadLayoutInputs pad_layout_inputs(const Impl* impl, int dw, int dh);
};

}  // namespace gbarecomp