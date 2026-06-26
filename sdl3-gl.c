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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

static GLboolean should_rotate = GL_TRUE;
static int scene = 0;

SDL_Window* window;


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
} model;

#include "parse.h" // parse model to load

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

void draw_model(model m){
  glBegin(GL_TRIANGLES);
    for(int i=0; i<m.mesh.count; i++)
    draw_triangle(m.mesh.array[i],m);
  glEnd();
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

    model model1=load_model_obj(asset_path("assets/head.obj"));
    cube=load_model_obj(asset_path("assets/cube.obj"));

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
