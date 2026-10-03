#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <cstdlib>
int main(int argc, char **argv) {
 if (!SDL_Init(SDL_INIT_VIDEO) || !NET_Init()) return 2;
 SDL_Delay(50);
 if (argc > 1) { NET_Quit(); SDL_Quit(); }
 else { SDL_Quit(); NET_Quit(); }
 return 0;
}
