# Forsaken — Android (native) port

A native Android build of [ForsakenX](https://github.com/ForsakenX/forsaken),
running on SDL2's Android backend with a GLES 3.0 renderer. No Termux,
no X11, no virgl — the game renders straight to an Android Surface on the
device GPU.

Measured on an Orange Pi 4 Pro (PowerVR B-Series BXM-4-64, Android 13):

| Path | Resolution | FPS |
|---|---|---|
| Termux + termux-x11 + virgl | 1920×1080 | 15–20 |
| Termux + termux-x11 + virgl | 960×540 | 38–46 |
| **Native APK (this port)** | **1920×1080** | **~108** |

## What was fixed to get here

Three renderer bugs, each hiding the next:

1. **The orthographic vertex shader branch was never written.**
   In `render_gl_shared.c` the `if (orthographic)` branch of
   `default_vertex_shader` sat inside `#if 0`, so every 2D/HUD draw was
   submitted with an undefined `gl_Position`. On desktop GL1 this never
   mattered because 2D went through fixed-function. Implemented for GLES as
   `gl_Position = ortho_proj * vec4(tlpos.xyz, 1.0)` — `tlpos.w` carries a
   D3D `rhw` value, so `w` must be rebuilt rather than passed through.

2. **`ortho_update()` never uploaded its matrix.**
   `u_ortho_matrix` was declared `GLuint`, making the `>= 0` guard always
   true. With the shader branch empty, `ortho_proj` was optimised out,
   `glGetUniformLocation` returned `-1`, that wrapped to `0xFFFFFFFF`, the
   test passed anyway, the upload silently no-op'd against location `-1`,
   and `ortho_matrix_needs_update` was cleared permanently.

3. **A loop-counter collision that froze the game.**
   In `render_gl3.c`, the `SETUP_ATTRIBS` macro used `i` — the same variable
   as the enclosing texture-group loop. Desktop GL calls the macro once,
   before the loop, so it is harmless there. The GLES `BaseVertex` emulation
   must call it *inside* the loop, which reset `i` to the attribute count on
   every iteration: the group index oscillated 3 → 4 → 3 → 4 forever. This
   only triggered on geometry with several texture groups and differing
   `startVert`, which is why the menu VDU hung and simple scenes did not.

Also fixed:

- `PlotHighlightPoly()` used the result of `FindFreeScrPoly()` without
  checking for `-1`, writing to `ScrPolys[65535]`.
- `TEXTINFO.char1x` was seeded to `999.0F` as a running minimum over glyph
  x-positions. At 1080p `ModeScaleX` is 6.0 and real positions exceed 999,
  so the reduction never fired and the menu highlight box anchored to the
  left of its text. Now seeded from the framebuffer width.
- SDL2 reports **instance ids** in joystick events, not the device indices
  used by `SDL_JoystickOpen()`. The handlers indexed state arrays with
  `event->which`, so a controller reconnecting past `MAX_JOYSTICKS` wrote
  out of bounds. Added an instance→slot map.
- `SDLControllerManager.onGenericMotion()` matched `event.getSource()` with
  an equality `switch` against `SOURCE_JOYSTICK`. Sources are a **bitmask**
  and a gamepad reports `KEYBOARD|GAMEPAD|JOYSTICK`, so no case ever matched
  and all analog stick motion was discarded. Now bit-tested.
- Newer ENet API signatures (`enet_host_create`/`enet_host_connect`) and a
  few missing `util.h` includes.

## Layout

```
android/
  app/
    AndroidManifest.xml
    java/org/forsakenx/forsaken/ForsakenActivity.java   entry point
    java/org/libsdl/app/                                SDL2 Java glue
    res/                                                launcher icons
  scripts/
    android-env.sh          toolchain paths
    build-game-android.sh   compile + link libmain.so
    mkapk.sh                javac -> d8 -> aapt2 -> zip -> apksigner
  kit/
    gles-port.patch         original GLES port patch
```

The engine sources themselves live at the repository root, as in upstream.

## Building

The build runs **on-device under Termux**, because the NDK's prebuilt clang
is x86_64-only. It uses Termux's native aarch64 clang against the NDK
sysroot, then rewrites the resulting ELF with `patchelf`:

- `--remove-rpath`
- `--replace-needed libSDL2.so.10 libSDL2.so`
- `--replace-needed libGLESv2.so.2 libGLESv2.so`
- `--replace-needed libEGL.so.1 libEGL.so`

Requirements: `clang`, `patchelf`, `aapt2`, `d8`, `apksigner`, a JDK, an NDK
sysroot (r29 used here) and `android.jar`.

```sh
bash android/scripts/build-game-android.sh   # -> libmain.so
bash android/scripts/mkapk.sh                # -> forsaken.apk
```

> The scripts currently hardcode Termux paths under
> `/data/data/com.termux/files/home`. Adjust `android-env.sh` for another
> layout.

Gradle is deliberately not used — the Android build-tools jars available
here are x86_64, so the APK is assembled by hand.

## Game data

The APK ships only the engine. Forsaken's data (~185 MB) must be placed in
the app's internal storage:

```sh
adb install -r forsaken.apk
adb shell am start -n org.forsakenx.forsaken/.ForsakenActivity
adb shell am force-stop org.forsakenx.forsaken

adb push fdata.tar.gz /sdcard/
adb shell "cat /sdcard/fdata.tar.gz | \
  run-as org.forsakenx.forsaken sh -c 'cd files && tar xzf -'"
```

`run-as` works because the manifest sets `debuggable=true`; remove that for
a real release build. Android 13 blocks writing to another app's
`Android/data`, which is why the tarball is piped through `run-as`.

`ForsakenActivity.resolveDataDir()` prefers whichever candidate directory
actually contains `data/`, then passes it to the engine with `-chdir`, which
must be the final argument. Note `convert_path()` lowercases every path on
non-Windows builds, so the on-disk directories must be lowercase.

Resolution follows the panel: `getRealMetrics()`, normalised to landscape
and capped at 1920 on the long edge.

## Controls

A game controller is picked up automatically.

| Input | Action |
|---|---|
| Left stick | Thrust forward/back, strafe left/right |
| Right stick | Pitch (pull back = nose up), yaw |
| Right trigger | Primary fire |
| LB / RB | Roll left / right |
| Left trigger / Y | Thrust down / up |
| B / X | Turbo / drop mine |
| D-pad ←→ | Cycle primary weapons |
| D-pad ↑↓ | Cycle secondary weapons |

Saved joystick bindings in `pilots/player.txt` take priority over the
built-in defaults — `read_config()` marks each pad `assigned` before
`DefaultJoystickSettings()` runs, and that function skips anything already
assigned. Delete the `JOYSTICK` block from that file to pick up new defaults.

## Status

Working: renderer, menus, level loading, gameplay, audio, launcher icon,
adaptive resolution, controller auto-configuration.

Not done yet: on-screen touch controls (a virtual gamepad feeding the same
control path), and full verification that controller input reaches the ship
— the mapping is applied and the source-bitmask fix is in, but it has not
been confirmed on hardware.

## Licence

GPL v2, inherited from upstream ForsakenX. See `LICENSE`.
