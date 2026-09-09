// SPDX-License-Identifier: GPL-3.0-or-later
#include "../CertificateTrust.h"
#include <SDL_system.h>
#include <jni.h>

bool MobileCertificateTrust::platformVerify(const Chain& chain, const std::string& host) {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env || env->PushLocalFrame(32) < 0) { if (env) env->ExceptionClear(); return false; }
    const auto evaluate = [&]() -> bool {
        auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
        if (!activity || env->ExceptionCheck()) return false;
        auto type = env->GetObjectClass(activity);
        if (!type) return false;
        auto method = env->GetStaticMethodID(type, "verifyServerCertificates", "([[BLjava/lang/String;)Z");
        if (!method) return false;
        auto byteArray = env->FindClass("[B");
        if (!byteArray) return false;
        auto certificates = env->NewObjectArray(chain.size(), byteArray, nullptr);
        if (!certificates) return false;
        auto hostname = env->NewStringUTF(host.c_str()); // ASCII host validated by the C++ boundary.
        if (!hostname) return false;
        for (size_t i = 0; i < chain.size(); ++i) {
            auto bytes = env->NewByteArray(chain[i].size());
            if (!bytes) return false;
            env->SetByteArrayRegion(bytes, 0, chain[i].size(), reinterpret_cast<const jbyte*>(chain[i].data()));
            if (env->ExceptionCheck()) return false;
            env->SetObjectArrayElement(certificates, i, bytes);
            env->DeleteLocalRef(bytes);
            if (env->ExceptionCheck()) return false;
        }
        return env->CallStaticBooleanMethod(type, method, certificates, hostname);
    };
    bool trusted = evaluate();
    if (env->ExceptionCheck()) { env->ExceptionClear(); trusted = false; }
    env->PopLocalFrame(nullptr);
    return trusted;
}
