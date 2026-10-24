// mobile_platform.h — process setup shared by gbarecomp mobile (Android) games.
//
// The Android shell (platform/android) installs the game's payload into the
// app's private files directory and starts SDL's native main with no
// arguments. mobile_prepare_process() turns that into the same relative
// layout the desktop runtime uses, so games need no Android path plumbing:
//
//   * chdir() to the internal storage root,
//   * redirect stdout/stderr to android-runtime.log there (logcat does not
//     reliably carry native stdio),
//   * append the staged per-game TOML and --no-launcher to the arguments,
//   * default GBARECOMP_SELFHEAL_RECOMPILE=0: a device has no toolchain, so
//     dispatch misses are bridged by the interpreter LOUDLY, logged to the
//     miss list/coverage report in the files directory, and fed back offline.
//     The statically recompiled code remains the execution path.
//
// On every other platform it does nothing and returns false.

#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace gbarecomp {

// Display info pushed from the Android Activity over JNI
// (org.gbarecomp.GbaNative.setSafeInsets / setDisplayDpi — the shared Java
// shell stays; the NativeActivity build exports these from this TU). Atomics:
// the JNI calls arrive on the UI thread while the runtime reads them on the
// glue thread, and the insets arrive after open(), so consumers refresh live.
// All zero / known=false on every other platform.
struct AndroidDisplayInfo {
    std::atomic<int> inset_left{0}, inset_top{0}, inset_right{0}, inset_bottom{0};
    std::atomic<bool> insets_known{false};
    std::atomic<float> xdpi{0.0f}, ydpi{0.0f};
};

AndroidDisplayInfo& android_display_info();

// Android: one-shot system-vibrator pulse for host haptics. Strength is
// clamped to 0..1 and mapped onto the platform's 1..255 amplitude range when
// the vibrator supports amplitude control (otherwise DEFAULT_AMPLITUDE).
// Returns false when there is no JNI path or the platform call failed; always
// false off Android. Safe to call from the game thread (the Activity method
// only touches thread-safe system services).
bool android_haptics_pulse(int duration_ms, float strength);

struct MobileProcessOptions {
    const char* game_config = nullptr;   // e.g. "variants/emerald/game.toml"
    const char* program_name = nullptr;  // argv[0] stand-in, e.g. "./EmeraldRecomp"
    const char* log_file = "android-runtime.log";
};

bool mobile_prepare_process(std::vector<std::string>& args,
                            const MobileProcessOptions& options);

// App-private files directory on Android (the mobile_prepare_process chdir
// target). SDL builds ask SDL_AndroidGetInternalStoragePath; raylib builds
// (no SDL, NativeActivity) use the build-time package constant
// GBARECOMP_ANDROID_PACKAGE (/data/user/0/<pkg>/files, primary user).
// nullptr when unavailable — callers keep app-relative behavior.
const char* mobile_internal_storage_path();

// Run `entry(argc, argv)` on a thread with a `stack_bytes` stack and return
// its result. Recompiled guest code maps guest calls (and some tail-call
// loops) onto the host call stack, so game executables reserve a large stack
// (desktop links 256 MiB). SDL's Android main thread is a default Java thread
// (~1 MiB) — far too small — so mobile entry points run the game on a native
// thread sized like the desktop reservation. Elsewhere this calls directly.
constexpr std::size_t kMobileGameStackBytes = 256u * 1024u * 1024u;
int mobile_run_with_stack(int (*entry)(int, char**), int argc, char** argv,
                          std::size_t stack_bytes = kMobileGameStackBytes);

// Host-stack budget guard for a finite stack: the mobile game thread or the
// desktop TCP game thread. Recompiled guest branches (and some loop back-edges)
// map onto host C calls; a spin that outlives its expected wake — e.g. a
// busy-wait split across two generated functions — can nest host frames
// without bound. The runtime probes this at instruction boundaries and unwinds
// through the standard yield path before the thread stack is exhausted. False
// when no budget is armed (threads with generous stacks, or the desktop main
// path) or the probe is still above the limit.
bool mobile_stack_over_budget(const void* probe);

// Critical tier of the same guard: true when the probe is within the last
// ~0.5 MiB of the stack, where even a live IRQ handler cannot complete; the
// runtime may unwind in that state despite g_irq_nest_depth > 0.
bool host_stack_critically_low(const void* probe);

// Arm the guard for the calling thread from its real pthread stack bounds
// (Linux). No-op on unsupported platforms.
void host_stack_guard_arm_current(std::size_t reserve_bytes);

// Keep unsupported hosts' instruction-boundary path a compile-time no-op.
// Their arming operation cannot establish bounds; an out-of-line probe would
// still pay the thread-local lookup (emulated TLS on MinGW) on every branch.
constexpr bool host_stack_guard_supported() {
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

// True when built for a mobile platform (compile-time).
constexpr bool mobile_platform() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

}  // namespace gbarecomp
