#ifndef MAT4_H
#define MAT4_H

/* mat4.h - minimal column-major matrix/vector math for the modern renderer.
 *
 * GL stores matrices column-major (m[col*4 + row]); these helpers follow that
 * convention so a `mat4` can be handed straight to glUniformMatrix4fv with
 * transpose = GL_FALSE. This replaces the gluPerspective / gluLookAt calls and
 * the fixed-function matrix stack used by the legacy sdl3-gl.c.
 */

#include <math.h>

typedef struct { float m[16]; } mat4;

static inline mat4 mat4_identity(void)
{
    mat4 r = {{0}};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

/* Matrix product a*b (column-major). */
static inline mat4 mat4_mul(mat4 a, mat4 b)
{
    mat4 r;
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a.m[k*4 + row] * b.m[col*4 + k];
            r.m[col*4 + row] = s;
        }
    return r;
}

static inline mat4 mat4_translate(float x, float y, float z)
{
    mat4 r = mat4_identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

static inline mat4 mat4_scale(float x, float y, float z)
{
    mat4 r = mat4_identity();
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

/* Rotation by `angle` radians about axis (x,y,z); axis need not be unit. */
static inline mat4 mat4_rotate(float angle, float x, float y, float z)
{
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0.0f) { x /= len; y /= len; z /= len; }
    float c = cosf(angle), s = sinf(angle), ic = 1.0f - c;
    mat4 r = mat4_identity();
    r.m[0] = c + x*x*ic;   r.m[1] = y*x*ic + z*s; r.m[2]  = z*x*ic - y*s;
    r.m[4] = x*y*ic - z*s; r.m[5] = c + y*y*ic;   r.m[6]  = z*y*ic + x*s;
    r.m[8] = x*z*ic + y*s; r.m[9] = y*z*ic - x*s; r.m[10] = c + z*z*ic;
    return r;
}

/* Orthographic projection, right-handed, clip z in [-1,1]. Used to render the
   shadow map from a directional light's point of view. */
static inline mat4 mat4_ortho(float l, float r, float b, float t, float n, float f)
{
    mat4 m = {{0}};
    m.m[0]  =  2.0f / (r - l);
    m.m[5]  =  2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    m.m[15] =  1.0f;
    return m;
}

/* Perspective projection; fovy in radians, right-handed, clip z in [-1,1]. */
static inline mat4 mat4_perspective(float fovy, float aspect, float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);
    mat4 r = {{0}};
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

/* ---- small vec3 helpers (plain float[3]) ---------------------------- */
static inline void vec3_sub(const float* a, const float* b, float* o)
{ o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2]; }
static inline void vec3_cross(const float* a, const float* b, float* o)
{ o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0]; }
static inline void vec3_normalize(float* v)
{ float l=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(l>0.0f){v[0]/=l;v[1]/=l;v[2]/=l;} }

static inline mat4 mat4_look_at(const float* eye, const float* center, const float* up)
{
    float f[3], s[3], u[3];
    vec3_sub(center, eye, f); vec3_normalize(f);
    vec3_cross(f, up, s);     vec3_normalize(s);
    vec3_cross(s, f, u);
    mat4 r = mat4_identity();
    r.m[0]=s[0]; r.m[4]=s[1]; r.m[8]=s[2];
    r.m[1]=u[0]; r.m[5]=u[1]; r.m[9]=u[2];
    r.m[2]=-f[0];r.m[6]=-f[1];r.m[10]=-f[2];
    r.m[12]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    r.m[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    r.m[14]=  f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2];
    return r;
}

/* Normal matrix = transpose(inverse(upper-left 3x3 of `model`)), written as a
   column-major mat3 (9 floats) for glUniformMatrix3fv. For a pure rotation
   this equals the rotation itself; doing it properly keeps normals correct
   under non-uniform scale. The result is the cofactor matrix over the
   determinant (since transpose(inverse(M)) == cofactor(M)/det(M)). */
static inline void mat3_normal_from_mat4(mat4 model, float* out9)
{
    /* M[row][col], extracted from the column-major upper-left 3x3 */
    float a=model.m[0], b=model.m[4], c=model.m[8];
    float d=model.m[1], e=model.m[5], f=model.m[9];
    float g=model.m[2], h=model.m[6], i=model.m[10];

    float C00 =  (e*i - f*h), C01 = -(d*i - f*g), C02 =  (d*h - e*g);
    float C10 = -(b*i - c*h), C11 =  (a*i - c*g), C12 = -(a*h - b*g);
    float C20 =  (b*f - c*e), C21 = -(a*f - c*d), C22 =  (a*e - b*d);

    float det = a*C00 + b*C01 + c*C02;
    if (fabsf(det) < 1e-8f) {
        out9[0]=1;out9[1]=0;out9[2]=0;
        out9[3]=0;out9[4]=1;out9[5]=0;
        out9[6]=0;out9[7]=0;out9[8]=1;
        return;
    }
    float id = 1.0f / det;
    /* column-major: out[col*3 + row] = cofactor[row][col] / det */
    out9[0]=C00*id; out9[1]=C01*id; out9[2]=C02*id;  /* col 0 */
    out9[3]=C10*id; out9[4]=C11*id; out9[5]=C12*id;  /* col 1 */
    out9[6]=C20*id; out9[7]=C21*id; out9[8]=C22*id;  /* col 2 */
}

#endif /* MAT4_H */
