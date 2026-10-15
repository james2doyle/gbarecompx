// menu_model_test.cpp — Phase 3a: the pure menu model (plan §3.4).
//
// Ports the SEMANTICS of recomp-ui's tests/runtime_ui_test.c to the V2
// model's shape (values in the model, no callbacks), plus the V2 policy
// additions: skip disabled/invisible rows, skip sections with nothing
// selectable, pointer select/activate, and the none-case input mapping.
//
// CHECK (not <cassert>): Release erases assert() via -DNDEBUG; see BRINGUP
// "vacuous test" finding.
#include "menu_model.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

namespace {

using gbarecomp::MenuInput;
using gbarecomp::MenuModel;
using gbarecomp::MenuPointer;
using gbarecomp::MenuResult;
using gbarecomp::MenuRow;
using gbarecomp::MenuRowKind;
using gbarecomp::PointerState;

// GBA keyinput is active-low: a CLEAR bit means pressed.
constexpr std::uint16_t kAllUp = 0x03FFu;
std::uint16_t pressed(int bit) { return static_cast<std::uint16_t>(kAllUp & ~(1u << bit)); }
constexpr int kBitA = 0, kBitB = 1, kBitRight = 4, kBitLeft = 5, kBitUp = 6, kBitDown = 7;

const char* const kViewChoices[] = {"Native", "16:9", "Adaptive"};

// A model mirroring recomp-ui's test fixture: a "Gameplay" section with a
// Toggle and a StepInt, and a "System" section with an Action.
MenuModel make_fixture() {
    MenuModel m;
    m.sections[0].title = "Gameplay";
    m.sections[1].title = "System";
    m.section_count = 2;

    MenuRow toggle;
    toggle.kind = MenuRowKind::Toggle;
    toggle.label = "Auto-run";
    toggle.section = 0;
    m.rows[0] = toggle;

    MenuRow level;
    level.kind = MenuRowKind::StepInt;
    level.label = "Level";
    level.section = 0;
    level.min_value = 0.0f;
    level.max_value = 5.0f;
    level.step = 1.0f;
    level.int_value = 1;
    m.rows[1] = level;

    MenuRow save;
    save.kind = MenuRowKind::Action;
    save.label = "Save now";
    save.section = 1;
    m.rows[2] = save;

    m.row_count = 3;
    return m;
}

void open_close_toggle() {
    MenuModel m = make_fixture();
    CHECK(!m.open);

    // Navigate while closed does nothing (the open trigger is host policy).
    const MenuResult closed_nav = gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(!closed_nav.opened && !closed_nav.closed);
    CHECK(m.section_index == 0 && !m.editing);

    MenuResult r = gbarecomp::menu_open(m);
    CHECK(r.opened && m.open);
    // Opening again is idempotent.
    r = gbarecomp::menu_open(m);
    CHECK(!r.opened && m.open);
    r = gbarecomp::menu_close(m);
    CHECK(r.closed && !m.open);
    r = gbarecomp::menu_close(m);
    CHECK(!r.closed);
    r = gbarecomp::menu_toggle(m);
    CHECK(r.opened && m.open);
    r = gbarecomp::menu_toggle(m);
    CHECK(r.closed && !m.open);
}

void section_navigation_wraps() {
    MenuModel m = make_fixture();
    // Three sections: with two, +1 and -1 mod 2 are indistinguishable, so a
    // direction bug in the wrap would slip through.
    m.sections[2].title = "Third";
    MenuRow third;
    third.kind = MenuRowKind::Info;
    third.label = "Info";
    third.section = 2;
    m.rows[3] = third;
    m.row_count = 4;
    m.section_count = 3;
    gbarecomp::menu_open(m);

    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.section_index == 1);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.section_index == 2);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.section_index == 0);  // wrap
    gbarecomp::menu_navigate(m, MenuInput::Up);
    CHECK(m.section_index == 2);  // wrap back
    CHECK(!m.editing);
}

void drill_in_and_back() {
    MenuModel m = make_fixture();
    // A third row in section 0 so the row wrap is direction-sensitive too.
    MenuRow third;
    third.kind = MenuRowKind::Info;
    third.label = "Info";
    third.section = 0;
    m.rows[3] = third;
    m.row_count = 4;
    gbarecomp::menu_open(m);

    gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(m.editing);
    CHECK(m.section_index == 0);
    CHECK(m.row_index == 0);

    // Inside a section, Up/Down wrap over that section's rows.
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 1);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 2);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 0);  // wrap within the section
    gbarecomp::menu_navigate(m, MenuInput::Up);
    CHECK(m.row_index == 2);  // wrap back

    // Back leaves the section first, then closes.
    gbarecomp::menu_navigate(m, MenuInput::Back);
    CHECK(!m.editing && m.open);
    CHECK(m.row_index == 0);
    gbarecomp::menu_navigate(m, MenuInput::Back);
    CHECK(!m.open);
}

void skip_disabled_and_invisible() {
    MenuModel m = make_fixture();
    // Row 1 (Level) disabled; row 0 invisible: nothing selectable in section
    // 0, so it is skipped as a whole.
    m.rows[0].visible = false;
    m.rows[1].enabled = false;
    gbarecomp::menu_open(m);

    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.section_index == 1);  // section 0 has nothing selectable
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.section_index == 1);  // only one selectable section: stays

    gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(m.editing && m.row_index == 0);  // section 1 has one row (global 2)

    // Re-enable row 1: entering section 0 lands on it (row 0 stays invisible).
    m.rows[1].enabled = true;
    gbarecomp::menu_navigate(m, MenuInput::Back);
    gbarecomp::menu_navigate(m, MenuInput::Up);
    CHECK(m.section_index == 0);
    gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(m.editing && m.row_index == 1);

    // Down wraps over selectables only (row 0 invisible).
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 1);
}

void values_toggle_step_choice() {
    MenuModel m = make_fixture();
    MenuRow choice;
    choice.kind = MenuRowKind::Choice;
    choice.label = "View";
    choice.section = 0;
    choice.choices = kViewChoices;
    choice.choice_count = 3;
    choice.int_value = 0;
    m.rows[3] = choice;

    MenuRow flt;
    flt.kind = MenuRowKind::StepFloat;
    flt.label = "Gamma";
    flt.section = 0;
    flt.min_value = 0.0f;
    flt.max_value = 2.0f;
    flt.step = 0.25f;
    flt.float_value = 1.0f;
    m.rows[4] = flt;

    MenuRow info;
    info.kind = MenuRowKind::Info;
    info.label = "Version 1.0";
    info.section = 0;
    m.rows[5] = info;

    m.row_count = 6;
    gbarecomp::menu_open(m);
    gbarecomp::menu_navigate(m, MenuInput::Confirm);  // enter section 0

    // Toggle: Confirm flips; Left/Right flip too (direction-independent,
    // ported from recomp-ui).
    MenuResult r = gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(r.changed_row == 0 && m.rows[0].int_value == 1);
    r = gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(r.changed_row == 0 && m.rows[0].int_value == 0);

    // StepInt: Right/Left step by `step`, clamped at min/max.
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 1);
    r = gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(r.changed_row == 1 && m.rows[1].int_value == 2);
    for (int i = 0; i < 10; ++i) gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(m.rows[1].int_value == 5);  // clamped at max
    for (int i = 0; i < 10; ++i) gbarecomp::menu_navigate(m, MenuInput::Left);
    CHECK(m.rows[1].int_value == 0);  // clamped at min
    // At the clamp, no change is reported.
    r = gbarecomp::menu_navigate(m, MenuInput::Left);
    CHECK(r.changed_row == -1);

    // Choice: Right cycles with wrap; Left wraps back.
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 2);  // within-section index (global row 3)
    gbarecomp::menu_navigate(m, MenuInput::Left);
    CHECK(m.rows[3].int_value == 2);  // 0 -> 2 (wrap)
    r = gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(r.changed_row == 3 && m.rows[3].int_value == 0);  // 2 -> 0 (wrap)

    // StepFloat: steps by the float step, clamped.
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 3);  // within-section index (global row 4)
    r = gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(r.changed_row == 4 && m.rows[4].float_value == 1.25f);
    for (int i = 0; i < 10; ++i) gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(m.rows[4].float_value == 2.0f);  // clamped at max

    // Info: nothing happens, ever.
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(m.row_index == 4);  // within-section index (global row 5)
    r = gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(r.changed_row == -1 && r.action_row == -1);
    r = gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(r.changed_row == -1);

    // Action: Confirm fires it; Left/Right do not.
    gbarecomp::menu_navigate(m, MenuInput::Back);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    gbarecomp::menu_navigate(m, MenuInput::Confirm);  // enter section 1
    CHECK(m.editing && m.row_index == 0);  // section 1 has one row (global 2)
    r = gbarecomp::menu_navigate(m, MenuInput::Right);
    CHECK(r.action_row == -1);
    r = gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(r.action_row == 2 && r.changed_row == -1);

    // Disabled rows are never selected or adjusted.
    MenuModel d = make_fixture();
    d.rows[1].enabled = false;  // Level disabled
    gbarecomp::menu_open(d);
    gbarecomp::menu_navigate(d, MenuInput::Confirm);  // enter -> row 0
    CHECK(d.row_index == 0);
    gbarecomp::menu_navigate(d, MenuInput::Down);  // skips the disabled row
    CHECK(d.row_index == 0);
    d.rows[0].enabled = false;  // now nothing in section 0 is selectable
    gbarecomp::menu_navigate(d, MenuInput::Back);
    const MenuResult nr = gbarecomp::menu_navigate(d, MenuInput::Confirm);
    CHECK(!d.editing && nr.changed_row == -1);
}

void recomp_ui_port_flow() {
    // The exact scenario flow of recomp-ui's runtime_ui_test.c, in the V2
    // model's vocabulary: enter, toggle, step, leave, next section, action.
    MenuModel m = make_fixture();
    gbarecomp::menu_open(m);
    gbarecomp::menu_navigate(m, MenuInput::Confirm);  // enter "Gameplay"
    MenuResult r = gbarecomp::menu_navigate(m, MenuInput::Confirm);  // toggle ON
    CHECK(r.changed_row == 0 && m.rows[0].int_value == 1);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    r = gbarecomp::menu_navigate(m, MenuInput::Right);  // level 1 -> 2
    CHECK(r.changed_row == 1 && m.rows[1].int_value == 2);
    gbarecomp::menu_navigate(m, MenuInput::Back);
    gbarecomp::menu_navigate(m, MenuInput::Down);     // next section
    gbarecomp::menu_navigate(m, MenuInput::Confirm);  // enter "System"
    r = gbarecomp::menu_navigate(m, MenuInput::Confirm);  // action
    CHECK(r.action_row == 2);
}

void values_and_helpers() {
    MenuModel m = make_fixture();

    CHECK(gbarecomp::menu_value_int(m, 1) == 1);
    gbarecomp::menu_set_value_int(m, 1, 4);
    CHECK(gbarecomp::menu_value_int(m, 1) == 4);
    gbarecomp::menu_set_value_float(m, 1, 2.5f);
    CHECK(gbarecomp::menu_value_float(m, 1) == 2.5f);
    gbarecomp::menu_set_value_int(m, 0, 1);
    CHECK(gbarecomp::menu_value_int(m, 0) == 1);

    // Out-of-range row indices are safe and report nothing.
    CHECK(gbarecomp::menu_value_int(m, -1) == 0);
    CHECK(gbarecomp::menu_value_int(m, 99) == 0);

    // Section helpers: storage order, global row mapping.
    CHECK(gbarecomp::menu_section_row_count(m, 0) == 2);
    CHECK(gbarecomp::menu_section_row_count(m, 1) == 1);
    CHECK(gbarecomp::menu_section_row(m, 0, 0) == 0);
    CHECK(gbarecomp::menu_section_row(m, 0, 1) == 1);
    CHECK(gbarecomp::menu_section_row(m, 1, 0) == 2);
    CHECK(gbarecomp::menu_section_row(m, 1, 1) == -1);

    gbarecomp::menu_open(m);
    CHECK(gbarecomp::menu_selected_row(m) == -1);  // top level: no row selected
    gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(gbarecomp::menu_selected_row(m) == 0);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    CHECK(gbarecomp::menu_selected_row(m) == 1);
    gbarecomp::menu_navigate(m, MenuInput::Back);
    gbarecomp::menu_navigate(m, MenuInput::Down);
    gbarecomp::menu_navigate(m, MenuInput::Confirm);
    CHECK(gbarecomp::menu_selected_row(m) == 2);
}

void pointer_select_and_activate() {
    MenuModel m = make_fixture();
    gbarecomp::menu_open(m);

    PointerState ps;
    ps.pressed = true;
    ps.x = 10.0f;
    ps.y = 20.0f;
    (void)ps;

    // Press on the Action row (global 2): selects it and drills into its
    // section, no activation yet.
    MenuPointer p;
    p.row = 2;
    p.pressed = true;
    MenuResult r = gbarecomp::menu_pointer(m, p);
    CHECK(r.action_row == -1 && r.changed_row == -1);
    CHECK(m.editing && m.section_index == 1 && m.row_index == 0);
    CHECK(gbarecomp::menu_selected_row(m) == 2);

    // Pressing the already-selected row activates it.
    r = gbarecomp::menu_pointer(m, p);
    CHECK(r.action_row == 2);

    // Press on the Toggle row: selects; second press flips.
    p.row = 0;
    gbarecomp::menu_pointer(m, p);
    CHECK(gbarecomp::menu_selected_row(m) == 0);
    r = gbarecomp::menu_pointer(m, p);
    CHECK(r.changed_row == 0 && m.rows[0].int_value == 1);

    // Disabled/invisible rows and out-of-range hits are ignored.
    m.rows[0].enabled = false;
    r = gbarecomp::menu_pointer(m, p);
    CHECK(r.changed_row == -1 && r.action_row == -1);
    p.row = 99;
    r = gbarecomp::menu_pointer(m, p);
    CHECK(r.changed_row == -1 && r.action_row == -1);
    m.rows[0].enabled = true;
    m.rows[0].visible = false;
    p.row = 0;
    r = gbarecomp::menu_pointer(m, p);
    CHECK(r.changed_row == -1);

    // Closed menu: the pointer does nothing.
    gbarecomp::menu_close(m);
    p.row = 1;
    r = gbarecomp::menu_pointer(m, p);
    CHECK(r.changed_row == -1 && !m.editing);
}

void status_toast_backdrop() {
    MenuModel m = make_fixture();

    gbarecomp::menu_set_status(m, "Saved");
    CHECK(std::strcmp(m.status, "Saved") == 0);
    CHECK(m.status_frames == 90);  // the host decrements; no clock in the model

    gbarecomp::menu_set_toast(m, "Achievement", "You found the menu");
    CHECK(std::strcmp(m.toast_title, "Achievement") == 0);
    CHECK(std::strcmp(m.toast_body, "You found the menu") == 0);
    CHECK(m.toast_frames == 180);

    // Long strings truncate safely; null clears.
    gbarecomp::menu_set_status(m, "0123456789012345678901234567890123456789012345678901234567890");
    CHECK(std::strlen(m.status) < sizeof(m.status));
    gbarecomp::menu_set_status(m, nullptr);
    CHECK(m.status[0] == '\0' && m.status_frames == 0);
    gbarecomp::menu_set_toast(m, nullptr, nullptr);
    CHECK(m.toast_title[0] == '\0' && m.toast_body[0] == '\0' && m.toast_frames == 0);

    gbarecomp::menu_set_backdrop(m, 0.5f, 0.25f);
    CHECK(m.dim == 0.5f && m.opacity == 0.25f);
    gbarecomp::menu_set_backdrop(m, -1.0f, 9.0f);  // clamped 0..1
    CHECK(m.dim == 0.0f && m.opacity == 1.0f);
}

void input_mapping() {
    MenuInput in = MenuInput::Up;

    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitUp), &in) &&
          in == MenuInput::Up);
    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitDown), &in) &&
          in == MenuInput::Down);
    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitLeft), &in) &&
          in == MenuInput::Left);
    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitRight), &in) &&
          in == MenuInput::Right);
    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitA), &in) &&
          in == MenuInput::Confirm);
    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitB), &in) &&
          in == MenuInput::Back);

    // Nothing pressed -> false, out untouched.
    in = MenuInput::Left;
    CHECK(!gbarecomp::menu_input_from_keyinput(kAllUp, &in));
    CHECK(in == MenuInput::Left);
    // Null out is safe.
    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitA), nullptr));

    // Priority is documented and stable: Back > Confirm > Up > Down > Left >
    // Right.
    CHECK(gbarecomp::menu_input_from_keyinput(
              static_cast<std::uint16_t>(pressed(kBitA) & pressed(kBitB)), &in) &&
          in == MenuInput::Back);
    CHECK(gbarecomp::menu_input_from_keyinput(
              static_cast<std::uint16_t>(pressed(kBitUp) & pressed(kBitDown)),
              &in) &&
          in == MenuInput::Up);

    // End to end: a GBA A press maps to a toggle activation.
    MenuModel m = make_fixture();
    gbarecomp::menu_open(m);
    gbarecomp::menu_navigate(m, MenuInput::Confirm);  // enter
    CHECK(gbarecomp::menu_input_from_keyinput(pressed(kBitA), &in));
    const MenuResult r = gbarecomp::menu_navigate(m, in);
    CHECK(r.changed_row == 0);
}

}  // namespace

int main() {
    open_close_toggle();
    section_navigation_wraps();
    drill_in_and_back();
    skip_disabled_and_invisible();
    values_toggle_step_choice();
    recomp_ui_port_flow();
    values_and_helpers();
    pointer_select_and_activate();
    status_toast_backdrop();
    input_mapping();

    std::printf("menu_model tests: PASS\n");
    return 0;
}
