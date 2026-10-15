#pragma once

#include <atomic>

namespace gbarecomp {

// A single-owner slot, used for the audio callback target.
//
// WHY THIS EXISTS
// ---------------
// raylib's AudioCallback carries no userdata (open upstream limitation:
// raysan5/raylib#3911), so `RaylibBackend` keeps a static pointer to whichever
// Impl the audio thread should drain. That is only safe if exactly one backend
// is open at a time. `HostWindow::open` refuses to open twice, but that is a
// behavioural rule three layers away -- nothing enforced the invariant the
// callback actually depends on.
//
// This makes it explicit and enforced rather than documented:
//   * acquire() REFUSES a second owner instead of silently stealing the slot.
//     Two open backends would otherwise cross-talk: one audio thread pulling
//     from the other backend's ring buffer.
//   * release() only clears when the caller owns the slot, so a double release
//     or a late close cannot unhook a *different* backend's audio.
//
// The pointer is atomic with acquire/release ordering because the owner is
// written by the main thread at open/close and READ BY THE AUDIO THREAD from
// inside the callback.
class AudioSlot {
public:
    // Returns false when the slot is already held by a *different* owner.
    // Re-acquiring your own slot succeeds (idempotent).
    bool acquire(void* owner) {
        if (!owner) return false;
        void* expected = nullptr;
        // compare-exchange: if it fails, the slot is taken. If the taken slot is
        // ours, this is a harmless re-acquire.
        if (owner_.compare_exchange_strong(expected, owner,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            return true;
        }
        return expected == owner;
    }

    // Only the owner may release. Returns true if this call actually cleared it.
    bool release(void* owner) {
        if (!owner) return false;
        void* expected = owner;
        return owner_.compare_exchange_strong(expected, nullptr,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
    }

    // Read from the audio thread: an acquire load pairs with the owner's release,
    // so the callback either sees a fully-constructed Impl or null -- never a
    // half-torn one.
    void* owner() const { return owner_.load(std::memory_order_acquire); }

    bool is_held() const { return owner() != nullptr; }

private:
    std::atomic<void*> owner_{nullptr};
};

}  // namespace gbarecomp