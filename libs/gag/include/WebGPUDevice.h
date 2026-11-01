// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <RenderDevice.h>
namespace GAGCore {
// Browser-only, after asynchronous glob2_webgpu_prepare/poll succeeds.
std::unique_ptr<RenderDevice> makeWebGPURenderDevice(SDL_Window *borrowedWindow);
}
extern "C" {
int glob2_webgpu_prepare(const char *shaderDirectory);
int glob2_webgpu_poll();
void glob2_webgpu_error(char *destination, int capacity);
}
