package org.globulation.glob2;

import org.libsdl.app.SDLActivity;

public final class Glob2Activity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }
}
