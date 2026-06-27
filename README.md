# sdl3-gl

A small **SDL3 + legacy fixed-function OpenGL/GLU** demo in C. It opens a window,
loads Wavefront `.obj` models, and renders a rotating, lit scene. Originally an
SDL1 OpenGL tutorial (Michael Vance, 2000), ported SDL1 → SDL2 → **SDL3**.

## Features

- Window + OpenGL context creation via SDL3
- Wavefront `.obj` loader (positions, normals, triangular faces) in a single
  header, [parse.h](parse.h)
- Lit, smooth-shaded rendering with a rotating camera/model
- Three switchable scenes and a wireframe toggle
- Optional **VBO** (vertex-buffer-object) rendering path with automatic
  fall-back to immediate mode (see [Rendering paths](#rendering-paths))
- Working-directory-independent asset loading (see [Assets](#assets))

## Requirements

- A C11 compiler (tested with gcc 14)
- [CMake](https://cmake.org/) ≥ 3.16
- **SDL3** (tested with 3.2.10)
- OpenGL and GLU development libraries

On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake libsdl3-dev libgl1-mesa-dev libglu1-mesa-dev
```

## Build & run

```sh
cmake -S . -B build
cmake --build build
./build/sdl3-gl
```

The binary can be launched from any directory — it locates the `assets/` folder
on its own (see below).

## Controls

| Key            | Action                          |
| -------------- | ------------------------------- |
| `Space`        | Toggle rotation                 |
| `Enter`        | Cycle through the 3 scenes      |
| `W`            | Toggle wireframe rendering      |
| `B`            | Toggle VBO / immediate-mode path (when VBOs are available) |
| `Esc`          | Quit                            |
| Window close   | Quit                            |

## Rendering paths

The model renderer has two interchangeable back-ends that produce identical
output:

- **Immediate mode** — the original `glBegin(GL_TRIANGLES)` / `glVertex3f`
  path. Always available; used as the fallback.
- **VBO** — uploads each model once into an interleaved
  `[normal, position]` vertex-buffer object and draws it with
  `glDrawArrays` plus the fixed-function client-state arrays
  (`glNormalPointer` / `glVertexPointer`).

The VBO entry points are part of OpenGL 1.5, which the legacy `<GL/gl.h>`
typically does not expose, so they are resolved at runtime via
`SDL_GL_GetProcAddress()` (using the `PFNGL...PROC` typedefs from
`<GL/glext.h>`). No extension loader such as GLEW/GLAD is required. If the
functions cannot be loaded the program logs a notice and stays in immediate
mode; otherwise it defaults to the VBO path. Press `B` to switch between the
two at runtime. See `load_gl_vbo_functions()`, `upload_model_vbo()` and
`draw_model_vbo()` in [sdl3-gl.c](sdl3-gl.c).

## Assets

Models live in [assets/](assets/) (`head.obj`, `cube.obj`). At startup the
program resolves each asset by starting at the executable's directory
(`SDL_GetBasePath()`) and walking **up** the parent directories until the file is
found. This means it works whether the binary sits next to `assets/` or in a
subdirectory such as `build/` while `assets/` stays in the project root — and it
does not depend on the current working directory. See `asset_path()` in
[sdl3-gl.c](sdl3-gl.c).

## VS Code

The [.vscode/](.vscode/) folder provides:

- **tasks.json** — `cmake configure`, `cmake build` (default build task,
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd>), and `run`
- **launch.json** — a gdb (`cppdbg`) debug configuration; press <kbd>F5</kbd> to
  build and debug

Debugging requires the
[C/C++ extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools).

## Project layout

| Path             | Purpose                                            |
| ---------------- | -------------------------------------------------- |
| `sdl3-gl.c`      | Main program: window, input, OpenGL rendering      |
| `parse.h`        | Self-contained Wavefront `.obj` parser/loader      |
| `assets/`        | `.obj` models                                      |
| `CMakeLists.txt` | Build configuration (SDL3 + OpenGL + GLU)          |
| `.vscode/`       | Editor build tasks and debug launch config         |

## SDL2 → SDL3 migration notes

The OpenGL/GLU rendering code is unchanged across the migration. The SDL-facing
changes were:

- `SDL_Init()` now returns `bool` (true on success) — check `!SDL_Init(...)`
- `SDL_CreateWindow()` dropped the x/y position arguments
- Event types renamed: `SDL_KEYDOWN` / `SDL_QUIT` →
  `SDL_EVENT_KEY_DOWN` / `SDL_EVENT_QUIT`
- `SDL_Keysym` was removed — the keycode is now `event.key.key` (`SDL_Keycode`)
- Letter keycodes are uppercase: `SDLK_w` → `SDLK_W`
- Header path: `<SDL2/SDL.h>` → `<SDL3/SDL.h>`

## Credits & license

This project is licensed under the **MIT License** — see [LICENSE](LICENSE).

It began as the *SDL OpenGL Tutorial* by Michael Vance (© 2000), distributed
under the **LGPL**. Portions of [sdl3-gl.c](sdl3-gl.c) still derived from that
tutorial remain available under the LGPL, which is compatible with combining
them in this MIT-licensed work; retain both notices when redistributing.
