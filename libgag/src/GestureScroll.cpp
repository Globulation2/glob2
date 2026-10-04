// SPDX-License-Identifier: GPL-3.0-or-later
#include <GestureScroll.h>
#include <cmath>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
namespace GAGCore
{
Uint32 gestureScrollEventType()
{
	static const Uint32 type = SDL_RegisterEvents(1);
	return type;
}
SDL_Event gestureScrollEvent(GestureScrollEvent sample)
{
	sample.type = gestureScrollEventType();
	SDL_Event event{};
	std::memcpy(&event, &sample, sizeof(sample));
	return event;
}
std::optional<GestureScrollEvent> scrollGesture(const SDL_Event &event)
{
	if (event.type != gestureScrollEventType()) return std::nullopt;
	GestureScrollEvent sample;
	std::memcpy(&sample, &event, sizeof(sample));
	return sample;
}
SDL_Event gestureWheelFallback(const GestureScrollEvent &e)
{
	SDL_Event event{};
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.timestamp = e.timestamp;
	event.wheel.windowID = e.windowID;
	event.wheel.x = e.wheelX;
	event.wheel.y = e.wheelY;
	event.wheel.mouse_x = float(e.x);
	event.wheel.mouse_y = float(e.y);
	event.wheel.direction = e.direction;
	return event;
}
void GestureScrollController::begin(const GestureScrollEvent &e, ScrollAxis &axis, double offset, double maximum, double extent, bool sideways)
{
	sequence = e.sequence;
	last = e.timestamp / SDL_NS_PER_MS;
	active = down = true;
	blocked = false;
	horizontal = sideways;
	chosenAxis = 0;
	auto config = ScrollPresets::widget();
	config.momentum = false;
	config.rubberBandCoefficient *= 0.5;
	axis.setConfig(config);
	axis.setBounds(0, maximum, extent);
	// A new gesture takes over the current visual offset, including a spring.
	if (axis.offset() != offset) axis.setOffset(offset);
	axis.beginDrag(last);
}
void GestureScrollController::handle(const GestureScrollEvent &e, ScrollAxis &axis, double scale, double sign)
{
	if (!owns(e) || !active) return;
	last = e.timestamp / SDL_NS_PER_MS;
	const bool tail = e.momentum != ScrollGesturePhase::None;
	if (e.phase == ScrollGesturePhase::Cancelled || e.momentum == ScrollGesturePhase::Cancelled)
	{
		cancel(axis);
		return;
	}
	if (tail) down = false;
	if (!blocked && (!tail || scrollTuning().momentum > 0))
	{
		if (!chosenAxis && (e.dx != 0 || e.dy != 0))
			chosenAxis = horizontal && std::fabs(e.dx) >= std::fabs(e.dy) ? 1 : 2;
		const double delta = -(chosenAxis == 1 ? e.dx : e.dy) * scale * sign;
		// Discard invisible excess at firm bounds so reversing responds immediately.
		axis.drag(last, delta);
		if (!tail && !axis.config().bounce) axis.beginDrag(last);
		if (tail && ((delta < 0 && axis.offset() <= axis.minimum()) ||
					 (delta > 0 && axis.offset() >= axis.maximum())))
		{
			axis.endDrag(last);
			blocked = true;
		}
	}
	if (e.phase == ScrollGesturePhase::Ended)
	{
		down = false;
		if (axis.overscroll() != 0) blocked = true;
		axis.endDrag(last);
	}
	if (e.momentum == ScrollGesturePhase::Ended)
	{
		axis.endDrag(last);
		active = false;
	}
}
void GestureScrollController::update(Ticks now, ScrollAxis &axis)
{
	if (pending() && now >= last && now - last >= 150)
	{
		axis.endDrag(now);
		active = false;
	}
	if (axis.isAnimating()) axis.step(now);
}
void GestureScrollController::cancel(ScrollAxis &axis)
{
	active = down = false;
	blocked = true;
	axis.setOffset(axis.clampedOffset());
}
#if !defined(__APPLE__) || !TARGET_OS_OSX
void installMacScrollMonitor(SDL_Window *) {}
void removeMacScrollMonitor() {}
#endif
}
