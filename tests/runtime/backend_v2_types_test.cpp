// Phase 1 — V2 value types and interface shapes (plan §5.2 item 1).
//
// Guards the additive V2 generation in window_backend.h: defaults of
// OpenParams / Caps / FxSettings / TouchConfig, Result::ok semantics,
// interface purity (virtual destructors, abstract), and the "no-op implies
// false cap" rule (§3.3) exercised through a fully no-op backend.
//
// Deliberately uses the always-active CHECK macro rather than <cassert>:
// the tree builds tests with -DNDEBUG (Release), which erases assert() and
// would make this file vacuous (see BRINGUP "vacuous test" finding).
#include "input_translate.h"
#include "window_backend.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {

using gbarecomp::Audio;
using gbarecomp::Caps;
using gbarecomp::Err;
using gbarecomp::Events;
using gbarecomp::Fx;
using gbarecomp::FxSettings;
using gbarecomp::HostOverlay;
using gbarecomp::Input;
using gbarecomp::InputBindings;
using gbarecomp::InputSnapshot;
using gbarecomp::Menu;
using gbarecomp::MenuModel;
using gbarecomp::MenuResult;
using gbarecomp::OpenParams;
using gbarecomp::Overlay;
using gbarecomp::PointerState;
using ::RecompRuntimeUi;
using gbarecomp::Result;
using gbarecomp::TouchConfig;
using gbarecomp::Video;
using gbarecomp::WindowBackend;
using gbarecomp::WindowBackendV2;

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

// Events moved to seam scope in Phase 1; the flat class keeps a member alias
// so WindowBackend::Events (and HostWindow::Events) still name the same type.
static_assert(std::is_same_v<Events, WindowBackend::Events>,
              "WindowBackend::Events must alias the seam-scope Events");

// Both generations coexist during the port: the V2 facade is a distinct type,
// not an evolution of the flat class.
static_assert(!std::is_base_of_v<WindowBackend, WindowBackendV2>,
              "V2 facade must not derive from the flat generation");

// Interfaces are safely deletable through their base pointer...
static_assert(std::has_virtual_destructor_v<Video>);
static_assert(std::has_virtual_destructor_v<Audio>);
static_assert(std::has_virtual_destructor_v<Input>);
static_assert(std::has_virtual_destructor_v<Fx>);
static_assert(std::has_virtual_destructor_v<Overlay>);
static_assert(std::has_virtual_destructor_v<WindowBackendV2>);
// ...and abstract: nothing may instantiate a bare interface.
static_assert(std::is_abstract_v<Video>);
static_assert(std::is_abstract_v<Audio>);
static_assert(std::is_abstract_v<Input>);
static_assert(std::is_abstract_v<Fx>);
static_assert(std::is_abstract_v<Overlay>);
static_assert(std::is_abstract_v<WindowBackendV2>);

bool caps_all_false(const Caps& c) {
    return !c.window && !c.headless && !c.input && !c.audio && !c.shaders &&
           !c.rumble && !c.haptics && !c.touch && !c.menu &&
           !c.multi_monitor && !c.reopen && !c.vsync_report;
}

// A backend that implements every method as a no-op must advertise an
// all-false Caps and refuse to open (§3.3: a defaulted/no-op implementation
// is legal only where the caps bit is false and the setting is still
// stored/reported).
struct NoopVideo final : Video {
    void present(const std::uint8_t*, bool) override {}
    void interp_display_tick(const std::uint8_t*, std::uint64_t) override {}
    bool readback(std::vector<std::uint8_t>&, int&, int&) const override {
        return false;
    }
    float refresh_hz() const override { return 0.0f; }
    bool drawable_size(int*, int*) const override { return false; }
    bool set_surface_size(int, int) override { return false; }
    void set_title(const char*) override {}
    int scale() const override { return 1; }
    void adjust_scale(int) override {}
    void set_linear_filter(bool) override {}
    bool linear_filter() const override { return false; }
    void set_resize_driven_view(bool) override {}
    void set_view_margins(std::uint32_t, std::uint32_t, std::uint32_t,
                          std::uint32_t) override {}
    void set_fullscreen(int) override {}
    int fullscreen() const override { return 0; }
    float px_per_mm() const override { return 96.0f / 25.4f; }
    void set_presentation_anchor_top(bool) override {}
    void set_fps_readout(bool) override {}
    bool fps_readout() const override { return false; }
};

struct NoopAudio final : Audio {
    Result claim() override { return {Err::Unsupported, "no-op backend"}; }
    void release() override {}
    void push(const std::int16_t*, std::size_t) override {}
    void set_volume(int) override {}
    int volume() const override { return 100; }
    void set_enabled(bool) override {}
    bool enabled() const override { return false; }
};

struct NoopInput final : Input {
    static InputBindings empty_bindings;
    void read(InputSnapshot&) override {}
    void load_config(const char*, bool, int) override {}
    const InputBindings& bindings() const override { return empty_bindings; }
    bool assist_tools_enabled() const override { return false; }
    int fast_forward_multiplier() const override { return 1; }
    bool gamepad_connected() const override { return false; }
    void rumble(float, float, std::uint32_t) override {}
    void haptic_pulse(int, float) override {}
};

struct NoopFx final : Fx {
    FxSettings stored{};  // the rule: stored and reported even when unsupported
    FxSettings settings() const override { return stored; }
    void apply(const FxSettings& s) override { stored = s; }
    void reset_frame_blend() override {}
};

struct NoopOverlay final : Overlay {
    void set_host_overlay(void (*)(HostOverlay*)) override {}
    void configure_touch(const TouchConfig&) override {}
    void set_config_dir(const char*) override {}
    void set_touch_pad_visible(bool) override {}
    bool touch_pad_visible() const override { return false; }
};

struct NoopMenu final : Menu {
    bool available() const override { return false; }
    MenuResult present(MenuModel&, const PointerState&) override {
        return MenuResult{};
    }
};

struct NoopBackend final : WindowBackendV2 {
    NoopVideo v;
    NoopAudio a;
    NoopInput i;
    NoopFx f;
    NoopOverlay o;
    NoopMenu m;

    Result open(const OpenParams&) override {
        return {Err::Unsupported, "no-op backend"};
    }
    void close() override {}
    bool is_open() const override { return false; }
    Caps caps() const override { return Caps{}; }
    Video& video() override { return v; }
    Audio& audio() override { return a; }
    Input& input() override { return i; }
    Fx& fx() override { return f; }
    Overlay& overlay() override { return o; }
    Menu& menu() override { return m; }
    Events pump() override { return Events{}; }
    void set_runtime_ui(RecompRuntimeUi*) override {}
    bool suspend() override { return false; }
    bool resume() override { return false; }
    bool wait_for_foreground() override { return true; }
};

InputBindings NoopInput::empty_bindings{};

}  // namespace

int main() {
    // ── OpenParams defaults (§3.2) ───────────────────────────────────────
    OpenParams p;
    CHECK(p.scale == 3);
    CHECK(p.base_w == 240 && p.base_h == 160);
    CHECK(p.title != nullptr && std::strcmp(p.title, "gbarecomp") == 0);
    CHECK(p.display == nullptr);
    CHECK(p.mode == OpenParams::Mode::Windowed);
    CHECK(!p.linear_filter && !p.sharp_filter);
    CHECK(!p.resizable && !p.resize_driven_view);

    // ── Caps: all-false by default (a backend opts in, never out) ────────
    CHECK(caps_all_false(Caps{}));

    // ── FxSettings defaults (§3.2) ───────────────────────────────────────
    FxSettings fx;
    CHECK(!fx.frame_interp && fx.frame_blend == 0.0f);
    CHECK(!fx.lcd && fx.lcd_boost == 1.0f && fx.lcd_dither == 1.0f &&
          fx.lcd_deband == 0.0f);

    // ── TouchConfig defaults ─────────────────────────────────────────────
    TouchConfig tc;
    CHECK(!tc.policy && tc.claims == 0 && tc.pad_default == 0);
    CHECK(!tc.emulate_touch && tc.pad_idle_hide_seconds == 0);

    // ── Result::ok semantics ─────────────────────────────────────────────
    Result none;
    CHECK(none.ok() && none.code == Err::None && none.detail == nullptr);
    CHECK((!Result{Err::Window, "x"}.ok()));
    CHECK((!Result{Err::Renderer, nullptr}.ok()));
    CHECK((!Result{Err::Shader, nullptr}.ok()));
    CHECK((!Result{Err::AudioDevice, nullptr}.ok()));
    CHECK((!Result{Err::Unsupported, nullptr}.ok()));
    CHECK((!Result{Err::AlreadyOpen, nullptr}.ok()));
    CHECK((!Result{Err::NotOpen, nullptr}.ok()));
    Result detailed{Err::AudioDevice, "no device"};
    CHECK(!detailed.ok() && std::strcmp(detailed.detail, "no device") == 0);

    // ── No-op implies false cap (§3.3) ───────────────────────────────────
    NoopBackend b;
    CHECK(caps_all_false(b.caps()));
    Result r = b.open(OpenParams{});
    CHECK(!r.ok() && r.code == Err::Unsupported && r.detail != nullptr);
    CHECK(!b.is_open());

    // Every concern stays reachable and safe on a no-op backend.
    std::vector<std::uint8_t> frame;
    int w = 0, h = 0;
    CHECK(!b.video().readback(frame, w, h));
    CHECK(!b.audio().claim().ok());
    CHECK(!b.input().gamepad_connected());
    CHECK(!b.overlay().touch_pad_visible());
    CHECK(!b.suspend() && !b.resume() && b.wait_for_foreground());

    // Settings are stored and reported even where the cap is false.
    FxSettings wanted;
    wanted.lcd = true;
    wanted.lcd_boost = 1.25f;
    b.fx().apply(wanted);
    CHECK(b.fx().settings().lcd && b.fx().settings().lcd_boost == 1.25f);

    std::printf("backend_v2_types tests: PASS\n");
    return 0;
}
