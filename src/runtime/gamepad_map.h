#pragma once

// Host-neutral gamepad contract shared by every WindowBackend (SDL, raylib) and
// by the GBA input mapper. Backends translate their native gamepad API into a
// GamepadSnapshot and call gamepad_to_keyinput(); they never touch GBA bits or
// deadzone policy themselves.
//
// The design intent: the button->GBA mapping and the stick->dpad rule are ONE
// verified piece of logic used by all platforms, so a backend swap cannot
// remap Start<->Select or disagree on deadzone. Only the ~40 lines that read a
// native gamepad are platform-specific.

#include <cstdint>

namespace gbarecomp {

// GBA KEYINPUT is active-low. bit set = RELEASED, bit clear = pressed.
enum GbaKey : std::uint16_t {
    kGbaKeyA = 1u << 0,
    kGbaKeyB = 1u << 1,
    kGbaKeySelect = 1u << 2,
    kGbaKeyStart = 1u << 3,
    kGbaKeyRight = 1u << 4,
    kGbaKeyLeft = 1u << 5,
    kGbaKeyUp = 1u << 6,
    kGbaKeyDown = 1u << 7,
    kGbaKeyR = 1u << 8,
    kGbaKeyL = 1u << 9,
    kGbaKeyDpad = kGbaKeyRight | kGbaKeyLeft | kGbaKeyUp | kGbaKeyDown,
    kGbaKeyAll = 0x03FFu,
};

// One frame of gamepad state, in host-neutral terms. Backends fill `buttons`
// (physical buttons A/B/Select/Start/dpad/R/L, plus the raw stick-derived dpad),
// the left stick axes (raw, backend units), and the two triggers for the Assist
// bindings. Anything not applicable stays zero.
struct GamepadSnapshot {
    // Physical + stick-derived GBA button bits, ACTIVE-HIGH (bit set = pressed).
    std::uint16_t buttons = 0;
    // Left stick, raw signed axis values in the backend's native units.
    int stick_x = 0;
    int stick_y = 0;
    // Optional triggers used by the Assist (rewind / fast-forward) bindings.
    bool trigger_left = false;
    bool trigger_right = false;
};

// Fraction of full-scale deflection before the stick registers a d-pad
// direction. SDL runs -32768..32767 and raylib runs -32768..32767 too, so a
// single absolute deadzone works for both. 25% ignores thumb drift while still
// registering well before the physical stop (matches the historical value).
constexpr int kStickDeadzone = 8192;

// Active-high d-pad bits for a stick position. Inside the deadzone nothing
// registers, so stick drift cannot press a direction; a diagonal sets two bits.
// Pure so every backend and the tests share one mapping.
inline std::uint16_t stick_to_dpad(int axis_x, int axis_y, int deadzone = kStickDeadzone) {
    std::uint16_t keys = 0;
    if (axis_x < -deadzone) keys |= kGbaKeyLeft;
    if (axis_x > deadzone) keys |= kGbaKeyRight;
    if (axis_y < -deadzone) keys |= kGbaKeyUp;
    if (axis_y > deadzone) keys |= kGbaKeyDown;
    return keys;
}

// Convert a GamepadSnapshot to the ACTIVE-LOW GBA KEYINPUT register value the
// guest reads. The stick is folded into the d-pad here so backends pass raw axes.
inline std::uint16_t gamepad_to_keyinput(const GamepadSnapshot& pad) {
    const std::uint16_t pressed =
        static_cast<std::uint16_t>(pad.buttons |
                                   stick_to_dpad(pad.stick_x, pad.stick_y));
    return static_cast<std::uint16_t>(~pressed & kGbaKeyAll);
}

}  // namespace gbarecomp