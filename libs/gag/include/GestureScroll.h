// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ScrollPhysics.h>
#include <SDL3/SDL.h>
#include <cstring>
#include <optional>
#include <type_traits>

namespace GAGCore
{
enum class ScrollGesturePhase : Uint8 { None, Began, Changed, Ended, Cancelled };
// Value payload: SDL's queue and EventQueue copy the entire SDL_Event, including
// its padding. No Cocoa object or allocated pointer survives the native callback.
struct GestureScrollEvent
{
	Uint32 type = 0, reserved = 0;
	Uint64 timestamp = 0;
	SDL_WindowID windowID = 0;
	Uint32 padding = 0;
	Uint64 sequence = 0;
	double x = 0, y = 0, dx = 0, dy = 0;
	float wheelX = 0, wheelY = 0;
	SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL;
	ScrollGesturePhase phase = ScrollGesturePhase::None, momentum = ScrollGesturePhase::None;
	bool logical = false;
};
static_assert(std::is_trivially_copyable_v<GestureScrollEvent>);
static_assert(sizeof(GestureScrollEvent) <= sizeof(SDL_Event));
Uint32 gestureScrollEventType();
SDL_Event gestureScrollEvent(GestureScrollEvent sample);
std::optional<GestureScrollEvent> scrollGesture(const SDL_Event &event);
SDL_Event gestureWheelFallback(const GestureScrollEvent &sample);
void installMacScrollMonitor(SDL_Window *window);
void removeMacScrollMonitor();

// Drives an existing axis with native deltas. endDrag never synthesizes a fling.
// Consumers capture a surface only on Began and consume its remaining sequence
// even after cancellation. An unclaimed sequence keeps using wheel fallback.
class GestureScrollController
{
	Uint64 sequence = 0;
	Ticks last = 0;
	bool active = false, down = false, blocked = false, horizontal = false;
	int chosenAxis = 0;
  public:
	bool begins(const GestureScrollEvent &e) const { return e.phase == ScrollGesturePhase::Began && e.sequence != sequence; }
	bool owns(const GestureScrollEvent &e) const { return sequence != 0 && e.sequence == sequence; }
	bool pending() const { return active && !down; }
	bool needsFrames() const { return active; }
	void begin(const GestureScrollEvent &e, ScrollAxis &axis, double offset, double maximum, double extent, bool horizontal = false);
	void handle(const GestureScrollEvent &e, ScrollAxis &axis, double scale = 1, double sign = 1);
	void update(Ticks now, ScrollAxis &axis);
	void cancel(ScrollAxis &axis);
};
}
