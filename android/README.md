# Android build (SDL3 + OpenGL ES 3.0)

A Gradle/NDK wrapper that packages the modern renderer ([sdl3-glsl.c](../sdl3-glsl.c))
as an APK. The C/GLSL code is unchanged — it already auto-selects the GLES 3.0
path on `__ANDROID__` (see the `APP_USE_GLES` switch in the source).

## Layout

```
android/
  SDL/              git submodule, libsdl-org/SDL @ release-3.4.10 (source + Java glue)
  app/
    jni/CMakeLists.txt          builds libSDL3.so + libmain.so (the app, from ../../../sdl3-glsl.c)
    src/main/AndroidManifest.xml
    src/main/java/org/sdl3gl/MainActivity.java   extends org.libsdl.app.SDLActivity
    src/main/assets/assets -> ../../../../../assets   symlink; bundles models/textures
  build.gradle, settings.gradle, gradle.properties, gradlew, local.properties
```

The app's native library is named **`main`** because `SDLActivity` loads the
libraries `{"SDL3", "main"}` by default; `sdl3-glsl.c` includes `<SDL3/SDL_main.h>`,
which renames `main()` to `SDL_main()` for SDL's JNI bridge to call.

SDL's Java sources (`org.libsdl.app.*`) and the SDL native source are referenced
straight from the submodule (`sourceSets` + `add_subdirectory`), so there is no
vendored copy to keep in sync. The assets symlink lands files at APK path
`assets/<file>`, matching the `"assets/..."` prefixes the C code passes to the loader.

## Prerequisites

Set up once (already done on this machine — see the repo's setup):

- JDK 21, Android SDK at `~/apps/android-sdk` (`ANDROID_HOME`)
- Platform `android-35`, build-tools `35.0.0`, NDK `30.0.14904198`
- `android/local.properties` points `sdk.dir` at the SDK (machine-specific, git-ignored)

First clone must pull the submodule:

```bash
git submodule update --init --depth 1 android/SDL
```

## Build

```bash
cd android
./gradlew assembleDebug          # -> app/build/outputs/apk/debug/app-debug.apk
```

The first build downloads Gradle 8.12 and compiles SDL3 for each ABI
(`arm64-v8a`, `x86_64`), so it takes a few minutes; later builds are incremental.

## Install & run

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.sdl3gl/.MainActivity
adb logcat -s SDL SDL3GL                 # logs
```

`x86_64` is included so it runs on the standard emulator; drop it from
`abiFilters` in [app/build.gradle](app/build.gradle) for device-only builds.

## Asset loading

Asset I/O goes through `SDL_IOStream`, so one code path serves desktop and
Android. `SDL_LoadFile`/`SDL_IOFromFile` read the filesystem on desktop/Termux
and the APK's `AAssetManager` on Android, where the assets have no filesystem
path. Specifically:

- `asset_path()` in [sdl3-glsl.c](../sdl3-glsl.c) probes the relative
  `"assets/..."` path with `SDL_IOFromFile` first (this is what hits the APK
  asset manager on Android); on desktop it falls back to walking up from
  `SDL_GetBasePath()` so a build run from `build/` still finds `assets/`.
- [obj_loader.h](../obj_loader.h) loads each `.obj`/`.mtl` whole via
  `SDL_LoadFile` and parses it from memory.
- `load_texture()` reads the image with `SDL_LoadFile` and decodes it with
  `stbi_load_from_memory`.

The assets are bundled at APK path `assets/<file>` (via the
`app/src/main/assets/assets` symlink), matching the `"assets/..."` prefixes the
code passes to the loader.

**Verified on desktop** (GL and GLES configs load all models/textures) **and on
a real Android device** — the APK installs, loads the models from the APK assets,
and renders with GPU hardware acceleration. Install with:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n org.sdl3gl/.MainActivity
adb logcat -s SDL3GL SDL   # expect "model N: ..." and "texture loaded: ..." lines
```

The hosted APK (rebuild, then `rsync` to the server) is at
http://arkenidar.com/android/apk-files/sdl3-gl.apk — opening that URL on the
phone installs it without a cable.
