// SPDX-License-Identifier: GPL-3.0-or-later
#include "SafeArea.h"
#include <SDL3/SDL.h>
#include <UIKit/UIKit.h>
#include <GameController/GameController.h>

bool GAGCore::iosGameHasPointer() { return [GCMouse current] != nil; }

GAGCore::SafeInsets GAGCore::iosGameSafeInsets(SDL_Window* window)
{
    UIWindow *native = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    if (!native) return {};
    const UIEdgeInsets insets=native.safeAreaInsets;
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
    UIWindow *native = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    if (!native) return 0;
    CGRect frame=[native convertRect:keyboardFrame fromWindow:nil];
    CGRect overlap=CGRectIntersection(native.bounds,frame);
    if (CGRectIsNull(overlap) || CGRectIsEmpty(overlap)) return 0;
    // Only a bottom-docked keyboard reduces the available viewport. A floating
    // iPad keyboard is movable and must not remove an unrelated bottom strip.
    return CGRectGetMaxY(overlap)>=CGRectGetMaxY(native.bounds)-1 &&
        CGRectGetWidth(overlap)>=CGRectGetWidth(native.bounds)-1 ? CGRectGetHeight(overlap) : 0;
}
