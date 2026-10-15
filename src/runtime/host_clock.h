// host_clock.h — the backend's time source, behind one tiny interface.
//
// The real implementation uses raylib's GetTime() in raylib builds (seconds
// since InitWindow — the same source the backend used before this seam) and a
// steady clock in stub builds. Tests inject a fake for deterministic host
// loops (the Phase 2-tail fakes; §5.2 item 3).
//
// Raylib-free by design: the interface is includable from any build.
#pragma once

#include <cstdint>

namespace gbarecomp {

struct Clock {
    virtual ~Clock() = default;
    // Seconds since the backend's time origin (raylib: GetTime()).
    virtual double seconds() const = 0;
    // Milliseconds since the same origin, truncated to 32 bits — the input
    // snapshot's now_ms field (mirrors the previous GetTime()*1000 cast).
    std::uint32_t millis() const {
        return static_cast<std::uint32_t>(seconds() * 1000.0);
    }
};

// The real process-wide clock.
Clock& real_host_clock();

}  // namespace gbarecomp
