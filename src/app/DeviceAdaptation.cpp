// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceAdaptation.h"
#include "DeviceProfile.h"

#include <algorithm>
#include <atomic>
#include <utility>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace DeviceAdaptation
{
const char *signalName(MemorySignal signal)
{
	switch (signal)
	{
	case MemorySignal::SdlLowMemory: return "sdl-low-memory";
	case MemorySignal::AndroidRunningModerate: return "android-trim-running-moderate";
	case MemorySignal::AndroidRunningLow: return "android-trim-running-low";
	case MemorySignal::AndroidRunningCritical: return "android-trim-running-critical";
	case MemorySignal::AndroidUiHidden: return "android-trim-ui-hidden";
	case MemorySignal::AndroidBackground: return "android-trim-background";
	case MemorySignal::AndroidLowMemory: return "android-low-memory";
	case MemorySignal::BrowserHeapHigh: return "browser-heap-high";
	case MemorySignal::Count: break;
	}
	return "none";
}

std::optional<MemorySignal> androidTrimSignal(int level)
{
	// ComponentCallbacks2 constants. Levels are ordered; a future level between
	// two known ones gets the milder response.
	if (level >= 40)
		return MemorySignal::AndroidBackground;
	if (level >= 20)
		return MemorySignal::AndroidUiHidden;
	if (level >= 15)
		return MemorySignal::AndroidRunningCritical;
	if (level >= 10)
		return MemorySignal::AndroidRunningLow;
	if (level >= 5)
		return MemorySignal::AndroidRunningModerate;
	return std::nullopt;
}

Response respond(MemorySignal signal, bool androidLevels)
{
	switch (signal)
	{
	case MemorySignal::SdlLowMemory:
		// iOS: one memory warning steps down at once. Android: the graded
		// onTrimMemory level arrives separately and decides.
		return androidLevels ? Response{} : Response{Trim::Caches, true};
	case MemorySignal::AndroidRunningModerate: return {Trim::Caches, false};
	case MemorySignal::AndroidRunningLow: return {Trim::Caches, true};
	case MemorySignal::AndroidRunningCritical:
	case MemorySignal::AndroidLowMemory:
	case MemorySignal::BrowserHeapHigh: return {Trim::Smallest, true};
	case MemorySignal::AndroidUiHidden:
	case MemorySignal::AndroidBackground: return {Trim::Background, false};
	case MemorySignal::Count: break;
	}
	return {};
}

HitchWindow::Config HitchWindow::defaults()
{
	const auto &t = deviceThresholds;
	return {t.hitchNs, t.hitchWindowNs, t.hitchGraceNs, t.gapNs, t.hitchCount};
}

HitchWindow::HitchWindow(Config c) : config(c)
{
	config.count = std::clamp(config.count, 1u, Capacity);
}

void HitchWindow::restart(std::uint64_t nowNs)
{
	started = nowNs ? nowNs : 1;
	last = 0;
	next = filled = 0;
}

bool HitchWindow::presented(std::uint64_t nowNs)
{
	if (!started)
		started = nowNs ? nowNs : 1;
	const auto previous = last;
	last = nowNs;
	if (!previous || nowNs < previous)
		return false;
	const auto interval = nowNs - previous;
	if (nowNs - started < config.graceNs || interval > config.gapNs || interval <= config.hitchNs)
		return false;
	ring[next] = nowNs;
	next = (next + 1) % config.count;
	filled = std::min(filled + 1, config.count);
	// Full: ring[next] is now the oldest of the last count slow frames.
	if (filled == config.count && nowNs - ring[next] <= config.windowNs)
	{
		next = filled = 0;
		return true;
	}
	return false;
}

Controller::Controller(Hooks h, bool levels, HitchWindow::Config config)
	: hooks(std::move(h)), androidLevels(levels), hitches(config)
{
}

void Controller::matchStarted(std::uint64_t nowNs)
{
	hitches.restart(nowNs);
	if (std::exchange(held, false) && hooks.holdArtwork)
		hooks.holdArtwork(false);
	// Room refused during loading or the last match says nothing about this one.
	if (hooks.artworkShortOfRoom)
		hooks.artworkShortOfRoom();
}

bool Controller::stepDown(Event event)
{
	if (stepped || !hooks.artworkAutoOn || !hooks.artworkAutoOn())
		return false;
	if (std::exchange(held, false) && hooks.holdArtwork)
		hooks.holdArtwork(false);
	if (hooks.stepDownArtwork)
		hooks.stepDownArtwork();
	stepped = notice = true;
	event.steppedDown = true;
	stepEvent = event;
	return true;
}

void Controller::signal(MemorySignal signal)
{
	if (signal >= MemorySignal::Count)
		return;
	++counts[unsigned(signal)];
	const auto response = respond(signal, androidLevels);
	// An explicit HD choice is never changed, but caches are still trimmed.
	if (response.trim != Trim::None && hooks.trim)
		hooks.trim(response.trim);
	Event event{Cause::Memory, signal, response.trim};
	if (response.stepDown)
		event.steppedDown = stepDown(event);
	if (hooks.record)
		hooks.record(event);
}

void Controller::presented(std::uint64_t nowNs)
{
	if (stepped || !hitches.presented(nowNs))
		return;
	Event event{Cause::Hitches};
	// Measured over the window that just filled (it restarts on a trigger).
	const bool shortOfRoom = hooks.artworkShortOfRoom && hooks.artworkShortOfRoom();
	if (!held && shortOfRoom && hooks.holdArtwork && hooks.artworkAutoOn && hooks.artworkAutoOn())
	{
		hooks.holdArtwork(true);
		held = event.held = true;
	}
	else
		event.steppedDown = stepDown(event);
	if (hooks.record)
		hooks.record(event);
}

bool Controller::takeNotice()
{
	return std::exchange(notice, false);
}

namespace
{
std::atomic<std::uint32_t> queued{0};
}

void queueAndroidTrim(int level)
{
	if (const auto signal = androidTrimSignal(level))
		queued.fetch_or(1u << unsigned(*signal), std::memory_order_release);
}

void queueAndroidLowMemory()
{
	queued.fetch_or(1u << unsigned(MemorySignal::AndroidLowMemory), std::memory_order_release);
}

std::vector<MemorySignal> takeQueuedSignals()
{
	if (!queued.load(std::memory_order_relaxed))
		return {};
	const auto bits = queued.exchange(0, std::memory_order_acquire);
	std::vector<MemorySignal> signals;
	// Most severe first, so a critical trim is not preceded by a milder one.
	for (auto signal : {MemorySignal::AndroidLowMemory, MemorySignal::AndroidRunningCritical,
						MemorySignal::AndroidRunningLow, MemorySignal::AndroidRunningModerate,
						MemorySignal::AndroidBackground, MemorySignal::AndroidUiHidden})
		if (bits & (1u << unsigned(signal)))
			signals.push_back(signal);
	return signals;
}
} // namespace DeviceAdaptation

#ifdef __ANDROID__
// Glob2Activity.onTrimMemory/onLowMemory, on the Android UI thread.
extern "C" JNIEXPORT void JNICALL Java_org_globulation2_glob2_Glob2Activity_nativeTrimMemory(JNIEnv *, jclass,
																							 jint level)
{
	DeviceAdaptation::queueAndroidTrim(int(level));
}
extern "C" JNIEXPORT void JNICALL Java_org_globulation2_glob2_Glob2Activity_nativeSystemLowMemory(JNIEnv *, jclass)
{
	DeviceAdaptation::queueAndroidLowMemory();
}
#endif
