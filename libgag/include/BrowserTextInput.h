// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <functional>
#include <string>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <map>
#include <set>
#include <cstdlib>
#endif
namespace GAGCore {
using BrowserTextChange=std::function<void(const std::string&,size_t,int)>;
#ifdef __EMSCRIPTEN__
inline std::map<const void*,BrowserTextChange> browserTextCallbacks;
inline std::set<const void*> browserTextVisible;
inline void forgetBrowserTextInput(const void* owner) {
    browserTextCallbacks.erase(owner);browserTextVisible.erase(owner);
    EM_ASM({ Module.textBridge?.remove($0); },owner);
}
inline void beginBrowserTextFrame() {
    // DOM editing is synchronized before any dialog action can read its model.
    auto callbacks=browserTextCallbacks;
    for (const auto& [owner,changed]:callbacks) {
        size_t cursor=0;int action=0;
        char* value=reinterpret_cast<char*>(EM_ASM_PTR({
            const change=Module.textBridge?.take($0);
            if (!change) return 0;
            HEAPU32[$1>>2]=lengthBytesUTF8(change.value.slice(0,change.cursor));
            HEAP32[$2>>2]=change.action || 0;
            const size=lengthBytesUTF8(change.value)+1;
            const pointer=_malloc(size);stringToUTF8(change.value,pointer,size);return pointer;
        },owner,&cursor,&action));
        if (value) { std::string text(value);std::free(value);if(browserTextCallbacks.count(owner)) changed(text,cursor,action); }
    }
}
inline void endBrowserTextFrame() {
    std::erase_if(browserTextCallbacks,[](const auto& item){return !browserTextVisible.count(item.first);});
    EM_ASM({ Module.textBridge?.end();Module.textBridge?.begin(); });
    browserTextVisible.clear();
}
inline void browserTextInput(const void* owner,SDL_Rect rect,int width,int height,const std::string& value,
    bool password,size_t maximum,BrowserTextChange changed) {
    browserTextVisible.insert(owner);browserTextCallbacks[owner]=std::move(changed);
    EM_ASM({ Module.textBridge?.field($0,{x:$1,y:$2,w:$3,h:$4},$5,$6,UTF8ToString($7),!!$8,$9); },
        owner,rect.x,rect.y,rect.w,rect.h,width,height,value.c_str(),password,maximum);
}
#else
inline void forgetBrowserTextInput(const void*) {}
inline void beginBrowserTextFrame() {}
inline void endBrowserTextFrame() {}
inline void browserTextInput(const void*,SDL_Rect,int,int,const std::string&,bool,size_t,BrowserTextChange) {}
#endif
}
