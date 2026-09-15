// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct EmojiOptions
{
	// Variant selectors use 0 for Random; see the registered choice labels.
	int character, outline, inverse, crossings;
	int wheat, wood, stone, algae, fruit;
	explicit EmojiOptions(const GenerationRequest &);
};
GeneratorDefinition emojiDefinition();
