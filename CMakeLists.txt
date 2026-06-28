cmake_minimum_required(VERSION 3.16)
project(sdl3-gl C)

set(CMAKE_C_STANDARD 11)

find_package(SDL3 REQUIRED)

# --- Three renderers, each its own target ---
# The modern renderer sdl3-glsl.c targets both desktop OpenGL 3.3 and OpenGL ES
# 3.0 from one source (see the APP_USE_GLES switch in the file): the desktop
# build loads GL via GLAD (generated under glad/), while the GLES build defines
# USE_GLES and links the GLES library directly, skipping GLAD. obj_loader.h
# loads OBJ/MTL/texture bundles and mat4.h does the matrix math; no GLU either
# way. The legacy fixed-function sdl3-gl.c is desktop-only (glBegin / the matrix
# stack / GLU).
#
# Each flavor is a distinct, independently buildable target gated by its own
# option. Defaults follow what the platform actually provides — detected, not
# required — so a box without libGLESv2 (Windows/macOS) or without GLU (Termux)
# simply doesn't build what it can't.
find_package(OpenGL)                       # sets OPENGL_FOUND, OPENGL_GLU_FOUND
find_library(GLES_V2_LIB NAMES GLESv2)

if(GLES_V2_LIB)
    set(_gles_default ON)
else()
    set(_gles_default OFF)
endif()

option(SDL3GL_BUILD_GL        "Legacy fixed-function renderer (sdl3-gl)"        ${OPENGL_GLU_FOUND})
option(SDL3GL_BUILD_GLSL      "Modern desktop GL 3.3 renderer (sdl3-glsl)"      ${OPENGL_FOUND})
option(SDL3GL_BUILD_GLSL_GLES "Modern GLES 3.0 renderer (sdl3-glsl-gles)"       ${_gles_default})

if(NOT (SDL3GL_BUILD_GL OR SDL3GL_BUILD_GLSL OR SDL3GL_BUILD_GLSL_GLES))
    message(FATAL_ERROR
        "No targets enabled: set at least one of "
        "SDL3GL_BUILD_GL / SDL3GL_BUILD_GLSL / SDL3GL_BUILD_GLSL_GLES to ON.")
endif()

if((SDL3GL_BUILD_GL OR SDL3GL_BUILD_GLSL) AND NOT OPENGL_FOUND)
    message(FATAL_ERROR "Desktop GL targets enabled but OpenGL was not found.")
endif()

if(SDL3GL_BUILD_GL)
    # Legacy fixed-function app — desktop only; uses GLU (gluPerspective/gluLookAt).
    add_executable(sdl3-gl sdl3-gl.c)   # parse.h is #included by the .c
    target_link_libraries(sdl3-gl PRIVATE SDL3::SDL3 OpenGL::GL OpenGL::GLU m)
endif()

if(SDL3GL_BUILD_GLSL)
    # Modern renderer, desktop GL 3.3 via GLAD.
    add_executable(sdl3-glsl sdl3-glsl.c glad/src/gl.c)
    target_include_directories(sdl3-glsl PRIVATE glad/include third_party)
    target_link_libraries(sdl3-glsl PRIVATE SDL3::SDL3 OpenGL::GL m)
endif()

if(SDL3GL_BUILD_GLSL_GLES)
    # Modern renderer, OpenGL ES 3.0 — links GLESv2 directly, no GLAD.
    add_executable(sdl3-glsl-gles sdl3-glsl.c)
    target_include_directories(sdl3-glsl-gles PRIVATE third_party)
    target_compile_definitions(sdl3-glsl-gles PRIVATE USE_GLES)
    target_link_libraries(sdl3-glsl-gles PRIVATE SDL3::SDL3 GLESv2 m)
endif()

# No asset-copy step is needed: at runtime the program walks up from the
# executable's directory (via SDL_GetBasePath()) to locate the assets/
# folder, so it finds the project's assets/ whether the binary lives in
# build/ or anywhere under the project tree.
