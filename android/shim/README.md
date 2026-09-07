# Shim libs for single-APK Mali/Adreno/PowerVR

- `vaofix.c`: VAO + viewport + texture wrap fixes for Adreno 710 (also fixes Mali strict validator). Built as `libvaofix.so` and added via `patchelf --add-needed` + `wrap.sh` `LD_PRELOAD`.
- `menufix.c`: disables `SDL_ACCELEROMETER_AS_JOYSTICK` hint, blocks `Android Accelerometer` joystick, and adds touch menu nav (left stick -> DPAD, FIRE -> RETURN).

Both are `DT_NEEDED` of `libmain.so` + `wrap.sh` for `LD_PRELOAD` on debuggable builds.
