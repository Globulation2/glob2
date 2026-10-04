// SPDX-License-Identifier: GPL-3.0-or-later
#include <GestureScroll.h>
#include "MacScrollMonitorPrivate.h"
#import <AppKit/AppKit.h>
namespace GAGCore
{
namespace
{
id monitor = nil;
Uint64 sequence = 0;
std::optional<Uint64> timestampOffset;
ScrollGesturePhase phase(NSEventPhase value)
{
	if (value & NSEventPhaseCancelled) return ScrollGesturePhase::Cancelled;
	if (value & NSEventPhaseEnded) return ScrollGesturePhase::Ended;
	if (value & NSEventPhaseBegan) return ScrollGesturePhase::Began;
	if (value & (NSEventPhaseChanged | NSEventPhaseStationary)) return ScrollGesturePhase::Changed;
	return ScrollGesturePhase::None;
}
} // namespace
NSEvent *MacScrollDetail::routeScroll(SDL_WindowID windowID, NSWindow *native, NSEvent *event)
{
	if (event.window != native || (!event.phase && !event.momentumPhase) || !event.hasPreciseScrollingDeltas)
		return event;
	GestureScrollEvent sample;
	sample.phase = phase(event.phase);
	sample.momentum = phase(event.momentumPhase);
	// MayBegin is a hover/preflight notification, not a captured gesture.
	if (sample.phase == ScrollGesturePhase::None && sample.momentum == ScrollGesturePhase::None) return event;
	if (sample.phase == ScrollGesturePhase::Began) ++sequence;
	sample.sequence = sequence;
	sample.windowID = windowID;
	const Uint64 now = SDL_GetTicksNS();
	const Uint64 stamp = Uint64(event.timestamp * SDL_NS_PER_SECOND);
	if (stamp && !timestampOffset) timestampOffset = now - stamp;
	sample.timestamp = stamp ? stamp + *timestampOffset : now;
	if (sample.timestamp > now)
	{
		*timestampOffset -= sample.timestamp - now;
		sample.timestamp = now;
	}
	NSView *view = native.contentView;
	NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
	sample.x = point.x;
	sample.y = view.isFlipped ? point.y : view.bounds.size.height - point.y;
	sample.dx = event.scrollingDeltaX;
	sample.dy = event.scrollingDeltaY;
	// Match SDL's Cocoa wheel normalization only on the fallback path.
	sample.wheelX = float(-sample.dx * 0.1);
	sample.wheelY = float(sample.dy * 0.1);
	sample.direction = event.isDirectionInvertedFromDevice ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
	auto queued = gestureScrollEvent(sample);
	return SDL_PushEvent(&queued) ? nil : event;
}
void removeMacScrollMonitor()
{
	if (monitor) [NSEvent removeMonitor:monitor];
	monitor = nil;
	timestampOffset.reset();
	// Value payloads need no cleanup. Remove queued samples for the old window.
	SDL_FlushEvent(gestureScrollEventType());
}
void installMacScrollMonitor(SDL_Window *window)
{
	removeMacScrollMonitor();
	auto *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
	if (!native) return; // dummy driver and non-Cocoa test windows
	const SDL_WindowID windowID = SDL_GetWindowID(window);
	monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *event) {
		return MacScrollDetail::routeScroll(windowID, native, event);
	}];
}
}
