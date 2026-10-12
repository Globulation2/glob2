// SPDX-License-Identifier: GPL-3.0-or-later
#include "AndroidResourceObservations.h"
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif
namespace resource_policy {
AndroidResourceObservations decodeAndroidResourceObservations(const int* p, std::size_t n) {
    AndroidResourceObservations v;
    if(!p || n!=3 || p[0]!=1) return v;
    if(p[1]==0 || p[1]==1) v.batterySaver=p[1]==1;
    if(p[2]>=0) { v.rawThermalStatus=p[2]; if(p[2]<=6) v.recognizedThermalStatus=p[2]; }
    return v;
}
RuntimeConstraints mergeAndroidResourceObservations(RuntimeConstraints caller, const AndroidResourceObservations& v) {
    if(!caller.batterySaver) caller.batterySaver=v.batterySaver;
    return caller;
}
AndroidResourceObservations discoverAndroidResourceObservations() {
#if defined(__ANDROID__)
    auto* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    // Do not clear an exception belonging to the caller.
    if(!env || env->ExceptionCheck()) return {};
    struct LocalRefs {
        JNIEnv* env; jobject activity=nullptr; jclass type=nullptr; jintArray packet=nullptr;
        ~LocalRefs() {
            if(env->ExceptionCheck()) env->ExceptionClear();
            if(packet) env->DeleteLocalRef(packet);
            if(type) env->DeleteLocalRef(type);
            if(activity) env->DeleteLocalRef(activity);
        }
    } refs{env};
    refs.activity=static_cast<jobject>(SDL_GetAndroidActivity());
    if(!refs.activity || env->ExceptionCheck()) return {};
    refs.type=env->GetObjectClass(refs.activity);
    if(!refs.type || env->ExceptionCheck()) return {};
    auto method=env->GetMethodID(refs.type,"resourceObservations","()[I");
    if(!method || env->ExceptionCheck()) return {};
    refs.packet=static_cast<jintArray>(env->CallObjectMethod(refs.activity,method));
    if(!refs.packet || env->ExceptionCheck()) return {};
    auto n=env->GetArrayLength(refs.packet);
    if(env->ExceptionCheck() || n!=3) return {};
    jint packet[3]; env->GetIntArrayRegion(refs.packet,0,3,packet);
    if(env->ExceptionCheck()) return {};
    const int values[3]={packet[0],packet[1],packet[2]};
    return decodeAndroidResourceObservations(values,3);
#else
    return {};
#endif
}
}
