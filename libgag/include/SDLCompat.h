#include <SDL_version.h>
// Compatibility shim for SDL before version 2.0.18
#if SDL_VERSION_ATLEAST(2,0,18)
// nothing
#else // older than 2.0.18
#ifdef MSC_VER
#define SDL_GetTicks64 SDL_GetTicks
#else // not MSC_VER
#define SDL_GetTicks64() SDL_GetTicks()
#endif // MSC_VER
#endif // SDL_VERSION_ATLEAST