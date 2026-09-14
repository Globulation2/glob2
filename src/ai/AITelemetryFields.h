// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AITelemetry.h"
namespace AITrace
{
#define AI_BEGIN(number)                                                                           \
	namespace AI##number                                                                           \
	{                                                                                              \
		enum Field : unsigned                                                                      \
		{                                                                                          \
			Begin = AITelemetry::Specific - 1,
#define AI_FIELD(key, name, unit, meaning, type, kind) key,
#define AI_END()                                                                                   \
	}                                                                                              \
	;                                                                                              \
	}
#include "AITelemetryFields.inc"
#undef AI_BEGIN
#undef AI_FIELD
#undef AI_END
} // namespace AITrace
