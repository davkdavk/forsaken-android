# Build Info — Forsaken Android 0.7

**Primary ship-it:** Orange Pi 4 Pro (Allwinner A733 — 8× Cortex-A55/A76, BXM-4-64 @ GLES 3.2, Android 13, 4 GB, arm64-v8a).  
**Also targeted:** Mali (G57/G610/G52 — e.g. RK3588, S905X4, A311D), Adreno (6xx/7xx — Snapdragon), PowerVR (BXM/GE8320), and other GLES 3.2-capable SoCs. Single `arm64-v8a` APK, no per-GPU binaries.  
**OS:** Android 13 (SDK 33, `minSdk 28`).  
**Status:** Self-contained APK (game data `assets/fdata.tar.gz`, no storage permission, SAF fallback removed). Engine is GLES 3.2 + `precision highp` shaders with no vendor-specific intrinsics — PowerVR/Mali/Adreno differences are driver quirks, handled by generic fallbacks.

## What changed in 0.7
- **Xbox controller via SDL_GameController** (USB + Bluetooth normalized; raw `SDL_Joystick` handlers preserved, new path on `SDL_CONTROLLERDEVICEADDED`). Filtering ignores the YICHIP 2.4 GHz dongle's consumer-control device.
- Default map: RT/LT → forward/reverse throttle (analog), left stick → strafe/vertical (slide semantics), right stick → yaw/pitch, LB/RB → roll, A → primary fire, X → secondary, D-pad → weapon cycle (edge-triggered), R3 → turbo, Start → pause. Menu: stick/D-pad nav, A select, B/Start back.
- **Neutral intent layer:** `input_intent.h` (`INPUT_INTENT`) + `apply_intent(SHIPCONTROL*, INPUT_INTENT*, framelag)` mirrors the keyboard framelag scaling at `controls.c:730`; no device writes SHIPCONTROL directly (touch reuses the same layer).
- **Feel/tuning, persisted per pilot** (`pilots/<name>.txt` via `config.c`): radial stick deadzones (default 0.15) + trigger deadzone (0.08), `look_sensitivity`/`move_sensitivity`, `invert_pitch`, response curve (linear vs mild expo). All defaults applied via `PadApplyConfig()` and surviving a `pilots/` dir that is creatable at runtime (`file_open("pilots/...","w")` confirms writable on Orange Pi and on stock Android).
- **Touch overlay tidied:** vector Canvas overlay atop SDL — FIRE above the **left** stick, remaining buttons (ALT/TURBO/MINE/UP/DN/RL/RR) fanned in thumb arcs above each stick instead of a right-edge column, with screen-edge clamping and transparent pass-through (off-control touches still reach SDL/menus).
- **Controller diagnostics:** logcat reports detected name and any unmapped-axis warnings (`AndroidInput: controller name '…'`, `WARNING unmapped …`); no unmapped warnings on a proper Xbox Series X pad.

## GPU coverage
- **Render path:** GLES 3.2, `GL=3`/`RENDER_GLES`, `aapt2`/`d8` pipeline — no `libGLESv2.so.2` versioned links (`patchelf --replace-needed` keeps it to `libGLESv2.so` inside the APK). Shaders declare `precision highp float` and avoid vendor intrinsics; Mali's stricter validator and Adreno's `mediump` defaults are both satisfied.
- **Tested primary:** A733 / BXM-4-64 @ 24.2@6643903 — ~134 FPS @ 1920×1080, no `GL_INVALID` on launch (logcat clean).
- **Next to validate (help wanted):** Mali-G57 (OPi 5 / RK3566), Mali-G610 (RK3588), Mali-G52 (S905X3/A311D), Adreno 610/618/730/740, PowerVR GE8320 — contributions welcome with `adb shell getprop ro.hardware` + `dumpsys SurfaceFlinger | grep GLES` + `logcat | grep forsaken`.
- **Fallbacks in place:** `displaySize()` caps at 1920w (even dimensions), 16 KB page-size ELF (`0x4000`/`0x10000`) already verified for Android 15+ 16 KB devices, `SetupActivity` `getAppTasks().moveToFront()` prevents the SDL single-window double-launch on all GPUs.

## Build
On-device Termux (kept alive by `termux-wake-lock`):
```
~/forsaken-android $ bash ~/build-game-android.sh   # clang → libmain.so, strips/patchelfs NEEDED/lib names
~/forsaken-android-apk $ bash ~/mkapk.sh            # javac → d8 → aapt2 → zip -0 assets → apksigner
# Install:
adb install -r ~/forsaken-android-apk/forsaken.apk
```

## Verify on Orange Pi 4 Pro (ship-it)
- Pair Xbox pad over **both** USB and Bluetooth → confirm forward/reverse on triggers, strafe+vertical on left stick, yaw/pitch on right, roll on bumpers, A/X fire, D-pad weapon step (one per press), R3 turbo, menu nav, deadzones/sensitivity/expo/invert, persistence across relaunch, and **keyboard/mouse still functional**. Check `adb logcat | grep AndroidInput` for name + unmapped warnings.
