// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GAGSys.h>
#include <cstddef>

#include "Ressource.h"

// Frozen pre-runtime catalog, exclusively for the legacy ecology reference.
struct LegacyResourceType
{
	Sint32 gfxId;
	Sint32 sizesCount;
	Sint32 varietiesCount;
	// The following values are integers, but are used like booleans.
	Sint32 shrinkable; // whether the resource is depleted when it is collected.
	Sint32 expendable; // probably a misspelling of 'extendable'. What it actually determines is whether
	                   // the resource multiplies itself to adjacent squares over time.
	Sint32 eternal; // whether the resource cannot be destroyed or completely consumed.
	Sint32 granular; // whether the resource is decremented, rather than removed, when it is harvested/cleared.
	Sint32 visibleToBeCollected; // whether the resource can only be collected if the fog of war is cleared on its location.
	Sint32 minimapR, minimapG, minimapB;
	// Whether a worker's clearArea action will remove this resource. Stone, Cherry,
	// Orange and Prune are non-clearable; the rest (Wood, Wheat, Papyrus, Alga) are
	// clearable. Previously hard-coded as a type==X || type==Y predicate at the call sites.
	Sint32 clearable;
};

// Test-only lookup; never used by the production engine.
class LegacyResourcesTypes
{
public:
	const LegacyResourceType* get(unsigned int num) const;
	std::size_t size() const;
};
