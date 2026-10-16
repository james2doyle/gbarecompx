// raygui_impl.cpp — the single raygui implementation TU (framework).
//
// raygui.h is single-header; RAYGUI_IMPLEMENTATION must live in exactly one
// TU. It lived in launcher_raygui.cpp (launcher-only); the raygui menu engine
// needs it too, so the implementation moved here and every consumer
// (launcher, menu engine) includes raygui.h declarations-only.
//
// Compiled into gbarecomp_raylib_backend only when raylib is enabled — the
// vendored raygui.h includes raylib.h unconditionally.
//
// Icons are compiled out (RAYGUI_NO_ICONS): the menu and launcher draw text
// only (the spinner falls back to its "<"/">" text buttons), and the icon
// atlas is dead weight in the binary.
#if !defined(GBARECOMP_USE_RAYLIB)
#error "raygui_impl.cpp is only meaningful in raylib builds"
#endif

#define RAYGUI_NO_ICONS
#define RAYGUI_IMPLEMENTATION
#include "raygui/raygui.h"
