#include <HostViewport.h>
#ifndef SDL_PLATFORM_IOS
#error Expected SDL3 iOS platform detection
#endif
#ifdef __IPHONEOS__
#error Old SDL2 platform macro unexpectedly defined
#endif
GAGCore::SafeInsets safe(GAGCore::GraphicContext* gfx) { return GAGCore::mobileSafeInsets(gfx); }
double keyboard(GAGCore::GraphicContext* gfx) { return GAGCore::mobileKeyboardInset(gfx); }
bool pointer() { return GAGCore::mobilePointerAvailable(); }
