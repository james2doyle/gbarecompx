package org.gbarecomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.Sensor;
import android.hardware.SensorManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * First-launch setup shared by every gbarecomp Android game: the player picks
 * their own cartridge dump (and, optionally or when required, a GBA BIOS)
 * through the Storage Access Framework. Files are size- and SHA-1-verified,
 * then copied into app-private storage. Nothing copyrighted ships in the APK.
 * Games declaring a saveFile additionally offer an optional battery-save
 * (.sav) import, size-validated and mirrored to the user-visible external
 * saves/ dir; it never blocks PLAY.
 */
public class GbaSetupActivity extends Activity {
    private static final int PICK_BIOS = 1001;
    private static final int PICK_ROM = 1002;
    private static final int PICK_SAVE = 1003;
    private static final String SETUP_PREFERENCES = "setup";
    private static final String SKIP_LAUNCHER_ON_BOOT = "skip_launcher_on_boot";
    private static final long BIOS_SIZE = 16 * 1024;
    // Upper bound for an unpinned save import (128 KiB covers the largest
    // GBA battery type, Flash 1M; pinned games check an exact size instead).
    private static final long MAX_UNPINNED_SAVE_SIZE = 128 * 1024;
    /** Validation/launcher-shortcut extra: start the game when ready without
     *  changing the player's skip-this-screen preference. */
    public static final String EXTRA_AUTOSTART = "org.gbarecomp.extra.AUTOSTART";

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private GbaGameConfig config;
    private TextView biosStatus;
    private TextView romStatus;
    private TextView saveStatus;
    private TextView infoStatus;
    private Button playButton;
    private Button biosButton;
    private Button romButton;
    private Button saveButton;
    private CheckBox skipLauncherCheck;
    private boolean biosReady;
    private boolean romReady;
    private boolean saveReady;

    /** The Activity that runs the game; games may override. */
    protected Class<?> gameActivityClass() {
        return GbaGameActivity.class;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        config = GbaGameConfig.load(this);
        setRequestedOrientation(GbaGameActivity.orientationFor(config.orientation));
        if (hasBundledPrivateAssets()) {
            startActivity(new Intent(this, gameActivityClass()));
            finish();
            return;
        }
        setContentView(buildContent());
        enterImmersiveMode();
        refreshStatus();
        if (!ready()) return;
        if (getIntent().getBooleanExtra(EXTRA_AUTOSTART, false)) {
            startActivity(new Intent(this, gameActivityClass()));
        } else if (skipLauncherOnBoot()) {
            launchGame();
        }
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        if (playButton != null) {
            setContentView(buildContent());
            refreshStatus();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        enterImmersiveMode();
        if (playButton != null) refreshStatus();
    }

    @Override
    protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) enterImmersiveMode();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        if (requestCode != PICK_BIOS && requestCode != PICK_ROM
                && requestCode != PICK_SAVE) return;
        final Uri uri = data.getData();
        if (requestCode == PICK_SAVE) {
            setBusy(true, "Checking save…");
            worker.execute(() -> {
                try {
                    installVerifiedSave(uri);
                    runOnUiThread(() -> {
                        setBusy(false, null);
                        refreshStatus();
                    });
                } catch (Exception error) {
                    runOnUiThread(() -> {
                        setBusy(false, null);
                        refreshStatus();
                        showError("Save not accepted",
                            error.getMessage());
                    });
                }
            });
            return;
        }
        final boolean bios = requestCode == PICK_BIOS;
        setBusy(true, bios ? "Checking BIOS…" : "Checking ROM…");
        worker.execute(() -> {
            try {
                installVerifiedAsset(uri, bios);
                runOnUiThread(() -> {
                    setBusy(false, null);
                    refreshStatus();
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    setBusy(false, null);
                    refreshStatus();
                    showError(bios ? "BIOS not accepted" : "ROM not accepted",
                        error.getMessage());
                });
            }
        });
    }

    private boolean ready() {
        return romReady && (biosReady || !config.biosRequired);
    }

    private boolean portrait() {
        return getResources().getConfiguration().orientation ==
            Configuration.ORIENTATION_PORTRAIT;
    }

    private View buildContent() {
        final boolean vertical = portrait();
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(Color.rgb(7, 11, 20));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(7, 11, 20));

        // The desktop launch screen's layout, recreated natively: the box
        // art panel on one side, the vertical divider, the controls column
        // (title, ROM status, PLAY, SELECT ROM, extras) on the other.
        LinearLayout split = new LinearLayout(this);
        split.setOrientation(vertical ? LinearLayout.VERTICAL
                                      : LinearLayout.HORIZONTAL);
        split.setGravity(Gravity.CENTER);

        // ── left: box art panel ─────────────────────────────────────────
        android.widget.FrameLayout artPanel = new android.widget.FrameLayout(this);
        artPanel.setBackground(roundRect(Color.rgb(26, 31, 46), 18,
                                         Color.rgb(48, 58, 92)));
        android.widget.ImageView art = loadBoxart();
        if (art != null) {
            art.setAdjustViewBounds(true);
            art.setPadding(dp(10), dp(10), dp(10), dp(10));
            artPanel.addView(art, new android.widget.FrameLayout.LayoutParams(
                android.widget.FrameLayout.LayoutParams.MATCH_PARENT,
                android.widget.FrameLayout.LayoutParams.MATCH_PARENT));
        } else {
            TextView placeholder = text(config.title, vertical ? 22 : 26,
                                        Color.rgb(203, 212, 238));
            placeholder.setTypeface(Typeface.DEFAULT_BOLD);
            placeholder.setGravity(Gravity.CENTER);
            artPanel.addView(placeholder,
                new android.widget.FrameLayout.LayoutParams(
                    android.widget.FrameLayout.LayoutParams.MATCH_PARENT,
                    android.widget.FrameLayout.LayoutParams.MATCH_PARENT));
        }
        split.addView(artPanel, vertical
            ? margins(-1, dp(210), 0, dp(16), 0, dp(16))
            : weightedMargins(1, dp(24), dp(14), dp(12), dp(14)));
        artPanel.getLayoutParams().height = vertical
            ? dp(210) : android.view.ViewGroup.LayoutParams.MATCH_PARENT;

        if (!vertical) {
            View divider = new View(this);
            divider.setBackgroundColor(Color.rgb(48, 58, 92));
            split.addView(divider, margins(dp(1), -1, 0, dp(10), 0, dp(10)));
            divider.getLayoutParams().height =
                android.view.ViewGroup.LayoutParams.MATCH_PARENT;
        }

        // ── right: controls column ──────────────────────────────────────
        LinearLayout controls = new LinearLayout(this);
        controls.setOrientation(LinearLayout.VERTICAL);
        controls.setGravity(Gravity.CENTER_VERTICAL);
        controls.setPadding(dp(18), dp(10), dp(18), dp(6));

        TextView title = text(config.title, 21, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        controls.addView(title, margins(-1, -2, 0, 0, 0, dp(6)));

        romStatus = statusText();
        controls.addView(romStatus, margins(-1, -2, 0, 0, 0, dp(10)));

        playButton = actionButton("PLAY");
        playButton.setTextSize(20);
        playButton.setMinHeight(dp(62));
        playButton.setOnClickListener(view -> launchGame());
        controls.addView(playButton, margins(-1, dp(58), 0, 0, 0, dp(10)));

        romButton = actionButton("SELECT ROM");
        romButton.setOnClickListener(view -> pickFile(PICK_ROM));
        controls.addView(romButton, margins(-1, dp(48), 0, 0, 0, dp(6)));

        if (hasSaveImport()) {
            LinearLayout saveRow = new LinearLayout(this);
            saveRow.setOrientation(LinearLayout.HORIZONTAL);
            saveRow.setGravity(Gravity.CENTER_VERTICAL);
            saveStatus = statusText();
            saveStatus.setTextSize(11);
            saveRow.addView(saveStatus, weightedMargins(1, 0, dp(2), 0, dp(2)));
            saveButton = actionButton("IMPORT SAVE");
            saveButton.setTextSize(11);
            saveButton.setMinHeight(dp(40));
            saveButton.setOnClickListener(view -> pickFile(PICK_SAVE));
            saveRow.addView(saveButton, margins(dp(150), dp(40), 0, 0, 0, 0));
            controls.addView(saveRow, margins(-1, -2, 0, dp(4), 0, dp(4)));
        } else {
            saveStatus = null;
            saveButton = null;
        }

        skipLauncherCheck = new CheckBox(this);
        skipLauncherCheck.setText("Skip this screen on launch when ready");
        skipLauncherCheck.setTextColor(Color.rgb(132, 143, 173));
        skipLauncherCheck.setTextSize(12);
        skipLauncherCheck.setMinHeight(dp(36));
        skipLauncherCheck.setChecked(skipLauncherOnBoot());
        skipLauncherCheck.setOnCheckedChangeListener((button, checked) ->
            getSharedPreferences(SETUP_PREFERENCES, MODE_PRIVATE)
                .edit().putBoolean(SKIP_LAUNCHER_ON_BOOT, checked).apply());
        controls.addView(skipLauncherCheck, margins(-1, -2, 0, dp(2), 0, dp(2)));

        infoStatus = text("", 12, Color.rgb(132, 143, 173));
        controls.addView(infoStatus, margins(-1, -2, 0, 0, 0, dp(2)));

        TextView privacy = text(
            "Your files stay in this app’s private storage and are never included "
            + "in the APK." + (config.setupNote.isEmpty() ? "" : " " + config.setupNote)
            + (config.biosRequired
               ? ""
               : " The built-in clean-room BIOS substitute is used."),
            10, Color.rgb(110, 120, 148));
        controls.addView(privacy, margins(-1, -2, 0, 0, 0, 0));

        split.addView(controls, vertical
            ? margins(-1, -2, 0, dp(4), 0, dp(10))
            : weightedMargins(1, dp(14), dp(14), dp(20), dp(14)));

        scroll.addView(split, new ScrollView.LayoutParams(
            ScrollView.LayoutParams.MATCH_PARENT,
            ScrollView.LayoutParams.MATCH_PARENT));
        page.addView(scroll, new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));
        return page;
    }

    /**
     * The launch screen's box art, resolved by NAME at runtime
     * (getIdentifier — the engine's Java must stay game-namespace-agnostic;
     * the bitmap is staged into drawable-nodpi by the game's build). Null
     * when absent: the caller shows the placeholder panel.
     */
    private android.widget.ImageView loadBoxart() {
        int id = getResources().getIdentifier(
            "launcher_boxart", "drawable", getPackageName());
        if (id == 0) return null;
        android.graphics.Bitmap bitmap =
            android.graphics.BitmapFactory.decodeResource(getResources(), id);
        if (bitmap == null) return null;
        android.widget.ImageView view = new android.widget.ImageView(this);
        view.setImageBitmap(bitmap);
        view.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
        return view;
    }

    private TextView statusText() {
        TextView status = text("Checking…", 13, Color.rgb(255, 211, 78));
        status.setTypeface(Typeface.DEFAULT_BOLD);
        return status;
    }

    private Button actionButton(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextColor(Color.rgb(8, 16, 27));
        button.setTextSize(13);
        button.setTypeface(Typeface.DEFAULT_BOLD);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER);
        button.setPadding(dp(16), 0, dp(16), 0);
        button.setBackground(roundRect(Color.rgb(78, 216, 255), 14, Color.TRANSPARENT));
        return button;
    }

    private TextView text(String value, float size, int color) {
        TextView text = new TextView(this);
        text.setText(value);
        text.setTextSize(size);
        text.setTextColor(color);
        text.setLineSpacing(0, 1.12f);
        return text;
    }

    private File biosFile() { return new File(getFilesDir(), "bios/gba_bios.bin"); }
    private File romFile() { return new File(getFilesDir(), "roms/" + config.romFile); }

    /** True when this game declares a battery save import (saveFile non-empty). */
    private boolean hasSaveImport() {
        return config.saveFile != null && !config.saveFile.isEmpty();
    }

    /** App-private save location (the setup import target and TOML fallback). */
    private File saveFile() {
        return new File(getFilesDir(), "saves/" + config.saveFile);
    }

    /** User-visible external save location, or null when unavailable. The
     *  game runtime (src/main.cpp, issue #6) reads --save-path here, so the
     *  setup import mirrors to both: internal alone would strand a re-import
     *  once the one-time migration has already moved the first save outward. */
    private File externalSaveFile() {
        try {
            File root = getExternalFilesDir(null);
            if (root == null) return null;
            return new File(root, "saves/" + config.saveFile);
        } catch (Exception ignored) {
            return null;
        }
    }

    private boolean isSaveFileValid(File file) {
        if (file == null || !file.isFile()) return false;
        long length = file.length();
        if (config.saveSize > 0) return length == config.saveSize;
        return length > 0 && length <= MAX_UNPINNED_SAVE_SIZE;
    }

    private void refreshStatus() {
        biosReady = biosFile().isFile() && biosFile().length() == BIOS_SIZE;
        romReady = romFile().isFile() && romFile().length() == config.romSize;
        refreshRomStatus();
        setAssetStatus(biosStatus, biosButton, biosReady, "BIOS", config.biosRequired);
        if (hasSaveImport()) {
            saveReady = isSaveFileValid(saveFile())
                || isSaveFileValid(externalSaveFile());
            if (saveStatus != null && saveButton != null)
                setAssetStatus(saveStatus, saveButton, saveReady, "SAVE", false);
        } else {
            saveReady = false;
        }
        playButton.setEnabled(ready());
        playButton.setAlpha(playButton.isEnabled() ? 1.0f : 0.35f);

        if (config.showGyroStatus) {
            SensorManager manager = (SensorManager) getSystemService(SENSOR_SERVICE);
            Sensor gyro = manager == null ? null
                : manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
            infoStatus.setText(gyro != null
                ? "●  Device gyroscope ready  ·  " + gyro.getName()
                : "Device gyroscope unavailable — controller motion still works.");
            infoStatus.setTextColor(gyro != null ? Color.rgb(120, 235, 171)
                                                 : Color.rgb(255, 184, 104));
        } else {
            infoStatus.setText("");
        }
    }

    /** The ROM row in the launch screen's style: the resolved private path
     *  in mono green when staged and verified, the import hint in amber
     *  when not — the desktop launcher's path banner analog. */
    private void refreshRomStatus() {
        if (romStatus == null) return;
        romStatus.setTypeface(Typeface.MONOSPACE);
        if (romReady) {
            romStatus.setText(romFile().getAbsolutePath());
            romStatus.setTextSize(11);
            romStatus.setTextColor(Color.rgb(120, 235, 171));
        } else {
            romStatus.setText("Choose your clean cartridge dump (.gba).");
            romStatus.setTextSize(13);
            romStatus.setTextColor(Color.rgb(255, 211, 78));
        }
    }

    private void setAssetStatus(TextView status, Button button, boolean ready,
                                String kind, boolean required) {
        if (status == null || button == null) return;  // retired rows (BIOS)
        status.setText(ready ? "●  Verified and ready"
                             : (required ? "○  " + kind + " required" : "○  Not provided"));
        status.setTextColor(ready ? Color.rgb(120, 235, 171)
                                  : (required ? Color.rgb(255, 211, 78)
                                              : Color.rgb(164, 175, 207)));
        button.setText(ready ? "REPLACE " + kind : "CHOOSE " + kind);
    }

    private void pickFile(int requestCode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // DocumentsUI often gives .bin/.gba/.sav vendor-specific MIME types;
        // filter nothing and let the size (+ SHA-1 for ROM/BIOS) check decide.
        intent.setType("*/*");
        startActivityForResult(intent, requestCode);
    }

    private void launchGame() {
        if (!ready()) return;
        getSharedPreferences(SETUP_PREFERENCES, MODE_PRIVATE).edit()
            .putBoolean(SKIP_LAUNCHER_ON_BOOT,
                skipLauncherCheck == null || skipLauncherCheck.isChecked())
            .apply();
        startActivity(new Intent(this, gameActivityClass()));
    }

    private boolean skipLauncherOnBoot() {
        return getSharedPreferences(SETUP_PREFERENCES, MODE_PRIVATE)
            .getBoolean(SKIP_LAUNCHER_ON_BOOT, true);
    }

    private boolean hasBundledPrivateAssets() {
        try (InputStream rom = getAssets().open("payload/roms/" + config.romFile)) {
            if (rom.read() < 0) return false;
        } catch (IOException ignored) {
            return false;
        }
        if (!config.biosRequired) return true;
        try (InputStream bios = getAssets().open("payload/bios/gba_bios.bin")) {
            return bios.read() >= 0;
        } catch (IOException ignored) {
            return false;
        }
    }

    private void installVerifiedAsset(Uri uri, boolean bios)
            throws IOException, NoSuchAlgorithmException {
        String displayName = displayName(uri);
        long expectedSize = bios ? BIOS_SIZE : config.romSize;
        File destination = bios ? biosFile() : romFile();
        File directory = destination.getParentFile();
        if (directory != null && !directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("Could not create the private game directory.");
        }
        File temporary = File.createTempFile("import-", ".tmp", directory);
        MessageDigest digest = MessageDigest.getInstance("SHA-1");
        long total = 0;
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(temporary)) {
            if (input == null) throw new IOException("Android could not open " + displayName + ".");
            byte[] buffer = new byte[1024 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read == 0) continue;
                total += read;
                if (total > expectedSize) throw new IOException(displayName + " is larger than expected.");
                digest.update(buffer, 0, read);
                output.write(buffer, 0, read);
            }
            output.getFD().sync();
        } catch (IOException error) {
            temporary.delete();
            throw error;
        }
        String actual = toHex(digest.digest());
        boolean accepted = total == expectedSize &&
            (bios ? config.biosSha1.equals(actual) : config.romAccepted(actual));
        if (!accepted) {
            temporary.delete();
            throw new IOException(String.format(Locale.US,
                "%s is not the supported clean %s dump.\n\nSize: %,d bytes (expected %,d)\nSHA-1: %s",
                displayName, bios ? "GBA BIOS" : config.romLabel, total, expectedSize, actual));
        }
        if (destination.exists() && !destination.delete()) {
            temporary.delete();
            throw new IOException("Could not replace the previously imported file.");
        }
        if (!temporary.renameTo(destination)) {
            copyFile(temporary, destination);
            temporary.delete();
        }
    }

    // Issue #20: optional battery-save import. Player data carries no SHA pin
    // (every playthrough differs), so only the size is validated: an exact
    // match when the game declares saveSize, otherwise any non-empty file up
    // to MAX_UNPINNED_SAVE_SIZE. The import mirrors to the user-visible
    // external saves/ dir when available — the runtime reads --save-path
    // there (issue #6), so an internal-only write would strand re-imports
    // after the one-time migration. A failed external mirror is non-fatal:
    // the internal copy still migrates outward on the next launch.
    private void installVerifiedSave(Uri uri) throws IOException {
        if (!hasSaveImport()) throw new IOException("This game has no save import.");
        String displayName = displayName(uri);
        long expectedSize = config.saveSize;
        long limit = expectedSize > 0 ? expectedSize : MAX_UNPINNED_SAVE_SIZE;
        File destination = saveFile();
        File directory = destination.getParentFile();
        if (directory != null && !directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("Could not create the private game directory.");
        }
        File temporary = File.createTempFile("import-", ".tmp", directory);
        long total = 0;
        try (InputStream input = getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(temporary)) {
            if (input == null) throw new IOException("Android could not open " + displayName + ".");
            byte[] buffer = new byte[1024 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read == 0) continue;
                total += read;
                if (total > limit) {
                    temporary.delete();
                    throw new IOException(String.format(Locale.US,
                        "%s is larger than expected.\n\nSize: over %,d bytes%s",
                        displayName, limit,
                        expectedSize > 0 ? String.format(Locale.US,
                            " (expected %,d)", expectedSize) : ""));
                }
                output.write(buffer, 0, read);
            }
            output.getFD().sync();
        } catch (IOException error) {
            temporary.delete();
            throw error;
        }
        boolean accepted = expectedSize > 0 ? total == expectedSize
            : total > 0 && total <= MAX_UNPINNED_SAVE_SIZE;
        if (!accepted) {
            temporary.delete();
            throw new IOException(String.format(Locale.US,
                "%s is not a valid save for %s.\n\nSize: %,d bytes%s",
                displayName, config.title, total,
                expectedSize > 0 ? String.format(Locale.US,
                    " (expected %,d)", expectedSize) : ""));
        }
        if (destination.exists() && !destination.delete()) {
            temporary.delete();
            throw new IOException("Could not replace the previously imported file.");
        }
        if (!temporary.renameTo(destination)) {
            copyFile(temporary, destination);
            temporary.delete();
        }
        File external = externalSaveFile();
        if (external != null) {
            try {
                File parent = external.getParentFile();
                if (parent == null || parent.isDirectory() || parent.mkdirs())
                    copyFile(destination, external);
            } catch (IOException ignored) {
                // Internal copy stands; the runtime migration moves it outward.
            }
        }
    }

    private static void copyFile(File source, File destination) throws IOException {
        try (FileInputStream input = new FileInputStream(source);
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[1024 * 1024];
            int read;
            while ((read = input.read(buffer)) >= 0) {
                if (read > 0) output.write(buffer, 0, read);
            }
            output.getFD().sync();
        }
    }

    private String displayName(Uri uri) {
        try (android.database.Cursor cursor = getContentResolver().query(
                uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0) return cursor.getString(column);
            }
        } catch (Exception ignored) {
        }
        return "selected file";
    }

    private void setBusy(boolean busy, String label) {
        if (biosButton != null) biosButton.setEnabled(!busy);  // retired row
        romButton.setEnabled(!busy);
        if (saveButton != null) saveButton.setEnabled(!busy);
        playButton.setEnabled(!busy && ready());
        if (busy && label != null) {
            infoStatus.setText(label);
            infoStatus.setTextColor(Color.rgb(78, 216, 255));
        }
    }

    private void showError(String title, String message) {
        new AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(message == null ? "The selected file could not be read." : message)
            .setPositiveButton("OK", null)
            .show();
    }

    private void enterImmersiveMode() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    private GradientDrawable roundRect(int color, int radiusDp, int strokeColor) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        if (strokeColor != Color.TRANSPARENT) drawable.setStroke(dp(1), strokeColor);
        return drawable;
    }

    private LinearLayout.LayoutParams margins(int width, int height, int left, int top,
                                               int right, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(width, height);
        params.setMargins(left, top, right, bottom);
        return params;
    }

    private LinearLayout.LayoutParams weightedMargins(float weight, int left, int top,
                                                       int right, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, weight);
        params.setMargins(left, top, right, bottom);
        return params;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static String toHex(byte[] bytes) {
        StringBuilder value = new StringBuilder(bytes.length * 2);
        for (byte item : bytes) value.append(String.format(Locale.US, "%02x", item & 0xff));
        return value.toString();
    }
}
