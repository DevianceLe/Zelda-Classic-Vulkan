package com.deviance.zcvulkan;

import org.libsdl.app.SDLActivity;

/**
 * zplayer's activity. SDL3 is linked statically into libzplayer.so, which
 * also holds the game and exports SDL_main (src/port/entry/zplayer_main.cpp).
 */
public class ZPlayerActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "zplayer" };
    }

    /**
     * The player's command line, from the launch intent's "args" extra, e.g.
     *   adb shell am start -n com.deviance.zcvulkan/.ZPlayerActivity --es args "-replay walk.zcr -framehash fh.txt"
     * Paths are relative to the game folder (internal storage, files/game).
     */
    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty())
            return new String[0];
        return args.trim().split("\\s+");
    }
}
