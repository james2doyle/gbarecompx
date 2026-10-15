#include "assist_pad_binding.h"

#include <cassert>
#include <cstdio>

using namespace gbarecomp;

int main() {
    // Encoding round-trips. The historical defaults are triggers encoded
    // against SDL axis codes; these must keep producing the same integers so
    // existing keybinds.ini / config.ini files stay valid.
    // SDL_CONTROLLER_AXIS_TRIGGERLEFT == 4, TRIGGERRIGHT == 5.
    assert(assist_pad_axis(4, true) == 109);
    assert(assist_pad_axis(5, true) == 111);
    // And the on-disk format is stable for other axes (odd half = positive).
    assert(assist_pad_axis(0, true) == 101);
    assert(assist_pad_axis(0, false) == 100);
    assert(assist_pad_axis(1, true) == 103);

    // Decode analog.
    AssistAxisBinding ab{};
    assert(decode_assist_pad_axis(109, &ab));
    assert(ab.axis_code == 4 && ab.positive);

    assert(decode_assist_pad_axis(111, &ab));
    assert(ab.axis_code == 5 && ab.positive);

    assert(decode_assist_pad_axis(101, &ab));
    assert(ab.axis_code == 0 && ab.positive);

    assert(decode_assist_pad_axis(100, &ab));
    assert(ab.axis_code == 0 && !ab.positive);

    // The encoder's documented invariants decode consistently for a sweep.
    for (int code = 0; code < 16; ++code) {
        for (int pos = 0; pos < 2; ++pos) {
            const int enc = assist_pad_axis(code, pos != 0);
            AssistAxisBinding got{};
            assert(decode_assist_pad_axis(enc, &got));
            assert(got.axis_code == code);
            assert(got.positive == (pos != 0));
        }
    }

    // Digital vs analog discrimination.
    assert(!decode_assist_pad_axis(0, &ab));    // unbound
    assert(!decode_assist_pad_axis(1, &ab));    // button, not axis
    assert(!decode_assist_pad_axis(50, &ab));   // button, not axis

    int btn = -1;
    assert(decode_assist_pad_button(1, &btn) && btn == 0);
    assert(decode_assist_pad_button(12, &btn) && btn == 11);
    assert(!decode_assist_pad_button(0, &btn));       // unbound
    assert(!decode_assist_pad_button(100, &btn));     // axis, not button
    assert(!decode_assist_pad_button(108, &btn));     // axis, not button

    // Half-axis threshold logic (positive vs negative selection).
    assert(assist_axis_down(16000, true, 16000));    // exactly at threshold
    assert(assist_axis_down(20000, true, 16000));
    assert(!assist_axis_down(15999, true, 16000));
    assert(!assist_axis_down(-20000, true, 16000));  // wrong half

    assert(assist_axis_down(-16000, false, 16000));
    assert(assist_axis_down(-20000, false, 16000));
    assert(!assist_axis_down(-15999, false, 16000));
    assert(!assist_axis_down(20000, false, 16000));  // wrong half

    // Null out-params are rejected, not dereferenced.
    assert(!decode_assist_pad_axis(108, nullptr));
    assert(!decode_assist_pad_button(1, nullptr));

    std::printf("assist_pad_binding tests: PASS\n");
    return 0;
}