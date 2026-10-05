#define _GNU_SOURCE
#include <dlfcn.h>
#include <time.h>
#include <stdint.h>
uint64_t SDL_GetTicks(void) {
    static uint64_t (*real_ticks)(void);
    if (!real_ticks) real_ticks = dlsym(RTLD_NEXT, "SDL_GetTicks");
    struct timespec cost = {0, 7000000}; nanosleep(&cost, 0);
    return real_ticks();
}
