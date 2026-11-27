// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceAdaptationSession.h"
#include "DeviceProfile.h"
#include "GlobalContainer.h"

#include <GraphicContext.h>
#include <PerformanceTelemetry.h>
#include <SteadyClock.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <ApplicationHost.h>
#include <iostream>
#include <utility>

namespace DeviceAdaptation
{
namespace
{
const char *trimName(Trim trim)
{
	switch (trim)
	{
	case Trim::Caches: return "caches";
	case Trim::Smallest: return "smallest";
	case Trim::Background: return "background";
	case Trim::None: break;
	}
	return "none";
}

bool graphical()
{
	return globalContainer && !globalContainer->runNoX && globalContainer->gfx;
}

// Turning Auto artwork off goes through the same asynchronous path as the
// settings screen: the effective flag drops at once, and the HD frames are
// released by owner polling while the current textures keep drawing.
void releaseHighResolutionIfRequested()
{
	if (graphical() && !globalContainer->settings.highResolutionArtwork &&
		GAGCore::Sprite::highResolutionRequested())
		GAGCore::Sprite::requestHighResolution(false);
}

void record(const Event &event)
{
	const char *cause = event.cause == Cause::Hitches ? "hitches" : signalName(event.signal);
	if (event.steppedDown || event.cause == Cause::Hitches || event.trim != Trim::None)
		SDL_Log("Device adaptation: cause=%s trim=%s stepped_down=%d held=%d", cause, trimName(event.trim),
				int(event.steppedDown), int(event.held));
	auto &perf = PerformanceTelemetry::collector();
	if (perf.output)
		std::cout << "GLOB2_DEVICE_ADAPTATION session=" << perf.session
				  << " elapsed_ns=" << (perf.started ? perf.clock() - perf.started : 0) << " cause=" << cause
				  << " trim=" << trimName(event.trim) << " stepped_down=" << int(event.steppedDown)
				  << " held=" << int(event.held)
				  << " artwork=" << (globalContainer ? Settings::adaptiveChoiceName(
														  globalContainer->settings.highResolutionArtworkChoice)
													  : "auto")
				  << '\n';
}
} // namespace

Controller &session()
{
	static Controller controller(
		{
			.artworkAutoOn =
				[] {
					return globalContainer &&
						   globalContainer->settings.highResolutionArtworkChoice == Settings::AdaptiveChoice::Auto &&
						   globalContainer->settings.highResolutionArtwork;
				},
			.stepDownArtwork =
				[] {
					auto &settings = globalContainer->settings;
					settings.highResolutionArtworkSteppedDown = true;
					settings.resolveAdaptiveDefaults();
					releaseHighResolutionIfRequested();
				},
			.artworkShortOfRoom =
				[] {
					// On-demand renderers only; legacy GL keeps whole sheets.
					static std::size_t seen = 0;
					const auto counts = GAGCore::Sprite::highResolutionDrawCounts();
					const auto now = counts.room + counts.evicted;
					return std::exchange(seen, now) != now;
				},
			.holdArtwork = [](bool held) { GAGCore::Sprite::holdHighResolutionAdmission(held); },
			.trim = trimPresentationMemory,
			.record = record,
		},
#ifdef __ANDROID__
		true
#else
		false
#endif
	);
	return controller;
}

void serviceFrame(const std::vector<SDL_Event> &events)
{
	if (DeviceProfile::poll() && globalContainer)
	{
		globalContainer->settings.resolveAdaptiveDefaults();
		releaseHighResolutionIfRequested();
	}
	for (const auto &event : events)
		if (event.type == SDL_EVENT_LOW_MEMORY)
			session().signal(MemorySignal::SdlLowMemory);
	// The browser's Wasm heap only grows, so this fires at most once a page
	// load. Past four fifths of the maximum, give back caches and Auto HD
	// artwork before an allocation fails and stops the match.
	static bool heapHigh = false;
	if (!heapHigh && graphical() && GAGCore::ApplicationHost::heapNearlyFull())
	{
		heapHigh = true;
		session().signal(MemorySignal::BrowserHeapHigh);
	}
	for (const auto signal : takeQueuedSignals())
		session().signal(signal);
}

void matchStarted()
{
	session().matchStarted(std::uint64_t(GAGCore::steadyClockNanoseconds()));
	auto &perf = PerformanceTelemetry::collector();
	if (perf.output)
	{
		const auto &s = session();
		std::cout << "GLOB2_DEVICE_PROFILE session=" << perf.session << ' '
				  << DeviceProfile::current().telemetryFields() << " artwork="
				  << (globalContainer ? Settings::adaptiveChoiceName(globalContainer->settings.highResolutionArtworkChoice)
									  : "auto")
				  << " artwork_effective=" << int(globalContainer && globalContainer->settings.highResolutionArtwork)
				  << " stepped_down=" << int(s.steppedDown()) << " stepped_down_cause="
				  << (!s.stepDownEvent()						  ? "none"
					  : s.stepDownEvent()->cause == Cause::Hitches ? "hitches"
																	 : signalName(s.stepDownEvent()->signal))
				  << '\n';
	}
}

void framePresented()
{
	session().presented(std::uint64_t(GAGCore::steadyClockNanoseconds()));
}

void trimPresentationMemory(Trim trim)
{
	if (!graphical() || trim == Trim::None)
		return;
	// The presentation trim: device copies its snapshots restore, cached
	// snapshots spread over later frames (at once in the background), retired
	// HD frames and subscribed caches such as terrain pages. Repeating it for a
	// signal ScreenStack also handled (SDL_EVENT_LOW_MEMORY) frees nothing twice.
	globalContainer->gfx->trimPresentationMemory(trim == Trim::Background ? GAGCore::PresentationTrim::Background
																		  : GAGCore::PresentationTrim::LowMemory);
	// Retired HD frames are decoded copies kept for panning back; they reload
	// from disk when needed. Smallest keeps their budget at zero for the session.
	// The smallest RenderBudget is not applied: render budgets are fixed when
	// the presentation device is created.
	auto budget = GAGCore::Sprite::retiredHighResolutionBudget();
	const auto kept = budget;
	budget.bytes = 0;
	GAGCore::Sprite::setRetiredHighResolutionBudget(budget);
	if (trim != Trim::Smallest)
		GAGCore::Sprite::setRetiredHighResolutionBudget(kept);
}
} // namespace DeviceAdaptation
