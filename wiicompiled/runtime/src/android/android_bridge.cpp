// JNI entry points for Brawl Android (install ID com.genisson.brawl.android).
// Preserve the upstream Java/JNI namespace to avoid breaking SDL/native startup.
#if defined(__ANDROID__)

#include "display_settings.h"
#include "graphics_info.h"
#include "runtime_config.h"
#include "settings_overlay.h"
#include "android_touch_state.h"

#include "netplay/net_socket.h"
#include "netplay_session.h"
#include "netplay_start.h"
#include "netplay_ui.h"

#include <SDL3/SDL_system.h>
#include <jni.h>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace AndroidBridge {

namespace {
std::mutex touchMutex;
BrawlAndroid::TouchPadState touchState;
int8_t Axis(float value) {
    return std::isfinite(value) ? static_cast<int8_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 100.0f)) : 0;
}
}

// Network play: ask the activity to start the game again in a fresh process (GameActivity
// .restartForSession), then park this thread until Android ends the process.
bool RestartGame() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env == nullptr || activity == nullptr) {
        return false;
    }
    jclass type = env->GetObjectClass(activity);
    jmethodID restart = type != nullptr ? env->GetMethodID(type, "restartForSession", "()V") : nullptr;
    if (restart == nullptr) {
        env->ExceptionClear();
        return false;
    }
    env->CallVoidMethod(activity, restart);
    env->DeleteLocalRef(activity);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace AndroidBridge

extern "C" {

// Read by PADRead's local-source path, never by SDL's physical device enumeration.
// Positive Y is up. The phone is exclusively virtual player 1; physical pads keep their ports.
bool BrawlAndroidReadTouchPad(unsigned port, unsigned short* buttons, signed char* stickX,
    signed char* stickY, signed char* substickX, signed char* substickY,
    unsigned char* triggerL, unsigned char* triggerR)
{
    std::lock_guard<std::mutex> lock(AndroidBridge::touchMutex);
    BrawlAndroid::TouchPadSample sample;
    if (!AndroidBridge::touchState.Sample(port, sample)) return false;
    *buttons = sample.buttons;
    *stickX = sample.axes[0]; *stickY = sample.axes[1];
    *substickX = sample.axes[2]; *substickY = sample.axes[3];
    *triggerL = (*buttons & 0x40u) ? 255 : 0;
    *triggerR = (*buttons & 0x20u) ? 255 : 0;
    return true;
}

// Title input has its own edge latch so background PAD polls cannot consume
// it. The caller supplies the shader/credits-ready gate before acknowledging.
bool BrawlAndroidConsumeStartupPress(bool titleReady)
{
    std::lock_guard<std::mutex> lock(AndroidBridge::touchMutex);
    return AndroidBridge::touchState.ConsumeStartupPress(titleReady);
}

// Actual GameCube buttons and two continuous analog sticks for player 1.
// Ordinary button UP retains an unsampled press for one poll; axes are always the latest state.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetTouchState(
    JNIEnv*, jclass, jint buttons, jfloat x, jfloat y, jfloat cx, jfloat cy, jboolean enabled)
{
    std::lock_guard<std::mutex> lock(AndroidBridge::touchMutex);
    AndroidBridge::touchState.Set(static_cast<uint16_t>(buttons & 0x1f70),
        {AndroidBridge::Axis(x), AndroidBridge::Axis(y), AndroidBridge::Axis(cx), AndroidBridge::Axis(cy)},
        enabled == JNI_TRUE);
}

// Gesture cancellation, focus/size changes, pause and hiding the overlay discard pending edges.
// PAD's input-blocked path can call the same clear without changing the virtual connection.
void BrawlAndroidCancelTouchPad()
{
    std::lock_guard<std::mutex> lock(AndroidBridge::touchMutex);
    AndroidBridge::touchState.Cancel();
}

JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeCancelTouchState(JNIEnv*, jclass)
{
    BrawlAndroidCancelTouchPad();
}

// GameActivity.nativeApplyDisplaySettings: the Display menu. The aspect (0 original 4:3, 1 16:9,
// 2 fill the screen) and the render scale (480 lines per 1x) apply at the next frame; 4:3 versus a
// widescreen mode only at the next start, since the game reads it at boot.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeApplyDisplaySettings(
    JNIEnv*, jclass, jint aspect, jfloat renderScale, jboolean showFps)
{
    // Through the settings overlay, so the game's own settings window shows the same choice. A scale
    // of 0 is "match the screen".
    settings_overlay::SetAspect(aspect < 0 || aspect > 3 ? DisplaySettings::kAspectFill : static_cast<int32_t>(aspect));
    settings_overlay::SetRenderScale(renderScale >= 0.0f ? renderScale : 1.0f);
    settings_overlay::SetShowFps(showFps == JNI_TRUE);
}

// GameActivity.nativeGraphicsInfo: the GPU the game is drawing with and whether the GPU
// compatibility fixes are on, as "<name>\n<1|0>"; empty until the renderer has started.
JNIEXPORT jstring JNICALL Java_com_wither_nsmbw_GameActivity_nativeGraphicsInfo(JNIEnv* env, jclass)
{
    const std::string name = GraphicsInfo::Name();
    if (name.empty()) {
        return env->NewStringUTF("");
    }
    const std::string info = name + "\n" + (GraphicsInfo::CompatActive() ? "1" : "0");
    return env->NewStringUTF(info.c_str());
}

// GameActivity.nativeSetDeviceName: what this phone is called in room lists.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeSetDeviceName(JNIEnv* env, jclass, jstring name)
{
    if (name == nullptr) {
        return;
    }
    const char* utf8 = env->GetStringUTFChars(name, nullptr);
    if (utf8 != nullptr) {
        Netplay::SetDeviceName(utf8);
        env->ReleaseStringUTFChars(name, utf8);
    }
}

// GameActivity.nativeSessionActive: whether this run is a LAN session (the menu offers to leave it).
JNIEXPORT jboolean JNICALL Java_com_wither_nsmbw_GameActivity_nativeSessionActive(JNIEnv*, jclass)
{
    return NetplaySession::Active() ? JNI_TRUE : JNI_FALSE;
}

// GameActivity.nativePrepareToLeave: the menu's "Leave LAN game", before the app restarts the game.
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativePrepareToLeave(JNIEnv*, jclass)
{
    NetplayStart::PrepareToLeave();
}

// GameActivity.nativeTap: a tap on the game's menus, as fractions of the screen (netplay_ui.h).
JNIEXPORT void JNICALL Java_com_wither_nsmbw_GameActivity_nativeTap(JNIEnv*, jclass, jfloat x, jfloat y)
{
    NetplayUi::Tap(x, y);
}

// GameActivity.nativeTakeSettings: {render scale, show FPS 1/0, aspect, spinning coins 1/0, frame graph 1/0} when the game's settings
// window changed them since the last call, else null; the app saves them for the next start.
JNIEXPORT jfloatArray JNICALL Java_com_wither_nsmbw_GameActivity_nativeTakeSettings(JNIEnv* env, jclass)
{
    float scale = 1.0f;
    bool showFps = false;
    int aspect = DisplaySettings::kAspectFill;
    bool spinningCoins = true;
    bool frameGraph = false;
    if (!NetplayUi::TakeSettingsChange(scale, showFps, aspect, spinningCoins, frameGraph)) {
        return nullptr;
    }
    jfloatArray result = env->NewFloatArray(5);
    const jfloat values[5] = {scale, showFps ? 1.0f : 0.0f, static_cast<jfloat>(aspect), spinningCoins ? 1.0f : 0.0f,
                              frameGraph ? 1.0f : 0.0f};
    env->SetFloatArrayRegion(result, 0, 5, values);
    return result;
}

// GameActivity.nativeTapScreen: whether a menu that taps drive is up (the app hides its remote).
JNIEXPORT jboolean JNICALL Java_com_wither_nsmbw_GameActivity_nativeTapScreen(JNIEnv*, jclass)
{
    return NetplayUi::TapScreenUp() ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"

#endif // __ANDROID__
