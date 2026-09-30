// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GraphicContext.h>
#include <ViewportTransform.h>
#include <InterfacePresentation.h>
#include <cmath>
#include <vector>
#ifdef __ANDROID__
#include <SDL_system.h>
#include <jni.h>
#endif
#if defined(__IPHONEOS__)
#include "mobile/ios/SafeArea.h"
#endif
namespace GAGCore {
// Native presentation tests exercise real screen layouts with platform gutters.
// Unset in production; the normal Android/iOS bridge remains authoritative.
inline std::optional<SafeInsets> mobileSafeInsetsForTesting;

// Native shell tests and hosts without an attached Activity have no Java UI.
// Keep neutral viewport defaults until the Android bridge is available.
inline SafeInsets mobileSafeInsets(GraphicContext* gfx) {
    if (mobileSafeInsetsForTesting) return *mobileSafeInsetsForTesting;
    SafeInsets insets;
    [[maybe_unused]] const double unit=gfx->logicalUnitsPerPoint();
#if defined(__IPHONEOS__)
    insets=iosGameSafeInsets(SDL_GetWindowFromID(gfx->windowID()));
#endif
#ifdef __ANDROID__
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return insets;
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity) return insets;
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
    if (!env) return 0;
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity) return 0;
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

// Asks the host not to start system edge gestures (Android Back) inside these
// rectangles, given in drawable units. Android 10+ honours the request, capped
// by the system per edge; other hosts have no such gestures and ignore it.
inline void hostGestureExclusion([[maybe_unused]] GraphicContext* gfx,
                                 [[maybe_unused]] const std::vector<ViewRect>& rects) {
#ifdef __ANDROID__
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return;
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity) return;
    int w,h;SDL_GetWindowSize(SDL_GetWindowFromID(gfx->windowID()),&w,&h);
    const double sx=gfx->getW()>0 ? double(w)/gfx->getW() : 1, sy=gfx->getH()>0 ? double(h)/gfx->getH() : 1;
    std::vector<jint> values;
    for (const auto& r : rects) {
        values.push_back(jint(std::floor(r.x*sx)));values.push_back(jint(std::floor(r.y*sy)));
        values.push_back(jint(std::ceil((r.x+r.w)*sx)));values.push_back(jint(std::ceil((r.y+r.h)*sy)));
    }
    if (auto array=env->NewIntArray(jsize(values.size()))) {
        if (!values.empty()) env->SetIntArrayRegion(array,0,jsize(values.size()),values.data());
        auto cls=env->GetObjectClass(activity);
        auto method=env->GetMethodID(cls,"setGestureExclusion","([I)V");
        if (method) env->CallVoidMethod(activity,method,array);
        env->DeleteLocalRef(cls);env->DeleteLocalRef(array);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(activity);
#endif
}

inline bool mobilePointerAvailable() {
#if defined(__IPHONEOS__)
    return iosGameHasPointer();
#elif defined(__ANDROID__)
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return false;
    auto activity=static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity) return false;
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
