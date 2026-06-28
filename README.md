# sdl3-gl

A small **SDL3 + legacy fixed-function OpenGL/GLU** demo in C. It opens a window,
loads Wavefront `.obj` models, and renders a rotating, lit scene. Originally an
SDL1 OpenGL tutorial (Michael Vance, 2000), ported SDL1 → SDL2 → **SDL3**.

A companion **modern renderer** — `sdl3-glsl`, core-profile **OpenGL 3.3 + GLSL**
with shaders, VAOs/VBOs, textures and shadow mapping — is built alongside it.
See [Modern renderer (`sdl3-glsl`)](#modern-renderer-sdl3-glsl).

## Features

- Window + OpenGL context creation via SDL3
- Wavefront `.obj` loader (positions, normals, triangular faces) in a single
  header, [parse.h](parse.h)
- Lit, smooth-shaded rendering with a rotating camera/model
- Three switchable scenes and a wireframe toggle
- Optional **VBO** (vertex-buffer-object) rendering path with automatic
  fall-back to immediate mode (see [Rendering paths](#rendering-paths))
- Working-directory-independent asset loading (see [Assets](#assets))
- A separate **modern OpenGL 3.3 + GLSL** renderer (`sdl3-glsl`) with GLAD,
  shader lighting, key-light shadow mapping, per-material textures and
  transparency, model cycling, and mouse/touch orbit — see
  [Modern renderer (`sdl3-glsl`)](#modern-renderer-sdl3-glsl)

## Requirements

- A C11 compiler (tested with gcc 14)
- [CMake](https://cmake.org/) ≥ 3.16
- **SDL3** (tested with 3.2.10)
- OpenGL and GLU development libraries (GLU is used only by the legacy target)

The modern renderer needs no extra packages: its GL loader (**GLAD**) and image
decoder (**stb_image**) are vendored in the repo (`glad/`, `third_party/`).

On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake libsdl3-dev libgl1-mesa-dev libglu1-mesa-dev
```

## Build & run

```sh
cmake -S . -B build -DSDL3GLSL_USE_GLES=OFF   # desktop GL 3.3; ON for OpenGL ES 3.0
cmake --build build
./build/sdl3-gl        # legacy fixed-function renderer
./build/sdl3-glsl      # modern core-profile 3.3 + GLSL renderer
```

`SDL3GLSL_USE_GLES` has no default and **must** be set at configure time —
`=OFF` for the desktop GL 3.3 backend, `=ON` for OpenGL ES 3.0 (see
[OpenGL ES 3.0 / mobile portability](#opengl-es-30--mobile-portability)). Both
executables build and run independently, and either can be launched from any
directory — they locate the `assets/` folder on their own (see below).

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

## Modern renderer (`sdl3-glsl`)

Alongside the legacy app the project builds a second executable, **`sdl3-glsl`**,
a **core-profile OpenGL 3.3 + GLSL** renderer. A core context exposes no
`glBegin`, matrix stack, or `glLight*`, so geometry lives in VAOs/VBOs,
transforms come from [mat4.h](mat4.h), and all lighting is done in shaders. GL
entry points are loaded with **GLAD** (generated under [glad/](glad/)); no GLU.

### What it does

- Loads OBJ/MTL/texture **bundles** via [obj_loader.h](obj_loader.h) — the
  modern counterpart to `parse.h` — into one interleaved, GPU-ready vertex array
  with per-material **submeshes**.
- Shades with **Blinn-Phong**: a single directional key light plus a flat
  ambient fill, with **key-light shadow mapping** (a depth pass from the light's
  view, sampled with 3×3 PCF) for self-shadowing.
- Applies per-material **textures** (`map_Kd`, decoded by the vendored
  [stb_image](third_party/stb_image.h)) and **transparency** (`d`); glass is
  lifted by Fresnel + specular so it reads as glass rather than near-invisible.
- Frames each model automatically from its bounding sphere; **mouse and touch**
  orbit/zoom.

### Controls

| Input | Action |
| ----- | ------ |
| Left-drag / one-finger drag | Orbit the camera |
| Mouse wheel / two-finger pinch | Zoom |
| `Enter` | Cycle model (car → table → head → cube) |
| `Space` | Toggle auto-rotation |
| `↑` / `↓` | Brighten / darken ambient fill |
| `O` | Cycle orientation presets (corrects Z-up exports) |
| `W` | Toggle wireframe |
| `C` | Toggle back-face culling (off by default — assets have mixed winding) |
| `Esc` | Quit |

### Asset bundles (OBJ + MTL + image)

A textured model is a self-describing folder of files — the native Wavefront
convention, no custom format needed:

- `model.obj` — geometry, with `mtllib model.mtl` and `usemtl <name>` per face
  group;
- `model.mtl` — one `newmtl` block per material (`Kd`/`Ka`/`Ks`/`Ns`/`d`) and an
  optional `map_Kd <image>` diffuse texture;
- the image file(s) referenced by `map_Kd`.

`obj_loader.h` reads the `.mtl`, groups triangles into per-material submeshes,
and resolves each `map_Kd` path relative to the `.mtl`; the renderer decodes the
image and binds it per submesh. The bundled **Oriental Table** exercises this
end-to-end (four textured materials); the **Victory Car** shows multi-material
colour with translucent glass.

### OpenGL ES 3.0 / mobile portability

`sdl3-glsl.c` builds from one source against **either** desktop OpenGL 3.3 core
**or** OpenGL ES 3.0 (Android, iOS, web), selected by the `APP_USE_GLES` switch
at the top of the file. GLES 3.0 implements the GL 3.3 subset the renderer uses
(VAOs, sized internal formats, depth textures, `glGetStringi`, GLSL
`#version 300 es`); the few desktop-only calls are handled per-backend:

- shaders carry no `#version` line — `compile_shader()` prepends `#version 330
  core` or `#version 300 es` plus the ES precision qualifiers;
- the GL loader (**GLAD**) is desktop-only; the ES path links the GLES library
  directly and skips it;
- `glPolygonMode` (wireframe, `W`) and `glDrawBuffer` don't exist in GLES —
  wireframe is a no-op there, and the depth FBO uses `glDrawBuffers`;
- `CLAMP_TO_BORDER` + depth border colour are replaced by a light-frustum bounds
  check in the shadow shader, so `CLAMP_TO_EDGE` (all GLES has) behaves the same;
- the shadow depth texture uploads as `GL_UNSIGNED_INT` (the format/type combo
  GLES requires for `DEPTH_COMPONENT24`).

To build and run the ES path on a desktop with Mesa GLES (a quick way to test
the mobile renderer without a device):

```sh
cmake -S . -B build-gles -DSDL3GLSL_USE_GLES=ON   # links GLESv2, defines USE_GLES
cmake --build build-gles
./build-gles/sdl3-glsl
```

#### On a real device via Termux (no APK, no code changes)

[Termux](https://termux.dev/) is a Linux userland on Android, so the GLES build
runs there **unmodified** — assets live on the real filesystem, so the
`fopen`-based `asset_path()` works as-is. You only need a display server and a
GLES-capable Mesa. Install [Termux:X11](https://github.com/termux/termux-x11),
then in Termux:

```sh
pkg install x11-repo
pkg install termux-x11-nightly mesa mesa-dev sdl3 cmake clang   # verify: pkg search sdl3
# launch the Termux:X11 app, then back in Termux:
export DISPLAY=:0
cmake -S . -B build-gles -DSDL3GLSL_USE_GLES=ON
cmake --build build-gles
./build-gles/sdl3-glsl
```

Notes: if `sdl3` isn't packaged for your Termux, build SDL3 from source. GLES is
served through the host GPU via **virgl** or **zink-over-Vulkan** (Turnip on
Adreno, Panfrost on Mali), so performance depends on the device and driver. The
GLES configure needs **no GLU** and does **not** build the legacy fixed-function
`sdl3-gl` — that target is desktop-only (it uses GLU and the matrix stack), so it
is skipped automatically when `SDL3GLSL_USE_GLES=ON`.

#### Packaged APK

**Android** has a Gradle/NDK wrapper under [android/](android/) — see
[android/README.md](android/README.md). It builds an installable APK that bundles
SDL3 (a pinned `libsdl-org/SDL` submodule) and the modern renderer as `libmain.so`
via `SDL_main`:

```bash
cd android
git submodule update --init --depth 1 SDL    # first time
./gradlew assembleDebug                       # -> app/build/outputs/apk/debug/app-debug.apk
```

Asset loading is routed through `SDL_IOStream` (`SDL_LoadFile`/`SDL_IOFromFile`),
so the same code reads the filesystem on desktop/Termux and the APK's
`AAssetManager` on Android. The models are bundled at APK path `assets/<file>`
and the loader resolves them there. Verified on the desktop GL/GLES builds;
on-device run is documented but not yet exercised. Details in
[android/README.md](android/README.md#asset-loading).

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
| `sdl3-gl.c`      | Legacy program: window, input, fixed-function OpenGL |
| `parse.h`        | Self-contained Wavefront `.obj` parser/loader      |
| `sdl3-glsl.c`    | Modern core-profile 3.3 + GLSL renderer            |
| `obj_loader.h`   | Modern OBJ/MTL/texture loader (per-material submeshes) |
| `mat4.h`         | Column-major matrix/vector math for the modern path |
| `glad/`          | Generated GLAD OpenGL 3.3 core loader               |
| `third_party/`   | Vendored `stb_image.h` (image decoding)            |
| `assets/`        | `.obj` models, `.mtl` materials, and textures      |
| `CMakeLists.txt` | Build configuration (both targets)                 |
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
