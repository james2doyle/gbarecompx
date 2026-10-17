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

// The product backend: implements the single seam facade (Phase 4b deleted
// the flat generation) and returns the concern implementations it owns.
class RaylibBackend final : public WindowBackend {
public:
    // Dependencies are injectable for tests (Phase 2 tail / Phase 5): a fake
    // RaylibApi injects failures, a fake Clock makes host loops deterministic.
    // Nullptr (the default) uses the real raylib surface / real clock.
    explicit RaylibBackend(RaylibApi* api = nullptr, Clock* clock = nullptr);
    ~RaylibBackend() override;

    RaylibBackend(const RaylibBackend&) = delete;
    RaylibBackend& operator=(const RaylibBackend&) = delete;

    // ---- lifecycle (plan §3.2/§3.3) ---------------------------------------
    // The single entry point (AlreadyOpen/Unsupported per §3.3), the
    // capability report, and the mobile lifecycle hooks (desktop no-ops;
    // the Android port is where suspend/resume grow real work).
    Result open(const OpenParams& params) override;
    void close() override;
    bool is_open() const override;
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

    // ---- input pump + lifecycle -------------------------------------------
    // Full pump: fills save slots, hotkeys, gyro, assist, gamepad, touch.
    Events pump() override;
    // Suspend/resume. raylib has no blocking "wait for foreground" event;
    // this should poll IsWindowReady/IsGestureAvailable with a sleep.
    bool wait_for_foreground() override;

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
    // MMPX edge smoothing (mmpx.h): program + 2x target lifecycle.
    static void load_mmpx_shader(Impl* impl);
    static void unload_mmpx_shader(Impl* impl);
    static bool ensure_mmpx_target(Impl* impl);
    static void release_mmpx_target(Impl* impl);
    static void release_post_targets(Impl* impl);
    static bool ensure_post_target(Impl* impl, int which, int w, int h);
#if defined(__ANDROID__)
    // Android: apply the Activity-pushed display info (mobile_platform.h
    // store) — insets live (they arrive after open()), the panel density
    // through the same 4..60 px/mm sanity window the SDL path used.
    static void refresh_android_display_info(Impl* impl);
#endif
    // Touch config persistence (pure file I/O, no raylib).
    static void save_touch_config(const Impl* impl);
    // Pad visibility transition (state + persistence + logging), shared by
    // OverlayImpl::set_touch_pad_visible and InputImpl::read()'s tap handling
    // (read() has no backend object to call the overlay method through).
    static void apply_touch_pad_visible(Impl* impl, bool visible);
    // Draw (and clear) the menu frame stashed by MenuImpl::present. Called by
    // VideoImpl::present after the pad chrome — the menu is the topmost layer
    // and never runs inside a shader/scale pass (plan §3.4).
    static void draw_pending_menu(Impl* impl);
    // Pad geometry inputs from backend state (pure, no raylib).
    static PadLayoutInputs pad_layout_inputs(const Impl* impl, int dw, int dh);
};

}  // namespace gbarecomp