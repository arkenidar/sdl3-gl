// sdl3-glsl.c
// Modern OpenGL (core profile 3.3 + GLSL) renderer, alongside the legacy
// fixed-function sdl3-gl.c. Loads a Wavefront model via obj_loader.h and draws
// it with a VAO/VBO and a Blinn-Phong shader, one draw call per material run.
//
// How it differs from sdl3-gl.c (legacy):
//   - requests a CORE 3.3 context (no fixed-function pipeline available);
//   - functions are loaded through GLAD instead of being linked directly;
//   - matrices come from mat4.h (no gluPerspective/gluLookAt/matrix stack);
//   - geometry lives in a VAO/VBO and is lit by a GLSL shader, not glLight*.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Dario Cangialosi. See LICENSE.
//
// Build via CMake (target `sdl3-glsl`); see CMakeLists.txt. The legacy
// `sdl3-gl` target is unaffected.

#include <glad/gl.h>     /* must precede any other GL header */
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "obj_loader.h"
#include "mat4.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* ------------------------------------------------------------------ *
 * Asset path resolution (copied from sdl3-gl.c): walk up from the
 * executable's directory until the requested relative path is found, so
 * the program runs from any working directory.
 * ------------------------------------------------------------------ */
static const char* asset_path(const char* relative)
{
    static char path[1024];
    char dir[1024];

    const char* base = SDL_GetBasePath(); /* SDL3: do not free */
    snprintf(dir, sizeof(dir), "%s", base ? base : "./");

    for (int level = 0; level < 16; level++) {
        snprintf(path, sizeof(path), "%s%s", dir, relative);
        FILE* f = fopen(path, "r");
        if (f) { fclose(f); return path; }

        size_t len = strlen(dir);
        if (len && (dir[len-1] == '/' || dir[len-1] == '\\')) dir[--len] = '\0';
        char* sep  = strrchr(dir, '/');
        char* bsep = strrchr(dir, '\\');
        if (bsep > sep) sep = bsep;
        if (!sep) break;
        sep[1] = '\0';
    }
    snprintf(path, sizeof(path), "%s%s", base ? base : "", relative);
    return path;
}

/* ------------------------------------------------------------------ *
 * Shaders. Attribute slots match obj_loader.h: 0 = normal, 1 = position,
 * 2 = texcoord (texcoord stays at its default when a model has none).
 * ------------------------------------------------------------------ */
static const char* VERTEX_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 aNormal;\n"
    "layout(location=1) in vec3 aPosition;\n"
    "layout(location=2) in vec2 aTexCoord;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "uniform mat3 uNormalMat;\n"
    "uniform mat4 uLightSpace;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec2 vUV;\n"
    "out vec4 vLightPos;\n"        /* fragment position in light clip space */
    "void main(){\n"
    "    vec4 wp = uModel * vec4(aPosition, 1.0);\n"
    "    vWorldPos = wp.xyz;\n"
    "    vNormal = uNormalMat * aNormal;\n"
    "    vUV = aTexCoord;\n"
    "    vLightPos = uLightSpace * wp;\n"
    "    gl_Position = uMVP * vec4(aPosition, 1.0);\n"
    "}\n";

/* Depth-only program: renders the scene from the light's viewpoint into the
   shadow map. Reuses attribute slot 1 (position); normals/uvs are ignored. */
static const char* DEPTH_VERTEX_SRC =
    "#version 330 core\n"
    "layout(location=1) in vec3 aPosition;\n"
    "uniform mat4 uLightSpace;\n"
    "uniform mat4 uModel;\n"
    "void main(){ gl_Position = uLightSpace * uModel * vec4(aPosition, 1.0); }\n";

static const char* DEPTH_FRAGMENT_SRC =
    "#version 330 core\n"
    "void main(){}\n";

static const char* FRAGMENT_SRC =
    "#version 330 core\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec2 vUV;\n"
    "uniform vec3 uViewPos;\n"
    "uniform vec3 uAmbient;\n"
    "uniform vec3 uKd;\n"
    "uniform vec3 uKs;\n"
    "uniform float uNs;\n"
    "uniform float uOpacity;\n"
    "uniform int  uHasTexture;\n"   /* reserved; textures arrive in the next slice */
    "uniform sampler2D uTex;\n"
    "uniform vec3 uLightDir;\n"     /* key light direction (world) */
    "uniform sampler2D uShadowMap;\n"
    "in vec4 vLightPos;\n"
    "out vec4 fragColor;\n"
    "\n"
    "// Fraction of the key light reaching this fragment (1 = lit, 0 = shadow),\n"
    "// from a 3x3 PCF lookup with a slope-scaled depth bias to avoid acne.\n"
    "float shadow_factor(vec3 N, vec3 L){\n"
    "    vec3 p = vLightPos.xyz / vLightPos.w;\n"
    "    p = p * 0.5 + 0.5;\n"
    "    if (p.z > 1.0) return 1.0;            // beyond the light's far plane\n"
    "    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0006);\n"
    "    float cur = p.z - bias;\n"
    "    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));\n"
    "    float sum = 0.0;\n"
    "    for (int x = -1; x <= 1; x++)\n"
    "        for (int y = -1; y <= 1; y++) {\n"
    "            float d = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;\n"
    "            sum += (cur > d) ? 0.0 : 1.0;\n"
    "        }\n"
    "    return sum / 9.0;\n"
    "}\n"
    "void main(){\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec3 V = normalize(uViewPos - vWorldPos);\n"
    "    // Two-sided shading: flip the normal for back faces using the triangle's\n"
    "    // winding (gl_FrontFacing), which is constant across a triangle. A\n"
    "    // view-based flip (dot(N,V)<0) instead crosses zero *within* a face at\n"
    "    // grazing angles, lighting one triangle of a flat quad and not the other\n"
    "    // - a rotating light/dark seam along the triangulation diagonal.\n"
    "    if (!gl_FrontFacing) N = -N;\n"
    "    vec3 albedo = (uHasTexture == 1) ? texture(uTex, vUV).rgb * uKd : uKd;\n"
    "    // one directional key light (Blinn-Phong) with self-shadowing, plus a\n"
    "    // flat ambient fill so shadowed areas stay readable.\n"
    "    vec3 L = normalize(uLightDir);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float diff = max(dot(N, L), 0.0);\n"
    "    float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), max(uNs, 1.0)) : 0.0;\n"
    "    float sh = shadow_factor(N, L);\n"
    "    vec3 color = uAmbient * albedo + sh * (albedo * diff + uKs * spec);\n"
    "    // Make transparent surfaces (glass) read: a small visibility floor,\n"
    "    // plus Fresnel (opaque at grazing angles) and specular highlights.\n"
    "    // Opaque materials (uOpacity = 1) clamp back to fully opaque.\n"
    "    float fres = pow(1.0 - max(dot(N, V), 0.0), 5.0);\n"
    "    float base = (uOpacity < 0.999) ? max(uOpacity, 0.18) : 1.0;\n"
    "    float alpha = clamp(base + fres + 0.5 * spec, 0.0, 1.0);\n"
    "    fragColor = vec4(color, alpha);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        SDL_Log("shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(const char* vs_src, const char* fs_src)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        SDL_Log("program link failed: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------ *
 * GPU model: a VAO/VBO wrapper around an obj_mesh. The interleaved data
 * and submesh/material tables are owned by the obj_mesh.
 * ------------------------------------------------------------------ */
typedef struct {
    GLuint   vao, vbo;
    obj_mesh mesh;
    GLuint*  material_tex;   /* one GL texture per material (0 = none) */
} gpu_model;

static bool gpu_model_upload(gpu_model* g)
{
    const obj_mesh* m = &g->mesh;
    GLsizei stride = (GLsizei)m->floats_per_vertex * (GLsizei)sizeof(float);

    glGenVertexArrays(1, &g->vao);
    glBindVertexArray(g->vao);
    glGenBuffers(1, &g->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)m->vertex_count * stride,
                 m->data, GL_STATIC_DRAW);

    /* normal @0, position @12, optional texcoord @24 */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(3*sizeof(float)));
    if (m->has_texcoords) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(6*sizeof(float)));
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

/* Decode an image (stb_image) and upload it as a 2D texture, caching by path so
   an image shared by several materials/models loads once. Returns 0 on miss. */
typedef struct { char path[OBJ_PATH_MAX]; GLuint tex; } tex_cache_entry;
static tex_cache_entry g_texcache[256];
static int g_texcache_count = 0;

static GLuint load_texture(const char* path)
{
    if (!path || !path[0]) return 0;
    for (int i = 0; i < g_texcache_count; i++)
        if (strcmp(g_texcache[i].path, path) == 0) return g_texcache[i].tex;

    int w, h, n;
    stbi_set_flip_vertically_on_load(1);          /* OBJ UV origin is bottom-left */
    unsigned char* data = stbi_load(path, &w, &h, &n, 4);
    if (!data) { SDL_Log("texture load failed: %s (%s)", path, stbi_failure_reason()); return 0; }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    stbi_image_free(data);

    if (g_texcache_count < 256) {
        snprintf(g_texcache[g_texcache_count].path, OBJ_PATH_MAX, "%s", path);
        g_texcache[g_texcache_count].tex = tex;
        g_texcache_count++;
    }
    SDL_Log("texture loaded: %s (%dx%d)", path, w, h);
    return tex;
}

/* Resolve each material's map_Kd into a GL texture for this model. */
static void gpu_model_load_textures(gpu_model* g)
{
    if (g->mesh.material_count == 0) { g->material_tex = NULL; return; }
    g->material_tex = (GLuint*)calloc(g->mesh.material_count, sizeof(GLuint));
    if (!g->material_tex) return;
    for (size_t i = 0; i < g->mesh.material_count; i++)
        g->material_tex[i] = load_texture(g->mesh.materials[i].diffuse_map);
}

/* Axis-aligned bounding box centre and radius over the model's positions
   (position lives at offset 3 within each fpv-stride vertex). Used to frame
   the camera so any model, whatever its export scale, fills the view. */
static void model_bounds(const obj_mesh* m, float center[3], float* radius)
{
    if (m->vertex_count == 0) { center[0]=center[1]=center[2]=0; *radius=1; return; }
    int fpv = m->floats_per_vertex;
    float lo[3], hi[3];
    for (int k = 0; k < 3; k++) lo[k] = hi[k] = m->data[3 + k];
    for (size_t v = 0; v < m->vertex_count; v++) {
        const float* p = &m->data[v*fpv + 3];
        for (int k = 0; k < 3; k++) {
            if (p[k] < lo[k]) lo[k] = p[k];
            if (p[k] > hi[k]) hi[k] = p[k];
        }
    }
    for (int k = 0; k < 3; k++) center[k] = 0.5f * (lo[k] + hi[k]);
    float dx = hi[0]-lo[0], dy = hi[1]-lo[1], dz = hi[2]-lo[2];
    *radius = 0.5f * sqrtf(dx*dx + dy*dy + dz*dz);
    if (*radius <= 0.0f) *radius = 1.0f;
}

/* Uniform locations resolved once after linking. */
typedef struct {
    GLint mvp, model, normalMat, viewPos, ambient;
    GLint kd, ks, ns, opacity, hasTexture, tex;
    GLint lightSpace, lightDir, shadowMap;
} uniforms;

static void cache_uniforms(GLuint p, uniforms* u)
{
    u->mvp        = glGetUniformLocation(p, "uMVP");
    u->model      = glGetUniformLocation(p, "uModel");
    u->normalMat  = glGetUniformLocation(p, "uNormalMat");
    u->viewPos    = glGetUniformLocation(p, "uViewPos");
    u->ambient    = glGetUniformLocation(p, "uAmbient");
    u->kd         = glGetUniformLocation(p, "uKd");
    u->ks         = glGetUniformLocation(p, "uKs");
    u->ns         = glGetUniformLocation(p, "uNs");
    u->opacity    = glGetUniformLocation(p, "uOpacity");
    u->hasTexture = glGetUniformLocation(p, "uHasTexture");
    u->tex        = glGetUniformLocation(p, "uTex");
    u->lightSpace = glGetUniformLocation(p, "uLightSpace");
    u->lightDir   = glGetUniformLocation(p, "uLightDir");
    u->shadowMap  = glGetUniformLocation(p, "uShadowMap");
}

/* Bind submesh i's material (colour, specular, optional map_Kd on unit 0) and
   draw it. */
static void draw_submesh(const gpu_model* g, const uniforms* u, size_t i)
{
    const obj_submesh* s = &g->mesh.submeshes[i];
    float kd[3] = {0.8f,0.8f,0.8f}, ks[3] = {0.2f,0.2f,0.2f};
    float ns = 32.0f, opacity = 1.0f;
    GLuint tex = 0;
    if (s->material >= 0) {
        const obj_material* mt = &g->mesh.materials[s->material];
        memcpy(kd, mt->diffuse,  sizeof kd);
        memcpy(ks, mt->specular, sizeof ks);
        ns = mt->shininess; opacity = mt->opacity;
        if (g->material_tex) tex = g->material_tex[s->material];
    }
    glUniform3fv(u->kd, 1, kd);
    glUniform3fv(u->ks, 1, ks);
    glUniform1f(u->ns, ns);
    glUniform1f(u->opacity, opacity);
    glUniform1i(u->hasTexture, tex ? 1 : 0);
    if (tex) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex); }
    glDrawArrays(GL_TRIANGLES, (GLint)s->first_vertex, (GLsizei)s->vertex_count);
}

/* Opacity of submesh i (1 when it has no material). */
static float submesh_opacity(const gpu_model* g, size_t i)
{
    int m = g->mesh.submeshes[i].material;
    return (m >= 0) ? g->mesh.materials[m].opacity : 1.0f;
}

/* Shadow map: a depth-only framebuffer + depth texture. CLAMP_TO_BORDER with a
   white (depth=1) border means samples outside the light frustum read as lit. */
#define SHADOW_DIM 2048
typedef struct { GLuint fbo, tex; } shadow_map;

static bool shadow_map_create(shadow_map* sm)
{
    glGenTextures(1, &sm->tex);
    glBindTexture(GL_TEXTURE_2D, sm->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_DIM, SHADOW_DIM,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    glGenFramebuffers(1, &sm->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sm->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sm->tex, 0);
    glDrawBuffer(GL_NONE);   /* depth only, no color attachment */
    glReadBuffer(GL_NONE);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) SDL_Log("shadow FBO incomplete");
    return ok;
}

/* The .obj files come from a converter that exports Z-up, so in a Y-up
   viewer the model lies on its side (this is in the files - other viewers show
   it too). These presets correct that; the `O` key cycles them so the right
   upright orientation can be picked live. Rotations are applied about the
   model centre so framing is unaffected. */
#define ORIENT_COUNT 6
static mat4 orientation_matrix(int idx)
{
    const float H = (float)M_PI * 0.5f;
    switch (((idx % ORIENT_COUNT) + ORIENT_COUNT) % ORIENT_COUNT) {
        case 0: return mat4_identity();
        case 1: return mat4_rotate(-H, 1, 0, 0);   /* Z-up -> Y-up (default) */
        case 2: return mat4_rotate( H, 1, 0, 0);
        case 3: return mat4_rotate((float)M_PI, 1, 0, 0);
        case 4: return mat4_rotate(-H, 0, 0, 1);
        case 5: return mat4_rotate( H, 0, 0, 1);
    }
    return mat4_identity();
}

int main(int argc, char* argv[])
{
    (void)argc; (void)argv;
    int width = 960, height = 720;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Request an OpenGL 3.3 CORE context before creating the window. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    /* Open maximized (full-size) but resizable; `width`/`height` are the
       restored-down size. The render loop reads the actual pixel size each
       frame, so it adapts to maximize/resize automatically. */
    SDL_Window* window = SDL_CreateWindow("sdl3-glsl (modern OpenGL)",
                                          width, height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                          SDL_WINDOW_MAXIMIZED);
    if (!window) { fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError()); return 1; }

    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx) { fprintf(stderr, "GL context failed: %s\n", SDL_GetError()); return 1; }

    int glver = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
    if (glver == 0) { fprintf(stderr, "gladLoadGL failed\n"); return 1; }
    SDL_Log("OpenGL %d.%d  |  %s  |  %s",
            GLAD_VERSION_MAJOR(glver), GLAD_VERSION_MINOR(glver),
            (const char*)glGetString(GL_RENDERER),
            (const char*)glGetString(GL_VERSION));

    GLuint prog = link_program(VERTEX_SRC, FRAGMENT_SRC);
    if (!prog) return 1;
    uniforms u;
    cache_uniforms(prog, &u);

    /* Depth-only program + shadow map for key-light self-shadowing. */
    GLuint depthProg = link_program(DEPTH_VERTEX_SRC, DEPTH_FRAGMENT_SRC);
    if (!depthProg) return 1;
    GLint dLightSpace = glGetUniformLocation(depthProg, "uLightSpace");
    GLint dModel      = glGetUniformLocation(depthProg, "uModel");
    shadow_map sm;
    if (!shadow_map_create(&sm)) return 1;

    /* Load several models up front; ENTER cycles between them. Each keeps its
       own bounding-sphere centre/radius so the camera and shadow light reframe
       when switching. */
    const char* model_paths[] = {
        "assets/Victory Car_Toy N300326.obj",
        "assets/Oriental Asian Style_Table N210526.obj",
        "assets/head.obj",
        "assets/cube.obj",
    };
    const int NUM_MODELS = (int)(sizeof model_paths / sizeof model_paths[0]);
    gpu_model models[8];
    float mcenter[8][3];
    float mradius[8];
    for (int i = 0; i < NUM_MODELS; i++) {
        memset(&models[i], 0, sizeof models[i]);
        if (!obj_load(asset_path(model_paths[i]), &models[i].mesh)) {
            fprintf(stderr, "failed to load %s\n", model_paths[i]);
            return 1;
        }
        gpu_model_upload(&models[i]);
        gpu_model_load_textures(&models[i]);
        model_bounds(&models[i].mesh, mcenter[i], &mradius[i]);
        SDL_Log("model %d: %s  (%zu verts, %zu submeshes, %zu materials)", i,
                model_paths[i], models[i].mesh.vertex_count,
                models[i].mesh.submesh_count, models[i].mesh.material_count);
    }
    int mi = 0;   /* current model index (ENTER cycles) */

    /* Axis gizmo: the X/Y/Z glyph model is loaded once and drawn every frame
       into a small corner viewport (not part of the ENTER cycle). It shares
       the main shader; having no materials it shades in the default grey. */
    gpu_model gizmo;
    float gcenter[3];
    float gradius;
    memset(&gizmo, 0, sizeof gizmo);
    if (!obj_load(asset_path("assets/axes.obj"), &gizmo.mesh)) {
        fprintf(stderr, "failed to load axis gizmo assets/axes.obj\n");
        return 1;
    }
    gpu_model_upload(&gizmo);
    gpu_model_load_textures(&gizmo);
    model_bounds(&gizmo.mesh, gcenter, &gradius);

    /* Directional key light direction (surface->light), recomputed each frame
       as a three-quarter key anchored to the camera (see below) so the side you
       are looking at is always lit rather than turned away into shadow. The
       light-space matrix that frames each model is rebuilt per frame too. */
    float keyDir[3] = { 0.6f, 0.8f, 0.5f };
    float Ld[3] = { keyDir[0], keyDir[1], keyDir[2] };
    vec3_normalize(Ld);

    /* Orbit camera; distance frames the current model's sphere in the fovy. */
    const float fovy = 30.0f * (float)M_PI / 180.0f;
    float distance = mradius[mi] / sinf(fovy * 0.5f) * 1.2f;
    float yaw = 0.0f, pitch = 0.35f;   /* auto-rotate orbits the camera (advances
                                          yaw), so the model holds still under a
                                          camera-anchored light and self-shadows
                                          stay put instead of sweeping */
    int   orient_index = 1;            /* default Z-up -> Y-up correction */

    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    bool running   = true;
    bool rotate    = true;
    bool wireframe = false;
    float ambient  = 0.40f;    /* flat ambient fill; '[' / ']' adjust at runtime */
    bool cull      = false;    /* double-sided by default: these OBJs come from
                                  different exporters with inconsistent winding,
                                  so a single cull direction drops triangles on
                                  some of them. 'C' enables back-face culling. */

    /* Interaction state shared by mouse and touch (both drive yaw/pitch/dist). */
    const float ORBIT_SENS = 0.01f;    /* radians per pixel (mouse) */
    const float TOUCH_SENS = 3.0f;     /* radians per normalized unit (touch) */
    bool  dragging = false;            /* left mouse button held */
    /* up to two tracked fingers for one-finger orbit / two-finger pinch */
    SDL_FingerID fid[2] = { 0, 0 };
    float fpos[2][2] = {{0,0},{0,0}};
    int   nfingers = 0;
    float last_pinch = 0.0f;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                switch (e.key.key) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE:  rotate = !rotate; break;
                case SDLK_RETURN:
                    mi = (mi + 1) % NUM_MODELS;
                    distance = mradius[mi] / sinf(fovy * 0.5f) * 1.2f;
                    SDL_Log("model %d: %s", mi, model_paths[mi]);
                    break;
                case SDLK_W:      wireframe = !wireframe; break;
                case SDLK_C:      cull = !cull;
                    SDL_Log("backface culling: %s", cull ? "on" : "off"); break;
                case SDLK_UP:           /* arrows work on every keyboard layout */
                case SDLK_RIGHTBRACKET: ambient = fminf(1.0f, ambient + 0.05f);
                    SDL_Log("ambient: %.2f", ambient); break;
                case SDLK_DOWN:
                case SDLK_LEFTBRACKET:  ambient = fmaxf(0.0f, ambient - 0.05f);
                    SDL_Log("ambient: %.2f", ambient); break;
                case SDLK_O:
                    orient_index = (orient_index + 1) % ORIENT_COUNT;
                    SDL_Log("orientation preset %d/%d", orient_index, ORIENT_COUNT - 1);
                    break;
                default: break;
                }
                break;

            /* ---- mouse: left-drag orbits, wheel zooms ---- */
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) dragging = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) dragging = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (dragging) {
                    yaw   -= e.motion.xrel * ORBIT_SENS;
                    pitch += e.motion.yrel * ORBIT_SENS;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                distance *= powf(0.88f, e.wheel.y);
                break;

            /* ---- touch: one finger orbits, two fingers pinch-zoom ---- */
            case SDL_EVENT_FINGER_DOWN:
                if (nfingers < 2) {
                    int s = (nfingers == 0) ? 0 : 1;
                    fid[s] = e.tfinger.fingerID;
                    fpos[s][0] = e.tfinger.x; fpos[s][1] = e.tfinger.y;
                    nfingers++;
                    if (nfingers == 2) {
                        float dx = fpos[0][0]-fpos[1][0], dy = fpos[0][1]-fpos[1][1];
                        last_pinch = sqrtf(dx*dx + dy*dy);
                    }
                }
                break;
            case SDL_EVENT_FINGER_UP:
                for (int s = 0; s < 2; s++)
                    if (nfingers && fid[s] == e.tfinger.fingerID) {
                        if (s == 0 && nfingers == 2) { fid[0]=fid[1]; fpos[0][0]=fpos[1][0]; fpos[0][1]=fpos[1][1]; }
                        nfingers--;
                        break;
                    }
                break;
            case SDL_EVENT_FINGER_MOTION:
                for (int s = 0; s < 2; s++)
                    if (fid[s] == e.tfinger.fingerID) { fpos[s][0]=e.tfinger.x; fpos[s][1]=e.tfinger.y; }
                if (nfingers == 1) {
                    yaw   -= e.tfinger.dx * TOUCH_SENS;
                    pitch += e.tfinger.dy * TOUCH_SENS;
                } else if (nfingers == 2) {
                    float dx = fpos[0][0]-fpos[1][0], dy = fpos[0][1]-fpos[1][1];
                    float pinch = sqrtf(dx*dx + dy*dy);
                    if (pinch > 1e-4f && last_pinch > 1e-4f) distance *= last_pinch / pinch;
                    last_pinch = pinch;
                }
                break;

            default: break;
            }
        }

        /* ---- current model: its geometry, framing, and shadow light ---- */
        gpu_model* g = &models[mi];
        float* center = mcenter[mi];
        float  radius = mradius[mi];
        const float DIST_MIN = radius * 0.2f, DIST_MAX = radius * 30.0f;

        /* Three-quarter key light anchored to the camera: place it up and to the
           right of the viewer so whatever faces the camera reads as lit. A fixed
           world-space direction (the old behaviour) left the camera-facing side
           turned away from the light and looking dark. Because this depends only
           on yaw/pitch, the light orbits in lockstep with the camera during
           auto-rotate, keeping the lit side toward the viewer and the
           self-shadows stable in screen space (no sweep). */
        {
            /* camDir: model centre -> eye (toward the viewer) */
            float camDir[3] = { cosf(pitch)*sinf(yaw), sinf(pitch), cosf(pitch)*cosf(yaw) };
            float worldUp[3] = { 0.0f, 1.0f, 0.0f };
            float right[3], up[3];
            vec3_cross(worldUp, camDir, right); vec3_normalize(right);
            vec3_cross(camDir, right, up);      vec3_normalize(up);
            keyDir[0] = camDir[0] + 0.40f*right[0] + 0.55f*up[0];
            keyDir[1] = camDir[1] + 0.40f*right[1] + 0.55f*up[1];
            keyDir[2] = camDir[2] + 0.40f*right[2] + 0.55f*up[2];
            vec3_normalize(keyDir);
            Ld[0] = keyDir[0]; Ld[1] = keyDir[1]; Ld[2] = keyDir[2];
        }

        /* light-space matrix framing THIS model's bounding sphere; rebuilt
           each frame so cycling models reframes the shadow correctly. */
        float orthoDist = radius * 2.5f;
        float lightEye[3] = { center[0] + Ld[0]*orthoDist,
                              center[1] + Ld[1]*orthoDist,
                              center[2] + Ld[2]*orthoDist };
        float lightUp[3] = { 0.0f, 1.0f, 0.0f };
        if (fabsf(Ld[1]) > 0.99f) { lightUp[0]=0; lightUp[1]=0; lightUp[2]=1; }
        float orthoR = radius * 1.4f;
        mat4 lightView  = mat4_look_at(lightEye, center, lightUp);
        mat4 lightProj  = mat4_ortho(-orthoR, orthoR, -orthoR, orthoR,
                                     0.05f, orthoDist + radius*1.6f);
        mat4 lightSpace = mat4_mul(lightProj, lightView);

        /* clamp interactive camera params */
        if (pitch >  1.50f) pitch =  1.50f;
        if (pitch < -1.50f) pitch = -1.50f;
        if (distance < DIST_MIN) distance = DIST_MIN;
        if (distance > DIST_MAX) distance = DIST_MAX;

        /* auto-rotate by orbiting the CAMERA (advance yaw), but not while the
           user is actively dragging. Circling the viewer around a static model -
           rather than spinning the geometry under the light - keeps the light
           (camera-anchored) and the surface in a fixed relative pose, so the
           self-shadows hold still instead of sweeping. */
        if (rotate && !dragging && nfingers == 0) {
            yaw += 0.01f;
            if (yaw > 2.0f*(float)M_PI) yaw -= 2.0f*(float)M_PI;
        }

        /* model = orientation correction only, about the model centre; the
           geometry itself never spins. Shared by both passes, so the depth pass
           rebuilds the shadow map for the same static pose. */
        mat4 model = mat4_mul(mat4_translate(center[0], center[1], center[2]),
                     mat4_mul(orientation_matrix(orient_index),
                              mat4_translate(-center[0], -center[1], -center[2])));
        float normalMat[9];
        mat3_normal_from_mat4(model, normalMat);

        /* ---- pass 1: render depth from the light into the shadow map ---- */
        glViewport(0, 0, SHADOW_DIM, SHADOW_DIM);
        glBindFramebuffer(GL_FRAMEBUFFER, sm.fbo);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(depthProg);
        glUniformMatrix4fv(dLightSpace, 1, GL_FALSE, lightSpace.m);
        glUniformMatrix4fv(dModel,      1, GL_FALSE, model.m);
        /* Record BACK faces (cull front) so a convex surface never shadows
           itself - the main fix for acne on the smooth meshes - with a small
           polygon offset as backup. */
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);
        glBindVertexArray(g->vao);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)g->mesh.vertex_count);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(GL_BACK);               /* restore for the camera pass */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        /* ---- pass 2: shade the scene from the camera ---- */
        int pw, ph;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        float aspect = ph > 0 ? (float)pw / (float)ph : 1.0f;
        glViewport(0, 0, pw, ph);

        /* Camera eye orbiting the model centre. */
        float eye[3] = {
            center[0] + distance * cosf(pitch) * sinf(yaw),
            center[1] + distance * sinf(pitch),
            center[2] + distance * cosf(pitch) * cosf(yaw)
        };
        float up[3] = { 0.0f, 1.0f, 0.0f };

        float znear = distance - radius*1.5f; if (znear < 0.01f) znear = 0.01f;
        float zfar  = distance + radius*3.0f;
        mat4 proj  = mat4_perspective(fovy, aspect, znear, zfar);
        mat4 view  = mat4_look_at(eye, center, up);
        mat4 mvp   = mat4_mul(proj, mat4_mul(view, model));

        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(prog);
        glUniformMatrix4fv(u.mvp,        1, GL_FALSE, mvp.m);
        glUniformMatrix4fv(u.model,      1, GL_FALSE, model.m);
        glUniformMatrix3fv(u.normalMat,  1, GL_FALSE, normalMat);
        glUniformMatrix4fv(u.lightSpace, 1, GL_FALSE, lightSpace.m);
        glUniform3fv(u.viewPos,  1, eye);
        glUniform3fv(u.lightDir, 1, keyDir);
        glUniform3f(u.ambient, ambient, ambient, ambient);  /* flat ambient fill */
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sm.tex);
        glUniform1i(u.shadowMap, 1);
        glUniform1i(u.tex, 0);           /* material diffuse map on unit 0 */

        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glBindVertexArray(g->vao);

        /* opaque submeshes first... */
        for (size_t i = 0; i < g->mesh.submesh_count; i++)
            if (submesh_opacity(g, i) >= 0.999f) draw_submesh(g, &u, i);

        /* ...then transparent ones (e.g. glass), alpha-blended and without
           writing depth so they don't occlude each other. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (size_t i = 0; i < g->mesh.submesh_count; i++)
            if (submesh_opacity(g, i) < 0.999f) draw_submesh(g, &u, i);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        /* ---- axis gizmo: small bottom-left overlay showing X/Y/Z ----
           A scissored square in the corner gets its own depth clear so the
           gizmo draws over the scene. It uses the SAME yaw/pitch as the camera
           (so it tracks the view) but a fixed framing distance, so it stays the
           same size in the corner regardless of how far the scene is zoomed. */
        {
            int gs = (pw < ph ? pw : ph) / 5;
            if (gs < 96)  gs = 96;
            if (gs > 220) gs = 220;
            const int margin = 12;          /* viewport origin is bottom-left */
            glEnable(GL_SCISSOR_TEST);
            glScissor(margin, margin, gs, gs);
            glClear(GL_DEPTH_BUFFER_BIT);   /* fresh depth, only in the corner */
            glViewport(margin, margin, gs, gs);

            float gdist = gradius / sinf(fovy * 0.5f) * 1.25f;
            float geye[3] = {
                gcenter[0] + gdist * cosf(pitch) * sinf(yaw),
                gcenter[1] + gdist * sinf(pitch),
                gcenter[2] + gdist * cosf(pitch) * cosf(yaw)
            };
            float gnear = gdist - gradius * 1.5f; if (gnear < 0.01f) gnear = 0.01f;
            mat4 gproj  = mat4_perspective(fovy, 1.0f, gnear, gdist + gradius * 3.0f);
            mat4 gview  = mat4_look_at(geye, gcenter, up);
            /* same Z-up -> Y-up correction the scene model uses, about the gizmo
               centre. The model no longer spins, so the labels track the pose
               purely through the shared yaw/pitch of geye. */
            mat4 gmodel = mat4_mul(mat4_translate(gcenter[0], gcenter[1], gcenter[2]),
                          mat4_mul(orientation_matrix(orient_index),
                                   mat4_translate(-gcenter[0], -gcenter[1], -gcenter[2])));
            mat4 gmvp   = mat4_mul(gproj, mat4_mul(gview, gmodel));
            float gnormal[9];
            mat3_normal_from_mat4(gmodel, gnormal);

            glUseProgram(prog);
            glUniformMatrix4fv(u.mvp,       1, GL_FALSE, gmvp.m);
            glUniformMatrix4fv(u.model,     1, GL_FALSE, gmodel.m);
            glUniformMatrix3fv(u.normalMat, 1, GL_FALSE, gnormal);
            glUniform3fv(u.viewPos, 1, geye);
            glUniform3f(u.ambient, 0.55f, 0.55f, 0.55f);  /* brighter so it reads */
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_CULL_FACE);        /* thin glyphs: show both sides */
            glBindVertexArray(gizmo.vao);
            for (size_t i = 0; i < gizmo.mesh.submesh_count; i++)
                draw_submesh(&gizmo, &u, i);
            glDisable(GL_SCISSOR_TEST);
        }

        SDL_GL_SwapWindow(window);
        SDL_Delay(16);
    }

    for (int i = 0; i < NUM_MODELS; i++) { free(models[i].material_tex); obj_free(&models[i].mesh); }
    free(gizmo.material_tex); obj_free(&gizmo.mesh);
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
