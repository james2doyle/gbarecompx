// host_menu_test.cpp — Phase 3c: the host-loop wiring for the generic menu
// (plan §3.4 / §5.2 item 7).
//
// Drives HostMenu against the Phase 2-tail fakes: no window, no raylib, no
// display. Asserts the host contract end to end — build from the backend's
// current settings, navigation via GBA keyinput edges, the presentation call,
// application of MenuResult to the fake Fx/Audio/Video, the persistence hook,
// and the suppress_game_input derivation.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "host_menu.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "support/fake_backend.h"

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

using gbarecomp::Events;
using gbarecomp::FxSettings;
using gbarecomp::HostMenu;
using gbarecomp::MenuModel;
using gbarecomp::MenuResult;
using gbarecomp::MenuRow;
using gbarecomp::MenuRowKind;
using gbarecomp::MenuToggleAction;
using gbarecomp::PointerState;
using gbarecomp::WindowBackend;
using gbarecomp::testing::FakeBackend;

// GBA KEYINPUT bits, active-low (bit clear = pressed).
constexpr std::uint16_t kA = 1u << 0;
constexpr std::uint16_t kB = 1u << 1;
constexpr std::uint16_t kRight = 1u << 4;
constexpr std::uint16_t kDown = 1u << 7;

Events key_events(std::uint16_t pressed) {
    Events ev;
    ev.keyinput = static_cast<std::uint16_t>(0x03FFu & ~pressed);
    ev.menu_keyinput = ev.keyinput;  // nothing suppresses in these frames
    return ev;
}

// One frame with `pressed` keys down, then a release frame. HostMenu maps
// keyinput LEVELS to menu input on the newly-pressed edge only (one move per
// press), so tests must release between presses.
void press(HostMenu& host, FakeBackend& backend, std::uint16_t keys) {
    host.frame(backend, key_events(keys), PointerState{});
    host.frame(backend, key_events(0), PointerState{});
}

void frame(HostMenu& host, FakeBackend& backend) {
    host.frame(backend, key_events(0), PointerState{});
}

int row_by_label(const MenuModel& m, const char* label) {
    for (int i = 0; i < m.row_count; ++i)
        if (m.rows[i].label && std::strcmp(m.rows[i].label, label) == 0)
            return i;
    return -1;
}

int section_by_title(const MenuModel& m, const char* title) {
    for (int i = 0; i < m.section_count; ++i)
        if (m.sections[i].title && std::strcmp(m.sections[i].title, title) == 0)
            return i;
    return -1;
}

// Select a global row (in-section index derived), bypassing navigation —
// navigation itself is 3a's suite; this pins the apply path.
void select_row(MenuModel& m, int section, int global_row) {
    m.editing = true;
    m.section_index = section;
    int idx = 0;
    for (int i = 0; i < m.row_count; ++i) {
        if (m.rows[i].section != section) continue;
        if (i == global_row) {
            m.row_index = idx;
            return;
        }
        ++idx;
    }
}

struct HookLog {
    int calls = 0;
    int last_changed = -1;
    int last_action = -1;
    MenuModel* last_model = nullptr;
};

void hook_fn(void* user, WindowBackend&, const MenuResult& r) {
    auto* log = static_cast<HookLog*>(user);
    ++log->calls;
    if (r.changed_row >= 0) log->last_changed = r.changed_row;
    if (r.action_row >= 0) log->last_action = r.action_row;
}

void open_fake(FakeBackend& backend) {
    CHECK(backend.open(gbarecomp::OpenParams{}).ok());
}

// ── 1. build() seeds the default rows from the backend's settings ───────
void build_seeds_from_backend() {
    FakeBackend backend;
    open_fake(backend);
    backend.recording_video.adjust_scale(2);       // 3 -> 5
    backend.recording_video.set_linear_filter(true);
    backend.recording_video.set_fps_readout(true);
    backend.tapped_audio.set_volume(80);
    backend.tapped_audio.set_enabled(true);
    FxSettings fx;
    fx.frame_interp = true;
    fx.frame_blend = 1.0f;
    fx.lcd = true;
    fx.lcd_boost = 1.25f;
    fx.lcd_dither = 0.75f;
    fx.lcd_deband = 4.0f;
    backend.fake_fx.stored = fx;

    HostMenu host;
    host.build(backend);
    const MenuModel& m = host.model();

    CHECK(m.section_count == 4);
    CHECK(section_by_title(m, "Display") == 0);
    CHECK(section_by_title(m, "Graphics") == 1);
    CHECK(section_by_title(m, "Audio") == 2);
    CHECK(section_by_title(m, "System") == 3);
    CHECK(m.row_count == 13);

    const int scale = row_by_label(m, "Window scale");
    CHECK(scale >= 0 && gbarecomp::menu_value_int(m, scale) == 5);
    const int linear = row_by_label(m, "Linear filter");
    CHECK(linear >= 0 && gbarecomp::menu_value_int(m, linear) == 1);
    const int fps = row_by_label(m, "Show FPS");
    CHECK(fps >= 0 && gbarecomp::menu_value_int(m, fps) == 1);
    const int ghost = row_by_label(m, "LCD ghosting (frame blending)");
    CHECK(ghost >= 0 && gbarecomp::menu_value_int(m, ghost) == 1);
    const int interp = row_by_label(m, "Frame interpolation (experimental)");
    CHECK(interp >= 0 && gbarecomp::menu_value_int(m, interp) == 1);
    const int boost = row_by_label(m, "LCD boost");
    CHECK(boost >= 0 &&
          std::fabs(gbarecomp::menu_value_float(m, boost) - 1.25f) < 1e-4f);
    const int dither = row_by_label(m, "LCD dither");
    CHECK(dither >= 0 &&
          std::fabs(gbarecomp::menu_value_float(m, dither) - 0.75f) < 1e-4f);
    const int deband = row_by_label(m, "LCD deband");
    CHECK(deband >= 0 &&
          std::fabs(gbarecomp::menu_value_float(m, deband) - 4.0f) < 1e-4f);
    const int audio = row_by_label(m, "Audio");
    CHECK(audio >= 0 && gbarecomp::menu_value_int(m, audio) == 1);
    const int volume = row_by_label(m, "Volume");
    CHECK(volume >= 0 && gbarecomp::menu_value_int(m, volume) == 80);
    const int resume = row_by_label(m, "Resume game");
    CHECK(resume >= 0 && m.rows[resume].kind == MenuRowKind::Action);
}

// ── 2. open() seeds at open time; suppression derives from model.open ───
void open_seeds_and_suppresses() {
    FakeBackend backend;
    open_fake(backend);
    backend.tapped_audio.set_volume(80);

    HostMenu host;
    CHECK(!host.suppress_game_input());
    host.open(backend);
    CHECK(host.model().open);
    CHECK(host.suppress_game_input());
    const int volume = row_by_label(host.model(), "Volume");
    CHECK(gbarecomp::menu_value_int(host.model(), volume) == 80);

    host.close();
    CHECK(!host.model().open);
    CHECK(!host.suppress_game_input());

    // Values are re-seeded at the next open: changes made elsewhere show up.
    backend.tapped_audio.set_volume(30);
    host.toggle(backend);
    CHECK(host.model().open);
    CHECK(gbarecomp::menu_value_int(host.model(), volume) == 30);
    host.close();
}

// ── 3. keyinput LEVELS -> one navigation per newly-pressed edge ─────────
void edge_detection_one_move_per_press() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    host.open(backend);
    CHECK(host.model().section_index == 0);

    // Held Down across three frames: exactly one move.
    for (int i = 0; i < 3; ++i)
        host.frame(backend, key_events(kDown), PointerState{});
    CHECK(host.model().section_index == 1);

    // Release, press again: another move.
    frame(host, backend);
    host.frame(backend, key_events(kDown), PointerState{});
    CHECK(host.model().section_index == 2);
    host.close();
}

// ── 4. navigation applies a Toggle to the backend + persist hook ────────
void nav_applies_toggle_and_persists() {
    FakeBackend backend;
    open_fake(backend);
    backend.recording_video.set_linear_filter(false);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    // Display -> row 2 (Linear filter); Confirm toggles.
    press(host, backend, kA);        // enter Display (row 0)
    press(host, backend, kDown);     // row 1
    press(host, backend, kDown);     // row 2
    press(host, backend, kA);        // toggle
    CHECK(backend.recording_video.linear_filter());
    const int linear = row_by_label(host.model(), "Linear filter");
    CHECK(gbarecomp::menu_value_int(host.model(), linear) == 1);
    CHECK(log.calls == 1);
    CHECK(log.last_changed == linear);
    CHECK(host.model().status[0] != '\0');  // "Saved" (hook is wired)
    CHECK(host.model().status_frames > 0);

    // Toggle back.
    press(host, backend, kA);
    CHECK(!backend.recording_video.linear_filter());
    CHECK(gbarecomp::menu_value_int(host.model(), linear) == 0);
    CHECK(log.calls == 2);
    host.close();
}

// ── 5. StepInt volume: steps, clamps, and never persists a no-op ────────
void volume_steps_clamp_and_noop_no_persist() {
    FakeBackend backend;
    open_fake(backend);
    backend.tapped_audio.set_volume(80);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    press(host, backend, kDown);  // section 1
    press(host, backend, kDown);  // section 2 (Audio)
    press(host, backend, kA);     // enter; row 0 = Audio
    press(host, backend, kDown);  // row 1 = Volume
    const int volume = row_by_label(host.model(), "Volume");

    press(host, backend, kRight);  // 80 -> 85
    CHECK(backend.tapped_audio.volume() == 85);
    CHECK(gbarecomp::menu_value_int(host.model(), volume) == 85);
    CHECK(log.calls == 1);
    press(host, backend, kRight);  // 90
    press(host, backend, kRight);  // 95
    press(host, backend, kRight);  // 100
    CHECK(backend.tapped_audio.volume() == 100);
    CHECK(log.calls == 4);
    press(host, backend, kRight);  // clamped: model unchanged -> no hook
    CHECK(backend.tapped_audio.volume() == 100);
    CHECK(log.calls == 4);

    // Backend-side clamp reseed (same pattern as the scale row): widen the
    // model range past the backend's clamp and watch the row pull back.
    host.model().rows[volume].max_value = 105.0f;
    press(host, backend, kRight);  // model 105 -> set_volume clamps -> 100
    CHECK(backend.tapped_audio.volume() == 100);
    CHECK(gbarecomp::menu_value_int(host.model(), volume) == 100);
    CHECK(log.calls == 5);
    host.close();
}

// ── 6. StepFloat applies through fx.apply and reseeds ───────────────────
void float_row_applies() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    press(host, backend, kDown);      // section 1 (Graphics)
    press(host, backend, kA);         // enter; row 0 = ghosting
    press(host, backend, kDown);      // interp
    press(host, backend, kDown);      // LCD
    press(host, backend, kDown);      // LCD boost
    const int boost = row_by_label(host.model(), "LCD boost");
    const int before = backend.fake_fx.apply_calls;
    press(host, backend, kRight);     // 1.0 -> 1.05
    CHECK(std::fabs(backend.fake_fx.stored.lcd_boost - 1.05f) < 1e-4f);
    CHECK(backend.fake_fx.apply_calls > before);
    CHECK(std::fabs(gbarecomp::menu_value_float(host.model(), boost) - 1.05f) <
          1e-4f);
    CHECK(log.calls == 1);
    host.close();
}

// ── 7. Choice cycles through fullscreen modes ───────────────────────────
void choice_cycles_fullscreen() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    press(host, backend, kA);  // enter Display; row 0 = Fullscreen
    press(host, backend, kRight);
    CHECK(backend.recording_video.fullscreen() == 1);
    press(host, backend, kRight);
    CHECK(backend.recording_video.fullscreen() == 2);
    press(host, backend, kRight);  // wraps
    CHECK(backend.recording_video.fullscreen() == 0);
    CHECK(log.calls == 3);
    host.close();
}

// ── 8. Backend-side clamping reseeds the model row ──────────────────────
void scale_clamp_reseeds_model() {
    FakeBackend backend;
    open_fake(backend);
    backend.recording_video.adjust_scale(5);  // 3 -> 8 (max)
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    press(host, backend, kA);      // enter Display
    press(host, backend, kDown);   // row 1 = Window scale
    const int scale = row_by_label(host.model(), "Window scale");
    CHECK(gbarecomp::menu_value_int(host.model(), scale) == 8);

    // Widen the model's range past the backend's clamp: the model steps to
    // 9, the backend clamps to 8, the reseed must pull the row back.
    host.model().rows[scale].max_value = 9.0f;
    press(host, backend, kRight);
    CHECK(backend.recording_video.scale() == 8);
    CHECK(gbarecomp::menu_value_int(host.model(), scale) == 8);
    CHECK(log.calls == 1);
    host.close();
}

// ── 9. Back leaves a section; a second Back closes ──────────────────────
void back_leaves_then_closes() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    host.open(backend);

    press(host, backend, kA);  // enter Display
    CHECK(host.model().editing);
    press(host, backend, kB);  // leave
    CHECK(!host.model().editing);
    CHECK(host.model().open);
    CHECK(host.suppress_game_input());
    press(host, backend, kB);  // close
    CHECK(!host.model().open);
    CHECK(!host.suppress_game_input());
}

// ── 10. Resume game is framework-owned: closes, no host hook ────────────
void resume_action_closes_without_hook() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    press(host, backend, kDown);  // 1
    press(host, backend, kDown);  // 2
    press(host, backend, kDown);  // 3 (System)
    press(host, backend, kA);     // enter; row 0 = Resume game
    press(host, backend, kA);     // activate
    CHECK(!host.model().open);
    CHECK(log.calls == 0);  // nothing for the host to do
}

// ── 11. Game-appended Action rows reach the host hook ───────────────────
void game_action_row_reaches_hook() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    const int system = section_by_title(host.model(), "System");
    CHECK(system >= 0);
    MenuRow row;
    row.kind = MenuRowKind::Action;
    row.label = "Save now";
    row.section = system;
    const int appended = host.model().row_count++;
    host.model().rows[appended] = row;

    select_row(host.model(), system, appended);
    press(host, backend, kA);
    CHECK(host.model().open);  // game action must not close the menu
    CHECK(log.calls == 1);
    CHECK(log.last_action == appended);

    // A game-appended VALUE row reaches the hook too (its binding is None):
    // the framework applies nothing, the host owns it.
    MenuRow toggle_row;
    toggle_row.kind = MenuRowKind::Toggle;
    toggle_row.label = "Game toggle";
    toggle_row.section = system;
    const int value_row = gbarecomp::menu_add_row(host.model(), toggle_row);
    CHECK(value_row >= 0);
    select_row(host.model(), system, value_row);
    press(host, backend, kA);
    CHECK(log.calls == 2);
    CHECK(log.last_changed == value_row);
    CHECK(gbarecomp::menu_value_int(host.model(), value_row) == 1);
    CHECK(host.model().open);
}

// ── 12. The pointer-driven MenuResult from the presentation applies ─────
void pointer_result_applies() {
    FakeBackend backend;
    open_fake(backend);
    backend.tapped_audio.set_volume(80);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    const int volume = row_by_label(host.model(), "Volume");
    gbarecomp::menu_set_value_int(host.model(), volume, 42);
    MenuResult r;
    r.changed_row = volume;
    backend.fake_menu.next_result = r;

    frame(host, backend);
    CHECK(backend.tapped_audio.volume() == 42);
    CHECK(log.calls == 1);
    CHECK(log.last_changed == volume);
    CHECK(backend.fake_menu.present_calls == 1);
    CHECK(backend.fake_menu.last_model == &host.model());
    backend.fake_menu.next_result = MenuResult{};
    host.close();
}

// ── 13. Closed frames still present (toasts) but ignore navigation ──────
void closed_frame_presents_but_ignores_nav() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;

    press(host, backend, kA);
    CHECK(!host.model().open);
    CHECK(backend.fake_menu.present_calls == 2);  // press frame + release frame
    CHECK(backend.fake_menu.last_model == &host.model());
}

// ── 14. build() resets the model: game rows do not survive a rebuild ────
void build_resets_game_rows() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    host.build(backend);
    const int baseline = host.model().row_count;
    host.model().row_count += 1;
    host.build(backend);
    CHECK(host.model().row_count == baseline);
}

// ── 15. Without shader support the LCD group is not offered ─────────────
void build_without_shaders_skips_lcd_group() {
    FakeBackend backend;
    open_fake(backend);
    backend.caps_override.shaders = false;
    HostMenu host;
    host.build(backend);
    CHECK(host.model().row_count == 9);  // 13 - 4 LCD-group rows
    CHECK(row_by_label(host.model(), "LCD boost") == -1);
    CHECK(row_by_label(host.model(), "LCD ghosting (frame blending)") >= 0);
}

// ── 16. The hook is optional: no hook, no "Saved" status ────────────────
void no_hook_no_status() {
    FakeBackend backend;
    open_fake(backend);
    backend.recording_video.set_linear_filter(false);
    HostMenu host;
    host.open(backend);

    press(host, backend, kA);     // enter Display
    press(host, backend, kDown);  // row 1
    press(host, backend, kDown);  // row 2 (Linear filter)
    press(host, backend, kA);     // toggle
    CHECK(backend.recording_video.linear_filter());
    CHECK(host.model().status[0] == '\0');  // nothing persists: no claim
    host.close();
}

// ── 17. Suppressed game input still navigates the menu (the pump path) ──
// The regression this guards: the backend masks ev.keyinput while the menu
// is open (its flag derives from the presented model), so navigation MUST
// come from ev.menu_keyinput — otherwise the menu freezes on the first frame
// after opening.
void suppressed_pump_still_navigates() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    host.open(backend);

    backend.scripted_input.next.pad_gba_bits = kDown;
    backend.scripted_input.next.suppress_game_input = true;
    const Events ev = backend.pump();
    CHECK(ev.keyinput == 0x03FFu);       // the game sees nothing
    CHECK(ev.menu_keyinput != 0x03FFu);  // the menu sees the press
    host.frame(backend, ev, PointerState{});
    CHECK(host.model().section_index == 1);

    backend.scripted_input.next.pad_gba_bits = 0;
    host.frame(backend, backend.pump(), PointerState{});  // release edge
    backend.scripted_input.next.pad_gba_bits = kDown;
    host.frame(backend, backend.pump(), PointerState{});
    CHECK(host.model().section_index == 2);
    host.close();
}

// ── 18. apply() bounds and the nothing-handled result ───────────────────
void apply_bounds_and_nothing_handled() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    host.open(backend);
    backend.tapped_audio.set_volume(77);

    // A game that overran the model capacity must not walk binds_ out of
    // bounds; out-of-capacity rows are simply not ours.
    host.model().row_count = MenuModel::kMaxRows + 8;
    MenuResult r;
    r.changed_row = MenuModel::kMaxRows;
    CHECK(!host.apply(backend, r));
    r = MenuResult{};
    r.action_row = MenuModel::kMaxRows;
    CHECK(!host.apply(backend, r));

    // In-capacity but unbound (a game row with no hook): nothing handled,
    // nothing touched.
    r = MenuResult{};
    r.changed_row = MenuModel::kMaxRows - 1;
    CHECK(!host.apply(backend, r));
    CHECK(backend.tapped_audio.volume() == 77);

    // A row inside [row_count, kMaxRows): also not ours.
    host.model().row_count = 5;
    r = MenuResult{};
    r.changed_row = 10;
    CHECK(!host.apply(backend, r));

    // menu_add_row refuses past the capacity instead of corrupting.
    host.model().row_count = MenuModel::kMaxRows;
    MenuRow overflow;
    overflow.kind = MenuRowKind::Toggle;
    overflow.label = "Overflow";
    CHECK(gbarecomp::menu_add_row(host.model(), overflow) == -1);

    // An in-range non-Resume action with no hook: nothing handled.
    host.open(backend);
    const int system = section_by_title(host.model(), "System");
    MenuRow act;
    act.kind = MenuRowKind::Action;
    act.label = "Game action";
    act.section = system;
    const int act_row = gbarecomp::menu_add_row(host.model(), act);
    CHECK(act_row >= 0);
    r = MenuResult{};
    r.action_row = act_row;
    CHECK(!host.apply(backend, r));

    // Hand-fed Resume as a changed row (the model only ever reports it as an
    // action): still framework-owned — closes, nothing for the host.
    r = MenuResult{};
    r.changed_row = row_by_label(host.model(), "Resume game");
    CHECK(host.apply(backend, r));
    CHECK(!host.model().open);
    host.close();
}

// ── 19. Every remaining default bind applies to the backend ─────────────
void every_default_bind_applies() {
    FakeBackend backend;
    open_fake(backend);
    backend.tapped_audio.set_enabled(true);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);

    const int display = section_by_title(host.model(), "Display");
    const int graphics = section_by_title(host.model(), "Graphics");
    const int audio = section_by_title(host.model(), "Audio");

    // Show FPS (Toggle).
    select_row(host.model(), display, row_by_label(host.model(), "Show FPS"));
    press(host, backend, kA);
    CHECK(backend.recording_video.fps_readout());

    // LCD ghosting (Toggle), both ways: on = full strength, off = 0.
    const int ghost =
        row_by_label(host.model(), "LCD ghosting (frame blending)");
    select_row(host.model(), graphics, ghost);
    press(host, backend, kA);
    CHECK(backend.fake_fx.stored.frame_blend == 1.0f);
    press(host, backend, kA);
    CHECK(backend.fake_fx.stored.frame_blend == 0.0f);

    // Frame interpolation (Toggle).
    select_row(host.model(), graphics,
               row_by_label(host.model(),
                            "Frame interpolation (experimental)"));
    press(host, backend, kA);
    CHECK(backend.fake_fx.stored.frame_interp);

    // LCD (Toggle).
    select_row(host.model(), graphics, row_by_label(host.model(), "LCD"));
    press(host, backend, kA);
    CHECK(backend.fake_fx.stored.lcd);

    // LCD dither (StepFloat): 1.0 -> 1.05, reseeded.
    const int dither = row_by_label(host.model(), "LCD dither");
    select_row(host.model(), graphics, dither);
    press(host, backend, kRight);
    CHECK(std::fabs(backend.fake_fx.stored.lcd_dither - 1.05f) < 1e-4f);
    CHECK(std::fabs(gbarecomp::menu_value_float(host.model(), dither) -
                    1.05f) < 1e-4f);

    // LCD deband (StepFloat): 0.0 -> 1.0.
    const int deband = row_by_label(host.model(), "LCD deband");
    select_row(host.model(), graphics, deband);
    press(host, backend, kRight);
    CHECK(std::fabs(backend.fake_fx.stored.lcd_deband - 1.0f) < 1e-4f);

    // Audio (Toggle): on -> off.
    select_row(host.model(), audio, row_by_label(host.model(), "Audio"));
    press(host, backend, kA);
    CHECK(!backend.tapped_audio.enabled());

    CHECK(log.calls == 8);  // one per applied change above
    host.close();
}

// ── 20. The rebuild hook re-appends game rows on every open/toggle ──────
struct RebuildLog {
    int calls = 0;
};

void rebuild_hook_fn(void* user, MenuModel& m) {
    ++static_cast<RebuildLog*>(user)->calls;
    int system = 0;
    for (int i = 0; i < m.section_count; ++i)
        if (m.sections[i].title &&
            std::strcmp(m.sections[i].title, "System") == 0)
            system = i;
    MenuRow row;
    row.kind = MenuRowKind::Toggle;
    row.label = "Game option";
    row.section = system;
    row.int_value = 1;
    CHECK(gbarecomp::menu_add_row(m, row) >= 0);
}

void rebuild_hook_reappends_rows() {
    FakeBackend backend;
    open_fake(backend);
    HostMenu host;
    RebuildLog log;
    host.set_rebuild_hook(rebuild_hook_fn, &log);
    host.open(backend);
    CHECK(log.calls == 1);
    CHECK(row_by_label(host.model(), "Game option") >= 0);

    host.close();
    host.toggle(backend);  // rebuild -> hook again
    CHECK(log.calls == 2);
    CHECK(host.model().open);
    CHECK(row_by_label(host.model(), "Game option") >= 0);
    host.close();
}

// ── 21. toggle() closes when open; clearing the hook disarms it ─────────
void toggle_close_and_hook_cleared() {
    FakeBackend backend;
    open_fake(backend);
    backend.recording_video.set_linear_filter(false);
    HostMenu host;
    HookLog log;
    host.set_apply_hook(hook_fn, &log);
    host.open(backend);
    CHECK(host.model().open);
    host.toggle(backend);
    CHECK(!host.model().open);

    host.set_apply_hook(nullptr, nullptr);  // disarm
    host.open(backend);
    select_row(host.model(), section_by_title(host.model(), "Display"),
               row_by_label(host.model(), "Linear filter"));
    press(host, backend, kA);
    CHECK(backend.recording_video.linear_filter());
    CHECK(log.calls == 0);
    CHECK(host.model().status[0] == '\0');
    host.close();
}

}  // namespace

// ── 22. The View row follows the host's view-mode endpoint ─────────────
struct ViewEndpoint {
    int mode = 0;  // 0 Native, 1 Adaptive, 2 16:9 (ViewMode order)
    int sets = 0;
    int last_set = -1;
};

int view_get_fn(void* user) {
    return static_cast<ViewEndpoint*>(user)->mode;
}

void view_set_fn(void* user, int mode) {
    auto* v = static_cast<ViewEndpoint*>(user);
    v->mode = mode;  // the host applies synchronously, like view_mode
    v->last_set = mode;
    ++v->sets;
}

void view_row_follows_endpoint() {
    FakeBackend backend;
    open_fake(backend);

    // No endpoint: no View row (games without a resize opt-in).
    {
        HostMenu host;
        host.build(backend);
        CHECK(row_by_label(host.model(), "View") < 0);
    }
    // Endpoint present but unavailable: still no row.
    {
        HostMenu host;
        ViewEndpoint v;
        host.set_view_mode_endpoint(view_get_fn, view_set_fn, &v, false);
        host.build(backend);
        CHECK(row_by_label(host.model(), "View") < 0);
    }
    // Available: seeded from the getter; cycling forwards to the setter.
    // Two choices only (Native / Adaptive): a fixed CLI width is not a
    // menu-representable state, and the legacy 16:9 choice lied about
    // non-285 fixed widths (--view-width 320 displayed "16:9").
    {
        HostMenu host;
        ViewEndpoint v;
        v.mode = 1;  // Adaptive
        host.set_view_mode_endpoint(view_get_fn, view_set_fn, &v, true);
        host.open(backend);
        const int display = section_by_title(host.model(), "Display");
        const int view = row_by_label(host.model(), "View");
        CHECK(view >= 0);
        CHECK(gbarecomp::menu_value_int(host.model(), view) == 1);
        // Legacy Fixed16x9 (2) from an old config or a fixed CLI width
        // seeds as Native: the row is a binary adaptive-or-not now.
        {
            ViewEndpoint legacy;
            legacy.mode = 2;
            HostMenu host2;
            host2.set_view_mode_endpoint(view_get_fn, view_set_fn,
                                         &legacy, true);
            host2.open(backend);
            const int view2 = row_by_label(host2.model(), "View");
            CHECK(gbarecomp::menu_value_int(host2.model(), view2) == 0);
            host2.close();
        }
        select_row(host.model(), display, view);
        press(host, backend, kRight);  // Adaptive wraps -> Native
        CHECK(v.sets == 1);
        CHECK(v.last_set == 0);
        CHECK(gbarecomp::menu_value_int(host.model(), view) == 0);
        press(host, backend, kRight);  // Native -> Adaptive
        CHECK(v.last_set == 1);
        CHECK(gbarecomp::menu_value_int(host.model(), view) == 1);
        host.close();
    }
}

// ── 23. The Delete-key toggle decision: edge + open state -> action ──────
// The runtime's pump policy (open on edge while closed, close on edge while
// open, nothing otherwise) as a pure step the host loop applies.
void menu_toggle_step_decides_open_and_close() {
    using gbarecomp::menu_toggle_step;
    CHECK(menu_toggle_step(false, false) == MenuToggleAction::None);
    CHECK(menu_toggle_step(false, true) == MenuToggleAction::None);
    CHECK(menu_toggle_step(true, false) == MenuToggleAction::Open);
    CHECK(menu_toggle_step(true, true) == MenuToggleAction::Close);
    // Full cycle: a hold (edge, then levels) flips exactly once per press.
    bool open = false;
    CHECK(menu_toggle_step(true, open) == MenuToggleAction::Open);
    open = true;
    CHECK(menu_toggle_step(false, open) == MenuToggleAction::None);
    CHECK(menu_toggle_step(true, open) == MenuToggleAction::Close);
    open = false;
    CHECK(menu_toggle_step(true, open) == MenuToggleAction::Open);
}

// ── 24. The View dirty step: framework mode -> geometry policy ──────────
// Pins the policy sync_resize_driven_view consumes: Adaptive is
// drawable-driven, the fixed modes resolve to a width now.
void menu_view_step_resolves_geometry_policy() {
    using gbarecomp::menu_view_step;
    using gbarecomp::MenuViewGeometry;
    using gbarecomp::ViewMode;
    MenuViewGeometry nat = menu_view_step(ViewMode::Native, 448);
    CHECK(!nat.resize_driven && nat.fixed_width == 240);
    MenuViewGeometry ad = menu_view_step(ViewMode::Adaptive, 448);
    CHECK(ad.resize_driven);
    MenuViewGeometry wide = menu_view_step(ViewMode::Fixed16x9, 448);
    CHECK(!wide.resize_driven && wide.fixed_width == 285);  // 16:9 at 160 lines
    MenuViewGeometry clamped = menu_view_step(ViewMode::Fixed16x9, 240);
    CHECK(!clamped.resize_driven && clamped.fixed_width == 240);
}

int main() {
    build_seeds_from_backend();
    open_seeds_and_suppresses();
    edge_detection_one_move_per_press();
    nav_applies_toggle_and_persists();
    volume_steps_clamp_and_noop_no_persist();
    float_row_applies();
    choice_cycles_fullscreen();
    scale_clamp_reseeds_model();
    back_leaves_then_closes();
    resume_action_closes_without_hook();
    game_action_row_reaches_hook();
    pointer_result_applies();
    closed_frame_presents_but_ignores_nav();
    build_resets_game_rows();
    build_without_shaders_skips_lcd_group();
    no_hook_no_status();
    suppressed_pump_still_navigates();
    apply_bounds_and_nothing_handled();
    every_default_bind_applies();
    rebuild_hook_reappends_rows();
    toggle_close_and_hook_cleared();
    view_row_follows_endpoint();
    menu_toggle_step_decides_open_and_close();
    menu_view_step_resolves_geometry_policy();

    std::printf("host_menu tests: PASS\n");
    return 0;
}
