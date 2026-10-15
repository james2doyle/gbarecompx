// host_backend_factory.h — selects the WindowBackend implementation for a build.
//
// The only place that knows which backend is compiled in. HostWindow calls
// host_make_backend() and stays ignorant of SDL vs raylib, so adding a platform
// never reaches host_window.cpp.
//
// Selection is compile-time (GBARECOMP_USE_RAYLIB, default OFF). A later
// enhancement will allow runtime selection for a single binary; the seam already
// supports it, only this function would change.

#pragma once

#include "window_backend.h"

namespace gbarecomp {

// The SDL backend. Defined in host_window.cpp, where the concrete Backend lives in
// an anonymous namespace and cannot be named from elsewhere.
WindowBackend* host_make_sdl_backend();

// The backend this build was configured with.
WindowBackend* host_make_backend();

// True when this build selected the raylib backend. Useful for logging and for
// tests that assert the expected backend is wired in.
bool host_using_raylib_backend();

}  // namespace gbarecomp
