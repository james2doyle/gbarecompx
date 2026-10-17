// raylib_backend.cpp — raylib implementation of the WindowBackend seam.
//
// Every raylib call goes through RaylibApi (raylib_api.h) and time goes
// through Clock (host_clock.h), so tests can inject failures and a
// deterministic clock; the real implementations forward 1:1. No SDL anywhere
// in this translation unit — that is the point.

#include "raylib_backend.h"

#include "host_overlay.h"
#include "mobile_platform.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(GBARECOMP_USE_RAYLIB)
#include <raylib.h>
#include "raylib_api.h"
#endif
#if defined(__ANDROID__)
#include <EGL/egl.h>
#endif

#include "assist_pad_binding.h"
#include "audio_convert.h"
#include "audio_gain.h"
#include "audio_gain.h"
#include "color_lut.h"
#include "color_settings.h"
#include "gamepad_map.h"
#include "glfw_pad_quirks.h"
#include "haptic_pacing.h"
#include "host_clock.h"
#include "ini_section.h"
#include "input_translate.h"
#include "interp_pacing.h"
#include "lcd_shader.h"
#include "long_press_ring.h"
#include "menu_draw.h"
#include "menu_engine.h"
#include "presentation_layout.h"
#if defined(GBARECOMP_HAVE_FRAME_BLEND)
#include "recomp_frame_blend.h"
#else
// Primitive absent (framework standalone build): the blend flag is still
// recorded, but present() never applies it — SDL-without-RUNTIME_UI parity.
struct RecompFrameBlend;
#endif
#include "raylib_keymap.h"
#include "recomp_audio_drc.h"
#include "host_overlay.h"
#include "touch_input.h"
#include "touch_pad_draw.h"
#include "touch_pad_layout.h"
#include "window_open_params.h"

#if defined(GBARECOMP_RUNTIME_UI)
#endif

// Cumulative idle-elision skip telemetry (runtime_bus_bridge.cpp). C
// linkage — declared here rather than a header: the only consumer is the
// Android present-rate log below.
extern "C" void gbarecomp_idle_skip_stats(unsigned long long* iters,
                                          unsigned long long* cycles,
                                          unsigned long long* ops);

// Audio drop telemetry (extern "C", same shape as gbarecomp_idle_skip_stats;
// the only consumer is the audio mute contract test). muted counts push()
// calls dropped because the device is DISABLED; nodev counts pushes dropped
// because no bridge/device is ready yet. The mute counter is what makes the
// mute policy observable: enabled() alone cannot distinguish "flag set" from
// "flag honored" (the 2026-10-16 regression was exactly that — set but never
// read).
static unsigned long long g_audio_drops_muted = 0;
static unsigned long long g_audio_drops_nodev = 0;
extern "C" void gbarecomp_audio_drop_stats(unsigned long long* muted,
                                           unsigned long long* nodev) {
    if (muted) *muted = g_audio_drops_muted;
    if (nodev) *nodev = g_audio_drops_nodev;
}

namespace gbarecomp {

// The keymap header's integer constants MUST equal raylib.h's KeyboardKey
// values; the mapping was derived by reading raylib.h, not from memory, and
// these asserts are what keeps a future raylib bump from silently remapping
// the player's keys. If one fires, update raylib_keymap.h, not this file.
#if defined(GBARECOMP_USE_RAYLIB)
static_assert(kRayKeySpace == KEY_SPACE, "keymap drift");
static_assert(kRayKeyBackslash == KEY_BACKSLASH, "keymap drift");
static_assert(kRayKeyA == KEY_A, "keymap drift");
static_assert(kRayKeyB == KEY_B, "keymap drift");
static_assert(kRayKeyC == KEY_C, "keymap drift");
static_assert(kRayKeyD == KEY_D, "keymap drift");
static_assert(kRayKeyE == KEY_E, "keymap drift");
static_assert(kRayKeyF == KEY_F, "keymap drift");
static_assert(kRayKeyG == KEY_G, "keymap drift");
static_assert(kRayKeyH == KEY_H, "keymap drift");
static_assert(kRayKeyI == KEY_I, "keymap drift");
static_assert(kRayKeyJ == KEY_J, "keymap drift");
static_assert(kRayKeyK == KEY_K, "keymap drift");
static_assert(kRayKeyL == KEY_L, "keymap drift");
static_assert(kRayKeyM == KEY_M, "keymap drift");
static_assert(kRayKeyN == KEY_N, "keymap drift");
static_assert(kRayKeyO == KEY_O, "keymap drift");
static_assert(kRayKeyP == KEY_P, "keymap drift");
static_assert(kRayKeyQ == KEY_Q, "keymap drift");
static_assert(kRayKeyR == KEY_R, "keymap drift");
static_assert(kRayKeyS == KEY_S, "keymap drift");
static_assert(kRayKeyT == KEY_T, "keymap drift");
static_assert(kRayKeyU == KEY_U, "keymap drift");
static_assert(kRayKeyV == KEY_V, "keymap drift");
static_assert(kRayKeyW == KEY_W, "keymap drift");
static_assert(kRayKeyX == KEY_X, "keymap drift");
static_assert(kRayKeyY == KEY_Y, "keymap drift");
static_assert(kRayKeyZ == KEY_Z, "keymap drift");
static_assert(kRayKey0 == KEY_ZERO, "keymap drift");
static_assert(kRayKey1 == KEY_ONE, "keymap drift");
static_assert(kRayKey2 == KEY_TWO, "keymap drift");
static_assert(kRayKey3 == KEY_THREE, "keymap drift");
static_assert(kRayKey4 == KEY_FOUR, "keymap drift");
static_assert(kRayKey5 == KEY_FIVE, "keymap drift");
static_assert(kRayKey6 == KEY_SIX, "keymap drift");
static_assert(kRayKey7 == KEY_SEVEN, "keymap drift");
static_assert(kRayKey8 == KEY_EIGHT, "keymap drift");
static_assert(kRayKey9 == KEY_NINE, "keymap drift");
static_assert(kRayKeyEscape == KEY_ESCAPE, "keymap drift");
static_assert(kRayKeyEnter == KEY_ENTER, "keymap drift");
static_assert(kRayKeyTab == KEY_TAB, "keymap drift");
static_assert(kRayKeyBackspace == KEY_BACKSPACE, "keymap drift");
static_assert(kRayKeyDelete == KEY_DELETE, "keymap drift");
static_assert(kRayKeyRight == KEY_RIGHT, "keymap drift");
static_assert(kRayKeyLeft == KEY_LEFT, "keymap drift");
static_assert(kRayKeyDown == KEY_DOWN, "keymap drift");
static_assert(kRayKeyUp == KEY_UP, "keymap drift");
static_assert(kRayKeyF1 == KEY_F1, "keymap drift");
static_assert(kRayKeyF2 == KEY_F2, "keymap drift");
static_assert(kRayKeyF3 == KEY_F3, "keymap drift");
static_assert(kRayKeyF4 == KEY_F4, "keymap drift");
static_assert(kRayKeyF5 == KEY_F5, "keymap drift");
static_assert(kRayKeyF6 == KEY_F6, "keymap drift");
static_assert(kRayKeyF7 == KEY_F7, "keymap drift");
static_assert(kRayKeyF8 == KEY_F8, "keymap drift");
static_assert(kRayKeyF9 == KEY_F9, "keymap drift");
static_assert(kRayKeyF10 == KEY_F10, "keymap drift");
static_assert(kRayKeyF11 == KEY_F11, "keymap drift");
static_assert(kRayKeyF12 == KEY_F12, "keymap drift");
static_assert(kRayKeyLeftShift == KEY_LEFT_SHIFT, "keymap drift");
static_assert(kRayKeyLeftSuper == KEY_LEFT_SUPER, "keymap drift");
static_assert(kRayKeyRightSuper == KEY_RIGHT_SUPER, "keymap drift");
static_assert(kRayKeyLeftControl == KEY_LEFT_CONTROL, "keymap drift");
static_assert(kRayKeyLeftAlt == KEY_LEFT_ALT, "keymap drift");
static_assert(kRayKeyRightShift == KEY_RIGHT_SHIFT, "keymap drift");
static_assert(kRayKeyRightControl == KEY_RIGHT_CONTROL, "keymap drift");
static_assert(kRayKeyRightAlt == KEY_RIGHT_ALT, "keymap drift");
#endif

// raylib handles live here so raylib.h never reaches raylib_backend.h.
struct RaylibBackend::Impl {
    bool open = false;

    // Injected dependencies (real by default; tests inject fakes):
    // `api` is the raylib surface (raylib_api.h) — non-null only in raylib
    // builds; `clock` is the time source (host_clock.h), real in both.
    RaylibApi* api = nullptr;
    Clock* clock = nullptr;

    // V2 concern implementations (created by the ctor; the flat methods
    // forward into them).
    std::unique_ptr<Video> video;
    std::unique_ptr<Audio> audio;
    std::unique_ptr<Input> input;
    std::unique_ptr<Fx> fx;
    std::unique_ptr<Overlay> overlay;
    std::unique_ptr<Menu> menu;
    // Menu frame stash: MenuImpl::present() records the frame's model here;
    // draw_pending_menu() renders it through the pluggable MenuEngine inside
    // every present of the frame (the interp ticks present more than once per
    // pump). The stash survives until the next menu().present() clears and
    // re-sets it; close() clears it with the rest of the derived state.
    MenuModel* menu_model = nullptr;
    // The presentation engine (menu_engine.h) — the raygui widget engine in
    // raylib builds; menu_draw.h via the legacy engine otherwise. Owned
    // here so draw_pending_menu() reaches it directly.
    std::unique_ptr<MenuEngine> menu_engine;
    // Derived from the last presented MenuModel (plan §3.4): drives the
    // input snapshot's suppress_game_input. A plain flag — no model pointer
    // survives a frame.
    bool menu_open = false;
    // True once a monitor-refresh probe returned a sane rate: Caps::
    // vsync_report (Video::refresh_hz() is meaningful).
    bool vsync_probed = false;
    // Touch fast-forward pill state, refreshed by InputImpl::read() each pump
    // and consumed by pump()'s post-translate overlay (the SDL sibling does
    // the same). Plain state; false whenever input is not being read.
    bool touch_ff_held = false;
    // Audio claim (V2 ownership handshake): claim() succeeds once per
    // open when the device came up; reset by open()/close().
    bool audio_claimed = false;

    // Real, implemented state.
    std::string title;
    bool assist_tools = true;
    int fast_forward_multiplier = 4;
    int volume = 100;
    int scale = 3;
    bool linear_filter = false;
    bool resize_driven_view = false;
    bool fps_readout = false;
    bool frame_interp_enabled = false;
    bool frame_blend_enabled = false;
    bool audio_enabled = true;
    int fullscreen = 0;
    bool anchor_top = false;
    bool pad_visible = false;
    void (*host_overlay)(HostOverlay*) = nullptr;
    std::uint32_t view_margin_left = 0, view_margin_right = 0;
    std::uint32_t view_margin_top = 0, view_margin_bottom = 0;

    // Stub-stage placeholders for what needs the window.
    int base_w = 240, base_h = 160;
    float px_per_mm = 96.0f / 25.4f;
    std::string touch_config_dir;
    // Mutually exclusive with linear_filter (sharp wins at resolve time, and
    // enabling linear clears sharp — same rule as the SDL backend).
    bool sharp_filter = false;

    // ---- touch ----
    // Same policy fields as the SDL backend (see configure_touch there).
    // Touches are tracked by raylib point id across pumps; a touch is pad,
    // toggle or routed for its whole life, exactly as on SDL.
    bool touch_policy = false;
    // Touch pad millimetre multiplier (TouchConfig::pad_scale; 1 = authored).
    float touch_pad_scale = 1.0f;
    std::uint32_t touch_claims = 0;
    bool touch_controls = false;
    bool touch_emulation = false;
    bool pad_choice_saved = false;
    bool pad_idle_hidden = false;
    int pad_idle_hide_seconds = 0;
    std::uint32_t pad_last_touch_ms = 0;
    float toggle_fx = 0.0f, toggle_fy = 0.0f;
    bool toggle_pos_saved = false;
    int safe_left = 0, safe_top = 0, safe_right = 0, safe_bottom = 0;
    struct TouchTrack {
        bool active = false;
        float x = 0.0f, y = 0.0f, start_x = 0.0f, start_y = 0.0f;
        bool pad = false;
        std::uint16_t buttons = 0;
        bool routed = false;
        bool toggle = false;
        // Down landed on the FF pill: hold-to-run while inside (re-queried
        // on move like the pad buttons, so sliding off releases).
        bool ff = false;
        float grab_dx = 0.0f, grab_dy = 0.0f;
        bool toggle_dragging = false;
    };
    std::map<int, TouchTrack> touches;

    // ---- LCD shader (spin-off enhancement: what SDL2 cannot do) ----
    // GPU-side subpixel grid (see lcd_shader.h). Opt-in, default off, so the
    // attract golden and verify hashes never see it: copy_presented_frame()
    // serves the CPU staging buffer, pre-shader. lcd_env_override remembers
    // a set GBARECOMP_LCD_SHADER so load_input_config()'s [video] section
    // cannot clobber an explicit env choice.
    bool lcd_shader = false;
    bool lcd_env_override = false;
    // Pre-mask gain compensating the mask's ~0.43x throughput (see
    // lcd_shader.h). Read at present(); [video] lcd_boost tunes it.
    float lcd_boost = kLcdDefaultBoost;
    // Bayer dither strength against BGR555 gradient banding. INDEPENDENT of
    // the grid: active whenever strength > 0, through its own program and
    // post target (see present()). [video] lcd_dither / --lcd-dither tune it.
    float lcd_dither = kLcdDefaultDither;
    // Adaptive vertical deband threshold in display LSB (0 = off). Own
    // program ahead of dither; [video] lcd_deband / --lcd-deband tune it.
    float lcd_deband = kLcdDefaultDeband;
#if defined(GBARECOMP_USE_RAYLIB)
    Shader lcd_shader_handle{};
    int lcd_boost_loc = -1;
    // Game-grid uniforms: (origin_x, origin_y, scale_x, scale_y) and the
    // logical framebuffer size. Without them the grid indexed gl_FragCoord
    // directly, so its periods were drawable pixels rather than game pixels.
    int lcd_grid_loc = -1;
    int lcd_gamesize_loc = -1;
    Shader dither_handle{};
    int dither_loc = -1;
    // Same grid uniforms as the LCD program: the dither cell is indexed by
    // game pixel too, so it needs the same origin/scale.
    int dither_grid_loc = -1;
    int dither_gamesize_loc = -1;
    Shader deband_handle{};
    int deband_loc = -1;
    int deband_texel_loc = -1;
    // Chained fullscreen targets: deband renders game→A, dither renders A→B
    // (or game→A when alone), the screen blit reads the last stage.
    // Fullscreen because these passes sit between the last stage and the
    // screen; deband samples game texels through the same geometry, and
    // dither/grid address game space through the uGrid uniforms.
    RenderTexture2D post_a_target{};
    RenderTexture2D post_b_target{};
#endif
    // Valid only when the matching ready flag is set; recreated on drawable
    // resize. Plain state outside the guard so stub builds link.
    bool post_a_ready = false;
    int post_a_w = 0, post_a_h = 0;
    bool post_b_ready = false;
    int post_b_w = 0, post_b_h = 0;
    bool lcd_shader_ready = false;
    bool dither_ready = false;
    bool deband_ready = false;

    // ---- presentation frame blending (issue #9 strategy #1) ----
    // Same primitive as the SDL backend (recomp_frame_blend.c, blended CPU-side
    // on cached scratch, never in a mapped texture). One deliberate difference:
    // the SDL setters are dead without GBARECOMP_RUNTIME_UI while these honor
    // the flags unconditionally — matching the backend's existing interp
    // convention. App builds define RUNTIME_UI for both paths, so they agree
    // everywhere the menu (the only setter) exists.
    RecompFrameBlend* frame_blend = nullptr;
    std::vector<std::uint8_t> blend_argb;
    std::vector<std::uint8_t> blend_rgb;

    // ---- screen color grading ----
    // Same model as the SDL backend: the per-game [video].screen token (or
    // GBARECOMP_SCREEN) selects a ColorLut; Raw is passthrough and uploads
    // untouched so verify/frame-hash are unaffected.
    std::unique_ptr<runtime::ColorLut> color_lut;
    std::vector<std::uint8_t> graded_fb;

    // Sharp prescale target (integer pre-upscale before the screen blit).
    // Valid only when sharp_ready; sized base*factor and recreated when the
    // factor or surface changes.
    bool sharp_ready = false;
    int sharp_w = 0, sharp_h = 0;
#if defined(GBARECOMP_USE_RAYLIB)
    RenderTexture2D sharp_target{};
#endif

    // ---- display interpolation state ----
    bool interp_vsync = false;
    std::uint64_t interp_period_ns = 16666666u;  // 60 Hz until probed
    std::uint64_t interp_tick_ns = 0;
    std::uint64_t interp_rate_window_ns = 0;
    unsigned interp_tick_count = 0;
    unsigned interp_mode_counter = 0;

    // ---- FPS readout (DisplayPerf hotkey) ----
    std::uint32_t fps_presents = 0;
    double fps_window_start_s = 0.0;
#if defined(__ANDROID__)
    // Present-rate log (no title bar on Android, so the readout above is
    // invisible there): one line per ~10 s window. Cheap, permanent device
    // triage (added for the 2026-10-14 intro-lag investigation).
    std::uint32_t rate_presents = 0;
    double rate_window_start_s = 0.0;
    // Idle-elision skip telemetry, same window: proves whether the prover is
    // fast-forwarding spins (velocity > 0) or merely watching them.
    unsigned long long rate_idle_iters = 0;
    unsigned long long rate_idle_cycles = 0;
#endif

    // ---- input ----
    // Seeded with the built-in defaults (same layout as the SDL backend) so
    // pump() is meaningful even before load_input_config() runs. The config
    // file only overrides; reseeding there keeps repeated loads idempotent.
    InputBindings input_bindings;
    InputEdges input_edges;
    // Selected gamepad: GLFW joystick index, plus the raylib slot aliasing it
    // when visible through raylib's capped range (-1 when not). Refreshed
    // every pump; rumble uses the raylib slot. See find_standard_pad().
    int pad_glfw_index = -1;
    int pad_raylib_slot = -1;

    // Last presented layout + drawable size. Plain state (not raylib types),
    // kept outside the guard: the touch pad geometry helper reads them in
    // both builds, and present() refreshes them whenever it runs.
    PresentationLayout last_layout{};
    int last_drawable_w = 0, last_drawable_h = 0;

    // Audio readiness. Plain state (not raylib types) kept outside the guard
    // so the V2 AudioImpl's claim() can refuse identically in both builds;
    // set by open()'s audio block, cleared by close().
    bool audio_ready = false;
    bool audio_dev_open = false;
    bool bridge_ready = false;

    // Live raylib state.
#if defined(GBARECOMP_USE_RAYLIB)
    Texture2D texture{};
    bool texture_ready = false;
    // Backing store for the streaming texture: raylib 5+ removed
    // CreateTexture(), so the texture is created from an Image we own.
    std::vector<std::uint8_t> surface_pixels;
    Image surface{};
    // Exact bytes last handed to the GPU, for the `presented` debug command.
    std::vector<std::uint8_t> last_upload;
    mutable std::mutex last_upload_mu;

    // ---- audio ----
    // Same architecture as the SDL backend: the emulation thread pushes S16 into
    // the RAB ring (rab_push) and the *audio thread* pulls resampled frames out
    // (rab_pull). RAB does the 65536 Hz -> device-rate conversion.
    AudioStream stream{};
    rab_bridge bridge{};
    std::mutex bridge_mu;          // guards bridge between emu + audio threads
    std::vector<int16_t> volume_buf;
    // Scratch for the audio thread. Fixed-size so the callback never allocates.
    std::vector<int16_t> pull_scratch;
#endif
};

// Sharp-target and interp-mode helpers. Defined here, OUTSIDE the raylib
// guard, because stub builds call them from unguarded setters (and must
// link): declarations are unconditional in the header, raylib-dependent
// bodies are guarded inside each function. Stub equivalents are no-op/false.
void RaylibBackend::release_sharp_target(RaylibBackend::Impl* impl) {
    if (impl->sharp_ready) {
#if defined(GBARECOMP_USE_RAYLIB)
        impl->api->UnloadRenderTexture(impl->sharp_target);
        impl->sharp_target = RenderTexture2D{};
#endif
        impl->sharp_ready = false;
        impl->sharp_w = impl->sharp_h = 0;
    }
}

bool RaylibBackend::ensure_sharp_target(RaylibBackend::Impl* impl, int factor) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (factor < 2) return false;
    const int w = impl->base_w * factor;
    const int h = impl->base_h * factor;
    if (impl->sharp_ready && impl->sharp_w == w && impl->sharp_h == h)
        return true;
    release_sharp_target(impl);
    impl->sharp_target = impl->api->LoadRenderTexture(w, h);
    if (impl->sharp_target.id == 0) return false;
    impl->sharp_ready = true;
    impl->sharp_w = w;
    impl->sharp_h = h;
    impl->api->SetTextureFilter(impl->sharp_target.texture, TEXTURE_FILTER_POINT);
    return true;
#else
    (void)impl;
    (void)factor;
    return false;
#endif
}

// The refresh-rate probe's sanity range: raylib warns-and-returns 0 when the
// monitor query is unimplemented, and the injectable RaylibApi (tests) can
// return absurd values. Anything outside 1..kMaxSaneHz must never reach
// interp_period_ns: 1e9/hz truncates to 0 for hz > 1e9, and a zero period
// breaks the interp pacing (and divides by zero in VideoImpl::refresh_hz).
static constexpr int kMaxSaneHz = 1000;

void RaylibBackend::refresh_interp_mode(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    const int hz = impl->api->GetMonitorRefreshRate(impl->api->GetCurrentMonitor());
    if (hz <= 0 || hz > kMaxSaneHz) return;  // unprobed/absurd: keep the last period
    impl->vsync_probed = true;
    const std::uint64_t period =
        1000000000ull / static_cast<std::uint64_t>(hz);
    if (period == impl->interp_period_ns) return;
    impl->interp_period_ns = period;
    impl->interp_tick_ns = 0;
    std::fprintf(stderr,
                 "raylib_backend: interp display=%d@%dHz period=%lluns\n",
                 impl->api->GetCurrentMonitor(), hz,
                 static_cast<unsigned long long>(period));
#else
    (void)impl;
#endif
}

// LCD shader compile / release. The GLSL is #version 330 (desktop GL 3.3);
// Dialect comes from lcd_shader_fragment_source() (GLSL330 desktop, ES 1.00
// on Android). Idempotent: loading twice keeps the first program.
void RaylibBackend::load_lcd_shader(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (impl->lcd_shader_ready) return;
    if (!impl->open || !impl->api->IsWindowReady()) return;
    impl->lcd_shader_handle =
        impl->api->LoadShaderFromMemory(nullptr, gbarecomp::lcd_shader_fragment_source());
    if (!impl->api->IsShaderValid(impl->lcd_shader_handle)) {
        std::fprintf(stderr, "raylib_backend: LCD shader failed to compile\n");
        impl->lcd_shader_handle = Shader{};
        return;
    }
    impl->lcd_boost_loc =
        impl->api->GetShaderLocation(impl->lcd_shader_handle, "boost");
    impl->lcd_grid_loc =
        impl->api->GetShaderLocation(impl->lcd_shader_handle, "uGrid");
    impl->lcd_gamesize_loc =
        impl->api->GetShaderLocation(impl->lcd_shader_handle, "uGameSize");
    impl->lcd_shader_ready = true;
    std::fprintf(stderr, "raylib_backend: LCD shader enabled\n");
#else
    (void)impl;
#endif
}

void RaylibBackend::unload_lcd_shader(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (!impl->lcd_shader_ready) return;
    impl->api->UnloadShader(impl->lcd_shader_handle);
    impl->lcd_shader_handle = Shader{};
    impl->lcd_boost_loc = -1;
    impl->lcd_grid_loc = -1;
    impl->lcd_gamesize_loc = -1;
    impl->lcd_shader_ready = false;
#else
    (void)impl;
#endif
}

// Standalone dither program. Compiled lazily (config can enable it after
// open); idempotent like the LCD one; same dialect selector.
void RaylibBackend::load_dither_shader(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (impl->dither_ready) return;
    if (!impl->open || !impl->api->IsWindowReady()) return;
    impl->dither_handle =
        impl->api->LoadShaderFromMemory(nullptr, gbarecomp::dither_fragment_source());
    if (!impl->api->IsShaderValid(impl->dither_handle)) {
        std::fprintf(stderr,
                     "raylib_backend: dither shader failed to compile\n");
        impl->dither_handle = Shader{};
        return;
    }
    impl->dither_loc = impl->api->GetShaderLocation(impl->dither_handle, "dither");
    impl->dither_grid_loc =
        impl->api->GetShaderLocation(impl->dither_handle, "uGrid");
    impl->dither_gamesize_loc =
        impl->api->GetShaderLocation(impl->dither_handle, "uGameSize");
    impl->dither_ready = true;
    std::fprintf(stderr, "raylib_backend: dither shader enabled\n");
#else
    (void)impl;
#endif
}

void RaylibBackend::unload_dither_shader(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (!impl->dither_ready) return;
    impl->api->UnloadShader(impl->dither_handle);
    impl->dither_handle = Shader{};
    impl->dither_loc = -1;
    impl->dither_grid_loc = -1;
    impl->dither_gamesize_loc = -1;
    impl->dither_ready = false;
#else
    (void)impl;
#endif
}

// Standalone deband program. Same lazy/idempotent/Android story as dither.
void RaylibBackend::load_deband_shader(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (impl->deband_ready) return;
    if (!impl->open || !impl->api->IsWindowReady()) return;
    impl->deband_handle =
        impl->api->LoadShaderFromMemory(nullptr, gbarecomp::deband_fragment_source());
    if (!impl->api->IsShaderValid(impl->deband_handle)) {
        std::fprintf(stderr,
                     "raylib_backend: deband shader failed to compile\n");
        impl->deband_handle = Shader{};
        return;
    }
    impl->deband_loc = impl->api->GetShaderLocation(impl->deband_handle, "deband");
    impl->deband_texel_loc = impl->api->GetShaderLocation(impl->deband_handle, "texel");
    impl->deband_ready = true;
    std::fprintf(stderr, "raylib_backend: deband shader enabled\n");
#else
    (void)impl;
#endif
}

void RaylibBackend::unload_deband_shader(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (!impl->deband_ready) return;
    impl->api->UnloadShader(impl->deband_handle);
    impl->deband_handle = Shader{};
    impl->deband_loc = -1;
    impl->deband_texel_loc = -1;
    impl->deband_ready = false;
#else
    (void)impl;
#endif
}

// Chained fullscreen post targets (A for deband output / dither I/O, B for
// dither output when both run). Same cached-target pattern as the sharp
// prescale: recreated on drawable resize, released at close and whenever
// neither pass needs them (a dead target must never linger).
void RaylibBackend::release_post_targets(RaylibBackend::Impl* impl) {
    if (impl->post_a_ready) {
#if defined(GBARECOMP_USE_RAYLIB)
        impl->api->UnloadRenderTexture(impl->post_a_target);
        impl->post_a_target = RenderTexture2D{};
#endif
        impl->post_a_ready = false;
        impl->post_a_w = impl->post_a_h = 0;
    }
    if (impl->post_b_ready) {
#if defined(GBARECOMP_USE_RAYLIB)
        impl->api->UnloadRenderTexture(impl->post_b_target);
        impl->post_b_target = RenderTexture2D{};
#endif
        impl->post_b_ready = false;
        impl->post_b_w = impl->post_b_h = 0;
    }
}

// which: 0 = A, 1 = B. Releases and recreates only the requested target;
// on a drawable resize both ensures fire stale in the same present and each
// refreshes its own, so no cross-talk.
bool RaylibBackend::ensure_post_target(RaylibBackend::Impl* impl, int which,
                                       int w, int h) {
#if defined(GBARECOMP_USE_RAYLIB)
    if (w < 1 || h < 1) return false;
    if (which == 0) {
        if (impl->post_a_ready && impl->post_a_w == w && impl->post_a_h == h)
            return true;
        if (impl->post_a_ready) {
            impl->api->UnloadRenderTexture(impl->post_a_target);
            impl->post_a_target = RenderTexture2D{};
            impl->post_a_ready = false;
        }
        impl->post_a_target = impl->api->LoadRenderTexture(w, h);
        if (impl->post_a_target.id == 0) return false;
        impl->post_a_ready = true;
        impl->post_a_w = w;
        impl->post_a_h = h;
        return true;
    }
    if (impl->post_b_ready && impl->post_b_w == w && impl->post_b_h == h)
        return true;
    if (impl->post_b_ready) {
        impl->api->UnloadRenderTexture(impl->post_b_target);
        impl->post_b_target = RenderTexture2D{};
        impl->post_b_ready = false;
    }
    impl->post_b_target = impl->api->LoadRenderTexture(w, h);
    if (impl->post_b_target.id == 0) return false;
    impl->post_b_ready = true;
    impl->post_b_w = w;
    impl->post_b_h = h;
    return true;
#else
    (void)impl;
    (void)which;
    (void)w;
    (void)h;
    return false;
#endif
}


#if defined(GBARECOMP_USE_RAYLIB)
// Sharp prescale target management. Mirrors ensure_sharp_texture() on the SDL
// side: one cached target sized base*factor, recreated when the size changes,
// point-filtered so the integer prescale stays crisp (the screen blit uses the
// main texture filter, which is nearest whenever sharp is on).


// Monotonic clock for interp pacing. std::chrono::steady_clock, same source
// as the SDL backend's steady_now_ns().
static std::uint64_t raylib_steady_now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

static void raylib_sleep_until_ns(std::uint64_t wake_ns) {
    if (!wake_ns) return;
    const std::uint64_t now = raylib_steady_now_ns();
    if (wake_ns <= now) return;
    std::this_thread::sleep_for(
        std::chrono::nanoseconds(wake_ns - now));
}

#if !defined(__ANDROID__) && !defined(PLATFORM_MEMORY)
// Standard-mapped gamepad selection, straight from GLFW past raylib.
//
// WHY NOT IsGamepadAvailable(0): raylib caps MAX_GAMEPADS at 4 (verified in
// the pinned raylib 6.0 sources), and GLFW enumerates every evdev joystick --
// including keyboards' consumer-control interfaces, touchscreens and
// touchpads. On this machine those phantoms occupy raylib slots 0-3 while a
// real 8BitDo sits at GLFW slot 4, unreachable through any raylib gamepad
// call. Any laptop with a touchscreen+touchpad hits the same wall.
//
// GLFW itself sees 16 slots (GLFW_JOYSTICK_1..16) and knows standard mappings
// -- the same database concept as SDL_IsGameController -- so select the first
// standard-mapped pad directly. Phantoms never match a standard mapping and
// are skipped without a name blocklist.
//
// Declarations mirror GLFW 3.4 (bundled with pinned raylib 6.0); the struct
// layout is buttons[15] + axes[6] in both. The raylib-compile gate builds
// this file, so a signature drift breaks the build instead of misreading.
// Never call glfwInit/Terminate here -- raylib owns the library -- and never
// use this on Android (no GLFW there; undefined symbols) or PLATFORM_MEMORY
// (no GLFW there either; the memory platform has no input system at all).
struct RayGlfwGamepadState {
    unsigned char buttons[15];
    float axes[6];
};
extern "C" {
int glfwJoystickPresent(int jid);
int glfwJoystickIsGamepad(int jid);
int glfwGetGamepadState(int jid, RayGlfwGamepadState* state);
const char* glfwGetJoystickGUID(int jid);
}  // extern "C"

// GLFW standard button/axis ids. Must match glfw3.h (3.4); kept as locals
// (not raylib's GAMEPAD_* names) so a reader never confuses the two layers.
constexpr int kGlfwBtnA = 0, kGlfwBtnB = 1, kGlfwBtnX = 2, kGlfwBtnY = 3;
constexpr int kGlfwBtnLB = 4, kGlfwBtnRB = 5;
constexpr int kGlfwBtnBack = 6, kGlfwBtnStart = 7, kGlfwBtnGuide = 8;
constexpr int kGlfwBtnLThumb = 9, kGlfwBtnRThumb = 10;
constexpr int kGlfwBtnDUp = 11, kGlfwBtnDRight = 12;
constexpr int kGlfwBtnDDown = 13, kGlfwBtnDLeft = 14;
constexpr int kGlfwAxisLX = 0, kGlfwAxisLY = 1;
constexpr int kGlfwAxisRX = 2, kGlfwAxisRY = 3;
constexpr int kGlfwAxisLT = 4, kGlfwAxisRT = 5;

// First present, standard-mapped joystick, or -1. Rescanned every pump and
// every gamepad_connected() call: hotplug on desktop just works, and a
// disconnected pad stops cleanly instead of sticking.
inline int find_standard_pad() {
    for (int jid = 0; jid <= 15; ++jid) {
        if (glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid)) return jid;
    }
    return -1;
}
#endif  // !defined(__ANDROID__) && !defined(PLATFORM_MEMORY)

// Re-probe the display period (adaptive-refresh panels can change rate at any
// time). Called every 600 interp ticks; re-anchors boundaries on change.
// Mirrors refresh_interp_mode() on the SDL side.

// HostOverlay over raylib shapes. Drawn inside present()'s Begin/EndDrawing,
// in drawable pixels -- the pad layout is already computed in that space.
// Matches SdlOverlay semantics: geometry only, text unsupported (no font
// stack; labels are skipped, exactly as on SDL without the runtime UI).
class RaylibOverlay final : public HostOverlay {
public:
    RaylibOverlay(RaylibApi* api, int dw, int dh,
                  const PresentationLayout& layout, int base_w, int base_h,
                  float px_per_mm, int in_l, int in_t, int in_r, int in_b,
                  std::uint32_t now_ms)
        : api_(api), dw_(dw), dh_(dh), layout_(layout), base_w_(base_w),
          base_h_(base_h), mm_(px_per_mm), now_ms_(now_ms) {
        insets_.left = in_l;
        insets_.top = in_t;
        insets_.right = in_r;
        insets_.bottom = in_b;
    }

    int drawable_width() const override { return dw_; }
    int drawable_height() const override { return dh_; }
    PresentationLayout game_rect() const override { return layout_; }
    int view_width() const override { return base_w_; }
    int view_height() const override { return base_h_; }
    float drawable_px_per_mm() const override { return mm_; }
    Insets safe_insets() const override { return insets_; }
    std::uint32_t host_ms() const override { return now_ms_; }

    void to_drawable(float x, float y, OverlaySpace space, float* dx,
                     float* dy) const override {
        if (space == OverlaySpace::Drawable) {
            *dx = x;
            *dy = y;
            return;
        }
        logical_point_to_presentation(layout_, base_w_, base_h_, x, y, dx,
                                      dy);
    }

    void line(float x0, float y0, float x1, float y1, float thickness,
              OverlayColor color, OverlaySpace space) override {
        float ax, ay, bx, by;
        to_drawable(x0, y0, space, &ax, &ay);
        to_drawable(x1, y1, space, &bx, &by);
        api_->DrawLineEx({ax, ay}, {bx, by}, thickness, to_ray(color));
    }

    void polyline(const OverlayPoint* points, std::size_t count,
                  float thickness, OverlayColor color,
                  OverlaySpace space) override {
        if (!points || count < 2) return;
        for (std::size_t i = 1; i < count; ++i)
            line(points[i - 1].x, points[i - 1].y, points[i].x,
                 points[i].y, thickness, color, space);
    }

    void fill_rect(float x, float y, float w, float h, float rounding,
                   OverlayColor color, OverlaySpace space) override {
        float dx, dy;
        to_drawable(x, y, space, &dx, &dy);
        const float s = view_scale(space);
        const Rectangle r{dx, dy, w * s, h * s};
        if (rounding > 0.5f) {
            const float roundness = std::clamp(
                rounding / (0.5f * std::min(w * s, h * s)), 0.0f, 1.0f);
            api_->DrawRectangleRounded(r, roundness, 8, to_ray(color));
        } else {
            api_->DrawRectangleV({dx, dy}, {w * s, h * s}, to_ray(color));
        }
    }

    void stroke_rect(float x, float y, float w, float h, float rounding,
                     float thickness, OverlayColor color,
                     OverlaySpace space) override {
        float dx, dy;
        to_drawable(x, y, space, &dx, &dy);
        const float s = view_scale(space);
        const Rectangle r{dx, dy, w * s, h * s};
        if (rounding > 0.5f) {
            // raylib 6 rounded-rectangle lines carry no thickness; the plain
            // outline is the honest mapping (same as SdlOverlay's 1px lines).
            api_->DrawRectangleRoundedLines(r,
                                      std::clamp(rounding / (0.5f * std::min(w * s, h * s)),
                                                 0.0f, 1.0f),
                                      8, to_ray(color));
        } else {
            api_->DrawRectangleLinesEx(r, thickness, to_ray(color));
        }
    }

    void fill_circle(float cx, float cy, float radius, OverlayColor color,
                     OverlaySpace space) override {
        float dx, dy;
        to_drawable(cx, cy, space, &dx, &dy);
        api_->DrawCircleV({dx, dy}, radius * view_scale(space), to_ray(color));
    }

    void stroke_circle(float cx, float cy, float radius, float thickness,
                       OverlayColor color, OverlaySpace space) override {
        float dx, dy;
        to_drawable(cx, cy, space, &dx, &dy);
        const float s = view_scale(space);
        const float outer = radius * s;
        api_->DrawRing({dx, dy}, std::max(0.0f, outer - thickness), outer, 0.0f,
                 360.0f, 32, to_ray(color));
    }

    void arc(float cx, float cy, float radius, float thickness,
             float start_turns, float end_turns, OverlayColor color,
             OverlaySpace space) override {
        // Turns are clockwise from 12 o'clock; raylib ring angles are degrees
        // counterclockwise from +x. The mapping mirrors direction, which is
        // fine for progress indication (nothing on raylib draws arcs yet --
        // the engine long-press ring needs settings-menu plumbing first).
        float dx, dy;
        to_drawable(cx, cy, space, &dx, &dy);
        const float s = view_scale(space);
        const float outer = radius * s;
        api_->DrawRing({dx, dy}, std::max(0.0f, outer - thickness), outer,
                 start_turns * 360.0f - 90.0f, end_turns * 360.0f - 90.0f, 24,
                 to_ray(color));
    }

    bool text_supported() const override { return true; }
    void text(float x, float y, float size, OverlayColor color, const char* utf8,
              OverlayAlign align, OverlaySpace space) override {
        if (!utf8 || !*utf8 || size <= 0.0f) return;
        float dx = 0.0f, dy = 0.0f;
        to_drawable(x, y, space, &dx, &dy);
        const int fsize = static_cast<int>(
            std::lround(size * view_scale(space)));
        const int fsz = fsize > 0 ? fsize : 1;
        const int w = api_->MeasureText(utf8, fsz);
        int ax = static_cast<int>(std::lround(dx));
        if (align == OverlayAlign::Center) ax -= w / 2;
        else if (align == OverlayAlign::Right) ax -= w;
        api_->DrawText(utf8, ax, static_cast<int>(std::lround(dy)), fsz,
                       to_ray(color));
    }
    float text_width(const char* utf8, float size,
                     OverlaySpace space) const override {
        if (!utf8 || size <= 0.0f) return 0.0f;
        int fsize =
            static_cast<int>(std::lround(size * view_scale(space)));
        if (fsize < 1) fsize = 1;
        return static_cast<float>(api_->MeasureText(utf8, fsize));
    }

private:
    static Color to_ray(OverlayColor c) { return {c.r, c.g, c.b, c.a}; }
    float view_scale(OverlaySpace space) const {
        if (space == OverlaySpace::Drawable || base_w_ <= 0) return 1.0f;
        return static_cast<float>(layout_.width) /
               static_cast<float>(base_w_);
    }

    RaylibApi* api_ = nullptr;
    int dw_, dh_, base_w_, base_h_;
    PresentationLayout layout_{};
    float mm_ = 0.0f;
    Insets insets_{};
    std::uint32_t now_ms_ = 0;
};
#endif
// ── V2 concern: Video ───────────────────────────────────────────
// The V2 `Video` interface (window_backend.h) over the backend's Impl
// state. The flat generation's Video methods forward here one-line; nested
// so the private static helpers (sharp target, post targets, shader
// programs, interp mode) stay reachable. Bodies are the flat bodies
// verbatim, with Impl* members respelled from impl_.get().
#if defined(__ANDROID__)
// Android: apply the Activity-pushed display info (mobile_platform.h store).
// Insets live — they arrive after open(), so a one-shot read would miss them.
// The panel density is applied only when it passes the sanity window the SDL
// path used (4..60 px/mm: phones ~8-25; a broken report must not shrink or
// balloon the touch UI).
void RaylibBackend::refresh_android_display_info(RaylibBackend::Impl* impl) {
    const gbarecomp::AndroidDisplayInfo& info = gbarecomp::android_display_info();
    if (info.insets_known.load()) {
        impl->safe_left = info.inset_left.load();
        impl->safe_top = info.inset_top.load();
        impl->safe_right = info.inset_right.load();
        impl->safe_bottom = info.inset_bottom.load();
    }
    const float xdpi = info.xdpi.load(), ydpi = info.ydpi.load();
    const float physical = (xdpi + ydpi) * 0.5f / 25.4f;
    if (xdpi > 0.0f && ydpi > 0.0f && physical >= 4.0f && physical <= 60.0f &&
        std::fabs(physical - impl->px_per_mm) > 0.05f) {
        impl->px_per_mm = physical;
        std::fprintf(stderr, "raylib_backend: android panel %.2f px/mm\n",
                     physical);
    }
}
#endif

class RaylibBackend::VideoImpl final : public Video {
public:
    explicit VideoImpl(Impl* impl) : impl_(impl) {}

    void present(const std::uint8_t* rgb888, bool fresh) override {
        (void)fresh;   // the SDL path only uses `fresh` to pick a blend variant
#if defined(GBARECOMP_USE_RAYLIB)
        if (!rgb888 || !impl_->open || !impl_->texture_ready) return;
#if defined(__ANDROID__)
        refresh_android_display_info(impl_);
#endif

        // Present-time color grading (opt-in). Raw is passthrough: the raw frame
        // is uploaded untouched, so verify/frame-hash are unaffected. Mirrors the
        // SDL backend, including recording the GRADED bytes as presented.
        if (impl_->color_lut && !impl_->color_lut->is_passthrough() &&
            impl_->graded_fb.size() ==
                static_cast<std::size_t>(impl_->base_w) * impl_->base_h * 3u) {
            impl_->color_lut->map_rgb888(rgb888, impl_->graded_fb.data(),
                                         impl_->base_w, impl_->base_h);
            rgb888 = impl_->graded_fb.data();
        }

#if defined(GBARECOMP_HAVE_FRAME_BLEND)
        // Presentation frame blending (issue #9 #1). Verbatim mirror of the SDL
        // backend's block: RGB888 -> ARGB8888, blend in place, back to RGB888 for
        // upload. Fresh frames blend (when on) and advance history; repeats blend
        // against UNCHANGED history (interp holding presents). Everything off
        // skips this block, so the upload below stays byte-identical to before —
        // and, like SDL, the BLENDED bytes are what copy_presented_frame serves.
        {
            const bool do_blend =
                impl_->frame_blend_enabled && impl_->frame_blend;
            // Holding blends only while blending is on: with blending off, repeats
            // upload raw (a stale-valid history from an earlier blend-on period
            // must never smear into them).
            const bool do_hold = !fresh && impl_->frame_blend_enabled &&
                                 impl_->frame_interp_enabled && impl_->frame_blend;
            if ((do_blend || do_hold) && impl_->frame_blend) {
                const std::size_t px =
                    static_cast<std::size_t>(impl_->base_w) * impl_->base_h;
                if (impl_->blend_argb.size() != px * 4u ||
                    impl_->blend_rgb.size() != px * 3u) {
                    impl_->blend_argb.assign(px * 4u, 0);
                    impl_->blend_rgb.assign(px * 3u, 0);
                    recomp_frame_blend_reset(impl_->frame_blend);
                }
                const uint8_t* src = rgb888;
                uint8_t* argb = impl_->blend_argb.data();
                for (std::size_t i = 0; i < px; ++i) {
                    argb[i * 4u + 0u] = src[i * 3u + 2u];  // B
                    argb[i * 4u + 1u] = src[i * 3u + 1u];  // G
                    argb[i * 4u + 2u] = src[i * 3u + 0u];  // R
                    argb[i * 4u + 3u] = 0xFF;              // A (opaque; untouched)
                }
                if (do_hold)
                    recomp_frame_blend_apply_holding(
                        impl_->frame_blend, argb, impl_->base_w, impl_->base_h,
                        static_cast<size_t>(impl_->base_w) * 4u);
                else
                    recomp_frame_blend_apply(impl_->frame_blend, argb,
                                             impl_->base_w, impl_->base_h,
                                             static_cast<size_t>(impl_->base_w) *
                                                 4u);
                uint8_t* dst = impl_->blend_rgb.data();
                for (std::size_t i = 0; i < px; ++i) {
                    dst[i * 3u + 0u] = argb[i * 4u + 2u];
                    dst[i * 3u + 1u] = argb[i * 4u + 1u];
                    dst[i * 3u + 2u] = argb[i * 4u + 0u];
                }
                rgb888 = dst;
            }
        }
#endif

        impl_->api->UpdateTexture(impl_->texture, rgb888);

        // Remember the exact bytes handed to the GPU for the `presented` command.
        {
            std::lock_guard<std::mutex> lk(impl_->last_upload_mu);
            impl_->last_upload.assign(
                rgb888, rgb888 + static_cast<std::size_t>(impl_->base_w) *
                                    impl_->base_h * 3u);
        }

        const int dw = impl_->api->GetScreenWidth();
        const int dh = impl_->api->GetScreenHeight();
        // The fit policy is the portable one from presentation_layout.h, shared with
        // the SDL backend -- no second aspect rule to drift.
        const PresentationLayout layout =
            compute_presentation_layout(dw, dh, impl_->base_w, impl_->base_h);
        impl_->last_layout = layout;
        impl_->last_drawable_w = dw;
        impl_->last_drawable_h = dh;

        impl_->api->BeginDrawing();
        impl_->api->ClearBackground(impl_->api->GetColor(0x070B14));  // matches the SDL clear color
        if (layout.width > 0 && layout.height > 0) {
            // Where the game grid sits on the drawable, and how big a game
            // pixel is there. Uploaded to the grid and dither programs so both
            // index the frame in GAME space; recomputed every frame because it
            // moves whenever the drawable is resized.
            const gbarecomp::PresentGrid grid = gbarecomp::resolve_present_grid(
                layout, impl_->base_w, impl_->base_h);
            const Rectangle src{0.0f, 0.0f,
                                static_cast<float>(impl_->base_w),
                                static_cast<float>(impl_->base_h)};
            const Rectangle dst{static_cast<float>(layout.x),
                                static_cast<float>(layout.y),
                                static_cast<float>(layout.width),
                                static_cast<float>(layout.height)};
            // Independent stages, evaluated separately so every knob works alone:
            // sharp prescale, deband pass, dither pass, LCD pass. Dither and
            // the grid are indexed in GAME space (uGrid carries the image
            // origin and the scale) but still run at drawable resolution
            // through a fullscreen post target -- never inside the game/sharp
            // targets, whose geometry is not the screen. Deband samples game
            // texels through the same geometry. The overlay below stays
            // unshaded too (pad UI must remain legible).
            const bool lcd = impl_->lcd_shader && impl_->lcd_shader_ready;
            // Deband stands alone: active whenever threshold > 0, grid or not.
            // Lazily compiled — config can enable it after open().
            bool deband = impl_->lcd_deband > 0.0f;
            if (deband) {
                load_deband_shader(impl_);
                deband = impl_->deband_ready;
            }
            // Dither stands alone: active whenever strength > 0, grid or not.
            // Lazily compiled — config can enable it after open().
            bool dither = impl_->lcd_dither > 0.0f;
            if (dither) {
                load_dither_shader(impl_);
                dither = impl_->dither_ready;
            }
            // Sharp prescale: integer pre-upscale into a render target, then blit
            // to the screen. Exact scales and factor<2 keep the crisp direct path
            // (compute_sharp_prescale_factor returns 0 there).
            // Resolve the stage to blit: the sharp target when engaged, else the
            // game texture. Any prescale failure falls back to direct — a missing
            // prescale must never drop a frame.
            const Texture2D* stage_tex = &impl_->texture;
            Rectangle stage_rect = src;
            const int sharp_factor =
                impl_->sharp_filter
                    ? compute_sharp_prescale_factor(layout, impl_->base_w,
                                                   impl_->base_h)
                    : 0;
            if (sharp_factor >= 2 && ensure_sharp_target(impl_, sharp_factor)) {
                impl_->api->BeginTextureMode(impl_->sharp_target);
                impl_->api->ClearBackground(BLANK);
                impl_->api->DrawTexturePro(impl_->texture, src,
                               {0.0f, 0.0f,
                                static_cast<float>(impl_->sharp_w),
                                static_cast<float>(impl_->sharp_h)},
                               {0.0f, 0.0f}, 0.0f, WHITE);
                impl_->api->EndTextureMode();
                // RenderTexture storage is Y-flipped relative to the screen.
                stage_tex = &impl_->sharp_target.texture;
                stage_rect = Rectangle{
                    0.0f, 0.0f,
                    static_cast<float>(impl_->sharp_w),
                    -static_cast<float>(impl_->sharp_h)};
            }
            // Uniform upload goes inside the shader window: the program is
            // current there, so the value lands on our program, not whatever
            // was bound before.
            auto upload_boost = [&] {
                if (impl_->lcd_boost_loc >= 0)
                    impl_->api->SetShaderValue(impl_->lcd_shader_handle,
                                   impl_->lcd_boost_loc, &impl_->lcd_boost,
                                   SHADER_UNIFORM_FLOAT);
            };
            // Same placement rule for the grid: origin+scale as a vec4, the
            // logical framebuffer size as a vec2. Guarded on location >= 0
            // like every other uniform here, so a program that did not expose
            // one (or a stub api) writes nothing.
            auto upload_grid = [&](Shader program, int grid_loc, int size_loc) {
                if (grid_loc >= 0) {
                    const float g[4] = {
                        static_cast<float>(grid.origin_x),
                        static_cast<float>(grid.origin_y), grid.scale_x,
                        grid.scale_y};
                    impl_->api->SetShaderValue(program, grid_loc, g,
                                               SHADER_UNIFORM_VEC4);
                }
                if (size_loc >= 0) {
                    const float s[2] = {
                        static_cast<float>(grid.game_w),
                        static_cast<float>(grid.game_h)};
                    impl_->api->SetShaderValue(program, size_loc, s,
                                               SHADER_UNIFORM_VEC2);
                }
            };
            // A failed post target degrades gracefully: drop that pass, keep the
            // rest, never drop the frame.
            if (deband && !ensure_post_target(impl_, 0, dw, dh)) deband = false;
            if (dither && !ensure_post_target(impl_, deband ? 1 : 0, dw, dh))
                dither = false;
            // First active pass reads the stage (sharp target or game texture);
            // each pass feeds the next; the screen blit reads the last stage.
            // No passes: direct blit, byte-identical to before any of this.
            const Texture2D* pass_tex = stage_tex;
            Rectangle pass_rect = stage_rect;
            if (deband) {
                impl_->api->BeginTextureMode(impl_->post_a_target);
                impl_->api->ClearBackground(impl_->api->GetColor(0x070B14));
                impl_->api->BeginShaderMode(impl_->deband_handle);
                if (impl_->deband_loc >= 0)
                    impl_->api->SetShaderValue(impl_->deband_handle, impl_->deband_loc,
                                   &impl_->lcd_deband, SHADER_UNIFORM_FLOAT);
                if (impl_->deband_texel_loc >= 0) {
                    // One GAME pixel in source UV -- independent of the drawn size,
                    // and still correct against the sharp pre-upscale target (its
                    // 1/base_w step spans exactly `factor` sharp texels).
                    const gbarecomp::TexelStep step =
                        gbarecomp::shader_deband_texel_step(impl_->base_w,
                                                             impl_->base_h);
                    const float texel[2] = {step.x, step.y};
                    impl_->api->SetShaderValue(impl_->deband_handle, impl_->deband_texel_loc,
                                   texel, SHADER_UNIFORM_VEC2);
                }
                impl_->api->DrawTexturePro(*pass_tex, pass_rect, dst, {0.0f, 0.0f}, 0.0f,
                               WHITE);
                impl_->api->EndShaderMode();
                impl_->api->EndTextureMode();
                pass_tex = &impl_->post_a_target.texture;
                pass_rect = Rectangle{0.0f, 0.0f, static_cast<float>(dw),
                                      -static_cast<float>(dh)};
            }
            if (dither) {
                const bool second = deband;
                impl_->api->BeginTextureMode(second ? impl_->post_b_target
                                       : impl_->post_a_target);
                impl_->api->ClearBackground(impl_->api->GetColor(0x070B14));
                impl_->api->BeginShaderMode(impl_->dither_handle);
                if (impl_->dither_loc >= 0)
                    impl_->api->SetShaderValue(impl_->dither_handle, impl_->dither_loc,
                                   &impl_->lcd_dither, SHADER_UNIFORM_FLOAT);
                // The Bayer cell is indexed by game pixel, so the dither needs
                // the same grid the LCD pass uses.
                upload_grid(impl_->dither_handle, impl_->dither_grid_loc,
                            impl_->dither_gamesize_loc);
                // NOTE: pass_rect is already fullscreen when chaining (a previous
                // pass wrote the whole target), so dst would double-letterbox.
                // Both passes render the layout rect into a fullscreen target, so
                // every stage after the first is fullscreen-to-fullscreen.
                const Rectangle chain_dst =
                    second ? Rectangle{0.0f, 0.0f, static_cast<float>(dw),
                                       static_cast<float>(dh)}
                           : dst;
                impl_->api->DrawTexturePro(*pass_tex, pass_rect, chain_dst, {0.0f, 0.0f},
                               0.0f, WHITE);
                impl_->api->EndShaderMode();
                impl_->api->EndTextureMode();
                pass_tex = second ? &impl_->post_b_target.texture
                                  : &impl_->post_a_target.texture;
                pass_rect = Rectangle{0.0f, 0.0f, static_cast<float>(dw),
                                      -static_cast<float>(dh)};
            }
            {
                const Rectangle screen{0.0f, 0.0f, static_cast<float>(dw),
                                       static_cast<float>(dh)};
                // After any target pass the stage is fullscreen; direct blits
                // keep the layout rect.
                const Rectangle final_dst =
                    (deband || dither) ? screen : dst;
                if (lcd) {
                    impl_->api->BeginShaderMode(impl_->lcd_shader_handle);
                    upload_boost();
                    // The mask is anchored to the game grid: one triad per game
                    // pixel, one dark row per game row, bars unshaded.
                    upload_grid(impl_->lcd_shader_handle, impl_->lcd_grid_loc,
                                impl_->lcd_gamesize_loc);
                    impl_->api->DrawTexturePro(*pass_tex, pass_rect, final_dst, {0.0f, 0.0f},
                                   0.0f, WHITE);
                    impl_->api->EndShaderMode();
                } else {
                    impl_->api->DrawTexturePro(*pass_tex, pass_rect, final_dst, {0.0f, 0.0f},
                                   0.0f, WHITE);
                }
            }
        }

        // Host chrome: spin-off overlay hook, on-screen pad + toggle. Same
        // gating as the SDL backend's render_host_layers (minus the settings
        // menu, which needs runtime-UI plumbing this backend doesn't have yet).
        // Drawn in drawable pixels inside the same Begin/EndDrawing, so no
        // viewport juggling is needed.
        {
            const bool overlay_shown = pad_overlay_shown(
                impl_->pad_visible, impl_->touch_controls, impl_->pad_idle_hidden);
            const bool toggle_shown = pad_toggle_shown(
                impl_->touch_policy, impl_->touch_controls, impl_->touch_emulation,
                impl_->pad_idle_hidden);
            const bool touch_on_this_frame = touch_input_enabled(
                impl_->touch_controls, impl_->touch_policy, impl_->touch_emulation);
            if (impl_->host_overlay || overlay_shown || toggle_shown ||
                touch_on_this_frame) {
                RaylibOverlay ov(impl_->api, dw, dh, impl_->last_layout,
                                 impl_->base_w, impl_->base_h,
                                 impl_->px_per_mm * impl_->touch_pad_scale,
                                 impl_->safe_left, impl_->safe_top,
                                 impl_->safe_right, impl_->safe_bottom,
                                 touch_clock_ms());
                if (impl_->host_overlay) impl_->host_overlay(&ov);
                if (overlay_shown || toggle_shown) {
                    const PadLayout pad = compute_pad_layout(
                        pad_layout_inputs(impl_, dw, dh));
                    std::uint16_t held = 0;
                    bool toggle_held = false;
                    bool ff_held = false;
                    for (const auto& [id, track] : impl_->touches) {
                        if (!track.active) continue;
                        if (track.pad) held |= track.buttons;
                        if (track.toggle) toggle_held = true;
                        if (track.ff) ff_held = true;
                    }
                    if (overlay_shown)
                        draw_touch_pad(ov, pad, held,
                                       impl_->px_per_mm * impl_->touch_pad_scale,
                                       ff_held);
                    if (toggle_shown)
                        draw_pad_toggle(ov, pad, toggle_held, impl_->pad_visible);
                }
                // Engine long-press cue (plan §3d): rings while an unclaimed
                // long press is arming — the same 150 ms/650 ms policy as the
                // SDL backend's draw_engine_long_press. The host consumes the
                // fired gesture and opens the menu; the backend only draws
                // the cue. Hidden while the menu is open.
                if (!impl_->menu_open &&
                    !TouchHub::instance().claimed(kTouchClaimLongPress)) {
                    const TouchHub& hub = TouchHub::instance();
                    const std::vector<TouchPointSnapshot> pts = hub.points();
                    LongPressRing rings[10];
                    const int n = collect_long_press_rings(
                        pts.data(), static_cast<int>(pts.size()),
                        touch_clock_ms(), hub.gesture_config().long_press_ms,
                        rings, 10);
                    for (int i = 0; i < n; ++i)
                        draw_long_press_ring(ov, rings[i],
                                             impl_->px_per_mm * impl_->touch_pad_scale);
                }
            }
        }

        // Menu: the topmost layer (plan §3.4 draw order), coalesced into
        // this frame's Begin/EndDrawing — never inside a shader/scale pass.
        draw_pending_menu(impl_);

        impl_->api->EndDrawing();

#if defined(__ANDROID__)
        // Present-rate log: presents/sec per ~10 s window (see Impl fields).
        ++impl_->rate_presents;
        {
            const double now_s = impl_->clock->seconds();
            if (impl_->rate_window_start_s == 0.0)
                impl_->rate_window_start_s = now_s;
            const double span_s = now_s - impl_->rate_window_start_s;
            if (span_s >= 10.0) {
                unsigned long long iters = 0, cycles = 0, ops = 0;
                gbarecomp_idle_skip_stats(&iters, &cycles, &ops);
                std::fprintf(stderr,
                             "raylib_backend: presents=%u rate=%.1f/s "
                             "idle-skipped=%llu iters (%llu cyc, %llu ops)\n",
                             impl_->rate_presents,
                             impl_->rate_presents / span_s,
                             iters - impl_->rate_idle_iters,
                             cycles - impl_->rate_idle_cycles, ops);
                impl_->rate_idle_iters = iters;
                impl_->rate_idle_cycles = cycles;
                impl_->rate_window_start_s = now_s;
                impl_->rate_presents = 0;
            }
        }
#endif

        // FPS readout (DisplayPerf hotkey): presents/sec, refreshed twice a
        // second in the title bar; the base title is restored when toggled off.
        // Same policy as the SDL backend, with the Clock (raylib GetTime) standing
        // in for SDL_GetTicks().
        if (impl_->fps_readout) {
            ++impl_->fps_presents;
            const double now_s = impl_->clock->seconds();
            if (impl_->fps_window_start_s == 0.0) impl_->fps_window_start_s = now_s;
            const double span_s = now_s - impl_->fps_window_start_s;
            if (span_s >= 0.5) {
                char buf[192];
                std::snprintf(buf, sizeof(buf), "%s — %.1f fps",
                              impl_->title.c_str(),
                              impl_->fps_presents / span_s);
                impl_->api->SetWindowTitle(buf);
                impl_->fps_window_start_s = now_s;
                impl_->fps_presents = 0;
            }
        }

        // Post-processing passes beyond the LCD grid belong here too (the spin-off
        // hook: a game draws custom shapes through set_host_overlay, full-screen
        // shader work lands on the blit above).
#else
        (void)rgb888;
#endif
    }

    void interp_display_tick(const std::uint8_t* rgb888,
                                            std::uint64_t deadline_ns) override {
#if defined(GBARECOMP_USE_RAYLIB)
        // Display-paced interpolation: subdivide the guest wait at display rate.
        // Policy (when to hold, how to sleep, how boundaries advance) is the
        // shared interp_pacing.h; this only supplies the clock, the sleep and the
        // present. Mirrors the SDL backend's interp_display_tick.
        if (!rgb888 || !impl_->frame_interp_enabled) return;
        if (!impl_->open || !impl_->api->IsWindowReady()) return;
        Impl* impl = impl_;
        if ((++impl->interp_mode_counter % 600u) == 0u)
            refresh_interp_mode(impl);
        const std::uint64_t now_ns = raylib_steady_now_ns();
        if (impl->interp_tick_ns == 0 ||
            now_ns >= impl->interp_tick_ns + impl->interp_period_ns * 2u)
            impl->interp_tick_ns = now_ns;  // (re)anchor when unset or far adrift
        // A vsync-blocked present lands one full display period from now; if that
        // is past the guest deadline, presenting would hand the guest an extra
        // display period. Hold the last frame and sleep out the remainder instead.
        if (interp_should_hold_for_deadline(impl->interp_vsync,
                                            impl->interp_period_ns, now_ns,
                                            deadline_ns)) {
            raylib_sleep_until_ns(
                interp_clamp_wake_ns(deadline_ns, now_ns, deadline_ns));
            return;
        }
        const InterpSleep sleep_kind =
            interp_sleep_kind(impl->interp_vsync, impl->interp_period_ns,
                              impl->interp_tick_ns, now_ns, deadline_ns);
        if (sleep_kind == InterpSleep::kFixed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else if (sleep_kind == InterpSleep::kBoundary) {
            raylib_sleep_until_ns(interp_clamp_wake_ns(impl->interp_tick_ns, now_ns,
                                                       deadline_ns));
        }
        present(rgb888, false);
        const std::uint64_t after_ns = raylib_steady_now_ns();
        impl->interp_tick_ns = interp_advance_tick_ns(impl->interp_vsync,
                                                      impl->interp_period_ns,
                                                      impl->interp_tick_ns,
                                                      after_ns);
        // Presents/sec report (~600-tick windows): the only remote proof the tick
        // loop is actually subdividing waits at display rate.
        if (++impl->interp_tick_count == 1u)
            impl->interp_rate_window_ns = after_ns;
        else if (impl->interp_tick_count >= 600u) {
            const std::uint64_t span_ns = after_ns - impl->interp_rate_window_ns;
            if (span_ns > 0u) {
                std::fprintf(stderr,
                             "raylib_backend: interp ticks=%u rate=%.1f/s\n",
                             impl->interp_tick_count,
                             static_cast<double>(impl->interp_tick_count) * 1e9 /
                                 static_cast<double>(span_ns));
            }
            impl->interp_tick_count = 0;
        }
#else
        (void)rgb888; (void)deadline_ns;
#endif
    }

    bool readback(std::vector<std::uint8_t>& out,
                                             int& w, int& h) const override {
#if defined(GBARECOMP_USE_RAYLIB)
        if (!impl_->open) return false;
        std::lock_guard<std::mutex> lk(impl_->last_upload_mu);
        if (impl_->last_upload.size() !=
            static_cast<std::size_t>(impl_->base_w) * impl_->base_h * 3u)
            return false;
        out = impl_->last_upload;
        w = impl_->base_w;
        h = impl_->base_h;
        return true;
#else
        (void)out; (void)w; (void)h;
        return false;
#endif
    }

    float refresh_hz() const override {
        // The display period probed at open (interp_period_ns; the 60 Hz
        // placeholder until then). Caps::vsync_report gates meaningfulness.
        return 1.0e9f / static_cast<float>(impl_->interp_period_ns);
    }

    bool drawable_size(int* width, int* height) const override {
#if defined(GBARECOMP_USE_RAYLIB)
        if (!impl_->open || !impl_->api->IsWindowReady()) return false;
        if (width) *width = impl_->api->GetScreenWidth();
        if (height) *height = impl_->api->GetScreenHeight();
        return width && height && *width > 0 && *height > 0;
#else
        if (width) *width = 0;
        if (height) *height = 0;
        return false;
#endif
    }

    bool set_surface_size(int base_w, int base_h) override {
        if (base_w < 1 || base_h < 1) return false;
        if (base_w == impl_->base_w && base_h == impl_->base_h) return true;
        impl_->base_w = base_w;
        impl_->base_h = base_h;
#if defined(GBARECOMP_USE_RAYLIB)
        // The prescale factor depends on the surface size; drop the cached target
        // so the next present recomputes it.
        release_sharp_target(impl_);
        // Recreate the streaming texture at the new size, mirroring
        // destroy_sharp_texture()/recreate on the SDL side. surface_pixels must
        // outlive Image::data, so re-point after reassigning.
        if (impl_->open && impl_->api->IsWindowReady() && impl_->texture_ready) {
            impl_->api->UnloadTexture(impl_->texture);
            impl_->texture_ready = false;
            impl_->surface_pixels.assign(
                static_cast<std::size_t>(base_w) * base_h * 3u, 0);
            impl_->surface = Image{};
            impl_->surface.width = base_w;
            impl_->surface.height = base_h;
            impl_->surface.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
            impl_->surface.mipmaps = 1;
            impl_->surface.data = impl_->surface_pixels.data();
            impl_->texture = impl_->api->LoadTextureFromImage(impl_->surface);
            if (impl_->texture.id == 0) {
                std::fprintf(stderr,
                             "raylib_backend: set_surface_size texture %dx%d failed\n",
                             base_w, base_h);
                return false;
            }
            impl_->texture_ready = true;
            impl_->api->SetTextureFilter(impl_->texture, impl_->linear_filter
                                                ? TEXTURE_FILTER_BILINEAR
                                                : TEXTURE_FILTER_POINT);
        }
#endif
        // The graded framebuffer tracks the surface size, not the window size.
        if (impl_->color_lut && !impl_->color_lut->is_passthrough())
            impl_->graded_fb.assign(
                static_cast<std::size_t>(base_w) * base_h * 3u, 0);
        return true;
    }

    void set_title(const char* title) override {
        if (title) impl_->title = title;
#if defined(GBARECOMP_USE_RAYLIB)
        if (impl_->open && impl_->api->IsWindowReady()) impl_->api->SetWindowTitle(impl_->title.c_str());
#endif
    }

    int scale() const override { return impl_->scale; }

    void adjust_scale(int delta) override {
        // Clamp is the shared policy; the SDL backend does the same. The
        // resize below is the part the port dropped (SDL called
        // SDL_SetWindowSize + recenter); without it the menu row is dead.
#if defined(__ANDROID__)
        (void)delta;  // fixed surface: the window is the display
        return;
#else
        if (impl_->fullscreen != 0) return;  // meaningless while fullscreen
        const int s = std::clamp(impl_->scale + delta, 1, 8);
        if (s == impl_->scale) return;
        impl_->scale = s;
#if defined(GBARECOMP_USE_RAYLIB)
        if (impl_->open && impl_->api->IsWindowReady())
            impl_->api->SetWindowSize(impl_->base_w * s, impl_->base_h * s);
#endif
#endif
    }

    void set_linear_filter(bool enabled) override {
        // Sharp and linear are mutually exclusive (sharp wins at resolve time);
        // enabling linear clears sharp, mirroring the SDL backend.
        if (enabled && impl_->sharp_filter) {
            impl_->sharp_filter = false;
            release_sharp_target(impl_);
        }
        impl_->linear_filter = enabled;
#if defined(GBARECOMP_USE_RAYLIB)
        if (impl_->open && impl_->api->IsWindowReady() && impl_->texture_ready)
            impl_->api->SetTextureFilter(impl_->texture, enabled ? TEXTURE_FILTER_BILINEAR
                                                     : TEXTURE_FILTER_POINT);
#endif
    }

    bool linear_filter() const override { return impl_->linear_filter; }

    void set_resize_driven_view(bool enabled) override {
        impl_->resize_driven_view = enabled;
    }

    void set_view_margins(std::uint32_t left, std::uint32_t right,
                                         std::uint32_t top,
                                         std::uint32_t bottom) override {
        impl_->view_margin_left = left;
        impl_->view_margin_right = right;
        impl_->view_margin_top = top;
        impl_->view_margin_bottom = bottom;
    }

    void set_fullscreen(int mode) override {
        impl_->fullscreen = std::clamp(mode, 0, 2);
#if defined(GBARECOMP_USE_RAYLIB)
        if (impl_->open && impl_->api->IsWindowReady()) {
            // Mirror the SDL mapping: 0 windowed, 1 borderless desktop,
            // 2 exclusive fullscreen. raylib has only FLAG_FULLSCREEN_MODE, so
            // both non-zero modes land on the same state -- raylib has no
            // exclusive-fullscreen equivalent. `fullscreen_mode()` still reports
            // the requested value, so host behaviour is unchanged.
            //
            // Windowed CLEARS the flag; it must never hide the window. Hiding here
            // is what made a default-configured launch invisible.
            if (impl_->fullscreen == 0) impl_->api->ClearWindowState(FLAG_FULLSCREEN_MODE);
            else impl_->api->SetWindowState(FLAG_FULLSCREEN_MODE);
        }
#endif
    }

    int fullscreen() const override { return impl_->fullscreen; }

    float px_per_mm() const override { return impl_->px_per_mm; }

    void set_presentation_anchor_top(bool anchor_top) override {
        impl_->anchor_top = anchor_top;
    }

    void set_fps_readout(bool on) override { impl_->fps_readout = on; }

    bool fps_readout() const override { return impl_->fps_readout; }

private:
    Impl* impl_;
};

// ── V2 concern: Audio ───────────────────────────────────────────────────
// The V2 `Audio` interface over the backend's Impl state. claim/release are
// the V2 ownership handshake: claim succeeds once per open when the device
// came up (the condition Caps::audio will report) and refuses a second
// claimant or an unavailable device; release clears it. The device itself is
// still initialized/torn down by open()/close() — claim is not a second init
// path. push() keeps the flat behavior (no claim gate), so the flat
// generation's callers are unaffected; it stays the one cross-thread call.
class RaylibBackend::AudioImpl final : public Audio {
public:
    explicit AudioImpl(Impl* impl) : impl_(impl) {}

    Result claim() override {
        if (impl_->audio_claimed)
            return {Err::AudioDevice, "audio already claimed"};
        if (!impl_->audio_ready)
            return {Err::AudioDevice, "audio device unavailable"};
        impl_->audio_claimed = true;
        return {};
    }

    void release() override {
        // Single owner: a held claim is always this caller's.
        impl_->audio_claimed = false;
    }

    void push(const std::int16_t* samples, std::size_t count) override {
#if defined(GBARECOMP_USE_RAYLIB)
        if (!samples || count == 0) return;
        // Mute is device-independent and is honored BEFORE any device
        // gating: the Phase 4b carve left audio_enabled set-but-never-
        // read, so the menu toggle and the boot-time [Menu] audio_enabled
        // application were both silent no-ops (2026-10-16 user report).
        // Dropping here — not in the stream callback — keeps the decision
        // out of the device path and observable without an audio device;
        // whatever is already buffered drains its short tail.
        if (!impl_->audio_enabled) {
            ++g_audio_drops_muted;
            return;
        }
        if (!impl_->bridge_ready || !impl_->audio_ready) {
            ++g_audio_drops_nodev;
            return;
        }
        // Volume first, into scratch, exactly as the SDL backend does: 100 is
        // passthrough so the common path is byte-identical to unpatched behavior.
        if (impl_->volume != 100) {
            impl_->volume_buf.resize(count);
            for (std::size_t i = 0; i < count; ++i)
                impl_->volume_buf[i] = apply_volume_gain(samples[i], impl_->volume);
            samples = impl_->volume_buf.data();
        }
        std::lock_guard<std::mutex> lk(impl_->bridge_mu);
        rab_push(&impl_->bridge, samples, static_cast<int>(count));  // mono
#else
        (void)samples; (void)count;
#endif
    }

    void set_volume(int pct) override {
        impl_->volume = std::clamp(pct, 0, 100);
    }

    int volume() const override { return impl_->volume; }

    void set_enabled(bool enabled) override { impl_->audio_enabled = enabled; }

    bool enabled() const override { return impl_->audio_enabled; }

private:
    Impl* impl_;
};

// ── V2 concern: Input ───────────────────────────────────────────
// The V2 `Input` interface over the backend's Impl state. read() is the
// snapshot fill (levels only) that used to sit at the top of the flat
// pump(); the flat pump keeps the composition (translate_input is the only
// edge producer) plus the touch-FF post-translate overlay, exactly as the
// SDL sibling does. load_config is the input half of load_input_config;
// the [video] LCD block stays behind until the Fx step.
class RaylibBackend::InputImpl final : public Input {
public:
    explicit InputImpl(Impl* impl) : impl_(impl) {}

    void read(InputSnapshot& out) override {
#if defined(GBARECOMP_USE_RAYLIB)
#if defined(__ANDROID__)
        refresh_android_display_info(impl_);
#endif
        if (!impl_->open || !impl_->api->IsWindowReady()) {
            impl_->touch_ff_held = false;
            out = InputSnapshot{};
            return;
        }

        // raylib is PULL-based: there is no KEYDOWN event with a repeat flag, so
        // every edge in Events is recovered by translate_input() from prior-pump
        // state (InputEdges). This function only READS the current level into a
        // snapshot; all policy lives in input_translate.h, tested without a
        // display.
        InputSnapshot s;
        s.now_ms = impl_->clock->millis();
        s.close_requested = impl_->api->WindowShouldClose();

        unsigned mods = kModNone;
        if (impl_->api->IsKeyDown(KEY_LEFT_SHIFT) || impl_->api->IsKeyDown(KEY_RIGHT_SHIFT))
            mods |= kModShift;
        if (impl_->api->IsKeyDown(KEY_LEFT_CONTROL) || impl_->api->IsKeyDown(KEY_RIGHT_CONTROL))
            mods |= kModCtrl;
        if (impl_->api->IsKeyDown(KEY_LEFT_ALT) || impl_->api->IsKeyDown(KEY_RIGHT_ALT))
            mods |= kModAlt;
        s.mods = mods;

        // Poll exactly the bound keys, not the whole keyboard: IsKeyDown per key
        // is cheap, and this keeps the snapshot identical in shape to what the
        // config file means. OR in IsKeyPressed plus the GetKeyPressed queue
        // so a tap that presses and releases between two presents (raylib
        // only refreshes key state on PollInputEvents inside EndDrawing)
        // still appears held for one pump. IsKeyPressed alone is NOT enough:
        // it is previous==0 && current==1 at Poll time, so a press+release
        // inside one Poll interval reads up/up and is missed — only the
        // queue (enqueued on the PRESS callback) sees it. Without the queue,
        // quick Delete taps fall between pumps and never form the
        // translate_input edge; double-taps merely double the odds that one
        // press spans a Poll.
        const InputBindings& b = impl_->input_bindings;
        int queued[16];
        int queued_count = 0;
        for (int k = impl_->api->GetKeyPressed();
             k != 0 && queued_count < 16;
             k = impl_->api->GetKeyPressed())
            queued[queued_count++] = k;
        auto poll = [&](HostKey k) {
            if (k == kHostKeyNone) return;
            if (impl_->api->IsKeyDown(k) || impl_->api->IsKeyPressed(k)) {
                s.hold(k);
                return;
            }
            for (int i = 0; i < queued_count; ++i) {
                if (queued[i] == k) {
                    s.hold(k);
                    return;
                }
            }
        };
        for (int bit = 0; bit < 10; ++bit) poll(b.gba_key[bit]);
        for (int i = 0; i < 9; ++i) poll(b.slot_key[i]);
        poll(b.escape_key);
        for (int h = 0; h < HK_COUNT; ++h) poll(b.hotkeys[h].key);
        poll(b.assist_key[0]);
        poll(b.assist_key[1]);

        // Gamepad. The ONLY platform-specific part: native buttons/axes into
        // canonical PadButton/PadAxis ids plus a GamepadSnapshot. The button->GBA
        // mapping and stick deadzone live in gamepad_map.h, shared with the SDL
        // backend. The native->canonical table is explicit so an upstream
        // reordering cannot silently remap a binding.
#if defined(__ANDROID__)
        // No GLFW on Android: raylib's own (capped, unfiltered) slots are all
        // there is.
        s.pad_connected = impl_->api->IsGamepadAvailable(0);
        if (s.pad_connected) {
            static const struct {
                int ray_button;
                int pad_button;
                std::uint16_t gba_bit;   // 0 when the button has no GBA meaning
            } kPadMap[] = {
                {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, kPadA, kGbaKeyA},
                {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, kPadB, kGbaKeyB},
                {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, kPadX, 0},
                {GAMEPAD_BUTTON_RIGHT_FACE_UP, kPadY, 0},
                {GAMEPAD_BUTTON_LEFT_TRIGGER_1, kPadLeftBumper, kGbaKeyL},
                {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, kPadRightBumper, kGbaKeyR},
                {GAMEPAD_BUTTON_MIDDLE_LEFT, kPadBack, kGbaKeySelect},
                {GAMEPAD_BUTTON_MIDDLE_RIGHT, kPadStart, kGbaKeyStart},
                {GAMEPAD_BUTTON_LEFT_THUMB, kPadLeftThumb, 0},
                {GAMEPAD_BUTTON_RIGHT_THUMB, kPadRightThumb, 0},
                {GAMEPAD_BUTTON_LEFT_FACE_UP, kPadDpadUp, kGbaKeyUp},
                {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, kPadDpadRight, kGbaKeyRight},
                {GAMEPAD_BUTTON_LEFT_FACE_DOWN, kPadDpadDown, kGbaKeyDown},
                {GAMEPAD_BUTTON_LEFT_FACE_LEFT, kPadDpadLeft, kGbaKeyLeft},
                {GAMEPAD_BUTTON_LEFT_TRIGGER_2, kPadLeftTrigger, 0},
                {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, kPadRightTrigger, 0},
            };
            GamepadSnapshot pad;
            for (const auto& m : kPadMap) {
                if (impl_->api->IsGamepadButtonDown(0, m.ray_button)) {
                    s.hold_pad_button(m.pad_button);
                    pad.buttons |= m.gba_bit;
                }
            }
            // raylib axes are float -1..1; the shared mapping works in SDL's int
            // units (-32768..32767), so scale. Triggers rest at -1 and press to
            // +1, which lands on the same side of the 16000 threshold as SDL's
            // 0..32767 range does.
            const float lx = impl_->api->GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            const float ly = impl_->api->GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
            pad.stick_x = static_cast<int>(lx * 32767.0f);
            pad.stick_y = static_cast<int>(ly * 32767.0f);
            s.hold_pad_axis(kPadAxisLeftX, pad.stick_x);
            s.hold_pad_axis(kPadAxisLeftY, pad.stick_y);
            s.hold_pad_axis(kPadAxisRightX, static_cast<int>(
                impl_->api->GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X) * 32767.0f));
            s.hold_pad_axis(kPadAxisRightY, static_cast<int>(
                impl_->api->GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y) * 32767.0f));
            s.hold_pad_axis(kPadAxisLeftTrigger, static_cast<int>(
                impl_->api->GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) * 32767.0f));
            s.hold_pad_axis(kPadAxisRightTrigger, static_cast<int>(
                impl_->api->GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) * 32767.0f));
            // gamepad_to_keyinput() returns ACTIVE-LOW; the snapshot carries
            // ACTIVE-HIGH pressed bits (see input_translate.h), so invert once.
            s.pad_gba_bits =
                static_cast<std::uint16_t>(~gamepad_to_keyinput(pad) & kGbaKeyAll);
        }
#else
#if defined(PLATFORM_MEMORY)
        // Memory platform: no input system at all (plan §4.2) — and no GLFW
        // to select through. Report no pad; the snapshot stays all-up.
        s.pad_connected = false;
        impl_->pad_glfw_index = -1;
        impl_->pad_raylib_slot = -1;
#else
        // Desktop: select through GLFW's standard-mapping layer, NOT raylib slot
        // 0 (see find_standard_pad: phantoms eat raylib's 4 slots while a real
        // pad sits beyond them). GUIDE has no canonical/PadButton slot and no
        // GBA meaning -- skipped, same as the SDL table.
        impl_->pad_glfw_index = find_standard_pad();
        impl_->pad_raylib_slot = (impl_->pad_glfw_index >= 0 &&
                                  impl_->pad_glfw_index < 4)
                                     ? impl_->pad_glfw_index
                                     : -1;
        s.pad_connected = impl_->pad_glfw_index >= 0;
        if (s.pad_connected) {
            RayGlfwGamepadState st{};
            if (!glfwGetGamepadState(impl_->pad_glfw_index, &st)) {
                // Disconnected mid-frame: report nothing, rescan next pump.
                s.pad_connected = false;
                impl_->pad_glfw_index = -1;
                impl_->pad_raylib_slot = -1;
            } else {
                static const struct {
                    int glfw_button;
                    int pad_button;
                    std::uint16_t gba_bit;  // 0: no GBA meaning
                } kPadMap[] = {
                    {kGlfwBtnA, kPadA, kGbaKeyA},
                    {kGlfwBtnB, kPadB, kGbaKeyB},
                    {kGlfwBtnX, kPadX, 0},
                    {kGlfwBtnY, kPadY, 0},
                    {kGlfwBtnLB, kPadLeftBumper, kGbaKeyL},
                    {kGlfwBtnRB, kPadRightBumper, kGbaKeyR},
                    {kGlfwBtnBack, kPadBack, kGbaKeySelect},
                    {kGlfwBtnStart, kPadStart, kGbaKeyStart},
                    {kGlfwBtnLThumb, kPadLeftThumb, 0},
                    {kGlfwBtnRThumb, kPadRightThumb, 0},
                    {kGlfwBtnDUp, kPadDpadUp, kGbaKeyUp},
                    {kGlfwBtnDRight, kPadDpadRight, kGbaKeyRight},
                    {kGlfwBtnDDown, kPadDpadDown, kGbaKeyDown},
                    {kGlfwBtnDLeft, kPadDpadLeft, kGbaKeyLeft},
                };
                GamepadSnapshot pad;
                // Per-device mapping quirks (see glfw_pad_quirks.h): same
                // physical press must reach the same GBA bit as on SDL.
                const char* guid = glfwGetJoystickGUID(impl_->pad_glfw_index);
                for (const auto& m : kPadMap) {
                    const int btn = quirk_glfw_button(guid, m.glfw_button);
                    if (btn >= 0 && btn < 15 && st.buttons[btn]) {
                        s.hold_pad_button(m.pad_button);
                        pad.buttons |= m.gba_bit;
                    }
                }
                // GLFW gamepad axes are float -1..1 like raylib's; scale to the
                // shared int units identically.
                pad.stick_x =
                    static_cast<int>(st.axes[kGlfwAxisLX] * 32767.0f);
                pad.stick_y =
                    static_cast<int>(st.axes[kGlfwAxisLY] * 32767.0f);
                s.hold_pad_axis(kPadAxisLeftX, pad.stick_x);
                s.hold_pad_axis(kPadAxisLeftY, pad.stick_y);
                s.hold_pad_axis(kPadAxisRightX, static_cast<int>(
                    st.axes[kGlfwAxisRX] * 32767.0f));
                s.hold_pad_axis(kPadAxisRightY, static_cast<int>(
                    st.axes[kGlfwAxisRY] * 32767.0f));
                s.hold_pad_axis(kPadAxisLeftTrigger, static_cast<int>(
                    st.axes[kGlfwAxisLT] * 32767.0f));
                s.hold_pad_axis(kPadAxisRightTrigger, static_cast<int>(
                    st.axes[kGlfwAxisRT] * 32767.0f));
                s.pad_gba_bits = static_cast<std::uint16_t>(
                    ~gamepad_to_keyinput(pad) & kGbaKeyAll);
            }
        }
#endif  // PLATFORM_MEMORY gamepad guard
#endif

        // Mouse gyro stand-in (WarioWare-style twist): horizontal delta while the
        // left button is held. Same quantity as SDL_GetRelativeMouseState. Gated
        // off below whenever touch can drive the game.
        s.mouse_left = impl_->api->IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        s.mouse_dx = static_cast<int>(impl_->api->GetMouseDelta().x);

        // ---- touch ----
        // Poll raylib touch points + emulated mouse into TouchTracks, hit-test
        // the shared pad layout, and route the rest to the shared TouchHub.
        // Mirrors the SDL finger-event branch: a touch is pad, toggle or routed
        // for its whole life; sliding across pad buttons follows the finger.
        std::uint16_t touch_buttons = 0;
        bool touch_ff_held = false;
        Impl* impl = impl_;
        const bool touch_on = touch_input_enabled(
            impl->touch_controls, impl->touch_policy, impl->touch_emulation);
        if (touch_on) {
            TouchHub& hub = TouchHub::instance();
            TouchPresentation pres;
            pres.drawable_width = impl_->api->GetScreenWidth();
            pres.drawable_height = impl_->api->GetScreenHeight();
            pres.layout = impl->last_layout;
            pres.view_width = impl->base_w;
            pres.view_height = impl->base_h;
            pres.extra_left = impl->view_margin_left;
            pres.extra_right = impl->view_margin_right;
            pres.extra_top = impl->view_margin_top;
            pres.extra_bottom = impl->view_margin_bottom;
            pres.drawable_px_per_mm = impl->px_per_mm * impl->touch_pad_scale;
            pres.safe_insets.left = impl->safe_left;
            pres.safe_insets.top = impl->safe_top;
            pres.safe_insets.right = impl->safe_right;
            pres.safe_insets.bottom = impl->safe_bottom;
            hub.set_presentation(pres);
            const std::uint32_t now_ms = touch_clock_ms();
            hub.advance(now_ms);
            // Idle-hide: a quiet pad hides itself so it never burns in.
            // Held pad/toggle touches keep it up so a control never vanishes
            // mid-press.
            if (impl->pad_idle_hide_seconds > 0 && impl->touch_controls &&
                impl->pad_visible && !impl->pad_idle_hidden) {
                bool held = false;
                for (const auto& [id, t] : impl->touches)
                    held = held || (t.active && (t.pad || t.toggle));
                const std::uint32_t deadline =
                    impl->pad_last_touch_ms +
                    static_cast<std::uint32_t>(impl->pad_idle_hide_seconds) * 1000u;
                if (!held &&
                    static_cast<std::int32_t>(now_ms - deadline) >= 0) {
                    impl->pad_idle_hidden = true;
                    std::fprintf(stderr,
                                 "raylib_backend: pad idle-hidden (%d s quiet)\n",
                                 impl->pad_idle_hide_seconds);
                }
            }

            PadLayoutInputs lin = pad_layout_inputs(impl, pres.drawable_width,
                                                         pres.drawable_height);
            const PadLayout pad = compute_pad_layout(lin);
            const float mm = std::max(1.0f,
                                      impl->px_per_mm * impl->touch_pad_scale);
            const bool overlay_shown = pad_overlay_shown(
                impl->pad_visible, impl->touch_controls, impl->pad_idle_hidden);
            const bool toggle_avail = pad_toggle_available(
                impl->touch_policy, impl->touch_controls, impl->touch_emulation);

            auto route = [&](int pointer, TouchPhase phase, float px, float py,
                             TouchSource source) {
                TouchEvent te;
                te.t_ms = now_ms;
                te.pointer = pointer & 0x7FFFFFFF;
                te.phase = phase;
                te.source = source;
                te.drawable_x = px;
                te.drawable_y = py;
                te.in_view = presentation_point_to_logical(
                    impl->last_layout, impl->base_w, impl->base_h, px, py,
                    &te.view_x, &te.view_y);
                hub.submit(te);
            };

            // Current live points: raylib fingers plus the emulated mouse finger
            // when emulation is on (raylib has no MOUSE_TOUCHID hint; the left
            // drag feeds the same track path as a finger, id -1).
            struct LivePoint { int id; float x, y; TouchSource source; };
            std::vector<LivePoint> live;
            const int n_touch = impl_->api->GetTouchPointCount();
            for (int i = 0; i < n_touch; ++i)
                live.push_back({impl_->api->GetTouchPointId(i), impl_->api->GetTouchPosition(i).x,
                                impl_->api->GetTouchPosition(i).y, TouchSource::Finger});
            if (impl->touch_emulation && impl_->api->IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                live.push_back({-1, static_cast<float>(impl_->api->GetMouseX()),
                                static_cast<float>(impl_->api->GetMouseY()),
                                TouchSource::Mouse});
            if (!live.empty()) {
                // Any finger activity refreshes the pad idle timer and reveals a
                // hidden pad; the touch then acts normally, so a tap after the
                // quiet period is never swallowed.
                impl->pad_last_touch_ms = now_ms;
                impl->pad_idle_hidden = false;
            }
            for (const auto& lp : live) {
                auto it = impl->touches.find(lp.id);
                if (it == impl->touches.end()) {
                    Impl::TouchTrack track;
                    track.active = true;
                    track.x = track.start_x = lp.x;
                    track.y = track.start_y = lp.y;
                    const std::uint16_t buttons =
                        overlay_shown ? pad_buttons_at(pad, lp.x, lp.y, mm) : 0;
                    if (toggle_avail &&
                        pad.toggle.contains(lp.x, lp.y, 2.0f * mm)) {
                        track.toggle = true;
                        track.grab_dx = pad.toggle.x + pad.toggle.w * 0.5f - lp.x;
                        track.grab_dy = pad.toggle.y + pad.toggle.h * 0.5f - lp.y;
                    } else if (overlay_shown &&
                               pad_ff_at(pad, lp.x, lp.y, mm)) {
                        track.ff = true;
                    } else if (buttons) {
                        track.pad = true;
                        track.buttons = buttons;
                    } else {
                        track.routed = true;
                        route(lp.id, TouchPhase::Down, lp.x, lp.y, lp.source);
                    }
                    impl->touches[lp.id] = track;
                } else {
                    Impl::TouchTrack& track = it->second;
                    track.x = lp.x;
                    track.y = lp.y;
                    if (track.pad) {
                        track.buttons = pad_buttons_at(pad, lp.x, lp.y, mm);
                    } else if (track.ff) {
                        // Sliding off the pill releases, like the pad buttons.
                        track.ff = overlay_shown && pad_ff_at(pad, lp.x, lp.y, mm);
                    } else if (track.routed) {
                        route(lp.id, TouchPhase::Move, lp.x, lp.y, lp.source);
                    } else if (track.toggle) {
                        const float slop = 2.5f * mm;
                        const float mx = lp.x - track.start_x;
                        const float my = lp.y - track.start_y;
                        if (!track.toggle_dragging &&
                            mx * mx + my * my > slop * slop)
                            track.toggle_dragging = true;
                        if (track.toggle_dragging) {
                            const float l = static_cast<float>(impl->safe_left);
                            const float t = static_cast<float>(impl->safe_top);
                            const float w = std::max(
                                1.0f, lin.drawable_w - l - impl->safe_right);
                            const float h = std::max(
                                1.0f, lin.drawable_h - t - impl->safe_bottom);
                            impl->toggle_fx = std::clamp(
                                (lp.x + track.grab_dx - l) / w, 0.0f, 1.0f);
                            impl->toggle_fy = std::clamp(
                                (lp.y + track.grab_dy - t) / h, 0.0f, 1.0f);
                            impl->toggle_pos_saved = true;
                        }
                    }
                }
            }
            // Points that vanished since the last pump ended.
            for (auto it = impl->touches.begin(); it != impl->touches.end();) {
                const int id = it->first;
                Impl::TouchTrack& track = it->second;
                bool still = false;
                for (const auto& lp : live)
                    if (lp.id == id) { still = true; break; }
                if (still || !track.active) { ++it; continue; }
                if (track.routed)
                    route(id, TouchPhase::Up, track.x, track.y,
                          TouchSource::Finger);
                else if (track.toggle) {
                    if (track.toggle_dragging) {
                        save_touch_config(impl);
                    } else if (pad.toggle.contains(track.x, track.y,
                                                   2.0f * mm)) {
                        // A tap shows/hides the pad (the shared helper keeps
                        // persistence + logging in one place).
                        apply_touch_pad_visible(impl, !impl->pad_visible);
                    }
                }
                it = impl->touches.erase(it);
            }
            // Desktop stand-ins for multi-finger taps (same bindings as SDL:
            // right = two-finger, middle = three-finger).
            if (impl_->api->IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
                impl_->api->IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
                const bool right = impl_->api->IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
                Gesture g;
                g.kind = right ? GestureKind::TwoFingerTap
                               : GestureKind::ThreeFingerTap;
                g.fingers = right ? 2 : 3;
                g.t_ms = now_ms;
                g.source = TouchSource::Mouse;
                g.drawable_x = g.start_drawable_x =
                    static_cast<float>(impl_->api->GetMouseX());
                g.drawable_y = g.start_drawable_y =
                    static_cast<float>(impl_->api->GetMouseY());
                g.start_in_view = presentation_point_to_logical(
                    impl->last_layout, impl->base_w, impl->base_h, g.drawable_x,
                    g.drawable_y, &g.view_x, &g.view_y);
                g.start_view_x = g.view_x;
                g.start_view_y = g.view_y;
                hub.submit_gesture(g);
            }
            // Platform Back (Android Back key; desktop Backspace when emulating).
            if (impl_->api->IsKeyPressed(KEY_BACK) ||
                (impl->touch_emulation && impl_->api->IsKeyPressed(KEY_BACKSPACE))) {
                Gesture g;
                g.kind = GestureKind::Back;
                g.t_ms = now_ms;
                hub.submit_gesture(g);
            }
            // Engine host actions (unclaimed long-press, three-finger tap,
            // Back) are NOT drained here: the host policy consumes them
            // (plan §3d) — the runtime drains them after pump() and opens or
            // closes the generic menu.
            for (const auto& [id, track] : impl->touches) {
                if (!track.active) continue;
                if (track.pad) touch_buttons |= track.buttons;
                if (track.ff) touch_ff_held = true;
            }
        }
        s.touch_enabled = touch_on;
        s.touch_gba_bits = touch_buttons;

        // The generic menu (V2): the flag derives from the presented
        // MenuModel's open state (plan §3.4).
        if (impl_->menu_open) s.suppress_game_input = true;
        // gyro_rate_z stays 0: raylib exposes no gyroscope API, and silently
        // faking motion would be worse than reporting none.
        // The touch fast-forward pill is a level overlay the shared
        // translate_input cannot see; pump() ORs it in after translation.
        impl_->touch_ff_held = touch_ff_held;
        out = s;
#else
        impl_->touch_ff_held = false;
        out = InputSnapshot{};
        // State-only stub: no input device, but the derived menu flag is
        // pure state and reports the same as the raylib path.
        out.suppress_game_input = impl_->menu_open;
#endif
    }

    void load_config(const char* dir, bool assist_tools_default,
                     int fast_forward_multiplier_default) override {
        impl_->assist_tools = assist_tools_default;
        impl_->fast_forward_multiplier =
            std::clamp(fast_forward_multiplier_default, 2, 10);
        // Reseed before applying overrides so repeated loads are idempotent and a
        // missing file leaves the built-in defaults, exactly as the SDL backend's
        // open() seeding does.
        raylib_default_bindings(impl_->input_bindings);
        if (!dir) return;
        const std::string base = std::string(dir) + "/";

        // keybinds.ini [player1]: GBA button name -> key name. Same vocabulary as
        // the SDL backend (kBindKeys); "None"/unknown unbinds.
        ini_scan_section((base + "keybinds.ini").c_str(), "player1",
                         [this](const char* key, const char* val) {
            apply_keybinds_line(impl_->input_bindings, key, val);
        });

        // config.ini [KeyMap]: hotkey name -> "Ctrl+/Alt+/Shift+Key" value.
        ini_scan_section((base + "config.ini").c_str(), "KeyMap",
                         [this](const char* key, const char* val) {
            apply_keymap_line(impl_->input_bindings, key, val);
        });

        // Launcher-owned global Assist bindings. Numeric keyboard values are SDL
        // SCANCODES on disk (the SDL backend was first), so they go through the
        // scancode->raylib conversion, not atoi directly; controller values use
        // the portable button/axis encoding both backends share.
        ini_scan_section((base + "config.ini").c_str(), "Launcher",
                         [this](const char* key, const char* val) {
            apply_launcher_line(impl_->input_bindings, impl_->assist_tools,
                                impl_->fast_forward_multiplier, key, val);
        });

        // Effective Menu trigger (the Delete-key debug need): one line on
        // stderr so a "Delete does nothing" report can rule out a config
        // override vs a missed edge without a debugger.
        {
            const HotkeyBinding& mb = impl_->input_bindings.hotkeys[HK_MENU];
            std::fprintf(stderr, "raylib_backend: Menu hotkey key=%d mods=%u\n",
                         mb.key, mb.mods);
        }
    }

    const InputBindings& bindings() const override {
        return impl_->input_bindings;
    }

    bool assist_tools_enabled() const override { return impl_->assist_tools; }

    int fast_forward_multiplier() const override {
        return impl_->fast_forward_multiplier;
    }

    bool gamepad_connected() const override {
#if defined(GBARECOMP_USE_RAYLIB)
        if (!impl_->open || !impl_->api->IsWindowReady()) return false;
#if defined(__ANDROID__)
        return impl_->api->IsGamepadAvailable(0);
#elif defined(PLATFORM_MEMORY)
        return false;  // no input system, no GLFW (see above)
#else
        // Standard-mapped pad selection lives behind find_standard_pad()
        // (see above): raylib slot 0 is routinely a phantom.
        return find_standard_pad() >= 0;
#endif
#endif
        return false;
    }

    void rumble(float low_frequency, float high_frequency,
                    std::uint32_t duration_ms) override {
#if defined(GBARECOMP_USE_RAYLIB)
        if (!gamepad_connected()) return;
        const float low = std::clamp(low_frequency, 0.0f, 1.0f);
        const float high = std::clamp(high_frequency, 0.0f, 1.0f);
        // raylib 6 takes an int gamepad index and a duration in SECONDS. Rumble
        // the selected pad's raylib slot when it has one; a pad beyond raylib's
        // range (see find_standard_pad) cannot rumble through this API.
        const int slot = impl_->pad_raylib_slot;
        if (slot < 0) return;
        impl_->api->SetGamepadVibration(slot, low, high, duration_ms / 1000.0f);
#else
        (void)low_frequency; (void)high_frequency; (void)duration_ms;
#endif
    }

    void haptic_pulse(int duration_ms, float strength) override {
        // raylib core has no cross-platform system vibrator, and SDL_Haptic has
        // no raylib equivalent. Desktop haptics therefore ride the gamepad motors
        // via the rumble path (the spin-off enhancement working as designed); with
        // no pad attached this is a documented no-op, matching "no-op sans
        // vibrator" in the SDL path. Android should route to the platform
        // vibrator when that port lands.
        if (!haptic_should_play(duration_ms)) return;
#if defined(GBARECOMP_USE_RAYLIB)
        if (!impl_->open || !impl_->api->IsWindowReady() || !impl_->api->IsGamepadAvailable(0)) return;
        const float s = haptic_clamp_strength(strength);
        impl_->api->SetGamepadVibration(0, s, s, duration_ms / 1000.0f);
#else
        (void)strength;
#endif
    }

private:
    Impl* impl_;
};
// [video] config block (moved from load_input_config when the Fx concern
// landed). A set GBARECOMP_LCD_SHADER env var wins (recorded at open);
// without an explicit env choice the file decides, so the setting survives
// restarts. The LCD transition goes through the Fx concern's apply().
// ── V2 concern: Fx ──────────────────────────────────────────────
// The V2 `Fx` interface over the backend's Impl state. settings()/apply()
// assemble/write the FxSettings snapshot (the flat generation goes through
// them, per plan §3.5); the per-field transitions are private and carry
// the exact old semantics (clamps, live shader updates, blend-history
// resets). apply() touches only changed fields. Note: frame_blend is a
// bool under the hood — values round-trip as 0.0/1.0.
class RaylibBackend::FxImpl final : public Fx {
public:
    explicit FxImpl(Impl* impl) : impl_(impl) {}

    FxSettings settings() const override {
        FxSettings s;
        s.frame_interp = impl_->frame_interp_enabled;
        s.frame_blend = impl_->frame_blend_enabled ? 1.0f : 0.0f;
        s.lcd = impl_->lcd_shader;
        s.lcd_boost = impl_->lcd_boost;
        s.lcd_dither = impl_->lcd_dither;
        s.lcd_deband = impl_->lcd_deband;
        return s;
    }

    void apply(const FxSettings& s) override {
        // Changed fields only: the private transitions below carry
        // blend-history resets and post-target releases that must fire on
        // real toggles, not on every apply() (a host may sync its config
        // each frame).
        if (s.frame_interp != impl_->frame_interp_enabled)
            set_frame_interp(s.frame_interp);
        const bool blend = s.frame_blend != 0.0f;
        if (blend != impl_->frame_blend_enabled) set_frame_blend(blend);
        if (s.lcd != impl_->lcd_shader) set_lcd_shader(s.lcd);
        if (s.lcd_boost != impl_->lcd_boost) set_lcd_boost(s.lcd_boost);
        if (s.lcd_dither != impl_->lcd_dither) set_lcd_dither(s.lcd_dither);
        if (s.lcd_deband != impl_->lcd_deband) set_lcd_deband(s.lcd_deband);
    }

    void reset_frame_blend() override {
#if defined(GBARECOMP_HAVE_FRAME_BLEND)
        if (impl_->frame_blend) recomp_frame_blend_reset(impl_->frame_blend);
#endif
    }

    // The [video] config block: ini [video] -> the LCD group (Phase 4a moved
    // the flat forwarder's static here — Fx owns its keys, and the V2 path
    // needs a config entry that is not input). Values apply through the
    // private transitions, so only real changes fire; a missing/partial file
    // leaves the built-in defaults and an env pin (GBARECOMP_LCD_SHADER)
    // stays above config, exactly as before.
    void load_config(const char* dir) override {
        if (!dir || impl_->lcd_env_override) return;
        const std::string base = std::string(dir) + "/";
        bool lcd = impl_->lcd_shader;
        float boost = impl_->lcd_boost;
        float dither = impl_->lcd_dither;
        float deband = impl_->lcd_deband;
        ini_scan_section((base + "config.ini").c_str(), "video",
                         [&lcd, &boost, &dither, &deband](const char* key,
                                                          const char* val) {
            apply_lcd_shader_line(key, val, &lcd);
            apply_lcd_boost_line(key, val, &boost);
            apply_lcd_dither_line(key, val, &dither);
            apply_lcd_deband_line(key, val, &deband);
        });
        // Direct field writes (no clamps): the apply_*_line helpers validated
        // the ini values, exactly as the pre-4a static did.
        impl_->lcd_boost = boost;
        impl_->lcd_dither = dither;
        impl_->lcd_deband = deband;
        if (lcd != impl_->lcd_shader) {
            FxSettings s = settings();
            s.lcd = lcd;
            apply(s);
        }
    }

private:
    void set_frame_interp(bool enabled) {
        impl_->frame_interp_enabled = enabled;
#if defined(GBARECOMP_HAVE_FRAME_BLEND)
        // Toggling interp changes the present cadence (fresh vs holding), so stale
        // history would smear — same reset the SDL backend does here.
        if (impl_->frame_blend) recomp_frame_blend_reset(impl_->frame_blend);
#endif
    }

    void set_frame_blend(bool enabled) {
        impl_->frame_blend_enabled = enabled;
#if defined(GBARECOMP_HAVE_FRAME_BLEND)
        if (impl_->frame_blend) recomp_frame_blend_reset(impl_->frame_blend);
#endif
    }

    void set_lcd_shader(bool enabled) {
        // A set GBARECOMP_LCD_SHADER pins on/off above flags, config and menu.
        if (impl_->lcd_env_override) return;
        impl_->lcd_shader = enabled;
#if defined(GBARECOMP_USE_RAYLIB)
        // Enabling while the GL context is live compiles immediately; enabling
        // before open (or via config) compiles at open(). The program is kept
        // after disabling so toggling never hitches on a recompile -- close()
        // is what releases it.
        if (enabled && impl_->open && impl_->api->IsWindowReady()) load_lcd_shader(impl_);
#else
        (void)enabled;
#endif
    }

    void set_lcd_boost(float boost) {
        impl_->lcd_boost = lcd_clamp_boost(boost);
    }

    void set_lcd_dither(float dither) {
        impl_->lcd_dither = lcd_clamp_dither(dither);
        // Strength 0 disables the pass: drop the target so a dead fullscreen
        // buffer never lingers (mirrors the sharp-filter release rule).
        if (impl_->lcd_dither == 0.0f) release_post_targets(impl_);
    }

    void set_lcd_deband(float deband) {
        impl_->lcd_deband = lcd_clamp_deband(deband);
        // As with dither-off: toggling a pass drops the chain targets; the
        // surviving pass (if any) recreates what it needs next present.
        if (impl_->lcd_deband == 0.0f) release_post_targets(impl_);
    }

    Impl* impl_;
};
// ── V2 concern: Overlay ──────────────────────────────────────────
// The V2 `Overlay` interface over the backend's Impl state: the host
// overlay hook, the touch policy (TouchConfig value struct), the touch
// config dir, and pad visibility. The pad-visibility transition stays the
// private static apply_touch_pad_visible (shared with InputImpl::read()'s
// tap handling). The drawing side is RaylibOverlay, constructed by
// VideoImpl::present per frame.
class RaylibBackend::OverlayImpl final : public Overlay {
public:
    explicit OverlayImpl(Impl* impl) : impl_(impl) {}

    void set_host_overlay(void (*overlay)(HostOverlay*)) override {
        impl_->host_overlay = overlay;
    }

    void configure_touch(const TouchConfig& cfg) override {
            // Unpack the value struct into the flat body's parameter names;
            // the body below is the flat body verbatim.
            const bool policy = cfg.policy;
            const std::uint32_t claims = cfg.claims;
            const int pad_default = cfg.pad_default;
            const bool emulate_touch = cfg.emulate_touch;
            const int pad_idle_hide_seconds = cfg.pad_idle_hide_seconds;

        // Same policy as the SDL backend's configure_touch. One deliberate
        // difference: SDL asks the OS to re-report the left mouse button as a
        // touch finger (SDL_HINT_MOUSE_TOUCH_EVENTS); raylib has no such hint,
        // so emulation is handled at poll time in pump() instead (left-drag
        // feeds the same TouchTrack path as a finger).
        impl_->touch_policy = policy;
        impl_->touch_pad_scale = cfg.pad_scale;
        impl_->touch_claims = claims;
        impl_->touch_emulation = emulate_touch;
        impl_->pad_idle_hide_seconds = std::max(0, pad_idle_hide_seconds);
        impl_->pad_last_touch_ms = touch_clock_ms();
        impl_->pad_idle_hidden = false;
#if defined(__ANDROID__)
        if (emulate_touch) impl_->touch_controls = true;
#else
        // A desktop session that asks for touch emulation also gets the pad
        // capability so its toggle and layouts can be validated.
        if (emulate_touch) impl_->touch_controls = true;
#endif
        if (!impl_->pad_choice_saved) {
            const bool platform_default =
#if defined(__ANDROID__)
                true;
#else
                false;
#endif
            impl_->pad_visible = impl_->touch_controls &&
                (pad_default < 0 ? platform_default : pad_default != 0);
        }
        TouchHub& hub = TouchHub::instance();
        hub.set_claims(policy ? claims : 0);
        // A game that owns long-press gets a snappier threshold; the engine's own
        // settings long-press keeps its historical, deliberately slow 650 ms.
        const bool game_long_press = policy && (claims & kTouchClaimLongPress);
        hub.set_timing(350, game_long_press ? 450 : 650);
        std::fprintf(stderr,
                     "raylib_backend: touch policy=%s claims=0x%x pad=%s(%s) "
                     "emulation=%s px_per_mm=%.2f insets=%d,%d,%d,%d\n",
                     policy ? "game" : "none", static_cast<unsigned>(claims),
                     impl_->pad_visible ? "visible" : "hidden",
                     impl_->pad_choice_saved ? "saved" : "default",
                     emulate_touch ? "on" : "off",
                     static_cast<double>(impl_->px_per_mm),
                     impl_->safe_left, impl_->safe_top, impl_->safe_right,
                     impl_->safe_bottom);
    }

    void set_config_dir(const char* dir) override {
        if (dir) impl_->touch_config_dir = dir;
        if (impl_->touch_config_dir.empty()) return;
        const std::string path = impl_->touch_config_dir + "/config.ini";
        ini_scan_section(path.c_str(), "Touch",
                         [this](const char* key, const char* val) {
            if (detail::ci_equal(key, "pad_visible")) {
                impl_->pad_visible =
                    impl_->touch_controls && std::atoi(val) != 0;
                impl_->pad_choice_saved = true;
            } else if (detail::ci_equal(key, "toggle_x")) {
                impl_->toggle_fx =
                    std::clamp(static_cast<float>(std::atof(val)), 0.0f, 1.0f);
                impl_->toggle_pos_saved = true;
            } else if (detail::ci_equal(key, "toggle_y")) {
                impl_->toggle_fy =
                    std::clamp(static_cast<float>(std::atof(val)), 0.0f, 1.0f);
                impl_->toggle_pos_saved = true;
            }
        });
    }

    void set_touch_pad_visible(bool visible) override {
        apply_touch_pad_visible(impl_, visible);
    }

    bool touch_pad_visible() const override { return impl_->pad_visible; }

private:
    Impl* impl_;
};

// ── V2 concern: Menu ────────────────────────────────────────────────────
// The V2 `Menu` interface over the pluggable MenuEngine (menu_engine.h). The
// raylib frame context lives inside VideoImpl::present, so present() runs
// the suppression bookkeeping here (the result is immediate) and stashes the
// frame's model for draw_pending_menu(), which VideoImpl::present calls
// after the pad chrome (topmost layer, never inside a shader/scale pass).
// Rendering itself is the engine's job — menu_draw.h via the legacy engine,
// or raygui widgets via the raygui engine (make_menu_engine()).
class RaylibBackend::MenuImpl final : public Menu {
public:
    explicit MenuImpl(Impl* impl) : impl_(impl) {
        // The Impl OWNS the engine: draw_pending_menu() is a free function
        // and cannot reach MenuImpl's privates, so the engine lives on Impl
        // and both present() and draw_pending_menu() reach it through impl_.
        // (The first cut assigned a MenuImpl-private member instead — the
        // draw path read a never-assigned null and silently rendered
        // nothing: Delete opened the menu, suppressed input, and painted
        // nothing. Found by the TCP KEYINPUT probe: suppression flipped
        // with no visible overlay.)
        impl_->menu_engine = make_menu_engine();
    }

    bool available() const override {
#if defined(GBARECOMP_USE_RAYLIB)
        return impl_->menu_engine && impl_->menu_engine->available();
#else
        return false;  // stub build: state only, no presentation
#endif
    }

    MenuResult present(MenuModel& model, const PointerState& pointer) override {
        MenuResult r;
        (void)pointer;  // no mouse in the menu (menu_engine.h): the pointer
                        // stays out of the contract; the host's tap-to-act
                        // pointer path is gone with the widget renderer.
        impl_->menu_model = nullptr;  // a fresh frame's stash
        impl_->menu_open = model.open;  // §3.4: derives suppress_game_input
#if defined(GBARECOMP_USE_RAYLIB)
        if (impl_->open && impl_->api->IsWindowReady() &&
            impl_->menu_engine && impl_->menu_engine->available()) {
            // Stash for draw_pending_menu() (the frame context lives in
            // VideoImpl::present). The model is host-owned and outlives the
            // frame; the engine renders it read-only.
            impl_->menu_model = &model;
        }
#else
        (void)model;
#endif
        return r;
    }

private:
    Impl* impl_;
};

// Draw the menu frame stashed by MenuImpl::present through the pluggable
// MenuEngine. The stash lives until the NEXT menu().present() (which clears
// and re-sets it): a guest frame can present MORE than once — the
// frame-interpolation ticks subdivide the wait with extra display-paced
// presents — and every one of them must carry the menu, or it flickers at
// the tick rate. The engine contract makes repeats free: draw() is a pure
// renderer (locked widgets, no state), so the nth present of a pump renders
// identically to the first. The pointer is host-owned and refreshed every
// pump; close() clears it.
void RaylibBackend::draw_pending_menu(RaylibBackend::Impl* impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    MenuModel* model = impl->menu_model;
    impl->menu_model = nullptr;
    if (!model) return;
    RaylibOverlay ov(impl->api, impl->api->GetScreenWidth(),
                     impl->api->GetScreenHeight(), impl->last_layout,
                     impl->base_w, impl->base_h, impl->px_per_mm,
                     impl->safe_left, impl->safe_top, impl->safe_right,
                     impl->safe_bottom, touch_clock_ms());
    if (!impl->menu_engine) {
        // Unreachable since the engine is assigned in MenuImpl's
        // constructor — if this ever fires, say so loudly once instead of
        // silently rendering nothing (the 2026-10-16 regression: the menu
        // opened, input suppressed, and no one could tell the draw was
        // skipped). menu_render_contract_tests catches it functionally.
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr,
                         "raylib_backend: menu stashed but no engine — "
                         "MenuImpl wiring bug, draw skipped\n");
        }
        return;
    }
    impl->menu_engine->draw(ov, *model);
#else
    (void)impl;
#endif
}

// raylib's AudioCallback is `void (*)(void *framesOut, unsigned int frames)` --
// it carries NO userdata (open upstream limitation: raysan5/raylib#3911). SDL
// gives us userdata, raylib does not, so the callback can only reach backend
// state through a file-scope pointer. That is safe only because the process owns
// exactly one backend at a time (HostWindow::open refuses to open twice), and
// close() clears it. If that ever stops being true this must become a pull model
// (IsAudioStreamProcessed + UpdateAudioStream from the main loop) instead.
#if defined(GBARECOMP_USE_RAYLIB)
AudioSlot RaylibBackend::s_audio_slot;

// Audio thread. Drain the bridge and hand raylib float samples.
void RaylibBackend::audio_callback(void* framesOut, unsigned int frameCount) {
    auto* impl = static_cast<RaylibBackend::Impl*>(s_audio_slot.owner());
    auto* out = static_cast<float*>(framesOut);
    if (!impl || !out || frameCount == 0) return;

    // Never let a hostile device request size wipe our scratch.
    const unsigned int n =
        std::min<unsigned int>(frameCount,
                               static_cast<unsigned int>(
                                   impl->pull_scratch.size()));
    if (n == 0) return;

    {
        std::lock_guard<std::mutex> lk(impl->bridge_mu);
        rab_pull(&impl->bridge, impl->pull_scratch.data(), static_cast<int>(n));
    }
    for (unsigned int i = 0; i < n; ++i)
        out[i] = s16_to_float(impl->pull_scratch[i]);
    // Short pull (shouldn't happen once primed): fade the remainder rather than
    // leave stale audio, matching the SDL callback's fade-to-silence policy.
    for (unsigned int i = n; i < frameCount; ++i) out[i] = 0.0f;
}
#endif

RaylibBackend::RaylibBackend(RaylibApi* api, Clock* clock) : impl_(new Impl) {
#if defined(GBARECOMP_USE_RAYLIB)
    impl_->api = api ? api : &real_raylib_api();
#else
    (void)api;  // stub builds call no raylib
#endif
    impl_->clock = clock ? clock : &real_host_clock();
    impl_->video = std::make_unique<VideoImpl>(impl_.get());
    impl_->audio = std::make_unique<AudioImpl>(impl_.get());
    impl_->input = std::make_unique<InputImpl>(impl_.get());
    impl_->fx = std::make_unique<FxImpl>(impl_.get());
    impl_->overlay = std::make_unique<OverlayImpl>(impl_.get());
    impl_->menu = std::make_unique<MenuImpl>(impl_.get());
    // Same layout as the SDL backend's kDefaultBinds/kHotkeyDefaults, so the
    // backend is usable before any config file is read.
    raylib_default_bindings(impl_->input_bindings);
}
RaylibBackend::~RaylibBackend() = default;

// ── lifecycle ────────────────────────────────────────────────────────────
Result RaylibBackend::open(const OpenParams& params) {
    if (impl_->open) return {Err::AlreadyOpen, "backend already open"};
    // Headless needs a platform that has no OS window at all (raylib's
    // PLATFORM_MEMORY); a GLFW window cannot satisfy it.
#if !defined(PLATFORM_MEMORY)
    if (params.mode == OpenParams::Mode::Headless)
        return {Err::Unsupported, "headless requires the memory platform"};
#endif
    (void)params.resizable;  // the raylib backend never used it (see below)

    // Shared geometry/filter policy — identical to the SDL backend.
    const ResolvedOpenParams rp = resolve_open_params(
        params.scale, params.base_w, params.base_h, params.linear_filter,
        params.sharp_filter);
    impl_->scale = rp.scale;
    impl_->base_w = rp.base_w;
    impl_->base_h = rp.base_h;
    impl_->linear_filter = rp.linear_filter;
    impl_->sharp_filter = rp.sharp_filter;
    impl_->title = params.title ? params.title : "gbarecomp";
    impl_->resize_driven_view = params.resize_driven_view;
    impl_->open = true;
    // Fresh edge state: stale "was held" flags from a previous open would
    // swallow the first press after reopening.
    impl_->input_edges = InputEdges{};

    // LCD shader startup default: a set GBARECOMP_LCD_SHADER wins over the
    // config file (remembered so load_input_config cannot clobber it); unset
    // resets to off until [video] says otherwise at load_input_config().
    if (const char* lcd_env = std::getenv("GBARECOMP_LCD_SHADER")) {
        impl_->lcd_env_override = lcd_shader_env_set(lcd_env);
        impl_->lcd_shader = lcd_shader_env_on(lcd_env);
    } else {
        impl_->lcd_env_override = false;
        impl_->lcd_shader = false;
    }

    // Screen color grading: same model as the SDL backend (per-game
    // [video].screen token, GBARECOMP_SCREEN override, Raw passthrough).
    // Pure CPU with no windowing dependency, so it is set up outside the
    // raylib-guarded block below.
    impl_->color_lut = std::make_unique<runtime::ColorLut>(
        runtime::resolve_color_settings(params.display));
    if (!impl_->color_lut->is_passthrough())
        impl_->graded_fb.assign(
            static_cast<std::size_t>(rp.base_w) * rp.base_h * 3u, 0);
    else
        impl_->graded_fb.clear();

    // Touch capability: Android always has it; desktop reads the same env
    // override as the SDL backend. Safe insets likewise (Android supplies
    // real cutout insets separately when that port lands).
#if defined(__ANDROID__)
    impl_->touch_controls = true;
#else
    if (const char* touch_env = std::getenv("GBARECOMP_TOUCH_CONTROLS"))
        impl_->touch_controls = *touch_env && *touch_env != '0';
#endif
    impl_->pad_visible = impl_->touch_controls;
    impl_->pad_last_touch_ms = touch_clock_ms();
    if (const char* insets = std::getenv("GBARECOMP_SAFE_INSETS")) {
        int l = 0, t = 0, r = 0, bo = 0;
        if (std::sscanf(insets, "%d,%d,%d,%d", &l, &t, &r, &bo) == 4) {
            impl_->safe_left = std::max(0, l);
            impl_->safe_top = std::max(0, t);
            impl_->safe_right = std::max(0, r);
            impl_->safe_bottom = std::max(0, bo);
        }
    }

#if defined(GBARECOMP_USE_RAYLIB)
#if defined(__ANDROID__)
    // The Java shell writes the display size to files/display.ini before the
    // native thread starts (GbaGameActivity). Open the window at the display
    // size: raylib letterboxes any other request into the surface, and the
    // resize-driven view would then see the requested 3:2 window instead of
    // the display aspect (the fullscreen pre-4f shell matched the surface).
    // Fallback: the scaled request.
    int win_w = rp.base_w * rp.scale;
    int win_h = rp.base_h * rp.scale;
    if (std::FILE* display_file = std::fopen("display.ini", "r")) {
        int dw = 0, dh = 0;
        if (std::fscanf(display_file, "%dx%d", &dw, &dh) == 2 && dw > 0 &&
            dh > 0) {
            win_w = dw;
            win_h = dh;
        }
        std::fclose(display_file);
    }
#else
    const int win_w = rp.base_w * rp.scale;
    const int win_h = rp.base_h * rp.scale;
#endif

#if defined(__ANDROID__)
    // No MSAA on Android: we blit textured quads (no triangle edges to
    // smooth), so 4x MSAA at 1440p+ on a tile-based GPU is pure resolve cost
    // for zero visual gain. Desktop keeps it (negligible there, same picture
    // as before).
    // No VSYNC hint either: the GBA frame pacer owns timing with
    // non-blocking presents — exactly the SDL backend's default
    // (SDL_RENDERER_PRESENTVSYNC only with GBARECOMP_VSYNC=1). A vsync-blocked
    // present stacks up to one display period onto every pacer wait (8.3ms
    // at 120Hz, beating against the 16.74ms guest period: measured 52-57/s
    // vs the 59.7 target, 2026-10-14). Tearing exposure matches SDL default.
    impl_->api->SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#else
    impl_->api->SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT |
                   FLAG_MSAA_4X_HINT);
#endif
    impl_->api->InitWindow(win_w, win_h, impl_->title.c_str());
    if (!impl_->api->IsWindowReady()) {
        std::fprintf(stderr,
                     "raylib_backend: InitWindow(%dx%d) failed\n",
                     win_w, win_h);
        impl_->open = false;
        return {Err::Window, "window init failed"};
    }
    // NOTE: do NOT hide the window here. An earlier version did
    // `if (!rp.linear_filter) SetWindowState(FLAG_WINDOW_HIDDEN)`, which
    // combined with linear_filter defaulting to FALSE meant the default window
    // was invisible and never shown again -- nothing else cleared the flag.
    // Found by the raylib_present integration test, which asserts visibility.

    // raylib 5+ removed CreateTexture(); a streaming texture is created from an
    // Image we own, then fed with UpdateTexture() every present.
    impl_->surface_pixels.assign(
        static_cast<std::size_t>(rp.base_w) * rp.base_h * 3u, 0);
    impl_->surface = Image{};
    impl_->surface.width = rp.base_w;
    impl_->surface.height = rp.base_h;
    impl_->surface.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    impl_->surface.mipmaps = 1;
    impl_->surface.data = impl_->surface_pixels.data();
    impl_->texture = impl_->api->LoadTextureFromImage(impl_->surface);
    if (impl_->texture.id == 0) {
        std::fprintf(stderr, "raylib_backend: LoadTextureFromImage failed\n");
        impl_->api->CloseWindow();
        impl_->open = false;
        return {Err::Renderer, "streaming texture failed"};
    }
    impl_->texture_ready = true;
    impl_->api->SetTextureFilter(impl_->texture, rp.linear_filter
                                        ? TEXTURE_FILTER_BILINEAR
                                        : TEXTURE_FILTER_POINT);
#if defined(__ANDROID__)
    // True pacer-only pacing: raylib's Android layer never sets a swap
    // interval (its eglSwapInterval call is commented out upstream), so EGL
    // defaults to interval 1 and every present blocks to the next 120Hz
    // vsync — stacking up to 8.3ms onto each 16.74ms guest frame and beating
    // audibly (residual hitching after the MSAA fix, 2026-10-14). Interval 0
    // submits without blocking; SurfaceFlinger still composites at its own
    // vsync, so the guest clock (and the audio fed from it) stays exact and
    // the worst case degrades to ±1-vsync display judder. Some compositors
    // ignore 0 — then behavior is unchanged (still interval 1), never worse.
    // Runs on the glue thread with the context current (open() runs there).
    {
        EGLDisplay dpy = eglGetCurrentDisplay();
        if (dpy != EGL_NO_DISPLAY && eglSwapInterval(dpy, 0) == EGL_TRUE)
            std::fprintf(stderr, "raylib_backend: swap interval 0 (unblocked)\n");
        else
            std::fprintf(stderr, "raylib_backend: swap interval 0 refused; "
                                 "vsync-blocked presents remain\n");
    }
#endif
#if !defined(__ANDROID__)
    // Desktop keeps the 60 fps limiter (harmless there: 60Hz vsync aligns
    // with the GBA pacer's 59.7Hz). Android drops it: the GBA frame pacer
    // owns timing (as on SDL, which has no limiter) — a second limiter
    // stacks with the pacer's wait AND the vsync block, running the guest
    // clock slow with audio underrun on 120Hz panels (2026-10-14 intro lag).
    // Vsync stays everywhere (tearing guard + caps uncapped presents).
    impl_->api->SetTargetFPS(60);
#endif

    // The GL context exists from here on, so an env-enabled LCD shader can
    // compile now (needs IsWindowReady + GPU). A config-enabled one compiles
    // lazily at set_lcd_shader() when load_input_config() runs.
    if (impl_->lcd_shader) load_lcd_shader(impl_.get());

    // Display interpolation pacing: we request vsync at open, so assume it
    // until the periodic re-probe says otherwise (same leap the SDL backend
    // takes from renderer flags; see refresh below).
    impl_->interp_vsync = true;
    {
        const int hz = impl_->api->GetMonitorRefreshRate(impl_->api->GetCurrentMonitor());
        impl_->vsync_probed = hz > 0 && hz <= kMaxSaneHz;
        impl_->interp_period_ns =
            impl_->vsync_probed
                ? 1000000000ull / static_cast<std::uint64_t>(hz)
                : 16666666u;
        impl_->interp_tick_ns = 0;
    }

    // ---- audio ----
    impl_->audio_claimed = false;  // claims never survive a close/open cycle
    // RAB resamples the GBA's standardized 65536 Hz mono feed to the rate we
    // ask raylib for. We choose that rate, so no device-rate query is needed.
    if (impl_->api->IsAudioDeviceReady()) {
        impl_->audio_dev_open = false;   // someone else owns it; leave it alone
    } else {
        impl_->api->InitAudioDevice();               // returns void; check state after
        impl_->audio_dev_open = impl_->api->IsAudioDeviceReady();
    }
    if (impl_->audio_dev_open) {
        constexpr int kStreamRate = 48000;
        constexpr int kStreamBufferFrames = 4096;
        impl_->api->SetAudioStreamBufferSizeDefault(kStreamBufferFrames);
        impl_->stream = impl_->api->LoadAudioStream(kStreamRate, 32, 1);  // 32-bit float
        if (impl_->api->IsAudioStreamValid(impl_->stream)) {
            rab_config cfg;
            rab_config_defaults(&cfg);
            cfg.channels = 1;
            cfg.source_rate = 65536.0;              // GBA mixer rate
            cfg.host_rate = static_cast<double>(kStreamRate);
            cfg.target_ms = 60.0;                    // steady cushion
            cfg.preroll_ms = 250.0;                  // hide cold-start hitch
            if (rab_init(&impl_->bridge, &cfg) == 0) {
                impl_->bridge_ready = true;
                impl_->pull_scratch.assign(kStreamBufferFrames, 0);
                // Single-owner slot: refuses rather than steals, so a second
                // open backend cannot cross-talk through one ring buffer.
                if (!s_audio_slot.acquire(impl_.get())) {
                    std::fprintf(stderr,
                                 "raylib_backend: audio slot held by another "
                                 "backend; opening without audio\n");
                    impl_->api->StopAudioStream(impl_->stream);
                    impl_->api->UnloadAudioStream(impl_->stream);
                    rab_free(&impl_->bridge);
                    impl_->bridge_ready = false;
                } else {
                    impl_->api->SetAudioStreamCallback(impl_->stream,
                                           &RaylibBackend::audio_callback);
                    impl_->api->PlayAudioStream(impl_->stream);
                    impl_->audio_ready = true;
                }
            } else {
                impl_->api->UnloadAudioStream(impl_->stream);
            }
        }
    }

    // OpenParams::Mode maps onto the video fullscreen modes (raylib has no
    // exclusive-fullscreen equivalent; see Video::set_fullscreen). Applied
    // after init so the window state exists; the stub build records state only.
    if (params.mode == OpenParams::Mode::Borderless)
        impl_->video->set_fullscreen(1);
    else if (params.mode == OpenParams::Mode::Fullscreen)
        impl_->video->set_fullscreen(2);

    // Frame blending is CPU-side on cached scratch (see present()): the shared
    // primitive needs ordinary cached ARGB8888, never a mapped texture, and
    // grading above is the portable screen work that feeds it.
#if defined(GBARECOMP_HAVE_FRAME_BLEND)
    // Blend history starts invalid: boot presents the first frame as drawn.
    // A NULL handle (allocation failure) is legal everywhere below and
    // simply presents unblended frames — same contract as the SDL backend.
    impl_->frame_blend = recomp_frame_blend_create();
#endif
    return {};
#else
    // Stub build: state only, no window. Documented in raylib_backend.h.
    return {};
#endif
}

Caps RaylibBackend::caps() const {
    Caps c;
    // Static capability by build/platform, then live state. Valid after open
    // (§3.3); safe to query before — it reports the build's ability.
#if defined(GBARECOMP_USE_RAYLIB)
#if defined(PLATFORM_MEMORY)
    // raylib's memory platform: no OS window, no input system — but
    // present/readback work with no display server.
    c.headless = true;
#else
    c.window = true;
    c.input = true;
    c.touch = true;
    c.multi_monitor = impl_->api->GetMonitorCount() > 1;
#endif
#if !defined(GRAPHICS_API_OPENGL_SOFTWARE)
    // The FX pipeline actually renders; rlsw compiles the shader entry
    // points out (plan §4.1).
    c.shaders = true;
#endif
#endif  // GBARECOMP_USE_RAYLIB
    c.audio = impl_->audio_ready;  // the device came up at open()
    c.vsync_report = impl_->vsync_probed;
    // raylib has no rumble-capability query; the retired flat predicate also
    // answered false (the "assume true when a pad is present" intent never
    // landed). input().rumble() no-ops safely anyway.
    c.rumble = false;
    // No cross-platform system vibrator in raylib core (haptics ride the
    // gamepad motors); the Android port is where a real vibrator lands.
    c.haptics = false;
#if defined(GBARECOMP_USE_RAYLIB)
    c.menu = true;  // the shared menu_draw.h presentation exists (Phase 3b)
#endif
#if !defined(__ANDROID__)
    c.reopen = true;  // desktop can close -> open; Android cannot (plan §3.2)
#endif
    return c;
}

// Desktop has no lifecycle to suspend: no background state, no device
// teardown, nothing to rebind — a no-op success keeps the host's
// suspend/resume flow unconditional. The Android port (Phase 4f) is where
// these grow real work (raylib Android cannot CloseWindow; it pauses and
// rebinds).
bool RaylibBackend::suspend() { return true; }
bool RaylibBackend::resume() { return true; }

void RaylibBackend::close() {
#if defined(GBARECOMP_HAVE_FRAME_BLEND)
    if (impl_->frame_blend) recomp_frame_blend_destroy(impl_->frame_blend);
    impl_->frame_blend = nullptr;
#endif
#if defined(GBARECOMP_USE_RAYLIB)
    // Stop the callback before anything it touches goes away.
    s_audio_slot.release(impl_.get());
    impl_->audio_claimed = false;
    unload_lcd_shader(impl_.get());
    unload_dither_shader(impl_.get());
    unload_deband_shader(impl_.get());
    if (impl_->audio_ready) {
        impl_->api->StopAudioStream(impl_->stream);
        impl_->api->UnloadAudioStream(impl_->stream);
        impl_->audio_ready = false;
    }
    if (impl_->bridge_ready) {
        rab_free(&impl_->bridge);
        impl_->bridge_ready = false;
    }
    if (impl_->audio_dev_open) impl_->api->CloseAudioDevice();

    if (impl_->open) {
        if (impl_->texture_ready) {
            impl_->api->UnloadTexture(impl_->texture);
            impl_->texture_ready = false;
        }
        release_sharp_target(impl_.get());
        release_post_targets(impl_.get());
        // surface_pixels must outlive Image::data, so clear it last.
        impl_->surface.data = nullptr;
        impl_->surface_pixels.clear();
        impl_->surface_pixels.shrink_to_fit();
        impl_->api->CloseWindow();
    }
#endif
    impl_->open = false;
    impl_->input_edges = InputEdges{};
    // Derived menu state dies with the window (same rule as the edges), so a
    // reopen never inherits suppression before its first present. One reset
    // point is enough: close() is the only path into the closed state, and a
    // closed raylib read() reports an empty snapshot (the state-only stub
    // still reports the flag, but has no input device to leak into).
    impl_->menu_open = false;
    impl_->menu_model = nullptr;  // never draw a stale model after close
}
bool RaylibBackend::is_open() const { return impl_->open; }

Video& RaylibBackend::video() { return *impl_->video; }

Audio& RaylibBackend::audio() { return *impl_->audio; }

Input& RaylibBackend::input() { return *impl_->input; }

Fx& RaylibBackend::fx() { return *impl_->fx; }

Overlay& RaylibBackend::overlay() { return *impl_->overlay; }

Menu& RaylibBackend::menu() { return *impl_->menu; }

// ── touch / virtual pad ──────────────────────────────────────────────────
// Pad geometry inputs from backend state. Pure: the same values pump()
// builds, so layout and hit-testing can never disagree with what is drawn.
PadLayoutInputs RaylibBackend::pad_layout_inputs(
    const RaylibBackend::Impl* impl, int dw, int dh) {
    PadLayoutInputs in;
    in.drawable_w = static_cast<float>(dw);
    in.drawable_h = static_cast<float>(dh);
    in.px_per_mm = impl->px_per_mm;
    in.inset_left = impl->safe_left;
    in.inset_top = impl->safe_top;
    in.inset_right = impl->safe_right;
    in.inset_bottom = impl->safe_bottom;
    in.game = impl->last_layout;
    in.toggle_fx = impl->toggle_fx;
    in.toggle_fy = impl->toggle_fy;
    in.toggle_pos_saved = impl->toggle_pos_saved;
    return in;
}

// Persist the player's pad choice + toggle position next to the config dir.
// Same shape as the retired SDL backend's save_touch_config: the old [Touch]
// section is
// dropped and rewritten, so repeated saves never accumulate duplicates, and
// only actually-saved choices are written.
void RaylibBackend::save_touch_config(const RaylibBackend::Impl* impl) {
    if (impl->touch_config_dir.empty()) return;
    const std::string path = impl->touch_config_dir + "/config.ini";
    std::vector<std::string> kept;
    {
        std::ifstream f(path);
        std::string line;
        bool in_section = false;
        while (std::getline(f, line)) {
            while (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t s = line.find_first_not_of(" \t");
            const bool header = s != std::string::npos && line[s] == '[';
            if (header) in_section = line.compare(s, 7, "[Touch]") == 0;
            if (in_section) continue;
            kept.push_back(line);
        }
    }
    while (!kept.empty() && kept.back().empty()) kept.pop_back();
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    for (const auto& l : kept) f << l << "\n";
    if (!kept.empty()) f << "\n";
    f << "[Touch]\n";
    if (impl->pad_choice_saved)
        f << "pad_visible = " << (impl->pad_visible ? 1 : 0) << "\n";
    if (impl->toggle_pos_saved) {
        f << "toggle_x = " << impl->toggle_fx << "\n";
        f << "toggle_y = " << impl->toggle_fy << "\n";
    }
}

void RaylibBackend::apply_touch_pad_visible(RaylibBackend::Impl* impl, bool visible) {
    visible = visible && impl->touch_controls;
    if (impl->pad_visible == visible && impl->pad_choice_saved) return;
    impl->pad_visible = visible;
    impl->pad_choice_saved = true;
    // Showing the pad restarts its idle clock; it can hide again after the
    // configured quiet period.
    impl->pad_last_touch_ms = touch_clock_ms();
    impl->pad_idle_hidden = false;
    // Any finger currently on a pad button is released with the pad.
    for (auto& [id, t] : impl->touches)
        if (t.active && t.pad) t.buttons = 0;
    save_touch_config(impl);
    std::fprintf(stderr, "raylib_backend: touch pad %s\n",
                 visible ? "shown" : "hidden");
}

RaylibBackend::Events RaylibBackend::pump() {
    // V2 composition (plan §3.2): read() fills the levels; translate_input
    // is the only edge producer. The touch fast-forward pill is a
    // raylib-only level overlay read() records, ORed in here exactly as
    // the pre-conversion pump did (hold-to-run like Tab).
    InputSnapshot s;
    impl_->input->read(s);
    Events ev = translate_input(s, impl_->input->bindings(),
                                impl_->input_edges);
    if (impl_->touch_ff_held) ev.fast_forward = true;
    return ev;
}

bool RaylibBackend::wait_for_foreground() {
    // The SDL backend blocks on SDL_WaitEvent for APP_DIDENTERFOREGROUND;
    // raylib has no blocking foreground event and no background state on
    // desktop (there is no lifecycle to suspend for), so there is nothing to
    // wait for. Return true when the window is alive, false when it is gone
    // (the caller treats false as "terminated while suspended"). The mobile
    // port is where this grows a real wait.
    if (!impl_->open) return false;
#if defined(GBARECOMP_USE_RAYLIB)
    if (!impl_->api->IsWindowReady()) return false;
#endif
    std::fprintf(stderr, "raylib_backend: already in foreground\n");
    return true;
}

}  // namespace gbarecomp
