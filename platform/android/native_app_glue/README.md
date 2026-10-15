# Vendored `android_native_app_glue` (NDK 28.2.13676358, Apache-2.0 — see
# THIRD_PARTY_ATTRIBUTION.md). The Android scaffold's NativeActivity entry
# (platform/android/cpp + the manifest's android.app.lib_name): every Android
# game build uses this copy (the game CMake falls back to the NDK copy only
# when it is absent).
#
# Single deliberate diff from upstream (documented at the site): the app
# thread is created with a 256 MiB stack (`pthread_attr_setstacksize`,
# matching `kMobileGameStackBytes`). raylib is glue-thread-bound — its
# `ALooper` polling fails on any other thread — so a raylib game runs on
# this thread instead of `mobile_run_with_stack`'s pthread, and this thread
# must carry the desktop-sized stack the recompiled corpus needs.
#
# Maintenance: when the NDK is upgraded, re-copy both files from
# `$NDK/sources/android/native_app_glue/` and re-apply the one hunk above.
# Verify with: `diff $NDK/sources/android/native_app_glue/android_native_app_glue.c
# android_native_app_glue.c` shows only the gbarecompx hunk.
