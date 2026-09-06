// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GAGSys.h>
#include <cstddef>

#include "Ressource.h"

// RessourceType describes the static configuration of a resource kind
// (Wood, Corn, Papyrus, Stone, Alga, Cherry, Orange, Prune). Historically
// these values were loaded at runtime from data/ressources.txt via the
// EntitiesTypes<T> template; they are now baked into a compile-time const
// table in resources.cpp. The fields remain Sint32 for ABI parity with the
// old loader (booleans were stored as ints).
struct RessourceType
{
	Sint32 terrain;
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
	// Orange and Prune are non-clearable; the rest (Wood, Corn, Papyrus, Alga) are
	// clearable. Previously hard-coded as a type==X || type==Y predicate at the call sites.
	Sint32 clearable;
};

// RessourcesTypes is the read-only registry of resource types, indexed by the
// in-game RessourceType integer ID (WOOD=0, CORN=1, ..., PRUNE=7). The class
// keeps the same accessor surface (.get / .size) as the old EntitiesTypes<T>
// subclass so existing callers compile unchanged; it is now backed by a
// compile-time const array rather than a parsed text file.
class RessourcesTypes
{
public:
	const RessourceType* get(unsigned int num) const;
	std::size_t size() const;
};
