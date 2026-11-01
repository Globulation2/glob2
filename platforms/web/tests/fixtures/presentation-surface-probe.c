// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone pinned-SDK presentation lifecycle probe; see fixtures/README.md.
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <string.h>
extern void glob2_surface_retire(void);
extern int glob2_surface_prepare(int,int,int);
extern int glob2_surface_poll(void);
extern void glob2_surface_finish(int);
extern void glob2_surface_error(char*,int);
static int step=0,token=0x13579;
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context=0;
static void software(void) {
  MAIN_THREAD_EM_ASM({var c=Module.canvas.getContext('2d');if(!c)throw Error('2D context unavailable');c.fillStyle='green';c.fillRect(0,0,96,64);});
}
static void tick(void *opaque) {
  int result;
  if(step==0) {
    software(); glob2_surface_retire(); emscripten_exit_fullscreen(); result=glob2_surface_prepare(1,96,64);if(result<0)goto failed;step=1;
  } else if(step==1) {
    result=glob2_surface_poll();if(result<0)goto failed;if(!result)goto next;
    EmscriptenWebGLContextAttributes attrs;emscripten_webgl_init_context_attributes(&attrs);attrs.majorVersion=2;attrs.preserveDrawingBuffer=1;
    context=emscripten_webgl_create_context("#canvas",&attrs);if(!context)goto failed;
    emscripten_webgl_make_context_current(context);glViewport(0,0,96,64);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    unsigned char pixel[4]; glReadPixels(10,10,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if (glGetError() != GL_NO_ERROR || pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0) goto failed;
    glob2_surface_finish(1);step=2;
  } else if(step==2) {
    emscripten_webgl_destroy_context(context);context=0;
    glob2_surface_retire(); emscripten_exit_fullscreen(); result=glob2_surface_prepare(0,96,64);if(result<0)goto failed;step=3;
  } else if(step==3) {
    result=glob2_surface_poll();if(result<0)goto failed;if(!result)goto next;
    software();glob2_surface_finish(1);
    EmscriptenFullscreenChangeEvent status; emscripten_get_fullscreen_status(&status);
    if (status.isFullscreen && strcmp(status.id,"canvas")) goto failed;
    MAIN_THREAD_EM_ASM({Module.probe=({ok:true,token:$0,fullscreen:!!$1,id:UTF8ToString($2)});},token,status.isFullscreen,status.id);return;
  }
next: emscripten_set_timeout(tick,30,0);return;
failed: {
  char error[256];glob2_surface_error(error,sizeof(error));
  MAIN_THREAD_EM_ASM({Module.probe=({ok:false,step:$0,error:UTF8ToString($1)});},step,error);
}
}
static void *application(void *p) {tick(0);emscripten_exit_with_live_runtime();return 0;}
EMSCRIPTEN_KEEPALIVE int probe_fullscreen(void) {
  EmscriptenFullscreenStrategy strategy = {0};
  strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
  strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_STDDEF;
  return emscripten_request_fullscreen_strategy("#canvas", 0, &strategy);
}
int main(void) {
#ifdef __EMSCRIPTEN_PTHREADS__
  pthread_t thread;pthread_create(&thread,0,application,0);
#else
  tick(0);
#endif
  emscripten_exit_with_live_runtime();return 0;
}
