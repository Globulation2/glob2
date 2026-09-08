#pragma once
#include <SDL.h>
#include <typeinfo>
#include <emscripten.h>
// Every desktop wait must yield so browser input and audio can run.
#define SDL_Delay(ms) emscripten_sleep((ms) > 0 ? (ms) : 1)
