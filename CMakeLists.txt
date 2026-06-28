cmake_minimum_required(VERSION 3.16)
project(sdl3-gl C)

set(CMAKE_C_STANDARD 11)

find_package(SDL3 REQUIRED)

# --- Modern renderer: desktop OpenGL 3.3 core, or OpenGL ES 3.0 ---
# sdl3-glsl.c targets both APIs from one source (see the APP_USE_GLES switch in
# the file). The desktop build loads GL via GLAD (generated under glad/); the
# GLES build (Android/Termux, or desktop with -DSDL3GLSL_USE_GLES=ON against
# Mesa GLES) links the GLES library directly and skips GLAD. obj_loader.h loads
# OBJ/MTL/texture bundles and mat4.h does the matrix math; no GLU either way.
#
# Backend must be chosen explicitly — no default. This forces every fresh
# configure to state the API in use rather than silently building desktop GL.
if(NOT DEFINED SDL3GLSL_USE_GLES)
    message(FATAL_ERROR
        "SDL3GLSL_USE_GLES must be set explicitly: "
        "-DSDL3GLSL_USE_GLES=ON (OpenGL ES 3.0) or -DSDL3GLSL_USE_GLES=OFF (desktop GL 3.3).")
endif()
if(SDL3GLSL_USE_GLES)
    # GLES / mobile (Termux, Android): only the modern renderer. No desktop
    # OpenGL and no GLU are needed, and the legacy fixed-function sdl3-gl is
    # skipped — it relies on glBegin / the matrix stack / GLU, none of which
    # GLES (or Termux's Mesa, which ships no GLU) provides.
    add_executable(sdl3-glsl sdl3-glsl.c)              # no GLAD on the ES path
    target_include_directories(sdl3-glsl PRIVATE third_party)
    target_compile_definitions(sdl3-glsl PRIVATE USE_GLES)
    target_link_libraries(sdl3-glsl PRIVATE SDL3::SDL3 GLESv2 m)
else()
    find_package(OpenGL REQUIRED)   # provides OpenGL::GL and OpenGL::GLU

    # Legacy fixed-function app — desktop only; uses GLU (gluPerspective/gluLookAt).
    add_executable(sdl3-gl sdl3-gl.c)   # parse.h is #included by the .c
    target_link_libraries(sdl3-gl PRIVATE SDL3::SDL3 OpenGL::GL OpenGL::GLU m)

    # Modern renderer, desktop GL 3.3 via GLAD.
    add_executable(sdl3-glsl sdl3-glsl.c glad/src/gl.c)
    target_include_directories(sdl3-glsl PRIVATE glad/include third_party)
    target_link_libraries(sdl3-glsl PRIVATE SDL3::SDL3 OpenGL::GL m)
endif()

# No asset-copy step is needed: at runtime the program walks up from the
# executable's directory (via SDL_GetBasePath()) to locate the assets/
# folder, so it finds the project's assets/ whether the binary lives in
# build/ or anywhere under the project tree.
