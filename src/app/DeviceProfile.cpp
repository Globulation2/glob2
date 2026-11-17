// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceProfile.h"

#include <MobilePlatform.h>

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef __EMSCRIPTEN__
// platforms/web/canvas-size.js: navigator.deviceMemory in GiB, 0 when unknown.
extern "C" double glob2_device_memory_gib();
#endif
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

#if !defined(GLOB2_MOBILE) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
#define GLOB2_DEVICE_VRAM_PROBE 1
#include <SDL3/SDL_loadso.h>
#include <VulkanInstanceLock.h>
#include <vector>
#include "../../third_party/vulkan/vulkan_core.h"
#endif

std::uint64_t DeviceProfile::memoryMiB() const
{
	return inputs.androidTotalMemMiB ? inputs.androidTotalMemMiB : inputs.systemRamMiB;
}

DeviceProfile DeviceProfile::classify(const DeviceInputs &inputs, std::optional<DeviceTier> forced,
									  std::string forcedSource)
{
	const auto &t = deviceThresholds;
	DeviceProfile p;
	p.inputs = inputs;
	const auto memory = p.memoryMiB();
	if (forced)
	{
		p.tier = *forced;
		p.reason = DeviceReason::Override;
		p.overrideSource = forcedSource.empty() ? "override" : std::move(forcedSource);
		return p;
	}
	if (inputs.androidLowRam.value_or(false))
	{
		p.tier = DeviceTier::Low;
		p.reason = DeviceReason::AndroidLowRam;
	}
	else if (memory && memory < t.lowMemoryBelowMiB)
	{
		p.tier = DeviceTier::Low;
		p.reason = DeviceReason::Memory;
		p.reasonValue = memory;
	}
	else if (inputs.cores && inputs.cores <= t.lowCoresAtMost)
	{
		p.tier = DeviceTier::Low;
		p.reason = DeviceReason::Cores;
		p.reasonValue = inputs.cores;
	}
	else if (memory && memory <= t.highMemoryAboveMiB)
	{
		p.tier = DeviceTier::Mid;
		p.reason = DeviceReason::Memory;
		p.reasonValue = memory;
	}
	else if (memory)
	{
		p.tier = DeviceTier::High;
		p.reason = DeviceReason::Memory;
		p.reasonValue = memory;
	}
	// A phone or tablet that reports no memory (Safari and Firefox, which have
	// no navigator.deviceMemory) starts Mid: high-resolution artwork, the phone
	// map size. Elsewhere nothing reported keeps today's defaults.
	else if (inputs.mobile)
		p.tier = DeviceTier::Mid;
	return p;
}

DeviceDecision DeviceProfile::highResolutionArtwork() const
{
	// Too few cores make a device Low for the map size (simulation cost), but
	// artwork is a memory question: a CPU-only Low keeps high resolution.
	if (tier == DeviceTier::Low && reason != DeviceReason::Cores)
		return {false, reason, reasonValue};
	// Phones share one memory pool between CPU and GPU, and legacy GL has no
	// portable query, so the VRAM rule applies only to a measured discrete GPU.
	if (reason != DeviceReason::Override && !inputs.mobile && inputs.vramSource == "vulkan" &&
		inputs.vramMiB && inputs.vramMiB < deviceThresholds.artworkVramBelowMiB)
		return {false, DeviceReason::Vram, inputs.vramMiB};
	return {true, reason, reasonValue};
}

DeviceMapSize DeviceProfile::newMapSize() const
{
	const auto &t = deviceThresholds;
	if (tier == DeviceTier::Low)
		return {t.lowMapShift, reason, reasonValue};
	if (tier == DeviceTier::Mid && inputs.mobile)
		return {t.mobileMidMapShift, reason, reasonValue};
	return {t.defaultMapShift, reason, reasonValue};
}

std::optional<DeviceTier> DeviceProfile::parseTier(std::string_view text)
{
	if (text == "low")
		return DeviceTier::Low;
	if (text == "mid")
		return DeviceTier::Mid;
	if (text == "high")
		return DeviceTier::High;
	return std::nullopt;
}

const char *DeviceProfile::tierName(DeviceTier tier)
{
	switch (tier)
	{
	case DeviceTier::Low: return "low";
	case DeviceTier::Mid: return "mid";
	case DeviceTier::High: break;
	}
	return "high";
}

const char *DeviceProfile::reasonName(DeviceReason reason)
{
	switch (reason)
	{
	case DeviceReason::Memory: return "memory";
	case DeviceReason::AndroidLowRam: return "android-low-ram";
	case DeviceReason::Cores: return "cores";
	case DeviceReason::Vram: return "vram";
	case DeviceReason::Override: return "override";
	case DeviceReason::Unknown: break;
	}
	return "unknown";
}

std::string DeviceProfile::telemetryFields() const
{
	std::ostringstream out;
	const auto artwork = highResolutionArtwork();
	out << "tier=" << tierName(tier) << " source=" << (overrideSource.empty() ? "detected" : overrideSource)
		<< " reason=" << reasonName(reason) << " memory_mib=" << memoryMiB()
		<< " system_ram_mib=" << inputs.systemRamMiB << " cores=" << inputs.cores
		<< " mobile=" << int(inputs.mobile) << " android_low_ram="
		<< (inputs.androidLowRam ? (*inputs.androidLowRam ? "1" : "0") : "na")
		<< " android_memory_class_mib=" << inputs.androidMemoryClassMiB
		<< " android_large_memory_class_mib=" << inputs.androidLargeMemoryClassMiB
		<< " android_total_mem_mib=" << inputs.androidTotalMemMiB << " vram_mib=" << inputs.vramMiB
		<< " vram_source=" << inputs.vramSource << " auto_artwork=" << (artwork.on ? "high" : "original")
		<< " auto_artwork_reason=" << reasonName(artwork.reason)
		<< " auto_map_size=" << (1 << newMapSize().shift);
	return out.str();
}

namespace
{
#ifdef __ANDROID__
// Glob2Activity.getMemoryProfile: {isLowRamDevice, getMemoryClass,
// getLargeMemoryClass, MemoryInfo.totalMem in bytes}.
void probeAndroid(DeviceInputs &inputs)
{
	auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	auto activity = env ? static_cast<jobject>(SDL_GetAndroidActivity()) : nullptr;
	if (!activity)
		return;
	auto cls = env->GetObjectClass(activity);
	if (auto method = env->GetMethodID(cls, "getMemoryProfile", "()[J"))
	{
		auto array = static_cast<jlongArray>(env->CallObjectMethod(activity, method));
		if (!env->ExceptionCheck() && array && env->GetArrayLength(array) == 4)
		{
			jlong values[4];
			env->GetLongArrayRegion(array, 0, 4, values);
			inputs.androidLowRam = values[0] != 0;
			inputs.androidMemoryClassMiB = unsigned(std::max<jlong>(0, values[1]));
			inputs.androidLargeMemoryClassMiB = unsigned(std::max<jlong>(0, values[2]));
			inputs.androidTotalMemMiB = std::uint64_t(std::max<jlong>(0, values[3])) / (1024 * 1024);
		}
		if (array)
			env->DeleteLocalRef(array);
	}
	if (env->ExceptionCheck())
		env->ExceptionClear();
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(activity);
}
#endif

DeviceInputs probeCheap()
{
	DeviceInputs inputs;
	const int ram = SDL_GetSystemRAM(); // MiB; the figure AssetLoader budgets from
	inputs.systemRamMiB = ram > 0 ? std::uint64_t(ram) : 0;
#ifdef __EMSCRIPTEN__
	// In a browser SDL reports the WebAssembly heap limit, not the device's
	// memory. navigator.deviceMemory (Chromium; rounded, at most 8 GiB) is the
	// device's own figure; elsewhere memory stays unknown (Mid on a phone).
	inputs.systemRamMiB = std::uint64_t(glob2_device_memory_gib() * 1024);
#endif
	inputs.cores = std::thread::hardware_concurrency();
	inputs.mobile = GAGCore::platformIsMobile();
#ifdef __ANDROID__
	probeAndroid(inputs);
#endif
	return inputs;
}

#ifdef GLOB2_DEVICE_VRAM_PROBE
// Instance creation loads the installed drivers, which can take tens of
// milliseconds, so it runs on its own thread. Returns 0 with source
// "integrated" when the best device shares system memory.
std::pair<std::uint64_t, std::string> probeVulkanVram()
{
#ifdef _WIN32
	SDL_SharedObject *library = SDL_LoadObject("vulkan-1.dll");
#else
	SDL_SharedObject *library = SDL_LoadObject("libvulkan.so.1");
#endif
	if (!library)
		return {0, "unavailable"};
	struct Unload
	{
		SDL_SharedObject *library;
		~Unload() { SDL_UnloadObject(library); }
	} unload{library};
	auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library, "vkGetInstanceProcAddr"));
	if (!getProc)
		return {0, "unavailable"};
	auto create = reinterpret_cast<PFN_vkCreateInstance>(getProc(nullptr, "vkCreateInstance"));
	if (!create)
		return {0, "unavailable"};
	VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	application.pApplicationName = "Globulation 2 device profile";
	application.apiVersion = VK_API_VERSION_1_0;
	VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	info.pApplicationInfo = &application;
	// One instance at a time in this process: SDL GPU may be creating its own
	// on the main thread right now (see VulkanInstanceLock.h).
	std::lock_guard instanceLock(GAGCore::vulkanInstanceMutex());
	VkInstance instance = VK_NULL_HANDLE;
	if (create(&info, nullptr, &instance) != VK_SUCCESS || !instance)
		return {0, "unavailable"};
	auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(getProc(instance, "vkDestroyInstance"));
	auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getProc(instance, "vkEnumeratePhysicalDevices"));
	auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(getProc(instance, "vkGetPhysicalDeviceProperties"));
	auto memory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
		getProc(instance, "vkGetPhysicalDeviceMemoryProperties"));
	std::pair<std::uint64_t, std::string> result{0, "unavailable"};
	std::uint32_t count = 0;
	if (enumerate && properties && memory && enumerate(instance, &count, nullptr) == VK_SUCCESS && count)
	{
		std::vector<VkPhysicalDevice> devices(count);
		if (enumerate(instance, &count, devices.data()) == VK_SUCCESS)
		{
			bool integrated = false;
			for (auto device : devices)
			{
				VkPhysicalDeviceProperties p{};
				properties(device, &p);
				if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
					integrated = true;
				if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
					continue;
				VkPhysicalDeviceMemoryProperties m{};
				memory(device, &m);
				std::uint64_t largest = 0;
				for (std::uint32_t i = 0; i < m.memoryHeapCount; ++i)
					if (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
						largest = std::max<std::uint64_t>(largest, m.memoryHeaps[i].size);
				if (largest / (1024 * 1024) > result.first)
					result = {largest / (1024 * 1024), "vulkan"};
			}
			if (!result.first && integrated)
				result.second = "integrated";
		}
	}
	if (destroy)
		destroy(instance, nullptr);
	return result;
}
#endif

struct State
{
	std::mutex mutex;
	std::unique_ptr<DeviceProfile> profile;
	std::optional<DeviceTier> forced;
	std::string forcedSource;
#ifdef GLOB2_DEVICE_VRAM_PROBE
	std::thread vram;
	std::atomic<bool> vramDone{false};
	std::pair<std::uint64_t, std::string> vramResult;
	std::uint64_t vramStartedMs = 0;
	~State()
	{
		if (vram.joinable())
			vram.join();
	}
#endif
	void build(DeviceInputs inputs)
	{
		// In place: current() hands out references for the life of the process.
		auto classified = DeviceProfile::classify(inputs, forced, forcedSource);
		if (profile)
			*profile = std::move(classified);
		else
			profile = std::make_unique<DeviceProfile>(std::move(classified));
	}
	void environment()
	{
		if (const char *value = std::getenv("GLOB2_DEVICE_TIER"))
		{
			if (const auto tier = DeviceProfile::parseTier(value))
			{
				forced = tier;
				forcedSource = "environment";
			}
			else
				SDL_Log("Ignoring GLOB2_DEVICE_TIER=%s (expected low, mid or high)", value);
		}
	}
};
State &state()
{
	static State s;
	return s;
}
} // namespace

const DeviceProfile &DeviceProfile::current()
{
	auto &s = state();
	std::lock_guard lock(s.mutex);
	if (!s.profile)
	{
		s.environment();
		s.build(probeCheap());
	}
	return *s.profile;
}

void DeviceProfile::initialize(std::optional<DeviceTier> commandLine, bool probeVram)
{
	current();
	auto &s = state();
	{
		std::lock_guard lock(s.mutex);
		if (commandLine)
		{
			s.forced = commandLine;
			s.forcedSource = "command-line";
		}
		auto inputs = s.profile->inputs;
#ifdef GLOB2_DEVICE_VRAM_PROBE
		// An override decides the tier outright; it would also ignore the VRAM.
		if (probeVram && !s.forced && !s.vram.joinable())
		{
			inputs.vramSource = "pending";
			s.vramStartedMs = SDL_GetTicks();
			s.vram = std::thread([&s] {
				s.vramResult = probeVulkanVram();
				s.vramDone.store(true, std::memory_order_release);
			});
		}
#endif
		s.build(inputs);
	}
	if (probeVram) // windowed runs; headless tools keep their output clean
		SDL_Log("Device profile: %s", current().telemetryFields().c_str());
}

bool DeviceProfile::poll()
{
#ifdef GLOB2_DEVICE_VRAM_PROBE
	auto &s = state();
	if (!s.vram.joinable() || !s.vramDone.load(std::memory_order_acquire))
		return false;
	s.vram.join();
	{
		std::lock_guard lock(s.mutex);
		auto inputs = s.profile->inputs;
		inputs.vramMiB = s.vramResult.first;
		inputs.vramSource = s.vramResult.second;
		s.build(inputs);
	}
	SDL_Log("Device profile (VRAM probed in %u ms): %s", unsigned(SDL_GetTicks() - s.vramStartedMs),
			current().telemetryFields().c_str());
	return true;
#else
	return false;
#endif
}

DeviceProfile DeviceProfile::replace(DeviceProfile profile)
{
	current();
	auto &s = state();
	std::lock_guard lock(s.mutex);
	auto previous = *s.profile;
	*s.profile = std::move(profile);
	return previous;
}
