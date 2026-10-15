// host_window.h — minimal host window + input surface.
//
// The implementation delegates to the backend seam (window_backend.h); raylib
// is the only backend (Phase 4b deleted the SDL backend). Builds with raylib
// OFF compile the same symbols as documented no-op stubs so headless builds
// (CI, BIOS smoke without --window) still link.
//
// The window owns a logical-size streaming texture matching the active GBA
// framebuffer pixel format (RGB888). Expanded views opt into a resizable,
// aspect-correct viewport. pump() drains the backend event queue, returns a
// quit flag and a packed GBA KEYINPUT value (active-low, 1 = released).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "window_backend.h"

struct RecompRuntimeUi;

namespace gbarecomp {

class HostOverlay;

class HostWindow {
public:
    HostWindow();
    // Backend selection seam. The default builds the compile-selected backend
    // (host_make_backend()); a factory overrides that per window — tests
    // today, a future runtime selector (the seam already supports it; see
    // host_backend_factory.h). A factory returning nullptr behaves like "no
    // backend": open() fails and every method keeps its closed-window
    // fallback.
    using BackendFactory = WindowBackend* (*)();
    explicit HostWindow(BackendFactory factory);
    ~HostWindow();

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    // True if this build was compiled against a real windowing
    // backend. When false, open() always fails.
    static bool is_available();

    // Open a window. `scale` is the integer scale factor applied to
    // the logical surface, whose size is `base_w` x `base_h` (240x160 for the
    // faithful view, wider when view-area expansion is active). Returns false on
    // failure (also when is_available() is false).
    // `screen` is the per-game color model from [video].screen in game.toml
    // (raw|unlit|frontlit|backlit|classic), or nullptr for none. The
    // GBARECOMP_SCREEN env var, when set, overrides it.
    // `linear_filter` selects ordinary linear scaling. `sharp_filter` uses a
    // GPU integer prescale followed by a small fractional linear pass and
    // takes precedence when both are requested.
    bool open(int scale = 3, int base_w = 240, int base_h = 160,
              const char* title = "gbarecomp", const char* screen = nullptr,
              bool linear_filter = false, bool sharp_filter = false,
              bool resize_driven_view = false,
              bool freely_resizable_window = false);
    void close();
    void set_title(const char* title);
    bool is_open() const { return open_; }

    // Resize the logical streaming surface without changing the host window.
    // Used only by the explicit resize-driven view policy; fixed-width callers
    // never invoke it. drawable_size() reports the live client-window extent
    // (and therefore follows drag-resize and borderless desktop fullscreen).
    bool set_surface_size(int base_w, int base_h);
    bool drawable_size(int* width, int* height) const;

    // Load player keybinds + system hotkeys from `dir` (the exe directory):
    //   * keybinds.ini — recomp-ui's generic keybinds format ([player1],
    //     SDL scancode names). Absent file => the built-in defaults below,
    //     which MATCH recomp-ui's defaults so the launcher rebind page and
    //     the game always agree: A=X B=Z L=C R=V Start=Return Select=RShift
    //     + arrow keys.
    //   * config.ini [KeyMap] — hotkey bindings (SDL keycode names with
    //     Ctrl+/Alt+/Shift+ prefixes): Fullscreen, Pause, Turbo,
    //     WindowBigger, WindowSmaller, VolumeUp, VolumeDown, DisplayPerf.
    // Never called => built-in defaults for both. Safe to call when the
    // files don't exist.
    void load_input_config(const char* dir,
                           bool assist_tools_default = true,
                           int fast_forward_multiplier_default = 4);

    // Touch pad size multiplier (1.0 = authored; applied by the next
    // configure_touch, which is where the touch config reaches the backend).
    // No-op when the window isn't open or this build has no backend.
    void set_touch_pad_scale(float scale);

    // Live window/audio controls (hotkey + launcher-driven). All no-ops when
    // the window isn't open or this build has no backend.
    // Tri-state fullscreen mode: 0 windowed, 1 borderless (desktop), 2
    // exclusive.
    // Out-of-range values clamp to 0..2. fullscreen() reports the current mode.
    void set_fullscreen(int mode);
    int  fullscreen() const;
    void adjust_scale(int delta);       // integer window scale, clamped 1..8
    void set_volume(int pct);           // 0..100, applied to pushed samples
    int  volume() const;
    int  window_scale() const;
    void set_linear_filter(bool enabled);
    bool linear_filter() const;
    // Presentation frame blending (issue #9 strategy #1): average each
    // presented frame with the previous one (recomp-ui's RecompFrameBlend —
    // the GBA LCD's own slow response, which is what makes 30 Hz palette
    // fades read as smooth). Driven by the runtime menu's Graphics/LCD
    // ghosting row (and --frame-blend); default off so faithful runs stay
    // byte-exact. reset_frame_blend forgets history: call it wherever the
    // incoming frame is not what the player was just looking at
    // (savestate load; boot/reset start clean by construction).
    void set_frame_blend(bool enabled);
    bool frame_blend() const;
    void reset_frame_blend();
    // Decoupled display clock (issue #9 3a): subdivide the pacer wait into
    // display-paced holding presents (runtime menu's Graphics/Frame
    // interpolation row, and --frame-interp). No vsync requirement: ticks
    // sleep to display boundaries read from the current display mode
    // (re-queried periodically, so adaptive-refresh panels just work);
     // where presents already block on the display the tick skips its own
     // sleep; default off.
    void set_frame_interp(bool enabled);
    bool frame_interp() const;
    // LCD shader tunables (raylib-only capability; the stub reports defaults).
    // Driven by --lcd-shader/--lcd-boost/--lcd-dither (and later the settings
    // menu); default off/1.0/1.0. Closed-window guards return the defaults.
    void set_lcd_shader(bool enabled);
    bool lcd_shader() const;
    void set_lcd_boost(float boost);
    float lcd_boost() const;
    void set_lcd_dither(float dither);
    float lcd_dither() const;
    void set_lcd_deband(float deband);
    float lcd_deband() const;
    // One display-clock tick for the pacer wait: sleep until the next
    // display boundary (or deadline_ns, whichever comes first — a slower
    // panel must never hold the guest wait past its deadline), then
    // re-present rgb888 against unchanged history. No-op builds route here
    // in headless stubs.
    void interp_display_tick(const uint8_t* rgb888, uint64_t deadline_ns);
    void set_audio_enabled(bool enabled);
    bool audio_enabled() const;
    void set_resize_driven_view(bool enabled);
#if defined(GBARECOMP_RUNTIME_UI)
    // Compile-compatibility shim (no-op): the seam stopped naming recomp-ui
    // types in Phase 4b — the generic menu is the in-game surface, and
    // recomp-ui's runtime UI is not ported to the raylib path.
    void set_runtime_ui(RecompRuntimeUi* ui);
#endif
    // ---- touch (touch_input.h) ---------------------------------------------
    // `policy` = the game consumes touch through RunOptions::input_frame;
    // non-pad touches are then routed to the TouchHub instead of only opening
    // settings. `claims` are the game's TouchClaim bits. `pad_default` is the
    // virtual-pad visibility when config.ini holds no saved choice (-1 =
    // platform default). `emulate_touch` turns desktop mouse input into touch
    // (left = finger, right = two-finger tap, middle = three-finger tap,
    // Backspace = platform Back) for development and validation.
    // `pad_idle_hide_seconds` hides the pad overlay (and its toggle) after
    // that many seconds of screen-wide touch inactivity (0 = never); any
    // touch reveals it and then acts normally.
    void configure_touch(bool policy, std::uint32_t claims, int pad_default,
                         bool emulate_touch, int pad_idle_hide_seconds = 0);
    // Loads/persists the player's pad choice in <dir>/config.ini [Touch].
    void set_touch_config_dir(const char* dir);
    bool touch_pad_visible() const;
    void set_touch_pad_visible(bool visible);
    // Logical view margins inside the surface (for touch/overlay mapping).
    void set_view_margins(std::uint32_t left, std::uint32_t right,
                          std::uint32_t top, std::uint32_t bottom);
    // Game-owned host overlay, drawn each present (null = none).
    void set_host_overlay(void (*overlay)(HostOverlay*));
    // Physical density of the window's display (drawable px per mm).
    float px_per_mm() const;
    // Portrait presentations may anchor the image to the top edge so host
    // chrome below it gets the remaining space (per-present game request).
    void set_presentation_anchor_top(bool anchor_top);
    // Orientation policy for rotating platforms: 0 landscape, 1 portrait,
    // 2 any. Must be set BEFORE open(); ignored on desktop.
    void set_orientation_policy(int policy) { orientation_policy_ = policy; }

    // Mobile lifecycle. pump() reports entering the background once; the
    // runtime then persists state and calls wait_for_foreground(), which
    // blocks (audio paused) until the app returns (true) or quits (false).
    bool wait_for_foreground();

    // Short vibration where the platform supports it (no-op otherwise).
    void haptic_pulse(int duration_ms, float strength);
    bool gamepad_supports_rumble() const;
    void gamepad_rumble(float low_frequency, float high_frequency,
                        std::uint32_t duration_ms);

    void set_fps_readout(bool on);      // presents-per-second in the title bar
    bool fps_readout() const;
    bool assist_tools_enabled() const;
    int  fast_forward_multiplier() const;

    // Upload one base_w x base_h RGB888 frame (the dimensions passed to open())
    // and present. `fresh` tells whether this is a newly rendered guest
    // frame (true) or a repeat of the last one (false, e.g. the paused
    // re-present loop or issue #9 3a display-tick holding presents):
    // repeats blend against UNCHANGED history (avg(f,f) = f — a clean
    // repeat, no smear), fresh frames blend and advance it.
    void present(const uint8_t* rgb888, bool fresh = true);

    // Copies the most recently uploaded frame (post color-grade AND post
    // blend — the exact bytes handed to the display) for the `presented`
    // TCP command. Returns false when nothing has been presented yet.
    // Recorded unconditionally (blend on or off); the copy costs one frame
    // memcpy per present.
    bool copy_presented_frame(std::vector<uint8_t>& out, int& w, int& h) const;

    // Push `count` int16_t mono samples (32.768 kHz) into the audio
    // output queue. Backend converts to the host device's format.
    // No-op if audio init failed or this build has no audio device.
    void push_audio_samples(const int16_t* samples, std::size_t count);

    // Service the native window-system queue without consuming input events.
    // Long guest frames use this to remain responsive between presentations.
    void service_events();

    // Defined by the backend seam; HostWindow::Events is an alias so a
    // backend can produce it directly (see pump()).
    using Events = WindowBackend::Events;
    Events pump();

    // The active backend (the facade). Historical name: it dates from the
    // two-generation transition; kept so app call sites stay untouched. Null
    // when the window is closed or the build has no backend (raylib OFF).
    WindowBackend* backend_v2() const;

private:
    bool open_ = false;
    // The backend. Typed at the seam, not as a concrete implementation, so
    // selecting a backend is a factory decision only.
    WindowBackend* impl_ = nullptr;
    // Backend selection: null = host_make_backend() (the compile-time choice).
    BackendFactory factory_ = nullptr;
    // Pad scale applied by configure_touch (see set_touch_pad_scale).
    float touch_pad_scale_ = 1.0f;
    int orientation_policy_ = 0;
};

// Engine-wide haptic hook for game input policies (touch_input.h users).
// Installed by the open HostWindow; a no-op when no window/vibrator exists.
void host_haptic_pulse(int duration_ms, float strength);

// Stable public API for RECOMPILED GAME CODE to add effects the base GBA
// hardware could never produce. This is the surface third-party recompilers
// target (see the enhancement-platform goal in RAYLIB_PLAN.md); backends stay
// internal behind WindowBackend.
//
// Dual motors so profiles can express "tap" vs "impact". Silently no-ops when no
// pad is attached or the pad has no rumble, so callers need no capability probe.
void host_gamepad_rumble(float low_frequency, float high_frequency,
                         std::uint32_t duration_ms);
bool host_gamepad_supports_rumble();

// Ask the open window to show the runtime settings menu at its next pump
// (thread-safe). Lets a policy that claims Back/long-press still offer the
// menu from a context of its choosing, e.g. Back on the free overworld.
void host_request_settings_menu();

}  // namespace gbarecomp
