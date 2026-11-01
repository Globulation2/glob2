// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <string>
namespace GAGCore {
using BrowserTextChange=std::function<void(const std::string&,size_t,int)>;
// Optional host-native editing; native SDL hosts provide no-op implementations.
void forgetBrowserTextInput(const void* owner);
void focusBrowserTextInput(const void* owner);
bool hasBrowserTextInput(const void* owner);
void beginBrowserTextFrame();
void endBrowserTextFrame();
// maximum is an exclusive UTF-8 byte bound; zero leaves bytes unrestricted.
void browserTextInput(const void* owner,SDL_Rect rect,int width,int height,const std::string& value,
    bool password,size_t maximum,BrowserTextChange changed,const SDL_Rect* clip=nullptr,
    bool selectAll=false);
// Independent inclusive Unicode-codepoint bound, matching TextFieldOptions.maxLength.
// Keep the original overload for callers that only impose a byte bound.
void browserTextInput(const void* owner,SDL_Rect rect,int width,int height,const std::string& value,
    bool password,size_t maximum,BrowserTextChange changed,const SDL_Rect* clip,
    bool selectAll,size_t maxCharacters);
}
