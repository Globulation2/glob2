// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <mutex>

namespace GAGCore {

//! Serialises Vulkan instance creation and teardown across the process. Two
//! threads creating instances at once (the device profile's background VRAM
//! probe and SDL GPU's device on the main thread) crash inside some drivers'
//! loaders (seen on NVIDIA, Linux). Hold it around vkCreateInstance /
//! SDL_CreateGPUDevice and the matching destroy; it is never taken per frame.
inline std::mutex &vulkanInstanceMutex()
{
	static std::mutex mutex;
	return mutex;
}

} // namespace GAGCore
