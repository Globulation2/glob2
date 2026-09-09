// SPDX-License-Identifier: GPL-3.0-or-later
#include "../Documents.h"
#include <SDL_system.h>
#include <jni.h>

namespace {
struct Activity {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject object = env ? static_cast<jobject>(SDL_AndroidGetActivity()) : nullptr;
    jclass type = object ? env->GetObjectClass(object) : nullptr;
    ~Activity() {
        if (env && env->ExceptionCheck()) env->ExceptionClear();
        if (type) env->DeleteLocalRef(type);
        if (object) env->DeleteLocalRef(object);
    }
};
jbyteArray bytes(JNIEnv* env, const void* data, std::size_t size) {
    auto array = env->NewByteArray(static_cast<jsize>(size));
    if (array && size) env->SetByteArrayRegion(array, 0, static_cast<jsize>(size), static_cast<const jbyte*>(data));
    return array;
}
}
namespace MobileDocuments {
bool platformOpen(Request request, const std::string& extension) {
    Activity a;
    if (!a.type) return false;
    auto method = a.env->GetMethodID(a.type, "openDocument", "(J)Z");
    return method && a.env->CallBooleanMethod(a.object, method, static_cast<jlong>(request)) && !a.env->ExceptionCheck();
}
void platformCancel(Request request) {
    Activity a;
    if (!a.type) return;
    auto method = a.env->GetMethodID(a.type, "cancelDocument", "(J)V");
    if (method) a.env->CallVoidMethod(a.object, method, static_cast<jlong>(request));
}
bool platformExport(const std::string& name, const std::vector<unsigned char>& contents, const std::string& error) {
    Activity a;
    if (!a.type) return false;
    auto method = a.env->GetMethodID(a.type, "exportDocument", "([B[B[B)Z");
    auto filename = bytes(a.env, name.data(), name.size());
    auto payload = filename ? bytes(a.env, contents.data(), contents.size()) : nullptr;
    auto message = payload ? bytes(a.env, error.data(), error.size()) : nullptr;
    bool accepted = method && filename && payload && message && !a.env->ExceptionCheck() &&
        a.env->CallBooleanMethod(a.object, method, filename, payload, message) && !a.env->ExceptionCheck();
    if (filename) a.env->DeleteLocalRef(filename);
    if (payload) a.env->DeleteLocalRef(payload);
    if (message) a.env->DeleteLocalRef(message);
    return accepted;
}
}
extern "C" JNIEXPORT void JNICALL
Java_org_globulation_glob2_Glob2Activity_documentResult(JNIEnv* env, jclass, jlong request, jint status, jbyteArray name, jbyteArray data) {
    using namespace GAGCore::ApplicationHost;
    auto result = status == 1 ? FileSelectionState::Selected : status == 2 ? FileSelectionState::Cancelled : FileSelectionState::Failed;
    SelectedFile file;
    try {
        if (result == FileSelectionState::Selected) {
            if (!name || !data || env->GetArrayLength(name) > 1024 ||
                static_cast<std::size_t>(env->GetArrayLength(data)) > MobileDocuments::maximumBytes) result = FileSelectionState::Failed;
            else {
                file.name.resize(env->GetArrayLength(name));
                file.bytes.resize(env->GetArrayLength(data));
                env->GetByteArrayRegion(name, 0, file.name.size(), reinterpret_cast<jbyte*>(file.name.data()));
                env->GetByteArrayRegion(data, 0, file.bytes.size(), reinterpret_cast<jbyte*>(file.bytes.data()));
                if (env->ExceptionCheck()) { env->ExceptionClear(); result = FileSelectionState::Failed; }
            }
        }
    } catch (...) { result = FileSelectionState::Failed; file = {}; }
    MobileDocuments::complete(static_cast<MobileDocuments::Request>(request), result, std::move(file));
}
