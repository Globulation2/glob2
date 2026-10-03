// SPDX-License-Identifier: GPL-3.0-or-later
#include "../libgag/src/RecordingPlatform.h"
#include <emscripten.h>
EM_JS(int, browserRecordingAvailable, (), { return typeof Worker === 'function'; });
EM_JS(int, browserRecordingRoom, (), { return Module.glob2Recording.active && Module.glob2Recording.frames < 3; });
EM_JS(void, browserRecordingStart, (const char *path,int fps,int crf,int software,int chapters,const char *base,double time), {
    Module.glob2Recording.start(UTF8ToString(path),fps,crf,software,chapters,UTF8ToString(base),time);
});
EM_JS(int, browserRecordingFrame, (const unsigned char *pixels,int width,int height,double time,const char *context), {
    return Module.glob2Recording.frame(pixels,width,height,time,UTF8ToString(context));
});
EM_JS(int, browserRecordingAudio, (const std::int16_t *pcm,int count,double time), { return Module.glob2Recording.pcm(pcm,count,time); });
EM_JS(void, browserRecordingStop, (double time), { Module.glob2Recording.message({type:'stop',time}); });
EM_JS(void, browserRecordingFail, (const char *error), { Module.glob2Recording.message({type:'fail',error:UTF8ToString(error)}); });
EM_JS(void, browserRecordingEvent, (double time,const char *kind,const char *value), { Module.glob2Recording.message({type:'event',time,kind:UTF8ToString(kind),value:UTF8ToString(value)}); });
EM_JS(void, browserRecordingDrops, (double frames,double audio), { Module.glob2Recording.message({type:'drops',frames,audio}); });
EM_JS(char *, browserRecordingStatus, (), {
    const status=Module.glob2Recording.takeStatus(); if (!status) return 0;
    const value=JSON.stringify(status),size=lengthBytesUTF8(value)+1,pointer=_malloc(size);
    stringToUTF8(value,pointer,size); return pointer;
});
char *browserRecordingBase()
{
 return reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
  const value=document.baseURI; const size=lengthBytesUTF8(value)+1; const pointer=_malloc(size);
  stringToUTF8(value,pointer,size); return pointer;
 }));
}
char *browserRecordingFilesStatus()
{
 return reinterpret_cast<char *>(MAIN_THREAD_EM_ASM_PTR({
  const value=JSON.stringify(Module.glob2RecordingFilesUI.status()); const size=lengthBytesUTF8(value)+1; const pointer=_malloc(size);
  stringToUTF8(value,pointer,size); return pointer;
 }));
}
void browserRecordingFilesRefresh() { MAIN_THREAD_EM_ASM({ Module.glob2RecordingFilesUI.refresh(); }); }
void browserRecordingFilesRecover(const char *path) { MAIN_THREAD_EM_ASM({ Module.glob2RecordingFilesUI.recover(UTF8ToString($0)); },path); }
void browserRecordingFilesRemove(const char *path) { MAIN_THREAD_EM_ASM({ Module.glob2RecordingFilesUI.remove(UTF8ToString($0)); },path); }
