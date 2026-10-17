#pragma once

// android_gamepad.h — Android-level gamepad capture for the raylib backend.
//
// WHY THIS EXISTS (the raylib #5387 device class): raylib's Android backend
// handles gamepad KEY events only when the event's source carries GAMEPAD or
// JOYSTICK and NOT KEYBOARD ("some devices report both", its own comment) —
// and its keyboard fallback table maps every AKEYCODE_BUTTON_* to 0. Devices
// whose gamepad is a KEYBOARD-class HID node lose every button: the events
// fail the guard, then fall through to a table full of zeros. Measured live
// on the vendor-0x2020 AYN Odin (2026-10-17, dumpsys + getevent): Classes
// KEYBOARD | GAMEPAD | JOYSTICK — face/shoulder/menu buttons arrived dead
// while sticks (JOYSTICK-source motion events) kept working.
//
// Upstream fixed the class AFTER our pinned 6.0 SHA (raysan5/raylib 5e14ac5,
// 2025-12-26; refined later as "trust gamepad keycode, not source bits").
// Rather than bump the pin and re-verify the whole stack, this capture owns
// the gamepad truth at the glue level and trusts the KEYCODE, not the source
// bits — the same direction upstream took. The keycode table below is
// verified against NDK r28b android/keycodes.h and against a live getevent
// capture + the device's Vendor_2020_Product_0111.kl (A=BTN_GAMEPAD→BUTTON_A,
// B=BTN_EAST→BUTTON_B, X=BTN_NORTH→BUTTON_X, Y=BTN_WEST→BUTTON_Y, L1/R1,
// L2/R2 dual-reporting with ABS_BRAKE/ABS_GAS, Select/Start, d-pad via
// ABS_HAT0X/HAT0Y only).
//
// OBSERVE, DON'T CONSUME: the hook returns 0 (fall through) for everything,
// so raylib keeps its behaviors — BACK→KEY_BACK (the Back gesture that opens
// the in-game menu), the launcher's gamepad nav (IsGamepadAvailable), touch.
// This state is ADDITIONAL truth: the backend's Android gamepad block reads
// it instead of raylib's gamepad state.
//
// The GBA mapping itself stays in gamepad_map.h (gamepad_to_keyinput +
// stick_to_dpad — the one verified piece of logic). This header only turns
// Android events into canonical PadButton/PadAxis levels and a raw
// GamepadSnapshot's inputs; the backend owns a state, feeds it, and builds
// the snapshot exactly like the desktop GLFW path does. Left stick → d-pad
// is inherited from there (25% deadzone), not re-implemented.

#include "input_translate.h"  // kPadButton / kPadAxis canonical ids

#include <atomic>
#include <cstdint>

namespace gbarecomp {

// Android keycodes, named locally: <android/keycodes.h> is NDK-only and this
// table must compile (and be tested) on desktop. Values verified against NDK
// r28b android/keycodes.h (BACK=4, DPAD_*=19..23, BUTTON_A..MODE=96..110).
enum AndroidKeycode : int {
    kAKeyCodeBack = 4,
    kAKeyCodeDpadUp = 19,
    kAKeyCodeDpadDown = 20,
    kAKeyCodeDpadLeft = 21,
    kAKeyCodeDpadRight = 22,
    kAKeyCodeDpadCenter = 23,
    kAKeyCodeButtonA = 96,
    kAKeyCodeButtonB = 97,
    kAKeyCodeButtonC = 98,
    kAKeyCodeButtonX = 99,
    kAKeyCodeButtonY = 100,
    kAKeyCodeButtonZ = 101,
    kAKeyCodeButtonL1 = 102,
    kAKeyCodeButtonR1 = 103,
    kAKeyCodeButtonL2 = 104,
    kAKeyCodeButtonR2 = 105,
    kAKeyCodeButtonThumbl = 106,
    kAKeyCodeButtonThumbr = 107,
    kAKeyCodeButtonStart = 108,
    kAKeyCodeButtonSelect = 109,
    kAKeyCodeButtonMode = 110,
};

// Android motion-axis ids (AMOTION_EVENT_AXIS_*), verified against NDK r28b
// android/input.h (X=0, Y=1, Z=11, RZ=14, HAT_X=15, HAT_Y=16, LTRIGGER=17,
// RTRIGGER=18, GAS=22, BRAKE=23). Sticks are the joystick-convention X/Y
// (left) and Z/RZ (right); triggers arrive as 0..1 GAS/BRAKE on standard
// pads (the Odin measured) or -1..1 LTRIGGER/RTRIGGER on some others.
enum AndroidMotionAxis : int {
    kAMotionAxisX = 0,
    kAMotionAxisY = 1,
    kAMotionAxisZ = 11,
    kAMotionAxisRZ = 14,
    kAMotionAxisHatX = 15,
    kAMotionAxisHatY = 16,
    kAMotionAxisLTrigger = 17,
    kAMotionAxisRTrigger = 18,
    kAMotionAxisGas = 22,
    kAMotionAxisBrake = 23,
};

// Pure: Android keycode → canonical PadButton, or -1 when there is no
// canonical slot. LABEL-wise for the face cluster (the pad's A is GBA A, the
// pad's B is GBA B), matching the desktop GLFW table's semantics. BACK is
// deliberately unmapped — it is the platform menu gesture, never a game key.
inline int android_keycode_to_pad(int keycode) {
    switch (keycode) {
        case kAKeyCodeButtonA: return kPadA;
        case kAKeyCodeButtonB: return kPadB;
        case kAKeyCodeButtonX: return kPadX;
        case kAKeyCodeButtonY: return kPadY;
        case kAKeyCodeButtonL1: return kPadLeftBumper;
        case kAKeyCodeButtonR1: return kPadRightBumper;
        case kAKeyCodeButtonL2: return kPadLeftTrigger;
        case kAKeyCodeButtonR2: return kPadRightTrigger;
        case kAKeyCodeButtonThumbl: return kPadLeftThumb;
        case kAKeyCodeButtonThumbr: return kPadRightThumb;
        case kAKeyCodeButtonStart: return kPadStart;
        case kAKeyCodeButtonSelect: return kPadBack;
        case kAKeyCodeButtonMode: return kPadGuide;
        case kAKeyCodeDpadUp: return kPadDpadUp;
        case kAKeyCodeDpadDown: return kPadDpadDown;
        case kAKeyCodeDpadLeft: return kPadDpadLeft;
        case kAKeyCodeDpadRight: return kPadDpadRight;
        // No canonical slot (Odin's extra face buttons C/Z, dpad CENTER) and
        // the menu contract (BACK): unmap, never collide.
        default: return -1;
    }
}

// Pure: Android motion-axis id → canonical PadAxis, or -1. HAT_X/HAT_Y are
// d-pad polarity (handled by AndroidPadState::hat), not an axis.
inline int android_axis_to_pad(int axis) {
    switch (axis) {
        case kAMotionAxisX: return kPadAxisLeftX;
        case kAMotionAxisY: return kPadAxisLeftY;
        case kAMotionAxisZ: return kPadAxisRightX;
        case kAMotionAxisRZ: return kPadAxisRightY;
        case kAMotionAxisGas: return kPadAxisRightTrigger;
        case kAMotionAxisBrake: return kPadAxisLeftTrigger;
        case kAMotionAxisLTrigger: return kPadAxisLeftTrigger;
        case kAMotionAxisRTrigger: return kPadAxisRightTrigger;
        default: return -1;
    }
}

// Pure: raw Android axis value → the shared backend-native int units
// (±32767), matching the raylib-backed block this replaces. GAS/BRAKE rest
// at 0 and press to +1; the historical transform maps them to -1..1
// ((*2-1)) so the shared assist threshold sees the same shape as SDL's
// 0..32767 triggers. LTRIGGER/RTRIGGER are already -1..1.
inline int android_axis_scaled(int axis, float value) {
    float f = value;
    if (axis == kAMotionAxisGas || axis == kAMotionAxisBrake) f = f * 2.0f - 1.0f;
    return static_cast<int>(f * 32767.0f);
}

// AKeyEvent_getFlags bit (NDK r28b android/input.h:276): the InputDispatcher
// set this when the event was SYNTHESIZED as a fallback for another,
// unhandled key — the Generic.kcm mechanism below.
constexpr int kAKeyEventFlagFallback = 0x400;

// Pure: should the hook CONSUME this KEY event (never shown to raylib)?
//
// Rule 1 — the gamepad-button range (BUTTON_A..BUTTON_MODE, 96..110) is
// consumed ALWAYS, source bits notwithstanding. Reason: raylib leaves these
// events unhandled — its gamepad branch is skipped when the source also
// carries KEYBOARD (#5387), its keyboard table maps every BUTTON_* to 0, and
// the callback then returns 0. The InputDispatcher answers an unhandled key
// by synthesizing the device KeyCharacterMap's FALLBACK key (verified
// verbatim on the Odin's Generic.kcm): BUTTON_B→BACK (the user-visible
// menu-on-B report, arriving ~36ms after the raw BUTTON_B, auto-repeating
// while held), BUTTON_A→DPAD_CENTER, BUTTON_X→DEL (a SECOND menu path
// through the touch-emulated Backspace arm), BUTTON_Y→SPACE,
// BUTTON_START→DPAD_CENTER, BUTTON_SELECT→MENU, BUTTON_MODE→HOME. Marking
// the event handled at the hook kills the fallback at its source, and the
// GBA mapping happens from OUR state instead.
//
// Rule 2 — defense in depth: a key event that still arrives tagged
// FLAG_FALLBACK with keycode BACK or DPAD_CENTER is consumed too. Those two
// fallback targets are the ones with host meaning (BACK = the menu gesture;
// CENTER is unmapped but pairs with A's fallback). A REAL Back-key press
// carries no fallback flag, so KEY_BACK → menu stays live — that is the
// contract, not a casualty.
//
// DPAD_* keys and ordinary keyboard keys are never consumed: the d-pad must
// reach raylib's keyboard table (DPAD_* → arrows) so the settings menu stays
// navigable by pad, and a real keyboard's B is AKEYCODE_B (30), not
// BUTTON_B.
inline bool android_key_consumed(int keycode, int flags) {
    if (keycode >= kAKeyCodeButtonA && keycode <= kAKeyCodeButtonMode)
        return true;
    if ((flags & kAKeyEventFlagFallback) != 0 &&
        (keycode == kAKeyCodeBack || keycode == kAKeyCodeDpadCenter))
        return true;
    return false;
}

// One capture slot. Written by the glue's input thread (onInputEvent), read
// by the game thread's input pump: every field is an atomic and each field
// is an independent level, so relaxed ordering is enough (no cross-field
// invariants, no ABA hazard — a press is a level, not a count).
//
// The apply functions (key / motion_axis / hat) are pure with respect to the
// struct they take — tests run them single-threaded on desktop.
struct AndroidPadState {
    std::atomic<bool> seen{false};
    // Bit (1 << kPadButton) held. Includes the d-pad bits from HAT polarity
    // and from DPAD_* key events (the two transports a pad may use; the
    // Odin's is HAT-only, measured).
    std::atomic<std::uint32_t> buttons{0};
    // Canonical-axis levels in the shared int units (see android_axis_scaled).
    std::atomic<std::int32_t> axes[kPadAxisCount] = {};

    void mark_seen() { seen.store(true, std::memory_order_relaxed); }

    bool available() const { return seen.load(std::memory_order_relaxed); }

    // Apply a KEY event. Returns true when the keycode is gamepad truth
    // (mapped); the caller logs those, not everything. Unmapped keycodes
    // (BACK, the extra face buttons) touch nothing and mark nothing.
    bool key(int keycode, bool down) {
        const int pad = android_keycode_to_pad(keycode);
        if (pad < 0) return false;
        mark_seen();
        set_button(pad, down);
        return true;
    }

    // Apply one motion-axis reading (float, Android units).
    void motion_axis(int axis, float value) {
        const int pad = android_axis_to_pad(axis);
        if (pad < 0) return;
        mark_seen();
        axes[pad].store(android_axis_scaled(axis, value),
                        std::memory_order_relaxed);
    }

    // Apply the d-pad polarity axes of a motion event. Exact ±1 comparisons,
    // mirroring raylib's own HAT branch (HAT values are quantized);
    // centering clears all four, which is how a HAT-only pad (the Odin)
    // releases. A motion event that carries no HAT axis reads 0 here — that
    // is the same "center" raylib would apply, so a pad that emits HAT only
    // on change still sees its bits cleared on stick-only events. Accepted:
    // it matches the raylib behavior this replaces, and the HAT-only pads
    // emit a motion report on every HAT change anyway.
    void hat(float hx, float hy) {
        mark_seen();
        std::uint32_t bits = buttons.load(std::memory_order_relaxed);
        bits &= ~kHatDpadBits;
        if (hx > 0.5f) bits |= 1u << kPadDpadRight;
        else if (hx < -0.5f) bits |= 1u << kPadDpadLeft;
        if (hy > 0.5f) bits |= 1u << kPadDpadDown;
        else if (hy < -0.5f) bits |= 1u << kPadDpadUp;
        buttons.store(bits, std::memory_order_relaxed);
    }

    bool button_down(int pad_button) const {
        if (pad_button < 0 || pad_button >= kPadButtonCount) return false;
        return (buttons.load(std::memory_order_relaxed) &
                (1u << pad_button)) != 0;
    }

    std::int32_t axis(int pad_axis) const {
        if (pad_axis < 0 || pad_axis >= kPadAxisCount) return 0;
        return axes[pad_axis].load(std::memory_order_relaxed);
    }

    void reset() {
        seen.store(false, std::memory_order_relaxed);
        buttons.store(0, std::memory_order_relaxed);
        for (auto& a : axes) a.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr std::uint32_t kHatDpadBits =
        (1u << kPadDpadUp) | (1u << kPadDpadDown) |
        (1u << kPadDpadLeft) | (1u << kPadDpadRight);

    void set_button(int pad_button, bool down) {
        const std::uint32_t bit = 1u << pad_button;
        std::uint32_t bits = buttons.load(std::memory_order_relaxed);
        bits = down ? (bits | bit) : (bits & ~bit);
        buttons.store(bits, std::memory_order_relaxed);
    }
};

}  // namespace gbarecomp
