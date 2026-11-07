package com.wither.nsmbw;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.os.Process;
import android.system.Os;
import android.util.Log;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity running libmain.so (the translated game, its runtime, SDL and Dawn,
 * linked into one library), with a multitouch GameCube overlay.
 */
public final class GameActivity extends SDLActivity
        implements TouchControlsView.Listener {
    private static final String TAG = "BrawlAndroid";

    /** Player 1's GameCube overlay, independent of SDL physical controller enumeration. */
    static native void nativeSetTouchState(int buttons, float x, float y, float cx, float cy, boolean enabled);
    static native void nativeCancelTouchState();
    /** Original Brawl menu items; geometry and selection stay on the native game thread. */
    static native boolean nativeTapScreen();
    static native void nativeTap(float x,float y);
    static native void nativePointer(int action,int id,float x,float y);

    /** The Display menu: aspect, render scale (480 lines per 1x), FPS counter (DisplaySettings). */
    static native void nativeApplyDisplaySettings(int aspect, float renderScale, boolean showFps);

    /** "GPU name\n1|0": the GPU the game draws with and whether its compatibility fixes are on. */
    static native String nativeGraphicsInfo();

    /** What this phone is called in LAN room lists (netplay). */
    static native void nativeSetDeviceName(String name);

    /** Whether this run is a LAN session, and leaving one (the host keeps the session's save). */
    static native boolean nativeSessionActive();
    static native void nativePrepareToLeave();
    /** {render scale, show FPS, aspect} after the game's own settings window changed them, else null. */
    static native float[] nativeTakeSettings();

    private WifiManager.MulticastLock multicastLock;

    private AppSettings settings;
    private boolean bootWidescreen;  // the game reads 4:3 versus widescreen once, when it starts
    private TouchControlsView touch;
    private boolean nativeReady;
    private boolean touchEnabled;
    private android.window.OnBackInvokedCallback backCallback;
    private BrawlTouchInput.State touchState = new BrawlTouchInput.State(0,0,0,0,0);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        settings = new AppSettings(this);
        bootWidescreen = settings.aspectMode() != AppSettings.ASPECT_4_3;
        try {
            AppFiles.prepareRuntime(this);
            AppFiles.writeConfig(this, settings);
            // Read by runtime/src/platform/host_platform.cpp before anything else starts.
            Os.setenv("WIICOMPILED_RUNTIME_DIR", AppFiles.runtimeDir(this).getAbsolutePath(), true);
            Os.setenv("WIICOMPILED_DATA_DIR", AppFiles.dataDir(this).getAbsolutePath(), true);
        } catch (Exception e) {
            Log.e(TAG, "could not prepare the game's files", e);
            Toast.makeText(this, "Could not prepare the game's files: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= 33) {
            backCallback = this::onMenu;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        }
        nativeReady = !SDLActivity.mBrokenLibraries;
        if (nativeReady) {
            nativeSetDeviceName(deviceName());
        }
        // LAN rooms are found with broadcasts, which many Wi-Fi drivers drop unless an app holds this.
        final WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            multicastLock = wifi.createMulticastLock("brawl-lan");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        touch = new TouchControlsView(this);
        touch.setListener(this);
        applyControlSettings();
        addContentView(touch, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }

    /** SDL is linked into libmain.so, so that one library is all there is to load. */
    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }

    // Preserve display choices and switch the overlay to direct taps for native menus.
    private final android.os.Handler menuPoll = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable pollMenu = new Runnable() {
        @Override
        public void run() {
            if (nativeReady) {
                if (touch != null) touch.setMenuMode(nativeTapScreen());
                // The game's settings window: keep its choices, the config is rewritten at each start.
                final float[] changed = nativeTakeSettings();
                if (changed != null) {
                    settings.setResolutionScale(changed[0]);
                    settings.setShowFps(changed[1] > 0.5f);
                    if (changed.length > 2) settings.setAspectMode(Math.round(changed[2]));
                    if (changed.length > 3) settings.setSpinningCoins(changed[3] > 0.5f);
                    if (changed.length > 4) settings.setFrameGraph(changed[4] > 0.5f);
                }
            }
            menuPoll.postDelayed(this, 100);
        }
    };

    @Override
    protected void onResume() {
        super.onResume();
        menuPoll.removeCallbacks(pollMenu);
        menuPoll.post(pollMenu);
        hideSystemBars();
        pushInput();
    }

    @Override
    protected void onPause() {
        menuPoll.removeCallbacks(pollMenu);
        if (touch != null) touch.release();
        // Backgrounding clears input, without unplugging Player 1 for a resumed frame.
        if (nativeReady) nativeSetTouchState(0,0,0,0,0,touchEnabled || (touch != null && touch.isMenuMode()));
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        } else if (touch != null) {
            touch.release();  // a dialog or the shade took the screen: let go of every button
        }
    }

    @Override
    public void onBackPressed() {
        onMenu();
    }

    /** SDL takes the Back key as keyboard input before onBackPressed: the Back menu gets it first. */
    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == android.view.KeyEvent.ACTION_UP && !event.isCanceled()) {
                onMenu();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    /** "Start Menu": back to the launcher (its settings, Skip Start Menu); the game's process ends. */
    private void openStartMenu(boolean inSession) {
        if (inSession) {
            nativePrepareToLeave();  // the host keeps the session's progress
        }
        startActivity(new Intent(this, LauncherActivity.class)
                .putExtra(LauncherActivity.EXTRA_SHOW_START_MENU, true)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP));
        finish();
    }

    /** "Leave LAN Game": keep the host's progress, then start the game again on its own. */
    private void leaveSession() {
        nativePrepareToLeave();
        restartForSession();  // with the session file gone, the next start is an ordinary one
    }

    /** The name the user gave the phone, else its model. */
    private String deviceName() {
        String name = Settings.Global.getString(getContentResolver(), Settings.Global.DEVICE_NAME);
        if (name == null || name.trim().isEmpty()) {
            name = Build.MODEL;
        }
        return name == null ? "Phone" : name.trim();
    }

    /**
     * Network play: the runtime has written the session (netplay_start.cpp); start the game again in a
     * fresh process. The launcher waits for this one to end, then opens the game, which finds the
     * session and boots into it. Called from the game thread.
     */
    void restartForSession() {
        runOnUiThread(() -> {
            final Intent intent = new Intent(this, LauncherActivity.class)
                    .putExtra(LauncherActivity.EXTRA_RESTART_GAME, true)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            startActivity(intent);
            finish();
        });
    }

    @Override
    protected void onDestroy() {
        if(touch!=null) touch.release();
        if (Build.VERSION.SDK_INT >= 33 && backCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        super.onDestroy();
        if (isFinishing()) {
            // The runtime starts once per process; the next start must get a fresh one.
            Process.killProcess(Process.myPid());
        }
    }

    // --- input ---------------------------------------------------------------------------------

    @Override
    public void onTouchInput(BrawlTouchInput.State state) {
        touchState = state;
        pushInput();
    }

    @Override
    public void onTouchCancel() {
        touchState = new BrawlTouchInput.State(0,0,0,0,0);
        if (nativeReady) nativeCancelTouchState();
    }

    private void pushInput() {
        if (!nativeReady) return;
        try {
            nativeSetTouchState(touchState.buttons,touchState.x,touchState.y,touchState.cx,touchState.cy,
                    touchEnabled || (touch != null && touch.isMenuMode()));
        } catch (UnsatisfiedLinkError e) {
            nativeReady = false;
            Log.e(TAG, "touch input bridge missing from libmain.so", e);
        }
    }

    private void applyControlSettings() {
        touchEnabled = settings.touchControls();
        touch.setControlsVisible(touchEnabled);
        touch.setOpacity(settings.touchOpacity());
        touch.setHaptics(settings.haptics());
    }

    @Override
    public void onMenuTap(float x,float y) {
        if (nativeReady) nativeTap(x,y);
    }

    @Override
    public void onMenuPointer(int action,int id,float x,float y) {
        if(nativeReady) nativePointer(action,id,x,y);
    }

    private void hideSystemBars() {
        getWindow().setDecorFitsSystemWindows(false);
        final WindowInsetsController controller = getWindow().getInsetsController();
        if (controller != null) {
            controller.hide(WindowInsets.Type.systemBars());
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    // --- menu ----------------------------------------------------------------------------------

    /**
     * The phone's Back gesture: back to the start menu (where the settings are), leave a LAN game,
     * or quit. The overlay menu button opens the same menu. Display options are available
     * in the launcher's Settings screen.
     */
    @Override
    public void onMenu() {
        touch.release();
        final boolean inSession = nativeReady && nativeSessionActive();
        final java.util.List<String> items = new java.util.ArrayList<>();
        items.add("Start Menu");
        if (inSession) items.add("Leave LAN Game");
        items.add("Quit Game");
        new AlertDialog.Builder(this)
                .setTitle("Super Smash Bros. Brawl")
                .setItems(items.toArray(new String[0]), (dialog, which) -> {
                    final String item = items.get(which);
                    if (item.equals("Start Menu")) openStartMenu(inSession);
                    else if (item.equals("Leave LAN Game")) leaveSession();
                    else finish();
                })
                .setNegativeButton("Back to the Game", null)
                .show();
    }
}
