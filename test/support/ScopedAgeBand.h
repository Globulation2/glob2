// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sets the player's age band (src/app/AgeGate.h) for a test and restores it.
// Chat is off until the band is 13 plus, so tests that exercise chat say so.
#include "GlobalContainer.h"
#include <string>

namespace glob2test
{
struct ScopedAgeBand
{
	std::string band, source;
	explicit ScopedAgeBand(const char *value = "13_plus", const char *by = "self")
		: band(globalContainer->settings.ageBand), source(globalContainer->settings.ageBandSource)
	{
		globalContainer->settings.ageBand = value;
		globalContainer->settings.ageBandSource = by;
	}
	~ScopedAgeBand()
	{
		if (globalContainer)
		{
			globalContainer->settings.ageBand = band;
			globalContainer->settings.ageBandSource = source;
		}
	}
	ScopedAgeBand(const ScopedAgeBand &) = delete;
	ScopedAgeBand &operator=(const ScopedAgeBand &) = delete;
};
}
