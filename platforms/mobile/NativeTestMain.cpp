// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>
#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

// Shell tests have no JVM or APK AssetManager. Use the extracted test assets;
// rendering, input routing, game logic and persistence remain production code.
extern "C" SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode)
{
    FILE *file = std::fopen(path, mode);
    if (!file) { SDL_SetError("Cannot open test file: %s", path); return nullptr; }
    SDL_IOStreamInterface io;
    SDL_INIT_INTERFACE(&io);
    io.size = [](void *data) -> Sint64 {
        auto *file = static_cast<FILE *>(data);
        const auto position = ftello(file);
        if (position < 0 || fseeko(file, 0, SEEK_END)) return -1;
        const auto size = ftello(file);
        if (fseeko(file, position, SEEK_SET)) return -1;
        return size;
    };
    io.seek = [](void *data, Sint64 offset, SDL_IOWhence whence) -> Sint64 {
        auto *file = static_cast<FILE *>(data);
        const int origin = whence == SDL_IO_SEEK_SET ? SEEK_SET : whence == SDL_IO_SEEK_CUR ? SEEK_CUR : SEEK_END;
        return fseeko(file, offset, origin) ? -1 : ftello(file);
    };
    io.read = [](void *data, void *buffer, size_t size, SDL_IOStatus *status) -> size_t {
        auto *file = static_cast<FILE *>(data);
        const auto count = std::fread(buffer, 1, size, file);
        if (count < size) *status = std::ferror(file) ? SDL_IO_STATUS_ERROR : SDL_IO_STATUS_EOF;
        return count;
    };
    io.write = [](void *data, const void *buffer, size_t size, SDL_IOStatus *status) -> size_t {
        const auto count = std::fwrite(buffer, 1, size, static_cast<FILE *>(data));
        if (count < size) *status = SDL_IO_STATUS_ERROR;
        return count;
    };
    io.flush = [](void *data, SDL_IOStatus *status) -> bool {
        if (!std::fflush(static_cast<FILE *>(data))) return true;
        *status = SDL_IO_STATUS_ERROR; return false;
    };
    io.close = [](void *data) -> bool { return std::fclose(static_cast<FILE *>(data)) == 0; };
    SDL_IOStream *stream = SDL_OpenIO(&io, file);
    if (!stream) std::fclose(file);
    return stream;
}

// The runner selects SDL's real dummy audio driver. Installed-APK tests cover
// hardware playback; shell suites still exercise stream creation and locking.
extern "C" void *SDL_GetAndroidJNIEnv() { return nullptr; }
extern "C" void *SDL_GetAndroidActivity() { return nullptr; }

// SDL3 creates its Android lifecycle queue in SDLActivity.nativeSetupJNI. Without
// it, renderer creation and every event pump spin on the absent semaphore
// (Android_WaitActiveAndLockActivity, Android_PumpEvents). A shell binary has no
// JVM, so hand SDL's own JNI_OnLoad a minimal VM whose RegisterNatives only
// records SDLActivity.nativeSetupJNI, and run that once on a short-lived thread.
// SDL then holds a real, empty, resumed lifecycle queue; every later JNI lookup
// still finds no Java environment, as before. Only the functions SDL calls during
// setup exist, so any other JNI use fails loudly instead of returning fake data.
namespace
{
using SetupJNI = void (*)(JNIEnv *, jclass);
SetupJNI activitySetup = nullptr;
int activityClassMarker;

jclass findClass(JNIEnv *, const char *name)
{
    return std::strcmp(name, "org/libsdl/app/SDLActivity") == 0 ? reinterpret_cast<jclass>(&activityClassMarker) : nullptr;
}
jint registerNatives(JNIEnv *, jclass, const JNINativeMethod *methods, jint count)
{
    for (jint i = 0; i < count; ++i)
        if (std::strcmp(methods[i].name, "nativeSetupJNI") == 0)
            activitySetup = reinterpret_cast<SetupJNI>(methods[i].fnPtr);
    return JNI_OK;
}
jobject newGlobalRef(JNIEnv *, jobject) { return nullptr; }
jmethodID getStaticMethodID(JNIEnv *, jclass, const char *, const char *) { return nullptr; }

constexpr JNINativeInterface makeEnvironment()
{
    JNINativeInterface functions{};
    functions.FindClass = findClass;
    functions.RegisterNatives = registerNatives;
    functions.NewGlobalRef = newGlobalRef;
    functions.GetStaticMethodID = getStaticMethodID;
    return functions;
}
constexpr JNINativeInterface environmentFunctions = makeEnvironment();
JNIEnv environment{&environmentFunctions};

jint getEnv(JavaVM *, void **env, jint)
{
    *env = &environment;
    return JNI_OK;
}
// Other threads get no Java environment, exactly as when SDL had no VM.
jint attachCurrentThread(JavaVM *, JNIEnv **env, void *)
{
    *env = nullptr;
    return JNI_ERR;
}
jint detachCurrentThread(JavaVM *) { return JNI_OK; }

constexpr JNIInvokeInterface makeMachine()
{
    JNIInvokeInterface functions{};
    functions.GetEnv = getEnv;
    functions.AttachCurrentThread = attachCurrentThread;
    functions.DetachCurrentThread = detachCurrentThread;
    return functions;
}
constexpr JNIInvokeInterface machineFunctions = makeMachine();
JavaVM machine{&machineFunctions};

void createLifecycleQueue()
{
    JNI_OnLoad(&machine, nullptr);
    if (!activitySetup)
    {
        std::fputs("NativeTestMain: SDL did not register SDLActivity.nativeSetupJNI\n", stderr);
        std::abort();
    }
    // SDL stores this thread's environment; the thread's exit clears it again.
    std::thread([] { activitySetup(&environment, reinterpret_cast<jclass>(&activityClassMarker)); }).join();
}
} // namespace

__attribute__((constructor)) static void prepareNativeTest()
{
    SDL_SetMainReady();
    // SDL3 otherwise asks the Android JVM for the executable/package identity.
    // These shell binaries have no JVM, even when using dummy video.
    SDL_SetAppMetadata("Glob2 native tests", "test", "org.globulation2.glob2.native-tests");
    createLifecycleQueue();
}
