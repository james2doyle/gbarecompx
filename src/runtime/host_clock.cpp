// host_clock.cpp — the real Clock (see host_clock.h).
#include "host_clock.h"

#include <chrono>

#if defined(GBARECOMP_USE_RAYLIB)
#include <raylib.h>
#endif

namespace gbarecomp {
namespace {

struct RealHostClock final : Clock {
    double seconds() const override {
#if defined(GBARECOMP_USE_RAYLIB)
        return GetTime();
#else
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
#endif
    }
};

}  // namespace

Clock& real_host_clock() {
    static RealHostClock clock;
    return clock;
}

}  // namespace gbarecomp
