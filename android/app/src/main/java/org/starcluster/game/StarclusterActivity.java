package org.starcluster.game;

import org.libsdl.app.SDLActivity;

// SDLActivity по умолчанию грузит только libSDL2 и libmain. Игре нужен ещё и
// libSDL2_mixer, причём ДО libmain — иначе линковщик не найдёт Mix_*.
public class StarclusterActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "SDL2_mixer",
            "main"
        };
    }
}
