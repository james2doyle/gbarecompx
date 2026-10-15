package org.gbarecomp;

/**
 * JNI entry points exported by the gbarecomp runtime (libmain.so).
 *
 * The native side lives in src/runtime/mobile_platform.cpp (ported there from
 * the retired pre-4f shell's native bridge in Phase 4f). Every method here is safe to
 * call from the UI thread; the runtime reads the values on its own thread at
 * the next present.
 */
public final class GbaNative {
    static {
        // NativeActivity loads libmain.so in its own linker namespace, which
        // the JNI native-method lookup does not search; an explicit load (a
        // no-op for the linker — the library is already mapped) registers it
        // with this classloader, exactly as the retired shell's activity did.
        // Without it every call below fails with "No implementation
        // found ... is the library loaded, e.g. System.loadLibrary?".
        System.loadLibrary("main");
    }

    private GbaNative() {}

    /**
     * Safe-area insets in physical pixels: display cutouts and mandatory
     * system-gesture regions. Host chrome (touch pad, native buttons) is laid
     * out inside them; the game image may extend underneath.
     */
    public static native void setSafeInsets(int left, int top, int right, int bottom);

    /**
     * Physical pixel density of the panel in the current display mode
     * (DisplayMetrics from getRealMetrics). Touch targets and gesture
     * thresholds are sized in millimetres from it; densityDpi is a user-
     * scalable logical setting and can differ from the glass by 25% or more.
     */
    public static native void setDisplayDpi(float xdpi, float ydpi);
}
