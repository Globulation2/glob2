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

// UIKit notifications publish geometry only; the frame loop consumes it.
double GAGCore::iosGameKeyboardInset(SDL_Window* window)
{
    static CGRect keyboardFrame=CGRectZero;
    static id changed=[[NSNotificationCenter defaultCenter] addObserverForName:UIKeyboardWillChangeFrameNotification
        object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification* note) {
            keyboardFrame=[note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
        }];
    static id hidden=[[NSNotificationCenter defaultCenter] addObserverForName:UIKeyboardWillHideNotification
        object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification*) { keyboardFrame=CGRectZero; }];
    (void)changed; (void)hidden;
    SDL_SysWMinfo info{};SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window,&info) || info.subsystem!=SDL_SYSWM_UIKIT) return 0;
    UIWindow* native=info.info.uikit.window;
    CGRect frame=[native convertRect:keyboardFrame fromWindow:nil];
    CGRect overlap=CGRectIntersection(native.bounds,frame);
    if (CGRectIsNull(overlap) || CGRectIsEmpty(overlap)) return 0;
    // Only a bottom-docked keyboard reduces the available viewport. A floating
    // iPad keyboard is movable and must not remove an unrelated bottom strip.
    return CGRectGetMaxY(overlap)>=CGRectGetMaxY(native.bounds)-1 &&
        CGRectGetWidth(overlap)>=CGRectGetWidth(native.bounds)-1 ? CGRectGetHeight(overlap) : 0;
}
