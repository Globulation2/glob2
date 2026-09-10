// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct ConcreteIslandsOptions
{
	int channel_width, extra_islands;
	explicit ConcreteIslandsOptions(const GenerationRequest &r)
		: channel_width(r.option("channel-width")), extra_islands(r.option("extra-islands"))
	{
	}
};
GeneratorDefinition concreteIslandsDefinition();
