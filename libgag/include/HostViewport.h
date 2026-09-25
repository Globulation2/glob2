// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <ViewportTransform.h>
#include <InterfacePresentation.h>
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
    [[maybe_unused]] const double unit=gfx->logicalUnitsPerPoint();
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
            const double factor=w>0 ? double(gfx->getW())/w/unit*gfx->getUiScale() : 1;
            insets={values[0]*factor,values[1]*factor,values[2]*factor,values[3]*factor};
        }
        if (array) env->DeleteLocalRef(array);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);
#endif
#ifdef __EMSCRIPTEN__
    insets=presentationViewport.safe;
#endif
    return insets;
}
inline double mobileKeyboardInset(GraphicContext* gfx) {
#if defined(__IPHONEOS__)
    return iosGameKeyboardInset(SDL_GetWindowFromID(gfx->windowID()));
#elif defined(__ANDROID__)
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    auto cls=env->GetObjectClass(activity);
    auto method=env->GetStaticMethodID(cls,"getKeyboardInset","()I");
    double inset=method ? env->CallStaticIntMethod(cls,method) : 0;
    if (env->ExceptionCheck()) {env->ExceptionClear();inset=0;}
    env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);
    float dpi=160;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(SDL_GetWindowFromID(gfx->windowID())),&dpi,nullptr,nullptr)==0 && dpi>0) inset*=160/dpi;
    return inset;
#elif defined(__EMSCRIPTEN__)
    return presentationViewport.keyboardInset;
#else
    return 0;
#endif
}

inline bool mobilePointerAvailable() {
#if defined(__IPHONEOS__)
    return iosGameHasPointer();
#elif defined(__ANDROID__)
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    auto cls=env->GetObjectClass(activity);
    auto method=env->GetStaticMethodID(cls,"hasPointer","()Z");
    bool available=method && env->CallStaticBooleanMethod(cls,method);
    if (env->ExceptionCheck()) {env->ExceptionClear();available=false;}
    env->DeleteLocalRef(cls);env->DeleteLocalRef(activity);
    return available;
#else
    return true;
#endif
}

}
