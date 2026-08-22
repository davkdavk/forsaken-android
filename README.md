# Forsaken

This repo holds the community port of Forsaken!

Check out the [Wiki](https://github.com/ForsakenX/forsaken/wiki) for more information.

## Android — Orange Pi 4 Pro (A733) + Mali / Adreno / PowerVR

Primary ship-it is **Orange Pi 4 Pro (Allwinner A733, BXM-4-64, Android 13, GLES 3.2, arm64-v8a)** — single self-contained APK (no external data). The GLES 3.2 render path is generic (`precision highp` shaders, no vendor intrinsics) and is now the target for **Mali (G57/G610/G52), Adreno (6xx/7xx) and other PowerVR** SoCs as well; no per-GPU APKs. See `BUILD_ANDROID_PI.md` for pad mappings, GPU matrix, and build/verify steps.

## Help Wanted — Other SoCs & Phones

0.7 ships on **Orange Pi 4 Pro (Allwinner A733 / BXM-4-64, Android 13)** as a self-contained arm64-v8a APK. It boots on other arm64 devices, but SoC/GPU/driver quirks (Mali, Adreno, PowerVR) mean per-device validation is needed.

If you have a phone, tablet, TV box or SBC you're willing to test on, help is very welcome:

- **Try the v0.7 APK** on your device and report what you see — `adb logcat | grep -i forsaken`, screen size, Android version, GPU string (`adb shell getprop ro.hardware` / `dumpsys SurfaceFlinger | grep GLES`).
- **What we're expanding toward:** broader arm64 coverage (other Rockchip/Qualcomm/MediaTek/Allwinner SoCs, Adreno/Mali/PowerVR), optional 32-bit compat checks, and input sanity across more pads (DualShock/DualSense/8BitDo).
- **How to contribute:** open an issue with your device + logs, or a PR if you fix a shader/driver quirk or a layout cut-off.

No device is too obscure — the Orange Pi 4 Pro was just the ship-it starting point. Every extra test report gets the port closer to "works everywhere."
