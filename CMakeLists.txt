cmake_minimum_required(VERSION 3.16)
project(sdl3-gl C)

set(CMAKE_C_STANDARD 11)

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)   # provides OpenGL::GL and OpenGL::GLU

add_executable(sdl3-gl sdl3-gl.c)   # parse.h is #included by the .c
target_link_libraries(sdl3-gl PRIVATE SDL3::SDL3 OpenGL::GL OpenGL::GLU m)

# --- Modern renderer: core-profile 3.3 + GLSL (alongside the legacy app) ---
# Uses GLAD (generated under glad/) to load GL entry points, obj_loader.h to
# load OBJ/MTL/texture bundles, and mat4.h for matrix math. No GLU; GLAD
# supplies all GL functions at runtime.
add_executable(sdl3-glsl sdl3-glsl.c glad/src/gl.c)
target_include_directories(sdl3-glsl PRIVATE glad/include third_party)
target_link_libraries(sdl3-glsl PRIVATE SDL3::SDL3 OpenGL::GL m)

# No asset-copy step is needed: at runtime the program walks up from the
# executable's directory (via SDL_GetBasePath()) to locate the assets/
# folder, so it finds the project's assets/ whether the binary lives in
# build/ or anywhere under the project tree.
