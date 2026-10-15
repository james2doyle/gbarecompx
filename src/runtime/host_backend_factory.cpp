// host_backend_factory.cpp — the single place that knows which backend a build
// uses. Kept out of host_window.cpp so that file never learns about raylib.

#include "host_backend_factory.h"

#include "host_window.h"  // host_make_sdl_backend()

#if defined(GBARECOMP_USE_RAYLIB)
#include "raylib_backend.h"
#endif

namespace gbarecomp {

WindowBackend* host_make_backend() {
#if defined(GBARECOMP_USE_RAYLIB)
    return new RaylibBackend{};
#elif defined(GBARECOMP_HAVE_SDL2)
    return host_make_sdl_backend();
#else
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
