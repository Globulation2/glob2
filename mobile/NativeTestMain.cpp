// SPDX-License-Identifier: GPL-3.0-or-later
// adb-shell tests use SDL's dummy drivers without SDLActivity's Java entrypoint.
#include <SDL.h>
#include <SDL_system.h>
#include <cstdio>

// Shell tests have no JVM or APK AssetManager. Assets are extracted to the
// disposable test directory by the runner. Interpose only the platform bridges;
// rendering, input routing, game logic and persistence remain production code.
// The installed APK is tested separately with the actual Activity and keyboard.
extern "C" SDL_RWops *SDL_RWFromFile(const char *path, const char *mode)
{
    FILE *file = std::fopen(path, mode);
    if (!file) {
        SDL_SetError("Cannot open test file: %s", path);
        return nullptr;
    }
    return SDL_RWFromFP(file, SDL_TRUE);
}
// SDL's Android audio thread calls Java even with the dummy audio driver.
// Shell suites have no audio device; exercise actual audio in the installed APK.
extern "C" int SDL_OpenAudio(SDL_AudioSpec *, SDL_AudioSpec *)
{
    return SDL_SetError("Audio device unavailable in native shell tests");
}
extern "C" void *SDL_AndroidGetJNIEnv() { return nullptr; }
extern "C" void *SDL_AndroidGetActivity() { return nullptr; }

// Retain each harness's real main: renaming main would remove C++'s implicit
// return-zero rule and make existing harnesses that fall through undefined.
__attribute__((constructor)) static void prepareNativeTest()
{
    SDL_SetMainReady();
}
