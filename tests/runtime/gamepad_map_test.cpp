#include "gamepad_map.h"
#include "glfw_pad_quirks.h"

#include <cassert>
#include <cstdio>

using namespace gbarecomp;

int main() {
    // stick_to_dpad: inside deadzone nothing registers.
    assert(stick_to_dpad(0, 0) == 0);
    assert(stick_to_dpad(kStickDeadzone, 0) == 0);          // exactly at dz
    assert(stick_to_dpad(kStickDeadzone + 1, 0) == kGbaKeyRight);
    assert(stick_to_dpad(-(kStickDeadzone + 1), 0) == kGbaKeyLeft);
    assert(stick_to_dpad(0, -(kStickDeadzone + 1)) == kGbaKeyUp);
    assert(stick_to_dpad(0, kStickDeadzone + 1) == kGbaKeyDown);

    // Diagonals set two bits naturally.
    const int far = kStickDeadzone * 2;
    assert(stick_to_dpad(far, far) == (kGbaKeyRight | kGbaKeyDown));
    assert(stick_to_dpad(-far, -far) == (kGbaKeyLeft | kGbaKeyUp));

    // Custom deadzone is honored (proves the parameter is live, not baked).
    assert(stick_to_dpad(500, 0, 100) == kGbaKeyRight);
    assert(stick_to_dpad(500, 0, 1000) == 0);

    // gamepad_to_keyinput returns ACTIVE-LOW; nothing pressed == 0x03FF.
    GamepadSnapshot idle;
    assert(gamepad_to_keyinput(idle) == kGbaKeyAll);

    // Physical buttons map to the documented bits.
    idle.buttons = kGbaKeyA;
    assert(gamepad_to_keyinput(idle) ==
           static_cast<std::uint16_t>(~kGbaKeyA & kGbaKeyAll));

    GamepadSnapshot combo;
    combo.buttons = kGbaKeyStart | kGbaKeySelect;
    assert(gamepad_to_keyinput(combo) ==
           static_cast<std::uint16_t>(~(kGbaKeyStart | kGbaKeySelect) & kGbaKeyAll));

    // Stick is folded in and ORs with physical dpad (additive, issue #14).
    GamepadSnapshot stick;
    stick.stick_x = far;
    assert(gamepad_to_keyinput(stick) ==
           static_cast<std::uint16_t>(~kGbaKeyRight & kGbaKeyAll));

    GamepadSnapshot both;
    both.buttons = kGbaKeyRight;   // physical dpad right...
    both.stick_x = -far;          // ...and stick left
    // Both directions cancel to released (keys &= ~pressed, physical+stick OR).
    const std::uint16_t merged =
        static_cast<std::uint16_t>(both.buttons | stick_to_dpad(both.stick_x, both.stick_y));
    assert((merged & (kGbaKeyLeft | kGbaKeyRight)) ==
           (kGbaKeyLeft | kGbaKeyRight));
    assert(gamepad_to_keyinput(both) ==
           static_cast<std::uint16_t>(~merged & kGbaKeyAll));

    // A single R press clears only bit 8.
    GamepadSnapshot r;
    r.buttons = kGbaKeyR;
    assert(gamepad_to_keyinput(r) == (0x03FFu & ~(1u << 8)));

    // GLFW-vs-SDL mapping quirks. Synthetic indices, no hardware: the
    // evidenced device is the 8BitDo Lite 2 X-input GUID, whose DB entries
    // disagree (SDL a:b0, GLFW a:b1).
    constexpr const char* kLite2 =
        "05000000c82d00001251000000010000";
    assert(!glfw_pad_needs_ab_swap(nullptr));
    assert(!glfw_pad_needs_ab_swap(""));
    assert(!glfw_pad_needs_ab_swap("03000000c82d00001251000000000000"));
    assert(glfw_pad_needs_ab_swap(kLite2));
    // The quirked pairs swap; everything else (incl. dpad, shoulders,
    // start, out-of-range) is identity, so unknown buttons can never be
    // silently remapped.
    assert(quirk_glfw_button(kLite2, 0) == 1);
    assert(quirk_glfw_button(kLite2, 1) == 0);
    assert(quirk_glfw_button(kLite2, 2) == 3);
    assert(quirk_glfw_button(kLite2, 3) == 2);
    for (int b = 4; b <= 14; ++b) assert(quirk_glfw_button(kLite2, b) == b);
    assert(quirk_glfw_button(kLite2, 99) == 99);
    assert(quirk_glfw_button(nullptr, 0) == 0);
    for (int b = 0; b <= 14; ++b)
        assert(quirk_glfw_button("some-other-pad", b) == b);

    std::printf("gamepad_map tests: PASS\n");
    return 0;
}