#include "safe_insets.h"

#include <cassert>
#include <cstdio>

int main() {
    using gbarecomp::SafeInsets;
    using gbarecomp::parse_safe_insets;

    SafeInsets out{};
    assert(!parse_safe_insets(nullptr, out));
    assert(!parse_safe_insets("1,2,3", out));
    assert(!parse_safe_insets("a,b,c,d", out));

    assert(parse_safe_insets("1,2,3,4", out));
    assert(out.left == 1 && out.top == 2 && out.right == 3 && out.bottom == 4);

    assert(parse_safe_insets("-1,-2,-3,-4", out));
    assert(out.left == 0 && out.top == 0 && out.right == 0 && out.bottom == 0);

    std::printf("safe_insets tests: PASS\n");
    return 0;
}