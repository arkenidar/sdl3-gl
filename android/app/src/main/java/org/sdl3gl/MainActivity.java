package org.sdl3gl;

import org.libsdl.app.SDLActivity;

/**
 * Entry activity. SDLActivity handles the GL surface, input and the JNI bridge
 * that calls the native main() (SDL_main) in libmain.so. Subclassing only to
 * give the app its own package/name; no overrides are needed for the default
 * "SDL3" + "main" library set.
 */
public class MainActivity extends SDLActivity {
}
