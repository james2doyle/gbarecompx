#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "menu_model.h"

// Backend seam for HostWindow: the facade (WindowBackend) plus the five
// concern interfaces (Video/Audio/Input/Fx/Overlay) and the generic Menu
// presentation. The raylib backend is the only implementation (Phase 4b
// deleted the flat generation and the SDL backend); this header stays free of
// SDL/raylib headers so it compiles on any backend target.
namespace gbarecomp {
struct HostOverlay;

// Full host event contract. Historically declared as HostWindow::Events;
// it lives in the seam so a backend can fill it directly without depending
// on HostWindow. HostWindow::Events is an alias of this type, and the flat
// WindowBackend below keeps a member alias for the transition (Phase 1).
struct Events {
    bool     quit = false;
    // GBA KEYINPUT layout. Active-low: 1 = released, 0 = pressed.
    // Bits: 0=A 1=B 2=Sel 3=Sta 4=Right 5=Left 6=Up 7=Down 8=R 9=L.
    uint16_t keyinput = 0x03FF;
    // The same levels BEFORE menu suppression (plan §3.4): `keyinput` is
    // masked to all-released while any suppression source is active (the
    // generic menu — the backend derives its flag from the presented model —
    // or the legacy runtime UI), but the host menu must still navigate from
    // the raw levels. V2 backends fill this; the flat SDL generation predates
    // the field and leaves the default (it has no generic-menu consumer).
    uint16_t menu_keyinput = 0x03FF;
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

// ═══════════════════════════════════════════════════════════════════════════
// The seam — WINDOW_BACKEND_V2_PLAN §3.2/§3.3.
//
// Single generation since Phase 4b (the flat class and the SDL backend were
// deleted; the facade took the plain `WindowBackend` name). Names, defaults
// and contracts are normative in the plan — keep them in sync.
//
// Dependency-free: no raylib, no SDL, no GL. Interfaces carry no data members
// (§3.1); state lives in the implementation behind them.
// ═══════════════════════════════════════════════════════════════════════════

struct InputSnapshot;  // canonical types live in input_translate.h
struct InputBindings;

// ── open / capabilities / errors ──────────────────────────────────────────
struct OpenParams {
    int scale = 3;
    int base_w = 240, base_h = 160;
    const char* title = "gbarecomp";
    const char* display = nullptr;            // monitor selector; backend parses
    enum class Mode { Windowed, Borderless, Fullscreen, Headless } mode = Mode::Windowed;
    bool linear_filter = false;
    bool sharp_filter = false;
    bool resizable = false;
    bool resize_driven_view = false;
};

struct Caps {
    bool window = false;        // a real OS window can be created
    bool headless = false;      // present/readback work with no display server
    bool input = false;         // Input::read is meaningful
    bool audio = false;         // an audio device can be claimed
    bool shaders = false;       // the FX pipeline actually renders (false under rlsw)
    bool rumble = false;        // pad motors deliver
    bool haptics = false;       // system vibrator deliver
    bool touch = false;         // touch/trackpad handling
    bool menu = false;          // an in-game menu presentation exists (§3.4)
    bool multi_monitor = false;
    bool reopen = false;        // close -> open again supported (false on Android)
    bool vsync_report = false;  // Video::refresh_hz() is meaningful
};

enum class Err { None, Window, Renderer, Shader, AudioDevice, Unsupported,
                 AlreadyOpen, NotOpen };
struct Result {
    Err code = Err::None;
    const char* detail = nullptr;   // static-lifetime string or nullptr
    bool ok() const { return code == Err::None; }
};

// ── frame effects ─────────────────────────────────────────────────────────
struct FxSettings {
    // core: every backend must implement (CPU-side in the source of truth)
    bool  frame_interp = false;
    float frame_blend = 0.0f;             // 0..1
    // enhanced: gated by Caps::shaders. Values are stored and reported even
    // when unsupported; rendering ignores them.
    bool  lcd = false;
    float lcd_boost = 1.0f, lcd_dither = 1.0f, lcd_deband = 0.0f;
};

struct TouchConfig {
    bool policy = false;
    std::uint32_t claims = 0;
    int pad_default = 0;
    bool emulate_touch = false;
    int pad_idle_hide_seconds = 0;
};

// ── concern interfaces ────────────────────────────────────────────────────
class Video {
public:
    virtual ~Video() = default;
    virtual void present(const std::uint8_t* rgb888, bool fresh) = 0;
    // Kept in V2: hoisting pacing host-side is out of scope for this refactor.
    virtual void interp_display_tick(const std::uint8_t* rgb888,
                                     std::uint64_t deadline_ns) = 0;
    // Fidelity: graded CPU bytes exactly as uploaded — pre-GPU-effects.
    // verify/attract hashes depend on this; the shader stack must not change it.
    virtual bool readback(std::vector<std::uint8_t>& out, int& w, int& h) const = 0;
    virtual float refresh_hz() const = 0;
    virtual bool drawable_size(int* w, int* h) const = 0;
    virtual bool set_surface_size(int base_w, int base_h) = 0;
    virtual void set_title(const char* title) = 0;
    virtual int  scale() const = 0;
    virtual void adjust_scale(int delta) = 0;
    virtual void set_linear_filter(bool enabled) = 0;
    virtual bool linear_filter() const = 0;
    virtual void set_resize_driven_view(bool enabled) = 0;
    virtual void set_view_margins(std::uint32_t l, std::uint32_t r,
                                  std::uint32_t t, std::uint32_t b) = 0;
    virtual void set_fullscreen(int mode) = 0;
    virtual int  fullscreen() const = 0;
    virtual float px_per_mm() const = 0;
    virtual void set_presentation_anchor_top(bool anchor_top) = 0;
    virtual void set_fps_readout(bool on) = 0;
    virtual bool fps_readout() const = 0;
};

class Audio {
public:
    virtual ~Audio() = default;
    virtual Result claim() = 0;            // single-owner; refuses a second claimant
    virtual void release() = 0;            // clears only if the caller owns it
    // ONLY cross-thread call in the entire seam (emulation thread -> ring).
    virtual void push(const std::int16_t* samples, std::size_t count) = 0;
    virtual void set_volume(int pct) = 0;
    virtual int  volume() const = 0;
    virtual void set_enabled(bool enabled) = 0;
    virtual bool enabled() const = 0;
};

class Input {
public:
    virtual ~Input() = default;
    virtual void read(InputSnapshot& out) = 0;   // main thread; LEVELS only
    virtual void load_config(const char* dir, bool assist_tools_default,
                             int fast_forward_multiplier_default) = 0;
    virtual const InputBindings& bindings() const = 0;
    virtual bool assist_tools_enabled() const = 0;
    virtual int  fast_forward_multiplier() const = 0;
    virtual bool gamepad_connected() const = 0;  // standard-mapped pad present
    virtual void rumble(float low, float high, std::uint32_t duration_ms) = 0;
    virtual void haptic_pulse(int duration_ms, float strength) = 0;
};

class Fx {
public:
    virtual ~Fx() = default;
    virtual FxSettings settings() const = 0;
    virtual void apply(const FxSettings& s) = 0;
    virtual void reset_frame_blend() = 0;
    // Per-exe config load: the [video] block in config.ini (the LCD group).
    // This is the Fx half of HostWindow::load_input_config — the seam's two
    // config entries are Input::load_config and this. Loaded values transition
    // through the same change-detected apply() as the setters; a missing or
    // partial file leaves the built-in defaults.
    virtual void load_config(const char* dir) = 0;
};

class Overlay {
public:
    virtual ~Overlay() = default;
    virtual void set_host_overlay(void (*overlay)(HostOverlay*)) = 0;
    virtual void configure_touch(const TouchConfig& cfg) = 0;
    virtual void set_config_dir(const char* dir) = 0;
    virtual void set_touch_pad_visible(bool visible) = 0;
    virtual bool touch_pad_visible() const = 0;
};

// The generic in-game menu presentation (plan §3.4). The host owns the
// MenuModel (single source of truth) and hands it to present() each frame;
// the backend stores no model pointer across frames. Navigation policy
// lives in menu_model.h — the presenter only draws and hit-tests, and every
// mutation goes through the shared mutators.
class Menu {
public:
    virtual ~Menu() = default;
    virtual bool available() const = 0;  // == Caps::menu
    // Draw `model` (when open; a live toast draws regardless) in screen
    // space and translate pointer/touch hits through the shared mutators.
    // Call once per frame, after pump() and before video().present(); a
    // backend whose frame context lives inside present() coalesces the draw
    // into that call — the hit-test still runs here, so the returned
    // MenuResult (the pointer-driven one) is immediate and the host applies
    // it exactly like menu_navigate()'s.
    virtual MenuResult present(MenuModel& model,
                               const PointerState& pointer) = 0;
};

// ── facade ────────────────────────────────────────────────────────────────
// The single seam entry point (Phase 4b renamed the transitional
// `WindowBackendV2` facade to this name): lifecycle + capabilities, and the
// five concern accessors plus the generic menu presentation.
class WindowBackend {
public:
    virtual ~WindowBackend() = default;

    // The historical alias (the flat generation declared it): keeps every
    // `WindowBackend::Events` reference and `HostWindow::Events` intact.
    using Events = gbarecomp::Events;

    virtual Result open(const OpenParams& params) = 0;   // single entry point
    virtual void close() = 0;                            // idempotent
    virtual bool is_open() const = 0;
    virtual Caps caps() const = 0;                       // valid after open

    virtual Video&   video() = 0;
    virtual Audio&   audio() = 0;
    virtual Input&   input() = 0;
    virtual Fx&      fx() = 0;
    virtual Overlay& overlay() = 0;
    virtual Menu&    menu() = 0;

    virtual Events pump() = 0;   // = translate_input(read(snapshot), bindings, edges)
    // set_runtime_ui is gone with the flat generation: recomp-ui's runtime UI
    // is not ported to the raylib path — the generic menu (§3.4) is the
    // in-game surface, and HostWindow::set_runtime_ui stays as a compile-
    // compatibility shim.

    // Mobile lifecycle. raylib Android CANNOT close/reopen (upstream
    // InitPlatform waits on an INIT_WINDOW the OS never re-sends): these
    // pause/rebind, they never CloseWindow.
    virtual bool suspend() = 0;
    virtual bool resume() = 0;
    virtual bool wait_for_foreground() = 0;
};

} // namespace gbarecomp