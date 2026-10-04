// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <string>

// Copied diagnostic values for presentation; no live simulation access.
namespace AITelemetry
{
struct NamedValue
{
	std::string name, value, unit, meaning;
	Uint32 updated = 0;
	bool operator==(const NamedValue &) const = default;
};
} // namespace AITelemetry
