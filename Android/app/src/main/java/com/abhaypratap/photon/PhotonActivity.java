package com.abhaypratap.photon;

import org.libsdl.app.SDLActivity;

public class PhotonActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "main"
        };
    }
}
