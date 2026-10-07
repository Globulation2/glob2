// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <cassert>
#include <cstddef>

#include "LegacyResourceTypes.h"

// Frozen legacy values for the ecology comparison harness.
static constexpr LegacyResourceType kLegacyResourceTypes[] = {
	// WOOD
	{ /*gfxId*/  0, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 1, /*expendable*/ 1, /*eternal*/ 0, /*granular*/ 0, /*visibleToBeCollected*/ 0,
	  /*minimapR*/   0, /*minimapG*/  60, /*minimapB*/   0, /*clearable*/ 1 },
	// WHEAT
	{ /*gfxId*/ 10, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 1, /*expendable*/ 1, /*eternal*/ 0, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/ 211, /*minimapG*/ 207, /*minimapB*/ 167, /*clearable*/ 1 },
	// PAPYRUS
	{ /*gfxId*/ 20, /*sizesCount*/ 5, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 0, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/   0, /*minimapG*/   0, /*minimapB*/   0, /*clearable*/ 1 },
	// STONE
	{ /*gfxId*/ 30, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 0, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/ 104, /*minimapG*/ 112, /*minimapB*/ 124, /*clearable*/ 0 },
	// ALGA
	{ /*gfxId*/ 40, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 1, /*expendable*/ 1, /*eternal*/ 0, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/  41, /*minimapG*/ 157, /*minimapB*/ 165, /*clearable*/ 1 },
	// CHERRY
	{ /*gfxId*/ 50, /*sizesCount*/ 4, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 1,
	  /*minimapR*/ 255, /*minimapG*/ 127, /*minimapB*/   0, /*clearable*/ 0 },
	// ORANGE
	{ /*gfxId*/ 55, /*sizesCount*/ 4, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 1,
	  /*minimapR*/ 255, /*minimapG*/ 127, /*minimapB*/   0, /*clearable*/ 0 },
	// PRUNE
	{ /*gfxId*/ 60, /*sizesCount*/ 4, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 1,
	  /*minimapR*/ 255, /*minimapG*/ 127, /*minimapB*/   0, /*clearable*/ 0 },
};

const LegacyResourceType* LegacyResourcesTypes::get(unsigned int num) const
{
	const std::size_t count = sizeof(kLegacyResourceTypes) / sizeof(kLegacyResourceTypes[0]);
	if (num < count)
		return &kLegacyResourceTypes[num];
	assert(false);
	return nullptr;
}

std::size_t LegacyResourcesTypes::size() const
{
	return sizeof(kLegacyResourceTypes) / sizeof(kLegacyResourceTypes[0]);
}
