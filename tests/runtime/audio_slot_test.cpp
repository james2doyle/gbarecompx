// Enforces the single-instance invariant that raylib's userdata-less audio
// callback depends on. Pure logic -- no window, no audio device, so it runs
// everywhere.

#include "audio_slot.h"

#include <cassert>
#include <cstdio>

int main() {
    using gbarecomp::AudioSlot;

    int backend_a = 0;
    int backend_b = 0;

    AudioSlot slot;
    assert(!slot.is_held());
    assert(slot.owner() == nullptr);

    // First owner wins.
    assert(slot.acquire(&backend_a));
    assert(slot.is_held());
    assert(slot.owner() == &backend_a);

    // A second, DIFFERENT owner is refused rather than stealing the slot. If it
    // could steal, the audio thread would pull from the wrong bridge.
    assert(!slot.acquire(&backend_b));
    assert(slot.owner() == &backend_a);   // unchanged

    // Re-acquiring your own slot is idempotent (open() called twice).
    assert(slot.acquire(&backend_a));
    assert(slot.owner() == &backend_a);

    // A non-owner cannot release it.
    assert(!slot.release(&backend_b));
    assert(slot.owner() == &backend_a);

    // The owner can, exactly once.
    assert(slot.release(&backend_a));
    assert(!slot.is_held());

    // A second release by the old owner is a no-op, not a clobber.
    assert(!slot.release(&backend_a));
    assert(!slot.is_held());

    // Now the slot is genuinely free for someone else.
    assert(slot.acquire(&backend_b));
    assert(slot.owner() == &backend_b);
    assert(!slot.acquire(&backend_a));     // a cannot take b's slot
    assert(slot.owner() == &backend_b);

    // Null is never a valid owner.
    assert(!slot.acquire(nullptr));
    assert(!slot.release(nullptr));
    assert(slot.owner() == &backend_b);
    assert(slot.release(&backend_b));

    // Another independent slot does not share state (no static leakage).
    AudioSlot other;
    int backend_c = 0;
    assert(!other.is_held());
    assert(other.acquire(&backend_c));
    assert(other.owner() == &backend_c);

    std::printf("audio_slot tests: PASS\n");
    return 0;
}