cmake_minimum_required(VERSION 3.22)

project(sdl3glsl C)

# Repo root: android/app/jni -> ../../../
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# --- SDL3 (built from the pinned submodule at android/SDL) ---
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../SDL" SDL)

# --- The app ---
# SDLActivity loads the libraries "SDL3" and "main" by default, so the app's
# entry-point library MUST be named "main" (-> libmain.so). sdl3-glsl.c includes
# <SDL3/SDL_main.h>, which on Android renames main() to SDL_main() for SDL's
# JNI bridge to call.
add_library(main SHARED
    "${REPO_ROOT}/sdl3-glsl.c"
)

# third_party holds stb_image (texture decode) used by obj_loader.h. The GL/GLES
# split inside the source auto-selects the ES path on __ANDROID__; USE_GLES is
# defined too for parity with the desktop GLES configure.
target_include_directories(main PRIVATE "${REPO_ROOT}/third_party")
target_compile_definitions(main PRIVATE USE_GLES)

# GLESv3 + EGL are provided by the platform; android/log are NDK system libs.
target_link_libraries(main PRIVATE
    SDL3::SDL3
    GLESv3
    EGL
    android
    log
    m
)
