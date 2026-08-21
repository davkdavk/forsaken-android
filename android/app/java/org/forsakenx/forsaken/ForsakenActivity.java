package org.forsakenx.forsaken;

import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import java.io.File;
import java.lang.reflect.Method;

import org.libsdl.app.SDLActivity;

public class ForsakenActivity extends SDLActivity {

    private static final String TAG = "forsaken";

    private static String dataDir = null;

    /**
     * The engine requires Configs/, Data/, Pilots/ and Scripts/ to exist in
     * the process working directory. Game data may have been installed to
     * either internal or external app storage, so prefer whichever actually
     * holds the payload; fall back to internal storage.
     */
    private String resolveDataDir() {
        if (dataDir != null) return dataDir;

        File internal = getFilesDir();
        File external = getExternalFilesDir(null);

        File chosen = null;
        if (internal != null && new File(internal, "data").isDirectory()) {
            chosen = internal;
        } else if (external != null && new File(external, "data").isDirectory()) {
            chosen = external;
        } else if (internal != null) {
            chosen = internal;
        } else {
            chosen = external;
        }

        chosen.mkdirs();
        dataDir = chosen.getAbsolutePath();
        return dataDir;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The engine loads everything via relative paths ("data/..."), so the
        // process working directory must point at the game data before
        // SDL_main runs. Two mechanisms, in order of preference:
        //   1. android.system.Os.chdir() - a @hide API absent from the public
        //      android.jar stub, so resolved reflectively.
        //   2. the engine's own "-chdir <path>" CLI option (see getArguments).
        try {
            String path = resolveDataDir();
            boolean ok = false;
            try {
                Class<?> osClass = Class.forName("android.system.Os");
                for (Method m : osClass.getMethods()) {
                    if (m.getName().equals("chdir")
                            && m.getParameterTypes().length == 1
                            && m.getParameterTypes()[0] == String.class) {
                        m.invoke(null, path);
                        ok = true;
                        break;
                    }
                }
            } catch (Throwable t) {
                Log.w(TAG, "Os.chdir reflection failed: " + t);
            }
            Log.i(TAG, "chdir(" + ok + ") -> " + path
                    + " (data present: " + new File(path, "data").isDirectory() + ")");
        } catch (Exception e) {
            Log.e(TAG, "chdir failed", e);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        // Order matters: SDL2 first, then the game (libmain.so).
        return new String[] { "SDL2", "main" };
    }

    /**
     * Physical resolution of the default display, ignoring any window
     * insets (nav bar / cutout). The engine renders to the full surface,
     * and touch-overlay hit boxes are derived from the same numbers, so
     * this must be the real panel size rather than the app window size.
     */
    private int[] displaySize() {
        int w = 1920, h = 1080;
        try {
            Display d = getWindowManager().getDefaultDisplay();
            DisplayMetrics m = new DisplayMetrics();
            d.getRealMetrics(m);
            w = m.widthPixels;
            h = m.heightPixels;
        } catch (Throwable t) {
            Log.w(TAG, "displaySize: falling back to 1920x1080", t);
        }
        // Forsaken is a landscape game; the manifest locks orientation but
        // getRealMetrics can still report portrait before the rotation
        // settles, so normalise here.
        if (h > w) { int t2 = w; w = h; h = t2; }

        // The engine scales its 320x200 virtual canvas by w/320 and h/200.
        // Absurdly large surfaces just waste fill rate, so cap the long edge.
        final int MAX_W = 1920;
        if (w > MAX_W) {
            h = (int) Math.round((double) h * MAX_W / (double) w);
            w = MAX_W;
        }
        // Keep dimensions even - some GLES drivers dislike odd surface sizes.
        w &= ~1; h &= ~1;
        return new int[] { w, h };
    }

    @Override
    protected String[] getArguments() {
        int[] wh = displaySize();
        String mode = "-mode:" + wh[0] + ":" + wh[1];
        Log.i(TAG, "requesting mode " + wh[0] + "x" + wh[1]);

        return new String[] {
            mode,
            "-Debug",
            "-chdir", "\"" + resolveDataDir() + "\""
        };
    }
}
