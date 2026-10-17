// audio_mute_contract_test.cpp — the Audio seam honors set_enabled(false).
//
// Context (2026-10-16, user report): the settings menu's Audio toggle did
// nothing — AudioImpl::push() never read audio_enabled, so the flag was
// set-but-never-read since the Phase 4b carve (the menu Bind called the
// seam, the seam flipped the flag, the sample path delivered everything).
// The boot-time [Menu] audio_enabled application (runtime.cpp) rode the
// same dead setter, so a persisted audio=off was equally silent-of-effect.
//
// The contract: a disabled device drops every push() BEFORE any device
// gating — no buffering, no callback work — so the policy is device-
// independent and observable WITHOUT an audio device or window. The mute
// counter (gbarecomp_audio_drop_stats) is the observable: enabled() alone
// cannot distinguish "flag set" from "flag honored", which is exactly the
// bug shape this suite pins. Never opens a window: the backend ctor owns
// every seam this test touches.
//
// Stub builds (no raylib) have state-only seams — skipped via the
// menu().available() probe (menu_render_contract_test's pattern).

#include "menu_engine.h"
#include "raylib_backend.h"
#include "window_backend.h"

#include <cstdint>
#include <cstdio>

extern "C" void gbarecomp_audio_drop_stats(unsigned long long* muted,
                                           unsigned long long* nodev);

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,      \
                         __LINE__, #x);                                      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

int main() {
    gbarecomp::RaylibBackend b;
    if (!b.menu().available()) {
        std::printf("audio_mute_contract: stub backend (no raylib) — "
                    "audio is state-only, mute contract skipped\n");
        return 0;
    }

    gbarecomp::Audio& audio = b.audio();
    std::int16_t samples[240] = {};  // one GBA frame of silence

    unsigned long long muted0 = 0, nodev0 = 0;
    gbarecomp_audio_drop_stats(&muted0, &nodev0);

    // A disabled device drops the push as MUTED — not as "no device yet",
    // and nothing buffers. This is the assertion that failed against the
    // set-but-never-read implementation: muted never moved.
    audio.set_enabled(false);
    audio.push(samples, 240);
    unsigned long long muted1 = 0, nodev1 = 0;
    gbarecomp_audio_drop_stats(&muted1, &nodev1);
    CHECK(muted1 - muted0 == 1);

    // Re-enabled (no window ever opened: no bridge/device exists), the drop
    // reason flips away from mute — the flag is out of the way.
    audio.set_enabled(true);
    audio.push(samples, 240);
    unsigned long long muted2 = 0, nodev2 = 0;
    gbarecomp_audio_drop_stats(&muted2, &nodev2);
    CHECK(muted2 == muted1);

    std::printf("audio_mute_contract: PASS (disabled push drops as muted; "
                "re-enabled push is not mute-dropped)\n");
    return 0;
}
