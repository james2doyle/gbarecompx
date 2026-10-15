#include "touch_pad_state.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    using gbarecomp::touch_pad_should_idle_hide;
    using gbarecomp::touch_pad_initial_visible;

    // initial visibility
    assert(!touch_pad_initial_visible(false, -1, true));
    assert(touch_pad_initial_visible(true, -1, true));
    assert(!touch_pad_initial_visible(true, -1, false));
    assert(touch_pad_initial_visible(true, 1, false));
    assert(!touch_pad_initial_visible(true, 0, true));

    // idle-hide gating
    assert(!touch_pad_should_idle_hide(true, true, true, false, true, 5000, 0, 2));
    assert(!touch_pad_should_idle_hide(true, true, true, false, false, 1000, 0, 2)); // 1s < 2s
    assert(touch_pad_should_idle_hide(true, true, true, false, false, 3000, 0, 2)); // 3s >= 2s
    assert(!touch_pad_should_idle_hide(true, true, true, true, false, 3000, 0, 2)); // already hidden
    assert(!touch_pad_should_idle_hide(true, true, true, false, false, 3000, 0, 0)); // disabled

    // wraparound: (now - last) >= seconds*1000 still holds
    assert(touch_pad_should_idle_hide(true, true, true, false, false,
                                     100u, 0xFFFFFFFFu - 1000u, 2) == false);

    std::printf("touch_pad_state tests: PASS\n");
    return 0;
}