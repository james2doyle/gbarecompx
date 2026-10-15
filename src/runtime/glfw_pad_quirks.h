#pragma once

// Per-device gamepad mapping quirks: where GLFW's bundled mapping database
// disagrees with SDL's about the SAME physical pad.
//
// WHY THIS EXISTS
// ---------------
// Both stacks number standard buttons identically (0=A/south... in their own
// frame), but their bundled mapping DBs can bind different RAW indices to
// those numbers for the same device. Verified live on an 8BitDo Lite 2
// (X-input mode), whose two DB entries say opposite things:
//
//   SDL  (guid 05009075c82d00001251000000010000): a:b0, b:b1, x:b3, y:b4
//   GLFW (guid 05000000c82d00001251000000010000): a:b1, b:b0, x:b4, y:b3
//
// (GLFW zeroes VID/PID version bytes in its GUIDs; same device.) Net effect:
// the south face button is SDL-A but GLFW-B, east is SDL-B but GLFW-A. The
// SDL backend is this project's reference behaviour, so the raylib backend
// remaps this GUID's pairs to agree with it. X/Y swap too (same mirror),
// even though GBA games ignore them: assist-pad bindings address canonical
// X/Y, and those must also mean the same physical button on both backends.
//
// Pure (string compare only), so the whole thing is unit-testable with
// synthetic indices and no hardware.

#include <cstring>

namespace gbarecomp {

// GLFW standard button indices needing no introduction (must match glfw3.h;
// the raylib-compile gate builds every consumer, so drift breaks the build).
constexpr int kGlfwStdA = 0;
constexpr int kGlfwStdB = 1;
constexpr int kGlfwStdX = 2;
constexpr int kGlfwStdY = 3;

// Devices whose GLFW mapping mirrors A/B (and X/Y) relative to SDL's entry
// for the same pad. GUIDs are GLFW-spelled (see above). Extend ONLY with both
// mapping strings in hand and a live two-stack probe to prove them.
inline bool glfw_pad_needs_ab_swap(const char* guid) {
    if (!guid || !*guid) return false;
    // 8BitDo Lite 2, X-input mode (the evidenced device).
    if (std::strcmp(guid, "05000000c82d00001251000000010000") == 0) return true;
    return false;
}

// Remap a GLFW standard button index through the quirk table. Identity for
// every device except the listed ones, so an unknown future pad can never be
// silently remapped -- worst case it behaves exactly as GLFW maps it.
inline int quirk_glfw_button(const char* guid, int button) {
    if (!glfw_pad_needs_ab_swap(guid)) return button;
    switch (button) {
        case kGlfwStdA: return kGlfwStdB;
        case kGlfwStdB: return kGlfwStdA;
        case kGlfwStdX: return kGlfwStdY;
        case kGlfwStdY: return kGlfwStdX;
        default: return button;
    }
}

}  // namespace gbarecomp
