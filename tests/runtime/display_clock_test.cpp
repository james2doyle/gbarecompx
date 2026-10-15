#include "display_clock.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    using gbarecomp::display_refresh_period_ns;
    using gbarecomp::display_refresh_changed;

    assert(display_refresh_period_ns(0) == 0);
    assert(display_refresh_period_ns(-60) == 0);
    assert(display_refresh_period_ns(60) == 1000000000ULL / 60);
    assert(display_refresh_period_ns(120) == 1000000000ULL / 120);
    assert(display_refresh_changed(60, 0));
    assert(!display_refresh_changed(60, 1000000000ULL / 60));

    std::printf("display_clock tests: PASS\n");
    return 0;
}