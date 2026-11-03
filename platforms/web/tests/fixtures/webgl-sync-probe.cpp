#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
extern "C" EMSCRIPTEN_KEEPALIVE int probe(int raw) {
  auto fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)return -1;
  glFlush();auto wait=raw?reinterpret_cast<PFNGLCLIENTWAITSYNCPROC>(emscripten_webgl_get_proc_address("glClientWaitSync")):&glClientWaitSync;
  const auto value=wait(fence,0,0);glDeleteSync(fence);return value;
}
int main(){EmscriptenWebGLContextAttributes attributes;emscripten_webgl_init_context_attributes(&attributes);attributes.majorVersion=2;attributes.minorVersion=0;auto context=emscripten_webgl_create_context("#canvas",&attributes);if(context<=0)return 1;return emscripten_webgl_make_context_current(context);}
