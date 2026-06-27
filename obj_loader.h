#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

/* obj_loader.h - modern Wavefront .obj/.mtl loader for the core-profile /
 * GLSL renderer (sdl3-glsl.c). It is the modern counterpart to the legacy
 * parse.h used by sdl3-gl.c; both live side by side.
 *
 * Same .obj family as parse.h (positions, normals, faces), extended for the
 * modern path:
 *
 *   - grows its scratch buffers geometrically instead of realloc-ing once
 *     per input line (the head model is ~40k faces);
 *   - resolves the face indices once at load time and emits a single
 *     interleaved, GPU-ready vertex array - no per-frame index deref;
 *   - fan-triangulates n-gons and accepts the p, p/t, p//n and p/t/n
 *     corner forms, plus OBJ's negative (relative) indices;
 *   - optionally carries texture coordinates: if the file supplies any
 *     `vt` lines, each vertex gains a trailing texcoord.uv pair;
 *   - reads the material library named by `mtllib`, and groups triangles
 *     into per-material submeshes (draw ranges) following `usemtl`;
 *   - synthesises a flat face normal when a face omits its normals;
 *   - reports failure via a return value instead of calling exit().
 *
 * Emitted interleaved layout, per vertex:
 *     normal.xyz, position.xyz, [texcoord.uv]
 * The texcoord pair is present only when has_texcoords is true. Keeping
 * normal+position first means a textured mesh is a strict superset of an
 * untextured one and stays byte-compatible with the legacy VBO layout.
 * Vertex-shader attribute slots: 0 = aNormal, 1 = aPosition, 2 = aTexCoord.
 *
 * SELF-DESCRIBING BUNDLE (Tier 1): a textured asset is a folder of
 *     model.obj   (mtllib model.mtl ; usemtl <name> before each face group)
 *     model.mtl   (newmtl <name> ; Kd/Ka/Ks/Ns/d ; map_Kd <image>)
 *     <image>     (the diffuse texture)
 * This loader is image-decoder-agnostic: it records each material's
 * parameters and the *resolved path* to its map_Kd image. The renderer
 * decodes that path (e.g. via stb_image) and uploads the GL texture, then
 * draws each submesh with its material bound.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define OBJ_FLOATS_NORMAL_POSITION 6   /* normal.xyz + position.xyz      */
#define OBJ_FLOATS_TEXCOORD        2   /* optional trailing texcoord.uv  */
#define OBJ_MAX_NAME               128
#define OBJ_PATH_MAX               1024

typedef struct {
    char  name[OBJ_MAX_NAME];          /* newmtl                          */
    float ambient[3];                  /* Ka                              */
    float diffuse[3];                  /* Kd                              */
    float specular[3];                 /* Ks                              */
    float shininess;                   /* Ns                              */
    float opacity;                     /* d  (or 1 - Tr)                  */
    char  diffuse_map[OBJ_PATH_MAX];   /* map_Kd, resolved path; "" = none */
} obj_material;

typedef struct {
    int    material;        /* index into obj_mesh.materials, or -1       */
    size_t first_vertex;    /* offset into obj_mesh.data (in vertices)    */
    size_t vertex_count;    /* vertices in this range                     */
} obj_submesh;

typedef struct {
    float* data;             /* interleaved vertices, NULL when empty       */
    size_t vertex_count;     /* number of vertices (== triangles * 3)       */
    int    floats_per_vertex;/* stride in floats: 6, or 8 with texcoords    */
    bool   has_texcoords;    /* true when each vertex carries a texcoord.uv */

    obj_submesh*  submeshes; /* one per material run; >=1 when faces exist  */
    size_t        submesh_count;
    obj_material* materials; /* from the .mtl library; may be empty         */
    size_t        material_count;
} obj_mesh;

/* ---- internal: a float buffer that grows by doubling ---------------- */

typedef struct {
    float* data;
    size_t count;  /* floats in use      */
    size_t cap;    /* floats allocated   */
} obj_fbuf;

static bool obj_fbuf_push(obj_fbuf* b, float x)
{
    if (b->count == b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        float* p = (float*)realloc(b->data, cap * sizeof(float));
        if (!p) return false;
        b->data = p;
        b->cap = cap;
    }
    b->data[b->count++] = x;
    return true;
}

static bool obj_fbuf_push3(obj_fbuf* b, float x, float y, float z)
{
    return obj_fbuf_push(b, x) && obj_fbuf_push(b, y) && obj_fbuf_push(b, z);
}

/* ---- internal: string / path helpers -------------------------------- */

/* Copy the directory portion of `path` (keeping the trailing separator)
   into `dir`; empty when `path` has no directory part. */
static void obj_dirname(const char* path, char* dir, size_t cap)
{
    snprintf(dir, cap, "%s", path);
    char* a = strrchr(dir, '/');
    char* b = strrchr(dir, '\\');
    char* sep = (b > a) ? b : a;
    if (sep) sep[1] = '\0';
    else     dir[0] = '\0';
}

static bool obj_is_abs(const char* p)
{
    if (!p || !p[0]) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    if (p[1] == ':') return true;                 /* Windows drive letter */
    return false;
}

/* Resolve `rel` against directory `dir` into `out`. */
static void obj_join(const char* dir, const char* rel, char* out, size_t cap)
{
    if (obj_is_abs(rel) || !dir || !dir[0]) snprintf(out, cap, "%s", rel);
    else                                    snprintf(out, cap, "%s%s", dir, rel);
}

/* Copy the rest of the line after a keyword into `out`, trimming leading and
   trailing whitespace. Used for names and paths that may legitimately contain
   spaces, e.g. `mtllib Victory Car_Toy N300326.mtl` or a `map_Kd` filename.
   (Trade-off: a `map_Kd` line carrying `-options before the filename` would be
   captured verbatim - the exporters these assets come from don't emit any.) */
static void obj_copy_rest(const char* s, char* out, size_t cap)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = 0;
    while (*s && *s != '\n' && *s != '\r' && n + 1 < cap)
        out[n++] = *s++;
    while (n > 0 && (out[n-1] == ' ' || out[n-1] == '\t')) n--;
    out[n] = '\0';
}

/* True if `s` begins with keyword `kw` followed by a blank. */
static bool obj_kw(const char* s, const char* kw)
{
    size_t n = strlen(kw);
    return strncmp(s, kw, n) == 0 && (s[n] == ' ' || s[n] == '\t');
}

/* ---- internal: face-corner parsing ---------------------------------- *
 * Parse one corner ("p", "p/t", "p//n", "p/t/n"): stores the (1-based or
 * negative/relative) position, texcoord and normal indices; a missing
 * component is reported as 0. Returns a pointer past the token, or NULL
 * on a malformed corner.                                                  */
static const char* obj_parse_corner(const char* s, long* pos, long* tex, long* nrm)
{
    char* end;
    long p = strtol(s, &end, 10);
    if (end == s) return NULL;          /* no position index */
    long t = 0, n = 0;
    if (*end == '/') {
        end++;
        if (*end == '/') {              /* p//n */
            end++;
            n = strtol(end, &end, 10);
        } else {                        /* p/t  or  p/t/n */
            t = strtol(end, &end, 10);
            if (*end == '/') {
                end++;
                n = strtol(end, &end, 10);
            }
        }
    }
    *pos = p;
    *tex = t;
    *nrm = n;
    return end;
}

/* Convert a 1-based or negative-relative OBJ index into a 0-based offset
   into a list of `count` elements. Returns -1 when out of range or zero. */
static long obj_resolve(long idx, size_t count)
{
    long base;
    if (idx > 0)      base = idx - 1;
    else if (idx < 0) base = (long)count + idx;   /* -1 == last element */
    else              return -1;                  /* 0 is invalid       */
    return (base >= 0 && (size_t)base < count) ? base : -1;
}

/* ---- internal: material library ------------------------------------- */

static obj_material* obj_push_material(obj_material** arr, size_t* count,
                                       size_t* cap, const char* name)
{
    if (*count == *cap) {
        size_t c = *cap ? *cap * 2 : 8;
        obj_material* p = (obj_material*)realloc(*arr, c * sizeof(obj_material));
        if (!p) return NULL;
        *arr = p;
        *cap = c;
    }
    obj_material* m = &(*arr)[(*count)++];
    memset(m, 0, sizeof *m);
    snprintf(m->name, sizeof m->name, "%s", name ? name : "");
    m->ambient[0]  = m->ambient[1]  = m->ambient[2]  = 0.2f;
    m->diffuse[0]  = m->diffuse[1]  = m->diffuse[2]  = 0.8f;
    m->specular[0] = m->specular[1] = m->specular[2] = 0.0f;
    m->shininess = 0.0f;
    m->opacity   = 1.0f;
    m->diffuse_map[0] = '\0';
    return m;
}

static int obj_find_material(const obj_material* mats, size_t count, const char* name)
{
    for (size_t i = 0; i < count; i++)
        if (strcmp(mats[i].name, name) == 0) return (int)i;
    return -1;
}

/* Parse a .mtl file, appending its materials. Resolves map_Kd paths
   relative to the .mtl's own directory. Warns but does not abort the
   geometry load if the file is missing. */
static bool obj_load_mtl(const char* mtl_path, obj_material** mats,
                         size_t* count, size_t* cap)
{
    FILE* f = fopen(mtl_path, "r");
    if (!f) {
        fprintf(stderr, "obj_load: cannot open material library '%s'\n", mtl_path);
        return false;
    }

    char mtl_dir[OBJ_PATH_MAX];
    obj_dirname(mtl_path, mtl_dir, sizeof mtl_dir);

    char line[1024];
    obj_material* cur = NULL;
    bool ok = true;

    while (ok && fgets(line, sizeof line, f)) {
        const char* s = line;
        while (*s == ' ' || *s == '\t') s++;

        if (obj_kw(s, "newmtl")) {
            char name[OBJ_MAX_NAME];
            obj_copy_rest(s + 6, name, sizeof name);
            cur = obj_push_material(mats, count, cap, name);
            if (!cur) ok = false;
        }
        else if (cur) {
            if      (obj_kw(s, "Ka")) sscanf(s + 2, "%f %f %f", &cur->ambient[0],  &cur->ambient[1],  &cur->ambient[2]);
            else if (obj_kw(s, "Kd")) sscanf(s + 2, "%f %f %f", &cur->diffuse[0],  &cur->diffuse[1],  &cur->diffuse[2]);
            else if (obj_kw(s, "Ks")) sscanf(s + 2, "%f %f %f", &cur->specular[0], &cur->specular[1], &cur->specular[2]);
            else if (obj_kw(s, "Ns")) sscanf(s + 2, "%f", &cur->shininess);
            else if (obj_kw(s, "d"))  sscanf(s + 1, "%f", &cur->opacity);
            else if (obj_kw(s, "Tr")) { float tr; if (sscanf(s + 2, "%f", &tr) == 1) cur->opacity = 1.0f - tr; }
            else if (obj_kw(s, "map_Kd")) {
                char rel[OBJ_PATH_MAX];
                obj_copy_rest(s + 6, rel, sizeof rel);
                if (rel[0]) obj_join(mtl_dir, rel, cur->diffuse_map, sizeof cur->diffuse_map);
            }
        }
    }

    fclose(f);
    return ok;
}

/* ---- internal: submeshes -------------------------------------------- */

static obj_submesh* obj_push_submesh(obj_submesh** arr, size_t* count,
                                     size_t* cap, int material, size_t first)
{
    if (*count == *cap) {
        size_t c = *cap ? *cap * 2 : 8;
        obj_submesh* p = (obj_submesh*)realloc(*arr, c * sizeof(obj_submesh));
        if (!p) return NULL;
        *arr = p;
        *cap = c;
    }
    obj_submesh* s = &(*arr)[(*count)++];
    s->material = material;
    s->first_vertex = first;
    s->vertex_count = 0;
    return s;
}

/* ---- public API ----------------------------------------------------- */

/* Load `path` into `out`. Returns true on success; on failure `out` is
   left empty and an error is written to stderr. Free with obj_free(). */
static bool obj_load(const char* path, obj_mesh* out)
{
    memset(out, 0, sizeof *out);
    out->floats_per_vertex = OBJ_FLOATS_NORMAL_POSITION;

    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "obj_load: cannot open '%s'\n", path);
        return false;
    }

    char obj_dir[OBJ_PATH_MAX];
    obj_dirname(path, obj_dir, sizeof obj_dir);

    obj_fbuf pos = {0}, nrm = {0}, tex = {0}, verts = {0};

    obj_material* materials = NULL;  size_t mat_count = 0, mat_cap = 0;
    obj_submesh*  submeshes = NULL;  size_t sub_count = 0, sub_cap = 0;

    char line[4096];
    bool ok = true;
    bool has_tc = false;            /* whether vertices carry a texcoord  */
    bool layout_locked = false;     /* has_tc decided at the first face   */
    int  fpv = OBJ_FLOATS_NORMAL_POSITION;
    int  current_material = -1;     /* set by usemtl                      */
    size_t cur_sub = (size_t)-1;    /* active submesh index, or none      */

    while (ok && fgets(line, sizeof line, f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3)
                ok = obj_fbuf_push3(&pos, x, y, z);
        }
        else if (line[0] == 'v' && line[1] == 'n' && line[2] == ' ') {
            float x, y, z;
            if (sscanf(line + 3, "%f %f %f", &x, &y, &z) == 3)
                ok = obj_fbuf_push3(&nrm, x, y, z);
        }
        else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
            float u = 0.0f, v = 0.0f;     /* 1-D `vt u` leaves v at 0 */
            sscanf(line + 3, "%f %f", &u, &v);
            ok = obj_fbuf_push(&tex, u) && obj_fbuf_push(&tex, v);
        }
        else if (obj_kw(line, "mtllib")) {
            char rel[OBJ_PATH_MAX], mtl_path[OBJ_PATH_MAX];
            obj_copy_rest(line + 6, rel, sizeof rel);
            if (rel[0]) {
                obj_join(obj_dir, rel, mtl_path, sizeof mtl_path);
                obj_load_mtl(mtl_path, &materials, &mat_count, &mat_cap); /* non-fatal */
            }
        }
        else if (obj_kw(line, "usemtl")) {
            char name[OBJ_MAX_NAME];
            obj_copy_rest(line + 6, name, sizeof name);
            current_material = obj_find_material(materials, mat_count, name);
        }
        else if (line[0] == 'f' && line[1] == ' ') {
            /* OBJ lists all v/vn/vt before any face, so once we reach the
               first face the texcoord count is final: lock the layout. */
            if (!layout_locked) {
                has_tc = (tex.count > 0);
                fpv = has_tc ? (OBJ_FLOATS_NORMAL_POSITION + OBJ_FLOATS_TEXCOORD)
                             : OBJ_FLOATS_NORMAL_POSITION;
                layout_locked = true;
            }

            /* Gather this face's corners, then fan-triangulate them. */
            long cp[64], ct[64], cn[64];
            int nc = 0;
            const char* s = line + 2;
            while (nc < 64) {
                while (*s == ' ' || *s == '\t') s++;
                if (*s == '\0' || *s == '\n' || *s == '\r') break;
                long pi, ti, ni;
                const char* next = obj_parse_corner(s, &pi, &ti, &ni);
                if (!next) break;
                cp[nc] = pi; ct[nc] = ti; cn[nc] = ni;
                nc++;
                s = next;
            }
            if (nc < 3) continue;

            /* Open a new submesh when the active material changes. */
            if (cur_sub == (size_t)-1 || submeshes[cur_sub].material != current_material) {
                size_t first = verts.count / (size_t)fpv;
                if (!obj_push_submesh(&submeshes, &sub_count, &sub_cap, current_material, first)) {
                    ok = false;
                    break;
                }
                cur_sub = sub_count - 1;
            }

            size_t np = pos.count / 3, nn = nrm.count / 3, nt = tex.count / 2;
            for (int t = 1; t + 1 < nc && ok; t++) {
                const int corner[3] = { 0, t, t + 1 };
                long nr[3];
                float P[3][3];

                for (int k = 0; k < 3 && ok; k++) {
                    long pp = obj_resolve(cp[corner[k]], np);
                    if (pp < 0) { ok = false; break; }   /* bad position index */
                    P[k][0] = pos.data[pp*3 + 0];
                    P[k][1] = pos.data[pp*3 + 1];
                    P[k][2] = pos.data[pp*3 + 2];
                    nr[k] = (cn[corner[k]] != 0) ? obj_resolve(cn[corner[k]], nn) : -1;
                }
                if (!ok) break;

                bool all_normals = (nr[0] >= 0 && nr[1] >= 0 && nr[2] >= 0);
                float fn[3] = {0, 0, 0};
                if (!all_normals) {
                    /* Flat normal from the triangle: (P1-P0) x (P2-P0). */
                    float e1[3] = { P[1][0]-P[0][0], P[1][1]-P[0][1], P[1][2]-P[0][2] };
                    float e2[3] = { P[2][0]-P[0][0], P[2][1]-P[0][1], P[2][2]-P[0][2] };
                    fn[0] = e1[1]*e2[2] - e1[2]*e2[1];
                    fn[1] = e1[2]*e2[0] - e1[0]*e2[2];
                    fn[2] = e1[0]*e2[1] - e1[1]*e2[0];
                    float len = fn[0]*fn[0] + fn[1]*fn[1] + fn[2]*fn[2];
                    if (len > 0.0f) {
                        len = 1.0f / (float)sqrt((double)len);
                        fn[0] *= len; fn[1] *= len; fn[2] *= len;
                    }
                }

                for (int k = 0; k < 3 && ok; k++) {
                    float nx, ny, nz;
                    if (all_normals) {
                        nx = nrm.data[nr[k]*3 + 0];
                        ny = nrm.data[nr[k]*3 + 1];
                        nz = nrm.data[nr[k]*3 + 2];
                    } else {
                        nx = fn[0]; ny = fn[1]; nz = fn[2];
                    }
                    ok = obj_fbuf_push3(&verts, nx, ny, nz) &&
                         obj_fbuf_push3(&verts, P[k][0], P[k][1], P[k][2]);

                    if (ok && has_tc) {
                        float tu = 0.0f, tv = 0.0f;
                        long tt = (ct[corner[k]] != 0) ? obj_resolve(ct[corner[k]], nt) : -1;
                        if (tt >= 0) { tu = tex.data[tt*2 + 0]; tv = tex.data[tt*2 + 1]; }
                        ok = obj_fbuf_push(&verts, tu) && obj_fbuf_push(&verts, tv);
                    }
                }
                if (ok) submeshes[cur_sub].vertex_count += 3;
            }
        }
        /* All other lines (#, o, s, ...) are ignored. */
    }

    fclose(f);
    free(pos.data);
    free(nrm.data);
    free(tex.data);

    if (!ok) {
        free(verts.data);
        free(submeshes);
        free(materials);
        fprintf(stderr, "obj_load: failed to parse '%s'\n", path);
        return false;
    }

    out->data = verts.data;
    out->vertex_count = verts.count / (size_t)fpv;
    out->floats_per_vertex = fpv;
    out->has_texcoords = has_tc;
    out->submeshes = submeshes;
    out->submesh_count = sub_count;
    out->materials = materials;
    out->material_count = mat_count;
    return true;
}

static void obj_free(obj_mesh* m)
{
    if (!m) return;
    free(m->data);
    free(m->submeshes);
    free(m->materials);
    memset(m, 0, sizeof *m);
    m->floats_per_vertex = OBJ_FLOATS_NORMAL_POSITION;
}

#endif /* OBJ_LOADER_H */
