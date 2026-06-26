cmake_minimum_required(VERSION 3.16)
project(sdl3-gl C)

set(CMAKE_C_STANDARD 11)

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)   # provides OpenGL::GL and OpenGL::GLU

add_executable(sdl3-gl sdl3-gl.c)   # parse.h is #included by the .c
target_link_libraries(sdl3-gl PRIVATE SDL3::SDL3 OpenGL::GL OpenGL::GLU)

# No asset-copy step is needed: at runtime the program walks up from the
# executable's directory (via SDL_GetBasePath()) to locate the assets/
# folder, so it finds the project's assets/ whether the binary lives in
# build/ or anywhere under the project tree.
