// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <RenderDevice.h>
#include <string>
namespace GAGCore
{
// Cheap offline-asset check; an empty result does not claim hardware execution.
// Device creation remains authoritative and can report driver/device failures.
std::string sdlGPUUnavailableReason(const std::string &shaderDirectory, const char *driver = nullptr);
// The caller owns the window and SDL video subsystem. Null window creates an
// offscreen device for differential tests. shaderDirectory is generator output
// containing metadata.json shader receipts and offline native binaries. Driver
// defaults to the platform family (Vulkan, Direct3D12 or Metal). Missing offline
// format variants fail before claiming a window; no runtime compilation occurs.
std::unique_ptr<RenderDevice> makeSDLGPUDevice(SDL_Window *borrowedWindow, int width, int height,
                                             const std::string &shaderDirectory,
                                             const char *driver = nullptr);
}
