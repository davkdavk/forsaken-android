# Build Info — Forsaken Android 0.7

**Target:** Orange Pi 4 Pro (Allwinner A733) — primary ship-it platform.  
**SoC:** A733 (8× Cortex-A55/A76, Mali-G57-class GPU via PowerVR BXM-4-64 on this board's Android 13), 4 GB RAM.  
**OS:** Android 13 (SDK 33), GLES 3.2.  
**ABI:** arm64-v8a only.  
**Status:** Self-contained APK (game data bundled as `assets/fdata.tar.gz`, no external files, no storage permission, SAF fallback removed).

## What changed in 0.7
- **Xbox controller via SDL_GameController** (USB + Bluetooth normalized; raw `SDL_Joystick` handlers preserved, new path on `SDL_CONTROLLERDEVICEADDED`). Filtering ignores the YICHIP 2.4 GHz dongle's consumer-control device.
- Default map: RT/LT → forward/reverse throttle (analog), left stick → strafe/vertical (slide semantics), right stick → yaw/pitch, LB/RB → roll, A → primary fire, X → secondary, D-pad → weapon cycle (edge-triggered), R3 → turbo, Start → pause. Menu: stick/D-pad nav, A select, B/Start back.
- **Neutral intent layer:** `input_intent.h` (`INPUT_INTENT`) + `apply_intent(SHIPCONTROL*, INPUT_INTENT*, framelag)` mirrors the keyboard framelag scaling at `controls.c:730`; no device writes SHIPCONTROL directly (touch reuses the same layer).
- **Feel/tuning, persisted per pilot** (`pilots/<name>.txt` via `config.c`): radial stick deadzones (default 0.15) + trigger deadzone (0.08), `look_sensitivity`/`move_sensitivity`, `invert_pitch`, response curve (linear vs mild expo). All defaults applied via `PadApplyConfig()` and surviving a `pilots/` dir that is creatable at runtime (`file_open("pilots/...","w")` confirms writable on Orange Pi and on stock Android).
- **Touch overlay tidied:** vector Canvas overlay atop SDL — FIRE above the **left** stick, remaining buttons (ALT/TURBO/MINE/UP/DN/RL/RR) fanned in thumb arcs above each stick instead of a right-edge column, with screen-edge clamping and transparent pass-through (off-control touches still reach SDL/menus).
- **Controller diagnostics:** logcat reports detected name and any unmapped-axis warnings (`AndroidInput: controller name '…'`, `WARNING unmapped …`); no unmapped warnings on a proper Xbox Series X pad.

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
