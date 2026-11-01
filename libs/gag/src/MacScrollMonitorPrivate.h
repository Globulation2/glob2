// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
@class NSWindow;
@class NSEvent;
namespace GAGCore::MacScrollDetail
{
// Shared by the native monitor and its synthetic Cocoa contract tests.
NSEvent *routeScroll(SDL_WindowID windowID, NSWindow *native, NSEvent *event);
}
