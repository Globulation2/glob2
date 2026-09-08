// SPDX-License-Identifier: GPL-3.0-or-later
#include "SafeArea.h"
#include <SDL_syswm.h>
#include <UIKit/UIKit.h>

GAGCore::SafeInsets GAGCore::iosGameSafeInsets(SDL_Window* window)
{
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window,&info) || info.subsystem!=SDL_SYSWM_UIKIT) return {};
    const UIEdgeInsets insets=info.info.uikit.window.safeAreaInsets;
    return {insets.left,insets.top,insets.right,insets.bottom};
}
