// fake_backend.h — §5.2 item 3 fakes: drive the V2 seam with no window, no
// display server, no devices.
//
// The plan names five: RecordingVideo, ScriptedInput, TappingAudio,
// FakeClock, FakeBackend (which composes them). FakeFx/FakeOverlay are the
// minimal companions needed to compose the facade (the plan's list covers
// the concerns with observable behavior; Fx/Overlay just store).
//
// Built for Phase 3's menu host-loop test and this phase's display-free
// contract slice; Phase 5's RaylibApi fake is a separate, orthogonal seam.
//
// All fake state is public: tests script it and read it directly.
#pragma once

#include "host_clock.h"
#include "input_translate.h"
#include "window_backend.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gbarecomp::testing {

// ── RecordingVideo ──────────────────────────────────────────────────────
// Records every present/interp call and serves readback from the last frame
// (the §3.3 fidelity rule: readback returns the exact bytes uploaded).
class RecordingVideo final : public Video {
public:
    void present(const std::uint8_t* rgb888, bool fresh) override {
        ++present_calls;
        last_fresh = fresh;
        if (rgb888) {
            last_frame.assign(
                rgb888, rgb888 + static_cast<std::size_t>(base_w) * base_h * 3u);
            have_frame = true;
        }
    }
    void interp_display_tick(const std::uint8_t* rgb888,
                             std::uint64_t deadline_ns) override {
        ++interp_calls;
        last_deadline_ns = deadline_ns;
        present(rgb888, false);
    }
    bool readback(std::vector<std::uint8_t>& out, int& w, int& h) const override {
        if (!have_frame) return false;
        out = last_frame;
        w = base_w;
        h = base_h;
        return true;
    }
    float refresh_hz() const override { return hz; }
    bool drawable_size(int* w, int* h) const override {
        if (!open) return false;
        if (w) *w = drawable_w;
        if (h) *h = drawable_h;
        return drawable_w > 0 && drawable_h > 0;
    }
    bool set_surface_size(int w, int h) override {
        if (w < 1 || h < 1) return false;
        base_w = w;
        base_h = h;
        return true;
    }
    void set_title(const char* t) override { title = t ? t : ""; }
    int scale() const override { return scale_; }
    void adjust_scale(int delta) override {
        scale_ = std::clamp(scale_ + delta, 1, 8);
    }
    void set_linear_filter(bool enabled) override { linear = enabled; }
    bool linear_filter() const override { return linear; }
    void set_resize_driven_view(bool enabled) override { resize_driven = enabled; }
    void set_view_margins(std::uint32_t l, std::uint32_t r, std::uint32_t t,
                          std::uint32_t b) override {
        margin_l = l;
        margin_r = r;
        margin_t = t;
        margin_b = b;
    }
    void set_fullscreen(int mode) override {
        fullscreen_ = std::clamp(mode, 0, 2);
    }
    int fullscreen() const override { return fullscreen_; }
    float px_per_mm() const override { return 96.0f / 25.4f; }
    void set_presentation_anchor_top(bool anchor_top_in) override {
        anchor_top = anchor_top_in;
    }
    void set_fps_readout(bool on) override { fps = on; }
    bool fps_readout() const override { return fps; }

    // Scripting / observation.
    bool open = false;
    int drawable_w = 720, drawable_h = 480;
    int base_w = 240, base_h = 160;
    float hz = 60.0f;
    int present_calls = 0, interp_calls = 0;
    bool last_fresh = false;
    std::uint64_t last_deadline_ns = 0;
    bool have_frame = false;
    std::vector<std::uint8_t> last_frame;
    std::string title;
    std::uint32_t margin_l = 0, margin_r = 0, margin_t = 0, margin_b = 0;
    bool resize_driven = false, anchor_top = false;

private:
    int scale_ = 3;
    bool linear = false;
    int fullscreen_ = 0;
    bool fps = false;
};

// ── ScriptedInput ───────────────────────────────────────────────────────
// Returns whatever snapshot the test scripts into `next`; records the read.
class ScriptedInput final : public Input {
public:
    void read(InputSnapshot& out) override {
        ++read_calls;
        out = next;
        last_snapshot = out;
    }
    void load_config(const char* dir, bool assist_tools_default,
                     int fast_forward_multiplier_default) override {
        ++load_config_calls;
        last_config_dir = dir ? dir : "";
        assist = assist_tools_default;
        ff = fast_forward_multiplier_default;
    }
    const InputBindings& bindings() const override { return binds; }
    bool assist_tools_enabled() const override { return assist; }
    int fast_forward_multiplier() const override { return ff; }
    bool gamepad_connected() const override { return pad_connected; }
    void rumble(float low, float high, std::uint32_t duration_ms) override {
        ++rumble_calls;
        last_low = low;
        last_high = high;
        last_duration_ms = duration_ms;
    }
    void haptic_pulse(int duration_ms, float strength) override {
        ++haptic_calls;
        last_haptic_ms = duration_ms;
        last_haptic_strength = strength;
    }

    // Scripting / observation.
    InputSnapshot next;
    InputSnapshot last_snapshot;
    InputBindings binds;
    bool assist = true;
    int ff = 4;
    bool pad_connected = false;
    std::string last_config_dir;
    int read_calls = 0, load_config_calls = 0, rumble_calls = 0, haptic_calls = 0;
    float last_low = 0.0f, last_high = 0.0f, last_haptic_strength = 0.0f;
    std::uint32_t last_duration_ms = 0;
    int last_haptic_ms = 0;
};

// ── TappingAudio ────────────────────────────────────────────────────────
// Single-owner claim state machine; records pushes. `refuse_claim` injects
// an unavailable device (the §3.3 refusal the real backend shares).
class TappingAudio final : public Audio {
public:
    Result claim() override {
        ++claim_calls;
        if (refuse_claim)
            return {Err::AudioDevice, "fake: device unavailable"};
        if (claimed) return {Err::AudioDevice, "fake: already claimed"};
        claimed = true;
        return {};
    }
    void release() override {
        ++release_calls;
        claimed = false;
    }
    void push(const std::int16_t* samples, std::size_t count) override {
        ++push_calls;
        pushed += count;
        if (samples && count)
            last_samples.assign(samples, samples + count);
    }
    void set_volume(int pct) override {
        volume_ = std::clamp(pct, 0, 100);
    }
    int volume() const override { return volume_; }
    void set_enabled(bool enabled) override { enabled_ = enabled; }
    bool enabled() const override { return enabled_; }

    // Scripting / observation.
    bool claimed = false;
    bool refuse_claim = false;
    int claim_calls = 0, release_calls = 0, push_calls = 0;
    std::size_t pushed = 0;
    std::vector<std::int16_t> last_samples;

private:
    int volume_ = 100;
    bool enabled_ = true;
};

// ── FakeClock ───────────────────────────────────────────────────────────
// Deterministic time; set `now_s` to script it.
class FakeClock final : public Clock {
public:
    double seconds() const override { return now_s; }
    double now_s = 0.0;
};

// ── FakeFx / FakeOverlay ────────────────────────────────────────────────
// Minimal storing companions (see the header comment).
class FakeFx final : public Fx {
public:
    FxSettings settings() const override { return stored; }
    void apply(const FxSettings& s) override {
        ++apply_calls;
        stored = s;
    }
    void reset_frame_blend() override { ++reset_calls; }
    void load_config(const char* dir) override {
        ++load_config_calls;
        config_dir = dir ? dir : "";
    }

    FxSettings stored;
    std::string config_dir;
    int apply_calls = 0, reset_calls = 0, load_config_calls = 0;
};

class FakeOverlay final : public Overlay {
public:
    void set_host_overlay(void (*overlay)(HostOverlay*)) override {
        host_overlay = overlay;
    }
    void configure_touch(const TouchConfig& cfg) override { touch = cfg; }
    void set_config_dir(const char* dir) override {
        config_dir = dir ? dir : "";
    }
    void set_touch_pad_visible(bool visible) override { pad_visible = visible; }
    bool touch_pad_visible() const override { return pad_visible; }

    void (*host_overlay)(HostOverlay*) = nullptr;
    TouchConfig touch;
    std::string config_dir;
    bool pad_visible = false;
};

// ── FakeMenu ────────────────────────────────────────────────────────────
// Records what the host handed it; `next_result` scripts the pointer-driven
// MenuResult (host-loop tests).
class FakeMenu final : public Menu {
public:
    bool available() const override { return true; }
    MenuResult present(MenuModel& model, const PointerState& pointer) override {
        ++present_calls;
        last_model = &model;
        last_pointer = pointer;
        return next_result;
    }

    int present_calls = 0;
    MenuModel* last_model = nullptr;
    PointerState last_pointer;
    MenuResult next_result;
};

// ── FakeBackend ─────────────────────────────────────────────────────────
// Composes the fakes behind the V2 facade. Behavior knobs: `open_fails`
// (Err::Window), `caps_override` (defaults mirror a desktop raylib build),
// `suspend_ok`/`resume_ok`. pump() mirrors the real backend's composition:
// ScriptedInput -> translate_input(edges), with the FakeClock supplying
// now_ms.
class FakeBackend final : public WindowBackend {
public:
    Result open(const OpenParams& params) override {
        ++open_calls;
        last_open = params;
        if (open_fails) return {Err::Window, "fake: window init refused"};
        if (is_open_) return {Err::AlreadyOpen, "fake: already open"};
        if (params.mode == OpenParams::Mode::Headless && !caps_override.headless)
            return {Err::Unsupported, "fake: headless unsupported"};
        is_open_ = true;
        recording_video.open = true;
        edges = InputEdges{};  // §3.3: edges reset on open
        return {};
    }
    void close() override {
        ++close_calls;
        is_open_ = false;
        recording_video.open = false;
        tapped_audio.release();
        edges = InputEdges{};
    }
    bool is_open() const override { return is_open_; }
    Caps caps() const override { return caps_override; }

    Video& video() override { return recording_video; }
    Audio& audio() override { return tapped_audio; }
    Input& input() override { return scripted_input; }
    Fx& fx() override { return fake_fx; }
    Overlay& overlay() override { return fake_overlay; }
    Menu& menu() override { return fake_menu; }

    Events pump() override {
        ++pump_calls;
        // The clock stamps the scripted snapshot each pump, mirroring the
        // real backend's now_ms source; ScriptedInput records what it read.
        scripted_input.next.now_ms = clock.millis();
        InputSnapshot s;
        scripted_input.read(s);
        return translate_input(s, scripted_input.bindings(), edges);
    }
    bool suspend() override {
        ++suspend_calls;
        return suspend_ok;
    }
    bool resume() override {
        ++resume_calls;
        return resume_ok;
    }
    bool wait_for_foreground() override { return is_open_; }

    // Components + scripting. (Named after their fake types: a member may
    // not share a name with the accessor methods.)
    RecordingVideo recording_video;
    TappingAudio tapped_audio;
    ScriptedInput scripted_input;
    FakeFx fake_fx;
    FakeOverlay fake_overlay;
    FakeMenu fake_menu;
    FakeClock clock;
    InputEdges edges;
    Caps caps_override{[] {
        Caps c;
        c.window = true;
        c.input = true;
        c.audio = true;
        c.shaders = true;
        c.touch = true;
        c.reopen = true;
        c.vsync_report = true;
        c.menu = true;  // FakeMenu is a real presentation
        return c;
    }()};
    bool is_open_ = false;
    bool open_fails = false;
    bool suspend_ok = true, resume_ok = true;
    int open_calls = 0, close_calls = 0, pump_calls = 0;
    int suspend_calls = 0, resume_calls = 0;
    OpenParams last_open;
};

}  // namespace gbarecomp::testing
