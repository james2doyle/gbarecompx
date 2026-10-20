package org.gbarecomp;

import android.annotation.TargetApi;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.graphics.Insets;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.Bundle;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.DisplayMetrics;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import android.app.NativeActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/**
 * The shared game Activity (a NativeActivity subclass since Phase 4f):
 * installs the packaged payload (game TOML, mod catalog, optional private
 * assets) into private storage, runs immersive and edge-to-edge (drawing
 * under display cutouts), follows the game's orientation policy with live
 * rotation, and reports safe-area insets to the runtime so host chrome avoids
 * cutouts and system-gesture edges. NativeActivity starts the native app
 * thread; the manifest's android.app.lib_name names the game's .so.
 */
public class GbaGameActivity extends NativeActivity {
    private GbaGameConfig config;
    private boolean nativeReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        config = GbaGameConfig.load(this);
        setRequestedOrientation(orientationFor(config.orientation));
        // Before super.onCreate (which starts the native thread and opens the
        // window): the runtime reads this to open its window at the display
        // size. raylib letterboxes any other request, which would starve the
        // resize-driven expanded view (it would see the requested 3:2 window
        // instead of the surface). The fullscreen pre-4f shell never had this
        // problem.
        writeDisplaySize();
        try {
            installPayload();
        } catch (IOException error) {
            throw new IllegalStateException("Unable to install game payload", error);
        }
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attributes);
        }
        View decor = getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            reportInsets(insets);
            return view.onApplyWindowInsets(insets);
        });
        enterImmersiveMode();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enterImmersiveMode();
            View decor = getWindow().getDecorView();
            WindowInsets insets = decor.getRootWindowInsets();
            if (insets != null) reportInsets(insets);
        }
    }

    /** The display's real size, for the runtime's window request. */
    private void writeDisplaySize() {
        try {
            WindowManager manager = (WindowManager) getSystemService(WINDOW_SERVICE);
            if (manager == null || manager.getDefaultDisplay() == null) return;
            DisplayMetrics metrics = new DisplayMetrics();
            manager.getDefaultDisplay().getRealMetrics(metrics);
            if (metrics.widthPixels <= 0 || metrics.heightPixels <= 0) return;
            File target = new File(getFilesDir(), "display.ini");
            try (FileOutputStream out = new FileOutputStream(target)) {
                out.write((metrics.widthPixels + "x" + metrics.heightPixels + "\n")
                    .getBytes(StandardCharsets.UTF_8));
            }
        } catch (Exception ignored) {
            // Fallback: the runtime keeps its scaled window request.
        }
    }

    static int orientationFor(String policy) {
        if ("any".equals(policy)) return ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
        if ("portrait".equals(policy)) return ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
        return ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
    }

    private void reportInsets(WindowInsets insets) {
        int left = 0, top = 0, right = 0, bottom = 0;
        if (Build.VERSION.SDK_INT >= 28) {
            DisplayCutout cutout = insets.getDisplayCutout();
            if (cutout != null) {
                left = cutout.getSafeInsetLeft();
                top = cutout.getSafeInsetTop();
                right = cutout.getSafeInsetRight();
                bottom = cutout.getSafeInsetBottom();
            }
        }
        if (Build.VERSION.SDK_INT >= 29) {
            // Edge swipes starting here belong to the system (home/back).
            Insets gestures = insets.getMandatorySystemGestureInsets();
            left = Math.max(left, gestures.left);
            top = Math.max(top, gestures.top);
            right = Math.max(right, gestures.right);
            bottom = Math.max(bottom, gestures.bottom);
        }
        try {
            GbaNative.setSafeInsets(left, top, right, bottom);
            DisplayMetrics metrics = new DisplayMetrics();
            getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
            GbaNative.setDisplayDpi(metrics.xdpi, metrics.ydpi);
            nativeReady = true;
        } catch (UnsatisfiedLinkError notLoadedYet) {
            // libmain loads during super.onCreate; later passes succeed.
            nativeReady = false;
        }
    }

    /**
     * One-shot system-vibrator pulse for host haptics. Called from native on
     * the game thread; Vibrator APIs are system-service calls, so they are
     * safe off the UI thread. Attributed as media/game on purpose: a bare
     * vibrate() carries USAGE_TOUCH and OEMs that disable touch haptics drop
     * it silently. Android 13+ takes VibrationAttributes; API 26-32 takes
     * AudioAttributes.USAGE_GAME.
     */
    public void vibrate(int durationMs, float strength) {
        if (durationMs <= 0 || !(strength > 0.0f)) return;
        float clamped = Math.min(1.0f, strength);
        Vibrator vibrator;
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager manager =
                (VibratorManager) getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
            if (manager == null) return;
            vibrator = manager.getDefaultVibrator();
        } else {
            vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        }
        if (vibrator == null || !vibrator.hasVibrator()) return;
        // Non-zero amplitudes round up to 100% on devices without amplitude
        // control; use DEFAULT_AMPLITUDE there rather than pretending to scale.
        int amplitude = vibrator.hasAmplitudeControl()
            ? Math.max(1, Math.min(255, Math.round(clamped * 255.0f)))
            : VibrationEffect.DEFAULT_AMPLITUDE;
        VibrationEffect effect =
            VibrationEffect.createOneShot(durationMs, amplitude);
        if (Build.VERSION.SDK_INT >= 33) {
            vibrateMedia(vibrator, effect);
        } else {
            vibrateGame(vibrator, effect);
        }
    }

    @TargetApi(33)
    private static void vibrateMedia(Vibrator vibrator, VibrationEffect effect) {
        vibrator.vibrate(effect,
            VibrationAttributes.createForUsage(VibrationAttributes.USAGE_MEDIA));
    }

    @SuppressWarnings("deprecation")  // VibrationEffect+AudioAttributes, API 26-32
    private static void vibrateGame(Vibrator vibrator, VibrationEffect effect) {
        AudioAttributes attributes = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build();
        vibrator.vibrate(effect, attributes);
    }

    protected void enterImmersiveMode() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars()
                    | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    /**
     * Copies assets/payload into getFilesDir() once per payload version.
     * Player data (saves, config.ini, suspend states, imported ROMs) lives
     * beside the payload and is never overwritten: only files that exist in
     * the payload are replaced, and the payload never contains player data.
     */
    private void installPayload() throws IOException {
        File marker = new File(getFilesDir(), ".payload-version");
        if (marker.isFile()) {
            byte[] version = new byte[(int) marker.length()];
            try (FileInputStream input = new FileInputStream(marker)) {
                int offset = 0;
                while (offset < version.length) {
                    int read = input.read(version, offset, version.length - offset);
                    if (read < 0) break;
                    offset += read;
                }
            }
            String installed = new String(version, StandardCharsets.UTF_8).trim();
            if (config.payloadVersion.equals(installed)) return;
        }
        copyAssetTree(getAssets(), "payload", getFilesDir());
        try (FileOutputStream output = new FileOutputStream(marker)) {
            output.write((config.payloadVersion + "\n").getBytes(StandardCharsets.UTF_8));
        }
    }

    private static void copyAssetTree(AssetManager assets, String assetPath,
                                      File destination) throws IOException {
        String[] children = assets.list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Unable to create " + destination);
            }
            for (String child : children) {
                copyAssetTree(assets, assetPath + "/" + child, new File(destination, child));
            }
            return;
        }
        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Unable to create " + parent);
        }
        // Player-owned files next to the payload are never replaced.
        if (destination.isFile() && isPlayerOwned(destination.getName())) return;
        try (InputStream input = assets.open(assetPath);
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[1024 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read > 0) output.write(buffer, 0, read);
            }
        }
    }

    /** Files the player (or the running game) owns once they exist. */
    private static boolean isPlayerOwned(String name) {
        return name.equals("state.toml") || name.endsWith(".sav") ||
               name.equals("config.ini") || name.equals("keybinds.ini");
    }
}
