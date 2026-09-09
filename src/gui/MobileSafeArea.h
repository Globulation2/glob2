// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <ViewportTransform.h>
#ifdef __ANDROID__
#include <SDL_system.h>
#include <jni.h>
#endif
#if defined(__IPHONEOS__)
#include "mobile/ios/SafeArea.h"
#endif
namespace GAGCore {
inline SafeInsets mobileSafeInsets(GraphicContext* gfx) {
    SafeInsets insets;
    const double unit=gfx->logicalUnitsPerPoint();
#if defined(__IPHONEOS__)
    insets=iosGameSafeInsets(SDL_GetWindowFromID(gfx->windowID()));
#endif
#ifdef __ANDROID__
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    auto cls=env->GetObjectClass(activity);
    auto method=env->GetStaticMethodID(cls,"getUiInsets","()[I");
    if (method) {
        auto array=static_cast<jintArray>(env->CallStaticObjectMethod(cls,method));
        if (array && env->GetArrayLength(array)==4) {
            jint values[4];env->GetIntArrayRegion(array,0,4,values);
            int w,h;SDL_GetWindowSize(SDL_GetWindowFromID(gfx->windowID()),&w,&h);
            const double factor=w>0 ? double(gfx->getW())/w/unit : 1;
            insets={values[0]*factor,values[1]*factor,values[2]*factor,values[3]*factor};
        }
        if (array) env->DeleteLocalRef(array);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);
#endif
    return insets;
}
inline ViewRect mobileDialogSafe(GraphicContext* gfx) {
    const double unit=gfx->logicalUnitsPerPoint();const auto i=mobileSafeInsets(gfx);
    ViewRect safe{i.left*unit,i.top*unit,std::max(0.0,gfx->getW()-(i.left+i.right)*unit),std::max(0.0,gfx->getH()-(i.top+i.bottom)*unit)};
#if defined(__IPHONEOS__)
    safe.h=std::max(0.0,std::min(safe.y+safe.h,gfx->getH()-iosGameKeyboardInset(SDL_GetWindowFromID(gfx->windowID()))*unit)-safe.y);
#endif
    return safe;
}
}
