#include "window_backend.h"

#include <cassert>
#include <cstdio>

int main() {
    gbarecomp::WindowBackendOpenParams p;
    assert(p.base_w == 240);
    assert(p.base_h == 160);
    std::printf("window_backend scaffold tests: PASS\n");
    return 0;
}