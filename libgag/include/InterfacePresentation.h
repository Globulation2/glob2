// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <string_view>
#ifdef HAVE_CONFIG_H
#include <glob2/BuildConfig.h>
#endif

namespace GAGCore
{
// Presentation is independent of the platform host and simulation. Desktop builds
// can opt in to the same phone layout for reproducible input and visual testing.
inline bool phonePresentationRequested()
{
    if (const char* value = SDL_getenv("GLOB2_MOBILE_UI"))
        return std::string_view(value) == "1";
#ifdef GLOB2_MOBILE
    return true;
#else
    for (const char* name : {"GLOB2_PHONE_FORMS", "GLOB2_RESPONSIVE_UI", "GLOB2_TOUCH_HUD"})
        if (const char* value = SDL_getenv(name); value && std::string_view(value) == "1") return true;
    return false;
#endif
}
}
