// host_backend_factory.h — the backend this build was configured with.
//
// The only place that knows the selection. HostWindow calls host_make_backend()
// and stays ignorant of it, so a future runtime selector never reaches
// host_window.cpp.
//
// Selection is compile-time and binary (Phase 4b retired GBARECOMP_USE_RAYLIB
// as a *backend* flag): raylib ON builds the product backend; raylib OFF is the
// stub compile-check — no window backend at all, host_make_backend() returns
// nullptr.

#pragma once

#include "window_backend.h"

namespace gbarecomp {

// The backend this build was configured with (nullptr in the stub build).
WindowBackend* host_make_backend();

// True when this build selected the raylib backend. Useful for logging and for
// tests that assert the expected backend is wired in.
bool host_using_raylib_backend();

}  // namespace gbarecomp
