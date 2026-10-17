// android_gamepad_test.cpp — the Android gamepad capture contract.
//
// The capture exists because raylib's Android backend drops gamepad KEY
// events whose source ALSO carries SOURCE_KEYBOARD (raylib issue #5387;
// the vendor-0x2020 AYN Odin measured live: Classes KEYBOARD|GAMEPAD|
// JOYSTICK, every face press dead) and its keyboard fallback table maps
// every AKEYCODE_BUTTON_* to 0. The capture trusts the KEYCODE instead of
// the source bits — the same direction upstream's post-pin fix took.
//
// These pins are the contract the raylib backend's Android block and the
// glue-level hook both depend on. Kernel-level facts behind the table come
// from a live getevent capture on the Odin (2026-10-17): A=BTN_GAMEPAD,
// B=BTN_EAST, X=BTN_NORTH, Y=BTN_WEST, L1/R1=BTN_TL/TR, L2/R2=BTN_TL2/TR2
// (+ABS_BRAKE/ABS_GAS analog), Select=BTN_SELECT, Start=BTN_START, and the
// d-pad reports through ABS_HAT0X/HAT0Y only (no BTN_DPAD_* keys).

#include "android_gamepad.h"
#include "gamepad_map.h"

#include <cassert>
#include <cstdint>

using namespace gbarecomp;

int main() {
    // ---- keycode → canonical pad button --------------------------------
    // Face buttons map LABEL-WISE onto the canonical ids, matching the
    // desktop GLFW table (kGlfwBtnA → kPadA → kGbaKeyA): the pad's A is
    // GBA A, the pad's B is GBA B.
    assert(android_keycode_to_pad(kAKeyCodeButtonA) == kPadA);
    assert(android_keycode_to_pad(kAKeyCodeButtonB) == kPadB);
    assert(android_keycode_to_pad(kAKeyCodeButtonX) == kPadX);
    assert(android_keycode_to_pad(kAKeyCodeButtonY) == kPadY);
    assert(android_keycode_to_pad(kAKeyCodeButtonL1) == kPadLeftBumper);
    assert(android_keycode_to_pad(kAKeyCodeButtonR1) == kPadRightBumper);
    assert(android_keycode_to_pad(kAKeyCodeButtonL2) == kPadLeftTrigger);
    assert(android_keycode_to_pad(kAKeyCodeButtonR2) == kPadRightTrigger);
    assert(android_keycode_to_pad(kAKeyCodeButtonThumbl) == kPadLeftThumb);
    assert(android_keycode_to_pad(kAKeyCodeButtonThumbr) == kPadRightThumb);
    assert(android_keycode_to_pad(kAKeyCodeButtonStart) == kPadStart);
    assert(android_keycode_to_pad(kAKeyCodeButtonSelect) == kPadBack);

    // No canonical slot: the Odin's extra face buttons (C/Z), MODE, dpad
    // CENTER. These must stay unmapped rather than collide with a real id.
    assert(android_keycode_to_pad(kAKeyCodeButtonC) < 0);
    assert(android_keycode_to_pad(kAKeyCodeButtonZ) < 0);
    assert(android_keycode_to_pad(kAKeyCodeButtonMode) < 0);
    assert(android_keycode_to_pad(kAKeyCodeDpadCenter) < 0);

    // THE MENU CONTRACT: BACK must NEVER become a pad button. The Back key
    // is the platform menu-open gesture (KEY_BACK → Back gesture → menu
    // toggle); capturing it as GBA B would strand the menu. This pin is the
    // regression that matters most.
    assert(android_keycode_to_pad(kAKeyCodeBack) < 0);

    // D-pad keys map to the dpad ids.
    assert(android_keycode_to_pad(kAKeyCodeDpadUp) == kPadDpadUp);
    assert(android_keycode_to_pad(kAKeyCodeDpadDown) == kPadDpadDown);
    assert(android_keycode_to_pad(kAKeyCodeDpadLeft) == kPadDpadLeft);
    assert(android_keycode_to_pad(kAKeyCodeDpadRight) == kPadDpadRight);

    // ---- motion axis id → canonical pad axis ---------------------------
    assert(android_axis_to_pad(kAMotionAxisX) == kPadAxisLeftX);
    assert(android_axis_to_pad(kAMotionAxisY) == kPadAxisLeftY);
    assert(android_axis_to_pad(kAMotionAxisZ) == kPadAxisRightX);
    assert(android_axis_to_pad(kAMotionAxisRZ) == kPadAxisRightY);
    // Triggers arrive under two vendor conventions; both canonical ids.
    assert(android_axis_to_pad(kAMotionAxisGas) == kPadAxisRightTrigger);
    assert(android_axis_to_pad(kAMotionAxisBrake) == kPadAxisLeftTrigger);
    assert(android_axis_to_pad(kAMotionAxisRTrigger) == kPadAxisRightTrigger);
    assert(android_axis_to_pad(kAMotionAxisLTrigger) == kPadAxisLeftTrigger);
    // HAT is d-pad polarity, not an axis: never a canonical axis.
    assert(android_axis_to_pad(kAMotionAxisHatX) < 0);
    assert(android_axis_to_pad(kAMotionAxisHatY) < 0);

    // ---- axis scaling ----------------------------------------------------
    // Sticks are -1..1 → the shared int units, matching the raylib-backed
    // block this replaces (raw * 32767).
    assert(android_axis_scaled(kAMotionAxisX, -1.0f) == -32767);
    assert(android_axis_scaled(kAMotionAxisX, 0.0f) == 0);
    assert(android_axis_scaled(kAMotionAxisX, 1.0f) == 32767);
    // GAS/BRAKE rest at 0 and press to +1 (measured on the Odin); the
    // historical raylib transform maps them to -1..1 (*2-1) so the shared
    // assist threshold sees the same shape as SDL's 0..32767.
    assert(android_axis_scaled(kAMotionAxisGas, 0.0f) == -32767);
    assert(android_axis_scaled(kAMotionAxisGas, 0.5f) == 0);
    assert(android_axis_scaled(kAMotionAxisGas, 1.0f) == 32767);
    assert(android_axis_scaled(kAMotionAxisBrake, 1.0f) == 32767);
    // LTRIGGER/RTRIGGER are already -1..1.
    assert(android_axis_scaled(kAMotionAxisRTrigger, 1.0f) == 32767);

    // ---- KEY event application ------------------------------------------
    AndroidPadState st;
    assert(!st.available());

    st.key(kAKeyCodeButtonA, true);
    assert(st.available());
    assert(st.button_down(kPadA));
    assert(!st.button_down(kPadB));
    st.key(kAKeyCodeButtonA, false);
    assert(!st.button_down(kPadA));

    // A keycode with no canonical slot must not disturb state (and the
    // struct must not light up `seen` for it — it is not gamepad truth).
    AndroidPadState quiet;
    assert(!quiet.key(kAKeyCodeButtonC, true));
    assert(!quiet.available());
    assert(!quiet.key(kAKeyCodeBack, true));   // menu contract, again
    assert(!quiet.available());

    // D-pad keys set the same ids the HAT path does.
    st.key(kAKeyCodeDpadUp, true);
    assert(st.button_down(kPadDpadUp));
    st.key(kAKeyCodeDpadUp, false);
    assert(!st.button_down(kPadDpadUp));

    // ---- HAT (the Odin's ONLY d-pad transport) ---------------------------
    // UP: HAT0Y = -1 (measured: ffffffff). Exact-polarity semantics, mirroring
    // raylib's own HAT branch.
    st.hat(0.0f, -1.0f);
    assert(st.button_down(kPadDpadUp));
    assert(!st.button_down(kPadDpadDown));
    assert(!st.button_down(kPadDpadLeft));
    assert(!st.button_down(kPadDpadRight));
    // Center (both 0) clears all four — release works with no KEY events.
    st.hat(0.0f, 0.0f);
    assert(!st.button_down(kPadDpadUp));
    // Diagonal: two bits at once.
    st.hat(1.0f, -1.0f);
    assert(st.button_down(kPadDpadRight) && st.button_down(kPadDpadUp));
    st.hat(0.0f, 0.0f);
    st.hat(-1.0f, 1.0f);
    assert(st.button_down(kPadDpadLeft) && st.button_down(kPadDpadDown));
    st.hat(0.0f, 0.0f);

    // ---- motion axis application -----------------------------------------
    st.motion_axis(kAMotionAxisX, -0.5f);
    assert(st.axis(kPadAxisLeftX) == android_axis_scaled(kAMotionAxisX, -0.5f));
    st.motion_axis(kAMotionAxisGas, 1.0f);
    assert(st.axis(kPadAxisRightTrigger) == 32767);
    st.motion_axis(kAMotionAxisGas, 0.0f);
    assert(st.axis(kPadAxisRightTrigger) == -32767);

    // ---- end-to-end: snapshot → the SHARED mapping ------------------------
    // The capture feeds GamepadSnapshot; gamepad_to_keyinput() (the one
    // verified piece of logic, shared with desktop) folds stick + buttons.
    AndroidPadState play;
    play.key(kAKeyCodeButtonA, true);
    play.key(kAKeyCodeButtonR1, true);
    play.key(kAKeyCodeButtonStart, true);
    play.hat(0.0f, -1.0f);                       // d-pad up
    play.motion_axis(kAMotionAxisX, 0.0f);       // stick centered
    play.motion_axis(kAMotionAxisY, 0.0f);

    GamepadSnapshot snap;
    snap.buttons = 0;
    if (play.button_down(kPadA)) snap.buttons |= kGbaKeyA;
    if (play.button_down(kPadB)) snap.buttons |= kGbaKeyB;
    if (play.button_down(kPadRightBumper)) snap.buttons |= kGbaKeyR;
    if (play.button_down(kPadStart)) snap.buttons |= kGbaKeyStart;
    if (play.button_down(kPadDpadUp)) snap.buttons |= kGbaKeyUp;
    snap.stick_x = play.axis(kPadAxisLeftX);
    snap.stick_y = play.axis(kPadAxisLeftY);

    const std::uint16_t keyinput = gamepad_to_keyinput(snap);  // ACTIVE-LOW
    assert(!(keyinput & kGbaKeyA));               // A pressed
    assert(!(keyinput & kGbaKeyR));               // R pressed
    assert(!(keyinput & kGbaKeyStart));           // Start pressed
    assert(!(keyinput & kGbaKeyUp));              // dpad up pressed
    assert((keyinput & kGbaKeyB) != 0);           // B released
    assert((keyinput & kGbaKeySelect) != 0);      // Select released
    assert((keyinput & kGbaKeyLeft) != 0);        // no stick drift left

    // The assist contract: full right-trigger pull crosses the shared
    // threshold (16000 of ±32767) on the POSITIVE half.
    AndroidPadState boost;
    boost.motion_axis(kAMotionAxisGas, 1.0f);
    assert(assist_axis_down(boost.axis(kPadAxisRightTrigger), true, 16000));
    boost.motion_axis(kAMotionAxisGas, 0.0f);
    assert(!assist_axis_down(boost.axis(kPadAxisRightTrigger), true, 16000));

    // ---- reset ------------------------------------------------------------
    boost.reset();
    assert(!boost.available());
    assert(!boost.button_down(kPadA));
    assert(boost.axis(kPadAxisLeftX) == 0);

    return 0;
}
