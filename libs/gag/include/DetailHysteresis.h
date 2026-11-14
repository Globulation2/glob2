// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace GAGCore
{
//! A detail level a view asks for each frame, where a higher level means less
//! detail. More detail is taken at once; less only after the view has wanted
//! it continuously for a delay, so zooming back and forth across a threshold
//! neither reloads nor recomposes anything. Shared by the HD sprite bands and
//! the portable renderer's terrain pages.
class DetailHysteresis
{
  public:
	explicit DetailHysteresis(unsigned level = 0) : current(level) {}
	//! Returns whether the level changed. `now` and `delay` are steady clock
	//! nanoseconds. The wait runs from the first frame wanting any lower
	//! detail, and the level then drops to whatever the latest frame wants; a
	//! frame wanting the current level restarts it.
	bool update(unsigned wanted, std::int64_t now, std::int64_t delay)
	{
		if (wanted >= current)
		{
			if (wanted == current)
			{
				waiting = false;
				return false;
			}
			if (!waiting)
			{
				waiting = true;
				lowerWantedSince = now;
			}
			if (now - lowerWantedSince < delay) return false;
		}
		waiting = false;
		current = wanted;
		return true;
	}
	unsigned level() const { return current; }

  private:
	unsigned current;
	// Whether every frame since lowerWantedSince has wanted less detail. A
	// separate flag, so a clock reading of zero is an ordinary time.
	bool waiting = false;
	std::int64_t lowerWantedSince = 0;
};
} // namespace GAGCore
