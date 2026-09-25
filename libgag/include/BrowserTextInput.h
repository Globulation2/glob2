// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
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
void browserTextInput(const void* owner,SDL_Rect rect,int width,int height,const std::string& value,
    bool password,size_t maximum,BrowserTextChange changed,const SDL_Rect* clip=nullptr);
}
