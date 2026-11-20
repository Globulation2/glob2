// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// Session step-down: memory pressure and sustained hitches turn Auto
// high-resolution artwork off for the rest of the session. Local presentation
// only; nothing here reaches the simulation, saves or orders.
namespace DeviceAdaptation
{
// The memory signals the operating system sends.
enum class MemorySignal : std::uint8_t
{
	// SDL_EVENT_LOW_MEMORY: iOS didReceiveMemoryWarning. On Android SDL 3.4
	// sends it for every onTrimMemory level, so there it carries no grade.
	SdlLowMemory,
	// Android ComponentCallbacks2.onTrimMemory, through Glob2Activity.
	AndroidRunningModerate, // TRIM_MEMORY_RUNNING_MODERATE (5)
	AndroidRunningLow,      // TRIM_MEMORY_RUNNING_LOW (10)
	AndroidRunningCritical, // TRIM_MEMORY_RUNNING_CRITICAL (15)
	AndroidUiHidden,        // TRIM_MEMORY_UI_HIDDEN (20)
	AndroidBackground,      // TRIM_MEMORY_BACKGROUND (40), MODERATE (60), COMPLETE (80)
	AndroidLowMemory,       // Activity.onLowMemory
	// The browser's Wasm heap has grown to most of its fixed maximum (it never
	// shrinks); an allocation past the maximum would stop the match.
	BrowserHeapHigh,
	Count
};
const char *signalName(MemorySignal signal);
// The signal for an onTrimMemory level; unknown levels map to the nearest lower one.
std::optional<MemorySignal> androidTrimSignal(int level);

// How much presentation memory to give back.
enum class Trim : std::uint8_t
{
	None,
	Caches,     // rebuildable caches, in bounded steps while the game keeps drawing
	Smallest,   // caches, and the smallest budgets for the rest of the session
	Background, // the app is hidden: what can be rebuilt goes at once
};
struct Response
{
	Trim trim = Trim::None;
	bool stepDown = false; // Auto artwork off for the session
};
// The response table. androidLevels: the platform reports graded levels
// separately (Android), so an ungraded SdlLowMemory changes nothing.
Response respond(MemorySignal signal, bool androidLevels);

// O(1) per frame: the timestamps of the last hitchCount slow frames in a ring.
class HitchWindow
{
  public:
	struct Config
	{
		std::uint64_t hitchNs, windowNs, graceNs, gapNs;
		unsigned count;
	};
	static Config defaults();
	explicit HitchWindow(Config config = defaults());
	// A match or editor session starts: the grace period begins again.
	void restart(std::uint64_t nowNs);
	// A presented frame; true once count slow frames fall within the window.
	bool presented(std::uint64_t nowNs);

  private:
	static constexpr unsigned Capacity = 256;
	Config config;
	std::array<std::uint64_t, Capacity> ring{};
	unsigned next = 0, filled = 0;
	std::uint64_t last = 0, started = 0;
};

enum class Cause : std::uint8_t
{
	Hitches,
	Memory, // see Event::signal
};
struct Event
{
	Cause cause;
	MemorySignal signal = MemorySignal::Count; // the OS signal behind a Memory cause
	Trim trim = Trim::None;
	bool steppedDown = false; // Auto artwork turned off (false: explicit or already off)
};

// Applies responses through hooks so tests drive it without a renderer.
class Controller
{
  public:
	struct Hooks
	{
		// True while artwork is on Auto and currently high resolution.
		std::function<bool()> artworkAutoOn;
		// Turn Auto artwork off for the session (asynchronous HD release).
		std::function<void()> stepDownArtwork;
		// The single trim entry point.
		std::function<void(Trim)> trim;
		// Telemetry for every handled signal and step-down.
		std::function<void(const Event &)> record;
	};
	Controller(Hooks hooks, bool androidLevels, HitchWindow::Config config = HitchWindow::defaults());
	void signal(MemorySignal signal);
	void matchStarted(std::uint64_t nowNs) { hitches.restart(nowNs); }
	void presented(std::uint64_t nowNs);
	// A step-down the player has not been told about yet (taken once).
	bool takeNotice();
	bool steppedDown() const { return stepped; }
	std::optional<Event> stepDownEvent() const { return stepEvent; }
	unsigned signals(MemorySignal signal) const { return counts[unsigned(signal)]; }

  private:
	Hooks hooks;
	bool androidLevels;
	HitchWindow hitches;
	bool stepped = false, notice = false;
	std::optional<Event> stepEvent;
	std::array<unsigned, unsigned(MemorySignal::Count)> counts{};
	bool stepDown(Event event);
};

// Android trims arrive on the UI thread; the game thread takes them once a
// frame. Lock-free: one bit per signal, most severe handled first.
void queueAndroidTrim(int level);
void queueAndroidLowMemory();
std::vector<MemorySignal> takeQueuedSignals();
} // namespace DeviceAdaptation
