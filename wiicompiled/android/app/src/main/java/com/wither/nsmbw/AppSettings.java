package com.wither.nsmbw;

import android.content.Context;


/** Player settings, read under a file lock by both launcher and game processes. */
final class AppSettings {
    static final int ASPECT_4_3 = 0;
    static final int ASPECT_16_9 = 1;
    static final int ASPECT_FILL = 2;

    /** GPU driver workarounds: picked from the phone's GPU, or forced (AuroraGpuCompat). */
    static final int GPU_COMPAT_AUTO = 0;
    static final int GPU_COMPAT_ON = 1;
    static final int GPU_COMPAT_OFF = 2;

    /** Render scales offered in the menu; 1x renders 480 lines, whatever the aspect; 0 the screen's own. */
    static final float[] RESOLUTION_SCALES = {0f, 0.5f, 0.75f, 1f, 1.5f, 2f, 3f, 4f};

    private static final String FILE = "settings";
    private static final java.util.Map<String,SharedSettingsFile.SafeAccess> STORES=new java.util.HashMap<>();
    private final SharedSettingsFile.SafeAccess prefs;

    AppSettings(Context context) {
        // Share failure notifications and last confirmed values within each process; the store
        // still reloads under its inter-process lock on every successful read/write.
        Context application=context.getApplicationContext();
        java.io.File file=new java.io.File(application.getFilesDir(),"settings-v2.properties");
        synchronized(STORES) {
            prefs=STORES.computeIfAbsent(file.getAbsolutePath(),path -> new SharedSettingsFile.SafeAccess(file,
                    application.getSharedPreferences(FILE,Context.MODE_PRIVATE).getAll(),failure -> {
                        android.util.Log.w("BrawlAndroid","Could not access player settings; previous settings retained",failure);
                        new android.os.Handler(android.os.Looper.getMainLooper()).post(() ->
                            android.widget.Toast.makeText(application,
                                "Could not access player settings. Keeping the last saved settings.",
                                android.widget.Toast.LENGTH_LONG).show());
                    }));
        }
    }
    private int integer(String key, int fallback) { return Integer.parseInt(prefs.get(key, String.valueOf(fallback))); }
    private float decimal(String key, float fallback) { return Float.parseFloat(prefs.get(key, String.valueOf(fallback))); }
    private boolean flag(String key, boolean fallback) { return Boolean.parseBoolean(prefs.get(key, String.valueOf(fallback))); }

    int aspectMode() { return integer("aspect_mode", ASPECT_16_9); }
    void setAspectMode(int mode) { prefs.put("aspect_mode", mode); }

    float resolutionScale() { return decimal("resolution_scale", 1f); }
    void setResolutionScale(float scale) { prefs.put("resolution_scale", scale); }

    int gpuCompat() { return integer("gpu_compat", GPU_COMPAT_AUTO); }
    void setGpuCompat(int mode) { prefs.put("gpu_compat", mode); }

    boolean showFps() { return flag("show_fps", false); }
    void setShowFps(boolean show) { prefs.put("show_fps", show); }
    boolean vsync() { return flag("vsync", true); }
    void setVsync(boolean enabled) { prefs.put("vsync", enabled); }
    boolean spinningCoins() { return flag("spinning_coins", true); }
    void setSpinningCoins(boolean on) { prefs.put("spinning_coins", on); }

    boolean frameGraph() { return flag("frame_graph", false); }
    void setFrameGraph(boolean on) { prefs.put("frame_graph", on); }

    boolean touchControls() { return flag("touch_controls", true); }
    void setTouchControls(boolean on) { prefs.put("touch_controls", on); }

    float touchOpacity() { return decimal("touch_opacity", 0.45f); }
    void setTouchOpacity(float opacity) { prefs.put("touch_opacity", opacity); }

    boolean motionTilt() { return flag("motion_tilt", false); }
    void setMotionTilt(boolean on) { prefs.put("motion_tilt", on); }

    boolean motionShake() { return flag("motion_shake", false); }
    void setMotionShake(boolean on) { prefs.put("motion_shake", on); }

    boolean haptics() { return flag("haptics", true); }
    void setHaptics(boolean on) { prefs.put("haptics", on); }

    /** Open the game straight away when the app starts; the in-game menu leads back here. */
    boolean skipStartMenu() { return flag("skip_start_menu", false); }
    void setSkipStartMenu(boolean on) { prefs.put("skip_start_menu", on); }

    /** "2× (960p)": the scale and the number of lines it renders, as the menu shows it. */
    static String resolutionLabel(float scale) {
        if (scale <= 0f) return "Match screen";
        final String factor = scale == Math.rint(scale)
                ? String.valueOf((int) scale) : String.valueOf(scale);
        return factor + "× (" + Math.round(480 * scale) + "p)";
    }
}
