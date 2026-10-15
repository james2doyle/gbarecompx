// host_backend_factory.cpp — the single place that knows which backend a build
// uses. Kept out of host_window.cpp so that file never learns about raylib.

#include "host_backend_factory.h"

#if defined(GBARECOMP_USE_RAYLIB)
#include "raylib_backend.h"
#endif

namespace gbarecomp {

WindowBackend* host_make_backend() {
#if defined(GBARECOMP_USE_RAYLIB)
    return new RaylibBackend{};
#else
    // The stub compile-check build (raylib OFF): no window backend at all.
    // Phase 4b retired GBARECOMP_HAVE_SDL2 as a selection input.
    return nullptr;
#endif
}

bool host_using_raylib_backend() {
#if defined(GBARECOMP_USE_RAYLIB)
    return true;
#else
    return false;
#endif
}

}  // namespace gbarecomp
