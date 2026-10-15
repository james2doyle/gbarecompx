#pragma once

// Host-neutral Assist (rewind / fast-forward) gamepad binding encoding.
//
// The Assist hotkeys can be bound to a gamepad button OR to half an analog
// axis (e.g. the two triggers). Those bindings are persisted in config.ini /
// keybinds.ini as integers, so the encoding is an on-disk format that EVERY
// backend must interpret identically. It used to live inline in host_window.cpp
// against SDL_CONTROLLER_BUTTON_MAX / SDL_CONTROLLER_AXIS_MAX with a hardcoded
// trigger threshold, which meant a non-SDL backend had to reverse-engineer the
// format from the SDL constants. It is a named, documented contract here.
//
// Layout of a binding integer:
//   0        -> unbound
//   1..N     -> digital button, (value - 1) is a backend-native button code
//   >= 100   -> analog half-axis: code = (value - 100) / 2, positive = bit 0
//
// Backends are responsible for validating that their native code is in range
// when decoding (the encoder here is unbounded on purpose, so it round-trips
// any native code without the helper needing to know the backend's enum size).

namespace gbarecomp {

// Lowest binding integer that means "analog half-axis" rather than a button.
constexpr int kAssistPadAxisBase = 100;

// Encode an analog half-axis binding (e.g. the positive half of an axis).
inline int assist_pad_axis(int axis_code, bool positive) {
    return kAssistPadAxisBase + axis_code * 2 + (positive ? 1 : 0);
}

// Which half of an analog axis a binding refers to.
struct AssistAxisBinding {
    int axis_code = 0;
    bool positive = true;
};

// Decode an analog binding. Returns false when `binding` is a digital button
// or unbound, i.e. when there is no half-axis to interpret.
inline bool decode_assist_pad_axis(int binding, AssistAxisBinding* out) {
    if (!out || binding < kAssistPadAxisBase) return false;
    const int encoded = binding - kAssistPadAxisBase;
    out->axis_code = encoded / 2;
    out->positive = (encoded & 1) != 0;
    return true;
}

// Decode a digital button binding. Returns false when `binding` is an analog
// half-axis or unbound.
inline bool decode_assist_pad_button(int binding, int* button_code) {
    if (!button_code || binding < 1 || binding >= kAssistPadAxisBase) return false;
    *button_code = binding - 1;
    return true;
}

// A trigger is "down" once it passes roughly half of full scale. Backends pass
// raw axis values in their native units; this compares against a threshold the
// backend scales from its own full-scale. Returns true when the selected half
// is at or beyond the threshold.
inline bool assist_axis_down(int raw_axis_value, bool positive, int threshold) {
    return positive ? raw_axis_value >= threshold : raw_axis_value <= -threshold;
}

}  // namespace gbarecomp