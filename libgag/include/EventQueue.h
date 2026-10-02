// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_events.h>
#include <deque>
#include <string>
#include <vector>

namespace GAGCore
{
// SDL3 owns text-event strings only until another event pump. Keep the payloads
// used by our input handlers alive through batched dispatch and deferred ticks.
class EventQueue
{
	std::vector<SDL_Event> queued;
	std::deque<std::string> text;
	const char *retain(const char *value)
	{
		if (!value)
			return nullptr;
		text.emplace_back(value);
		return text.back().c_str();
	}

  public:
	EventQueue() = default;
	EventQueue(const EventQueue &) = delete;
	EventQueue &operator=(const EventQueue &) = delete;
	void push_back(SDL_Event event)
	{
		if (event.type == SDL_EVENT_TEXT_INPUT)
			event.text.text = retain(event.text.text);
		else if (event.type == SDL_EVENT_TEXT_EDITING)
			event.edit.text = retain(event.edit.text);
		queued.push_back(event);
	}
	void clear()
	{
		queued.clear();
		text.clear();
	}
	const std::vector<SDL_Event> &events() const { return queued; }
};
} // namespace GAGCore
