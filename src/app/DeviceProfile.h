// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// One classification of the device the game runs on, computed once at startup.
// It chooses local defaults only (artwork, the size a new map starts at); a
// value the player set always wins, and nothing in the simulation path may
// read it. Every other part of the game reads this profile rather than
// probing hardware itself. See docs/architecture/rendering.md#device-profile-and-adaptive-defaults.

enum class DeviceTier : std::uint8_t
{
	Low,
	Mid,
	High
};

// Placeholder thresholds until phone measurements exist: every number that
// tunes device adaptation lives in this one table.
struct DeviceThresholds
{
	// Tier boundaries, in MiB of physical memory as the system reports it (a
	// "4 GB" phone reports about 3.6 GiB, a "3 GB" one about 2.7 GiB).
	std::uint64_t lowMemoryBelowMiB = 3 * 1024;
	std::uint64_t highMemoryAboveMiB = 6 * 1024;
	// A device with this many hardware threads or fewer is Low whatever its
	// memory; that lowers the map-size default but not artwork.
	unsigned lowCoresAtMost = 2;
	// Desktop only: a discrete GPU whose largest device-local heap is smaller
	// starts with original artwork. Phones share memory between CPU and GPU.
	std::uint64_t artworkVramBelowMiB = 1024;
	// New-game and new-map size, as a power of two (7 = 128, 8 = 256).
	int lowMapShift = 7;
	int mobileMidMapShift = 7;
	int defaultMapShift = 8;
	// Single-threaded runtime (a browser host without cross-origin isolation,
	// such as a web portal's iframe): the simulation shares the one thread with
	// rendering, so new games start small and against one computer opponent.
	int serialMapShift = 7;
	int serialColonies = 2;
	// Session step-down: this many presented frames slower than hitchNs within
	// hitchWindowNs turn Auto artwork off. Frames in the first hitchGraceNs of
	// a match (loading, artwork streaming in) and gaps over gapNs (pauses,
	// the app in the background) never count.
	std::uint64_t hitchNs = 50'000'000;
	unsigned hitchCount = 40;
	std::uint64_t hitchWindowNs = 15'000'000'000ULL;
	std::uint64_t hitchGraceNs = 20'000'000'000ULL;
	std::uint64_t gapNs = 1'000'000'000ULL;
};
inline constexpr DeviceThresholds deviceThresholds{};

// What a default was decided from, for the settings screen and telemetry.
enum class DeviceReason : std::uint8_t
{
	Unknown,       // nothing reported; today's defaults (Mid on a phone)
	Memory,        // value = MiB of memory
	AndroidLowRam, // ActivityManager.isLowRamDevice()
	Cores,         // value = hardware threads
	Vram,          // value = MiB of device-local GPU memory
	Override,      // GLOB2_DEVICE_TIER or --device-tier
	SerialRuntime  // no threads: GAGCore::ThreadSupport::available is false
};

struct DeviceInputs
{
	// SDL_GetSystemRAM (iOS: hw.memsize = physicalMemory; browsers use
	// navigator.deviceMemory, as SDL reports the heap limit there); 0 unknown.
	std::uint64_t systemRamMiB = 0;
	unsigned cores = 0;             // std::thread::hardware_concurrency; 0 unknown
	bool mobile = false;            // phone or tablet: GAGCore::platformIsMobile
	// Android ActivityManager, through Glob2Activity.getMemoryProfile.
	std::optional<bool> androidLowRam;
	unsigned androidMemoryClassMiB = 0, androidLargeMemoryClassMiB = 0;
	std::uint64_t androidTotalMemMiB = 0; // MemoryInfo.totalMem
	// Desktop Vulkan: the largest device-local heap of a discrete GPU.
	std::uint64_t vramMiB = 0;
	std::string vramSource = "none"; // none, pending, vulkan, integrated, unavailable
	// The game runs without threads (the serial browser runtime).
	bool serialRuntime = false;
};

struct DeviceDecision
{
	bool on = true;
	DeviceReason reason = DeviceReason::Unknown;
	std::uint64_t value = 0;
};
struct DeviceMapSize
{
	int shift = 8;
	DeviceReason reason = DeviceReason::Unknown;
	std::uint64_t value = 0;
};

struct DeviceProfile
{
	DeviceInputs inputs;
	DeviceTier tier = DeviceTier::High;
	DeviceReason reason = DeviceReason::Unknown;
	std::uint64_t reasonValue = 0;
	// Empty when detected; "environment" or "command-line" when overridden.
	std::string overrideSource;

	// Android's MemoryInfo.totalMem when known, otherwise SDL's figure.
	std::uint64_t memoryMiB() const;
	// Auto values. Callers apply them only where the player chose Auto.
	DeviceDecision highResolutionArtwork() const;
	DeviceMapSize newMapSize() const;
	// Colonies in a new game, the player's included; 0 keeps the setup's own default.
	int newGameColonies() const;
	// key=value fields for performance telemetry and the startup log.
	std::string telemetryFields() const;

	// Pure classification against deviceThresholds; tests drive it directly.
	static DeviceProfile classify(const DeviceInputs &inputs,
								  std::optional<DeviceTier> forced = std::nullopt,
								  std::string forcedSource = {});
	static std::optional<DeviceTier> parseTier(std::string_view text);
	static const char *tierName(DeviceTier tier);
	static const char *reasonName(DeviceReason reason);

	// The process profile. The first call probes the cheap inputs (memory,
	// cores, Android's ActivityManager) and honours GLOB2_DEVICE_TIER.
	static const DeviceProfile &current();
	// Startup: applies a --device-tier override and, on desktop windowed runs,
	// starts the background Vulkan VRAM probe. Logs the profile once.
	static void initialize(std::optional<DeviceTier> commandLine, bool probeVram);
	// Non-blocking, once a frame: folds a finished VRAM probe into current().
	// True when the profile changed, so Auto settings should resolve again.
	static bool poll();
	// Tests: replace the process profile (restore with the returned value).
	static DeviceProfile replace(DeviceProfile profile);
};
