#include "audio_convert.h"

#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    using gbarecomp::s16_to_float;
    using gbarecomp::kS16ToFloatScale;

    assert(s16_to_float(0) == 0.0f);
    assert(s16_to_float(32767) > 0.999f && s16_to_float(32767) <= 1.0f);
    // INT16_MIN maps to exactly -1.0 (no wrap past the representable range).
    assert(s16_to_float(std::numeric_limits<std::int16_t>::min()) == -1.0f);
    assert(s16_to_float(std::numeric_limits<std::int16_t>::max()) < 1.0f);

    // Monotonic and sign-preserving.
    assert(s16_to_float(-1000) < s16_to_float(-999));
    assert(s16_to_float(1000) > s16_to_float(999));
    assert(s16_to_float(-1) < 0.0f && s16_to_float(1) > 0.0f);

    // Nothing outside [-1, 1] for the whole domain.
    for (int s = -32768; s <= 32767; s += 97) {
        const float f = s16_to_float(static_cast<std::int16_t>(s));
        assert(f >= -1.0f && f <= 1.0f);
    }

    assert(kS16ToFloatScale > 0.0f);

    std::printf("audio_convert tests: PASS\n");
    return 0;
}
