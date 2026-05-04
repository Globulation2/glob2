/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <cassert>
#include <cstddef>

#include "ressource_type.h"

// Compile-time const table of resource types. The order MUST match the
// integer IDs declared in Ressource.h (WOOD=0, CORN=1, PAPYRUS=2, STONE=3,
// ALGA=4, CHERRY=5, ORANGE=6, PRUNE=7) — those IDs are persisted in saves,
// replays and network traffic, so reordering is a behavioral change.
//
// Values are transcribed from data/ressources.txt (which used a defaults +
// per-section overrides format); each entry below spells out every field
// explicitly. The 'clearable' field replaces a hard-coded predicate that
// previously listed WOOD/CORN/PAPYRUS/ALGA at the call sites in
// UnitMovement.cpp and MapGradientArea.cpp.
static constexpr RessourceType kRessourceTypes[] = {
	// WOOD
	{ /*terrain*/ 2, /*gfxId*/  0, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 1, /*expendable*/ 1, /*eternal*/ 0, /*granular*/ 0, /*visibleToBeCollected*/ 0,
	  /*minimapR*/   0, /*minimapG*/  60, /*minimapB*/   0, /*clearable*/ 1 },
	// CORN
	{ /*terrain*/ 2, /*gfxId*/ 10, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 1, /*expendable*/ 1, /*eternal*/ 0, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/ 211, /*minimapG*/ 207, /*minimapB*/ 167, /*clearable*/ 1 },
	// PAPYRUS
	{ /*terrain*/ 2, /*gfxId*/ 20, /*sizesCount*/ 5, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 0, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/   0, /*minimapG*/   0, /*minimapB*/   0, /*clearable*/ 1 },
	// STONE
	{ /*terrain*/ 2, /*gfxId*/ 30, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 0, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/ 104, /*minimapG*/ 112, /*minimapB*/ 124, /*clearable*/ 0 },
	// ALGA
	{ /*terrain*/ 0, /*gfxId*/ 40, /*sizesCount*/ 5, /*varietiesCount*/ 2,
	  /*shrinkable*/ 1, /*expendable*/ 1, /*eternal*/ 0, /*granular*/ 1, /*visibleToBeCollected*/ 0,
	  /*minimapR*/  41, /*minimapG*/ 157, /*minimapB*/ 165, /*clearable*/ 1 },
	// CHERRY
	{ /*terrain*/ 2, /*gfxId*/ 50, /*sizesCount*/ 4, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 1,
	  /*minimapR*/ 255, /*minimapG*/ 127, /*minimapB*/   0, /*clearable*/ 0 },
	// ORANGE
	{ /*terrain*/ 2, /*gfxId*/ 55, /*sizesCount*/ 4, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 1,
	  /*minimapR*/ 255, /*minimapG*/ 127, /*minimapB*/   0, /*clearable*/ 0 },
	// PRUNE
	{ /*terrain*/ 2, /*gfxId*/ 60, /*sizesCount*/ 4, /*varietiesCount*/ 1,
	  /*shrinkable*/ 1, /*expendable*/ 0, /*eternal*/ 1, /*granular*/ 1, /*visibleToBeCollected*/ 1,
	  /*minimapR*/ 255, /*minimapG*/ 127, /*minimapB*/   0, /*clearable*/ 0 },
};

const RessourceType* RessourcesTypes::get(unsigned int num) const
{
	const std::size_t count = sizeof(kRessourceTypes) / sizeof(kRessourceTypes[0]);
	if (num < count)
		return &kRessourceTypes[num];
	assert(false);
	return nullptr;
}

std::size_t RessourcesTypes::size() const
{
	return sizeof(kRessourceTypes) / sizeof(kRessourceTypes[0]);
}
