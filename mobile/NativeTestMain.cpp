// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>
#include <cstdio>

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

// SDL3 pumps Android lifecycle events independently of the video driver. A
// native shell has no JVM to initialize that queue; polling it spins on the
// absent lifecycle semaphore. Drain the real SDL event queue without the OS
// pump so synthetic input and application-loop tests still exercise routing.
// Installed APKs do not link this file and retain normal lifecycle pumping.
extern "C" bool SDL_PollEvent(SDL_Event *event)
{
    if (!event) return SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    return SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) > 0;
}

__attribute__((constructor)) static void prepareNativeTest()
{
    SDL_SetMainReady();
    // SDL3 otherwise asks the Android JVM for the executable/package identity.
    // These shell binaries have no JVM, even when using dummy video.
    SDL_SetAppMetadata("Glob2 native tests", "test", "org.globulation2.glob2.native-tests");
}
