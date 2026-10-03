// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <string>

// Presentation values shared by telemetry extraction and the Scene. Keep this
// independent of the live telemetry sink and simulation objects.
namespace AITelemetry
{
struct NamedValue
{
	std::string name, value, unit, meaning;
	Uint32 updated = 0;
	bool operator==(const NamedValue &) const = default;
};
} // namespace AITelemetry
