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

## ⚠️ Known limitation: asset loading

The app **builds, installs and launches**, but **model loading does not work on
device yet.** [obj_loader.h](../obj_loader.h) and `asset_path()` in
[sdl3-glsl.c](../sdl3-glsl.c) read files with `fopen()`/`fgets()`, which cannot
see files packed inside the APK — they're served by Android's `AAssetManager`,
not the filesystem. The models are bundled correctly (`assets/assets/*` in the
APK); only the *reading* side needs changing.

**The fix (next step):** route file reads through `SDL_IOStream`. On Android
`SDL_IOFromFile("assets/head.obj", "rb")` resolves relative paths against the
APK's asset manager automatically, and on desktop it reads the filesystem — so
one code path serves both. Concretely:

1. Replace `fopen`/`fgets`/`fread`/`fclose` in `obj_loader.h` with
   `SDL_IOFromFile` / `SDL_ReadIO` (or load whole-file via `SDL_LoadFile_IO`
   then parse from memory).
2. On Android, skip the `SDL_GetBasePath()` walk-up in `asset_path()` and pass
   the relative `"assets/..."` path straight to `SDL_IOFromFile`.
3. Decode textures from the in-memory buffer (`stbi_load_from_memory`) instead
   of `stbi_load` on a path.

This is the same desktop C/GLSL code; only its I/O layer changes.
