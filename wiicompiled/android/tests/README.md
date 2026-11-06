# Brawl Android touch validation

The production Java/JNI namespace remains `com.wither.nsmbw`; the installed application ID is `com.genisson.brawl.android`.

Run `./run-touch-tests.ps1` from this directory (PowerShell). The default JDK17 and Android36 SDK locations are the shared local `.tools` directory. Override `-Jdk` and `-AndroidJar` as needed. This runs 75 pure Java checks and compiles the complete frontend and the Android instrumentation source. It does not claim a native/gameplay test.

Run `./run-native-touch-tests.ps1` to compile and execute 45 C++ checks against the production `runtime/src/android/android_touch_state.h` helper. The default compiler is the local LLVM MinGW toolchain; override `-Compiler` if needed. These checks use the same state class as JNI/PAD, without Android dependencies. They prove short button taps survive until one PAD poll, held buttons remain continuous, current axes stay independent, ordinary releases do not linger, overlapping buttons coexist, and explicit cancellation drops every pending/held press while retaining the controller's connection status. The startup cases verify that PAD polling cannot consume the title's A/START edge, readiness delays preserve it, and a held dismiss button cannot also advance the next game dialog.

Package the isolated View-only QA APKs from `wiicompiled/android`:

```powershell
gradle --no-daemon --max-workers=2 '-Dorg.gradle.jvmargs=-Xmx1536m' '-PbrawlApplicationId=com.genisson.brawl.android.validation' :app:assembleDebug :app:assembleDebugAndroidTest
```

Install the two generated debug APKs, then run:

```powershell
adb shell am instrument -w com.genisson.brawl.android.validation.test/com.wither.nsmbw.TouchControlsInstrumentation
```

The five real Android View/MotionEvent cases use a recording listener and no native library, disc, assets, saves or game Activity. They exercise simultaneous movement/jump/attack/shield/grab and C-stick, sparse pointer IDs, per-finger release, cancellation, focus loss, resize, controller-source rejection, hidden controls, app menu, pause and analog directions. A passing result proves the overlay's Android input behavior only. A real Brawl match with the compiled runtime is a separate required validation.

Ordinary `ACTION_UP` publishes released state without cancelling unsampled native button edges. `ACTION_CANCEL`, focus loss, size/inset changes, pause, detachment and hiding the controls explicitly call native cancellation, which clears held buttons, pending press edges and both sticks. They cannot generate a deferred attack or jump. New deliberate presses after cancellation still work.

The overlay sends actual GameCube A, B, X, L, R, Z and Start bits and continuous main/C-stick axes. Radial touch deadzone is 12%; the movement ring marks the high-travel region. Slow travel preserves walk/tilt input; fast full travel preserves the game's smash timing. It never inserts an A press, changes a fighter or replaces Brawl's match camera. The game and the player's in-game controller configuration decide the resulting action.

Import root on the phone is `Android/data/<applicationId>/files/game`, containing `sys/boot.bin`, `sys/main.dol` and `files/**`. Only USA RSBE01 revision 1 is accepted. Runtime assets unpack to the app's internal `files/runtime`. Game data and runtime payloads remain outside source control.

Run `./run-import-tests.ps1` for isolated host filesystem checks of the production import transaction. The tests inject rename failures, process death between publication steps and validators; they verify successful/first installs, rollback and startup recovery, rejected candidates and executable checksums, and preservation of NAND, Config and unrelated cache files. All fixtures use tiny synthetic data in temporary directories; no private game input is required. Production imports verify the pinned USA revision 1 main.dol SHA256 on the import worker before touching the old game. Startup recovery uses the same validator on a worker only when `game.backup` exists. Routine launcher refresh checks the boot header without hashing.

For a live match already running in the public app, the same test runner has an explicit input-injection mode:

```powershell
gradle --no-daemon --max-workers=2 '-Dorg.gradle.jvmargs=-Xmx1536m' '-PbrawlApplicationId=com.genisson.brawl.android.validation' :app:assembleDebugAndroidTest
# Install only app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk.
# Keep the public game foreground; the installed validation app remains the instrumentation target.
adb shell am instrument -w -e liveBrawl true com.genisson.brawl.android.validation.test/com.wither.nsmbw.TouchControlsInstrumentation
```

Live mode uses `UiAutomation.injectInputEvent` to send real touchscreen MotionEvents to the foreground `com.genisson.brawl.android` app. It launches no Activity and edits no guest memory. It requires the calibrated 2772×1280 landscape display and aborts if the foreground package or display differs. If a native SDL surface exposes no accessibility root, the guard reads only `mCurrentFocus`/`mFocusedApp` from `dumpsys window displays`; unexpected or ambiguous focus fails closed, and other window/package information is never logged. Across approximately ten seconds it walks right/left, taps X/B/A, moves the C-stick right, taps Z, holds L/R, combines movement with X/A/R using independent pointers, then explicitly cancels a two-pointer gesture. Each combination lifts its action finger before lifting movement. Any interrupted stream receives cancellation in cleanup. It writes no screenshots or recordings.

Successful live output reports only acknowledged injections and completed phases. It does not prove that the game performed the intended actions: observe or separately record the actual match to assess movement, attacks, jumps, grabs and shields. The normal View-only regression mode remains the default when `liveBrawl` is absent.

For the calibrated native-options VSync experiment, first open the native options overlay manually, then run:

```powershell
adb shell am instrument -w -e liveVsyncClick true com.genisson.brawl.android.validation.test/com.wither.nsmbw.TouchControlsInstrumentation
```

This separate test-only mode sends exactly one mouse click at (2290,680): a `SOURCE_MOUSE`/`TOOL_TYPE_MOUSE` hover, DOWN with `BUTTON_PRIMARY`, then UP with button state zero. It uses the same foreground/display guard, releases an interrupted press in cleanup, and sends no keyboard, guest-memory or JNI commands. Expected output is three acknowledged injections across one phase. Check the native option label/log to establish whether VSync actually changed; injection acknowledgments alone do not prove it. Do not combine the two live-mode arguments.
