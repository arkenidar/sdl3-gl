// sdl3-gl.c
// SDL3 with legacy fixed-function OpenGL/GLU: renders a rotating .obj model.
// (Windowing for OpenGL needs SDL3, or alternatives like freeGLUT.)
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Dario Cangialosi. See LICENSE.
// Portions derived from the SDL OpenGL Tutorial (c) Michael Vance, 2000,
// remain available under the LGPL (see the notice below).

/*
Build with CMake (SDL3 + OpenGL + GLU):
    cmake -S . -B build && cmake --build build
    ./build/sdl3-gl            # runs from any directory; assets/ is auto-located

Ported SDL1 -> SDL2 -> SDL3. Key SDL3 changes from SDL2:
  - SDL_Init() returns bool (true on success)
  - SDL_CreateWindow() dropped the x/y position arguments
  - event types renamed: SDL_KEYDOWN/SDL_QUIT -> SDL_EVENT_KEY_DOWN/SDL_EVENT_QUIT
  - SDL_Keysym removed: the keycode is now event.key.key (SDL_Keycode)
  - letter keycodes are uppercase: SDLK_w -> SDLK_W
All OpenGL calls are unchanged across the migration.
*/

/*
 * SDL OpenGL Tutorial.
 * (c) Michael Vance, 2000
 * briareos@lokigames.com
 *
 * Distributed under terms of the LGPL.
 */

#include <SDL3/SDL.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>   /* PFNGL...PROC typedefs + GL 1.5 VBO tokens */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

static GLboolean should_rotate = GL_TRUE;
static int scene = 0;

SDL_Window* window;

/* ------------------------------------------------------------------ *
 * Optional VBO (vertex-buffer-object) rendering path.
 *
 * Conservative by design: the VBO entry points are part of OpenGL 1.5,
 * which the legacy <GL/gl.h> on most systems does not expose, so we
 * resolve them at runtime via SDL_GL_GetProcAddress(). If any pointer
 * fails to load, vbo_available stays false and the program transparently
 * keeps using the original immediate-mode (glBegin/glEnd) path.
 *
 * When the buffers are available we default to the VBO path (use_vbo),
 * and the 'B' key toggles between VBO and immediate mode at runtime.
 * ------------------------------------------------------------------ */
static PFNGLGENBUFFERSPROC    p_glGenBuffers    = NULL;
static PFNGLBINDBUFFERPROC    p_glBindBuffer    = NULL;
static PFNGLBUFFERDATAPROC    p_glBufferData    = NULL;
static PFNGLDELETEBUFFERSPROC p_glDeleteBuffers = NULL;

static bool vbo_available = false;  /* set once after context creation  */
static bool use_vbo       = false;  /* runtime toggle (B); honored only
                                       when vbo_available is true        */

/* Load the GL 1.5 buffer functions. Returns true if all are present. */
static bool load_gl_vbo_functions( void )
{
    p_glGenBuffers    = (PFNGLGENBUFFERSPROC)    SDL_GL_GetProcAddress( "glGenBuffers" );
    p_glBindBuffer    = (PFNGLBINDBUFFERPROC)    SDL_GL_GetProcAddress( "glBindBuffer" );
    p_glBufferData    = (PFNGLBUFFERDATAPROC)    SDL_GL_GetProcAddress( "glBufferData" );
    p_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC) SDL_GL_GetProcAddress( "glDeleteBuffers" );

    vbo_available = p_glGenBuffers && p_glBindBuffer &&
                    p_glBufferData && p_glDeleteBuffers;
    return vbo_available;
}


static void quit_tutorial( int code )
{
    /*
     * Quit SDL so we can release the fullscreen
     * mode and restore the previous video settings,
     * etc.
     */
    SDL_Quit( );

    /* Exit program. */
    exit( code );
}

static void handle_key_down( SDL_Keycode key )
{
  static bool wireframe = false;

    /*
     * We're only interested if 'Esc' has
     * been presssed.
     *
     * EXERCISE:
     * Handle the arrow keys and have that change the
     * viewing position/angle.
     */
    switch( key ) {
    case SDLK_ESCAPE:
        quit_tutorial( 0 );
        break;
    case SDLK_SPACE:
        should_rotate = !should_rotate;
        break;

    case SDLK_RETURN:
      scene++;
      scene%=3;
      break;

    case SDLK_W: // wire-frame toggle
      wireframe = !wireframe;
      glPolygonMode( GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL );
      break;

    case SDLK_B: // VBO / immediate-mode toggle (only if VBOs loaded)
      if( vbo_available ) {
        use_vbo = !use_vbo;
        SDL_Log( "rendering path: %s", use_vbo ? "VBO" : "immediate" );
      }
      break;

    default:
        break;
    }

}

static void process_events( void )
{
    /* Our SDL event placeholder. */
    SDL_Event event;

    /* Grab all the events off the queue. */
    while( SDL_PollEvent( &event ) ) {

        switch( event.type ) {
        case SDL_EVENT_KEY_DOWN:
            /* Handle key presses. */
            handle_key_down( event.key.key );
            break;
        case SDL_EVENT_QUIT:
            /* Handle quit requests (like Ctrl-c). */
            quit_tutorial( 0 );
            break;
        }

    }

}

//-----------------------------------------

typedef struct {
  float array[3];
} vector;
typedef struct {
  vector* array;
  int count;
} vector_array;

typedef struct {
  int array[6];
} triangle;
typedef struct {
  triangle* array;
  int count;
} triangle_array;

typedef struct {
vector_array vertex_positions;
vector_array vertex_normals;
triangle_array mesh;
/* Optional GPU-side copy used by the VBO path. vbo==0 means "not
   uploaded"; the immediate-mode path ignores these fields entirely. */
GLuint vbo;            /* buffer name, 0 when unallocated         */
GLsizei vertex_count;  /* number of vertices stored in the buffer */
} model;

#include "parse.h" // parse model to load

/* Interleaved layout uploaded to the VBO: normal (3 floats) followed by
   position (3 floats) per vertex. Matches the glNormal/glVertex order of
   the immediate-mode path so lighting/material results are identical. */
#define VBO_FLOATS_PER_VERTEX 6
#define VBO_STRIDE_BYTES      ( VBO_FLOATS_PER_VERTEX * (GLsizei)sizeof(float) )

/* Flatten the indexed triangles into an interleaved array and upload it to
   a GL buffer object. Safe no-op if VBOs are unavailable or the mesh is
   empty. Dereferences the .obj indices exactly like draw_triangle(). */
static void upload_model_vbo( model* m )
{
    if( !vbo_available || !m || m->mesh.count <= 0 ) return;

    GLsizei verts = (GLsizei)m->mesh.count * 3;
    float* data = (float*)malloc( (size_t)verts * VBO_FLOATS_PER_VERTEX * sizeof(float) );
    if( !data ) return;

    float* w = data;
    for( int i = 0; i < m->mesh.count; i++ ) {
        triangle t = m->mesh.array[i];
        /* (position index, normal index) pairs for the 3 corners */
        const int pos_idx[3] = { t.array[0], t.array[2], t.array[4] };
        const int nrm_idx[3] = { t.array[1], t.array[3], t.array[5] };
        for( int c = 0; c < 3; c++ ) {
            vector n = m->vertex_normals.array[ nrm_idx[c] - 1 ];
            vector p = m->vertex_positions.array[ pos_idx[c] - 1 ];
            *w++ = n.array[0]; *w++ = n.array[1]; *w++ = n.array[2];
            *w++ = p.array[0]; *w++ = p.array[1]; *w++ = p.array[2];
        }
    }

    p_glGenBuffers( 1, &m->vbo );
    p_glBindBuffer( GL_ARRAY_BUFFER, m->vbo );
    p_glBufferData( GL_ARRAY_BUFFER,
                    (GLsizeiptr)verts * VBO_STRIDE_BYTES,
                    data, GL_STATIC_DRAW );
    p_glBindBuffer( GL_ARRAY_BUFFER, 0 );

    m->vertex_count = verts;
    free( data );
}

void draw_triangle(triangle t, model m){

  vector vertex_normal, vertex_position;

  vertex_normal=m.vertex_normals.array[t.array[1]-1];
  glNormal3f(vertex_normal.array[0],vertex_normal.array[1],vertex_normal.array[2]);
  vertex_position=m.vertex_positions.array[t.array[0]-1];
  glVertex3f(vertex_position.array[0],vertex_position.array[1],vertex_position.array[2]);

  vertex_normal=m.vertex_normals.array[t.array[3]-1];
  glNormal3f(vertex_normal.array[0],vertex_normal.array[1],vertex_normal.array[2]);
  vertex_position=m.vertex_positions.array[t.array[2]-1];
  glVertex3f(vertex_position.array[0],vertex_position.array[1],vertex_position.array[2]);

  vertex_normal=m.vertex_normals.array[t.array[5]-1];
  glNormal3f(vertex_normal.array[0],vertex_normal.array[1],vertex_normal.array[2]);
  vertex_position=m.vertex_positions.array[t.array[4]-1];
  glVertex3f(vertex_position.array[0],vertex_position.array[1],vertex_position.array[2]);
}

/* Original immediate-mode path (glBegin/glEnd), kept as the fallback. */
void draw_model_immediate(model m){
  glBegin(GL_TRIANGLES);
    for(int i=0; i<m.mesh.count; i++)
    draw_triangle(m.mesh.array[i],m);
  glEnd();
}

/* VBO path: bind the interleaved buffer and draw it with the fixed-function
   client-state arrays, so lighting/material behave exactly as before. */
void draw_model_vbo(model m){
  p_glBindBuffer( GL_ARRAY_BUFFER, m.vbo );

  glEnableClientState( GL_NORMAL_ARRAY );
  glEnableClientState( GL_VERTEX_ARRAY );

  /* normal at offset 0, position at offset 3 floats (12 bytes) */
  glNormalPointer( GL_FLOAT, VBO_STRIDE_BYTES, (const void*)0 );
  glVertexPointer( 3, GL_FLOAT, VBO_STRIDE_BYTES, (const void*)(3 * sizeof(float)) );

  glDrawArrays( GL_TRIANGLES, 0, m.vertex_count );

  glDisableClientState( GL_VERTEX_ARRAY );
  glDisableClientState( GL_NORMAL_ARRAY );

  p_glBindBuffer( GL_ARRAY_BUFFER, 0 );
}

/* Dispatcher: use the VBO path when enabled, available, and the model has
   actually been uploaded; otherwise fall back to immediate mode. */
void draw_model(model m){
  if( use_vbo && vbo_available && m.vbo != 0 && m.vertex_count > 0 )
    draw_model_vbo(m);
  else
    draw_model_immediate(m);
}

model cube;

void draw_box(float scale){
  glPushMatrix();
  glScalef(scale,scale,scale);
  draw_model(cube);
  glPopMatrix();
}

// (cleaner) code import from gltest.cpp (part of https://fox-toolkit.org/)

// Draws a simple box using the given corners
void drawBox(GLfloat xmin, GLfloat ymin, GLfloat zmin, GLfloat xmax, GLfloat ymax, GLfloat zmax) {

  draw_box(1);

  return;

  if(1){
  glBegin(GL_TRIANGLE_STRIP);
    glNormal3f(1.,0.,0.);
    glVertex3f(xmax, ymin, zmin);
    glVertex3f(xmax, ymax, zmin);
    glVertex3f(xmax, ymin, zmax);
    glVertex3f(xmax, ymax, zmax);
  glEnd();
  }


  if(1){
  glBegin(GL_TRIANGLE_STRIP);
    glNormal3f(-1.,0.,0.);
    glVertex3f(xmin, ymin, zmax);
    glVertex3f(xmin, ymax, zmax);
    glVertex3f(xmin, ymin, zmin);
    glVertex3f(xmin, ymax, zmin);
  glEnd();
  }

  if(1){
  glBegin(GL_TRIANGLE_STRIP);
    glNormal3f(0.,1.,0.);
    glVertex3f(xmin, ymax, zmin);
    glVertex3f(xmin, ymax, zmax);
    glVertex3f(xmax, ymax, zmin);
    glVertex3f(xmax, ymax, zmax);
  glEnd();
  }

  if(0){
  glBegin(GL_TRIANGLE_STRIP);
    glNormal3f(0.,-1.,0.);
    glVertex3f(xmax, ymin, zmax);
    glVertex3f(xmax, ymin, zmin);
    glVertex3f(xmin, ymin, zmax);
    glVertex3f(xmin, ymin, zmin);
  glEnd();
  }

  if(1){
  glBegin(GL_TRIANGLE_STRIP);
    glNormal3f(0.,0.,1.);
    glVertex3f(xmax, ymin, zmax);
    glVertex3f(xmax, ymax, zmax);
    glVertex3f(xmin, ymin, zmax);
    glVertex3f(xmin, ymax, zmax);
  glEnd();
  }

  if(1){
  glBegin(GL_TRIANGLE_STRIP);
    glNormal3f(0.,0.,-1.);
    glVertex3f(xmin, ymin, zmin);
    glVertex3f(xmin, ymax, zmin);
    glVertex3f(xmax, ymin, zmin);
    glVertex3f(xmax, ymax, zmin);
  glEnd();
  }

}


// Draw the GL scene
void drawScene(model model1){
  //glDisable(GL_CULL_FACE);

  int w,h;
  SDL_GetWindowSize(window,&w,&h);

      /* Our angle of rotation. */
    static float angle = 0.0f;

    if( should_rotate ) {

        if( ++angle > 360.0f ) {
            angle = 0.0f;
        }

    }

  /// https://community.khronos.org/t/shininess/18327/10
  glEnable(GL_COLOR_MATERIAL);
  glColorMaterial(GL_FRONT, GL_SPECULAR);

  const GLfloat lightPosition[]={15.,10.,5.,1.};
  const GLfloat lightAmbient[]={.1f,.1f,.1f,1.};
  const GLfloat lightDiffuse[]={.9f,.9f,.9f,1.};
  const GLfloat redMaterial[]={1.,0.,0.,1.};
  const GLfloat blueMaterial[]={0.,0.,1.,1.};

  GLdouble width = w;
  GLdouble height = h;
  GLdouble aspect = height>0 ? width/height : 1.0;

  glViewport(0,0,width,height);

  glClearColor(1.0,1.0,1.0,1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
  glEnable(GL_DEPTH_TEST);

  glDisable(GL_DITHER);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(30.,aspect,1.,100.);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(5.,10.,15.,0.,0.,0.,0.,1.,0.);

  glShadeModel(GL_SMOOTH);
  glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);
  glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);

  /// https://community.khronos.org/t/shininess/18327/10
  GLfloat white[] = { 0.5, 0.5, 0.5, 1 };
  glLightfv(GL_LIGHT0, GL_SPECULAR, white);
  glLightfv(GL_LIGHT0, GL_SHININESS, white);

  glEnable(GL_LIGHT0);
  glEnable(GL_LIGHTING);

  /// https://community.khronos.org/t/shininess/18327/10
  GLfloat specularColor [3] = {1,1,1};
  GLfloat shininess [1] = {50};
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specularColor);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shininess);

  glMaterialfv(GL_FRONT, GL_AMBIENT, blueMaterial);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, blueMaterial);

  glPushMatrix();
  glRotated(angle, 0., 1., 0.);

  if(scene==0) draw_model(model1); // WIP
  if(scene==1 || scene==2) draw_box(1);
  //drawBox(-1, -1, -1, 1, 1, 1);

if(scene==1){

  /// https://community.khronos.org/t/shininess/18327/10
  GLfloat black[] = { 0,0,0, 1 };
  glLightfv(GL_LIGHT0, GL_SPECULAR, black);
  glLightfv(GL_LIGHT0, GL_SHININESS, black);

  glMaterialfv(GL_FRONT, GL_AMBIENT, redMaterial);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, redMaterial);

  glPushMatrix();
  glTranslated(0.,1.75,0.);
  glRotated(angle, 0., 1., 0.);
  //drawBox(-.5,-.5,-.5,.5,.5,.5);
  draw_box(0.5);
  glPopMatrix();

  glPushMatrix();
  glTranslated(0.,-1.75,0.);
  glRotated(angle, 0., 1., 0.);
  //drawBox(-.5,-.5,-.5,.5,.5,.5);
  draw_box(0.5);
  glPopMatrix();

  glPushMatrix();
  glRotated(90., 1., 0., 0.);
  glTranslated(0.,1.75,0.);
  glRotated(angle, 0., 1., 0.);
  //drawBox(-.5,-.5,-.5,.5,.5,.5);
  draw_box(0.5);
  glPopMatrix();

  glPushMatrix();
  glRotated(90., -1., 0., 0.);
  glTranslated(0.,1.75,0.);
  glRotated(angle, 0., 1., 0.);
  //drawBox(-.5,-.5,-.5,.5,.5,.5);
  draw_box(0.5);
  glPopMatrix();

  glPushMatrix();
  glRotated(90., 0., 0., 1.);
  glTranslated(0.,1.75,0.);
  glRotated(angle, 0., 1., 0.);
  //drawBox(-.5,-.5,-.5,.5,.5,.5);
  draw_box(0.5);
  glPopMatrix();

  glPushMatrix();
  glRotated(90., 0., 0., -1.);
  glTranslated(0.,1.75,0.);
  glRotated(angle, 0., 1., 0.);
  //drawBox(-.5,-.5,-.5,.5,.5,.5);
  draw_box(0.5);
  glPopMatrix();

}
  glPopMatrix();

  SDL_GL_SwapWindow(window);

}

static void setup_opengl( int width, int height )
{
    float ratio = (float) width / (float) height;

    /* Our shading model--Gouraud (smooth). */
    glShadeModel( GL_SMOOTH );

    /* Culling. */
    glCullFace( GL_BACK );
    glFrontFace( GL_CCW );
    glEnable( GL_CULL_FACE );

    /* Set the clear color. */
    glClearColor( 0, 0, 0, 0 );

    /* Setup our viewport. */
    glViewport( 0, 0, width, height );

    /*
     * Change to the projection matrix and set
     * our viewing volume.
     */
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity( );
    /*
     * EXERCISE:
     * Replace this with a call to glFrustum.
     */
    gluPerspective( 60.0, ratio, 1.0, 1024.0 );
}

/* Resolve a relative path (e.g. "assets/head.obj") by starting at the
   executable's directory and walking up the parent directories until the
   file is found. This lets the program be launched from any working
   directory, and finds the project's assets/ folder whether it sits next
   to the binary or in an ancestor directory (e.g. binary in build/,
   assets/ in the project root). */
static const char* asset_path( const char* relative )
{
    static char path[1024];
    char dir[1024];

    const char* base = SDL_GetBasePath(); /* SDL3: do not free; ends with a separator */
    snprintf( dir, sizeof(dir), "%s", base ? base : "./" );

    for( int level = 0; level < 16; level++ ) {
        snprintf( path, sizeof(path), "%s%s", dir, relative );
        FILE* f = fopen( path, "r" );
        if( f ) { fclose( f ); return path; }

        /* Move one directory up: drop the trailing separator, then the
           last path component, keeping a trailing separator. */
        size_t len = strlen( dir );
        if( len && (dir[len-1] == '/' || dir[len-1] == '\\') ) dir[--len] = '\0';
        char* sep = strrchr( dir, '/' );
        char* bsep = strrchr( dir, '\\' );
        if( bsep > sep ) sep = bsep;
        if( !sep ) break;          /* reached the filesystem root */
        sep[1] = '\0';
    }

    /* Not found anywhere: fall back to the executable's directory so the
       caller reports a sensible path in its error message. */
    snprintf( path, sizeof(path), "%s%s", base ? base : "", relative );
    return path;
}

int main( int argc, char* argv[] )
{
    /* Dimensions of our window. */
    int width = 640;
    int height = 480;

    /* First, initialize SDL's video subsystem. */
    if( !SDL_Init( SDL_INIT_VIDEO ) ) {
        /* Failed, exit. */
        fprintf( stderr, "Video initialization failed: %s\n",
             SDL_GetError( ) );
        quit_tutorial( 1 );
    }

    /*
     * Request our OpenGL window attributes: at least 5 bits each of
     * red/green/blue, a 16-bit depth buffer, and double buffering.
     */
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

    /* Create the window and its OpenGL context. */
    window = SDL_CreateWindow("Game Engine", width, height, SDL_WINDOW_OPENGL);
    SDL_GL_CreateContext(window);

    /*
     * At this point, we should have a properly setup
     * double-buffered window for use with OpenGL.
     */
    setup_opengl( width, height );

    /* Try to load the GL 1.5 buffer functions. On success we default to the
       VBO path; otherwise everything stays in immediate mode. */
    if( load_gl_vbo_functions() ) {
        use_vbo = true;
        SDL_Log( "VBO support detected: using VBO path (press B to toggle)." );
    } else {
        SDL_Log( "VBO functions unavailable: using immediate-mode path." );
    }

    model model1=load_model_obj(asset_path("assets/head.obj"));
    cube=load_model_obj(asset_path("assets/cube.obj"));

    /* Upload GPU copies for the VBO path (no-op if VBOs are unavailable). */
    upload_model_vbo( &model1 );
    upload_model_vbo( &cube );

    /*
     * Now we want to begin our normal app process--
     * an event loop with a lot of redrawing.
     */
    while( 1 ) {
        /* Process incoming events. */
        process_events( );
        /* Draw the screen. */
        drawScene( model1 );

        /* Throttle to roughly 33 fps. */
        SDL_Delay( 30 );
    }

    /*
     * EXERCISE:
     * Record timings using SDL_GetTicks() and
     * and print out frames per second at program
     * end.
     */

    /* Never reached. */
    return 0;
}
