// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL3/SDL.h>

#include <string>
#include "Material.h"

//! No map deposit. Old-format byte sentinel 255 is translated by the importer.
#define NO_RES_TYPE 0xFFFF

//! Either a resource type is NO_RES_TYPE and all others fields are zero, or the resource has a valid type and amount is NOT zero. This constraint does not apply if a resource is eternal.
struct Resource
{
	Uint16 type = NO_RES_TYPE;
	Uint8 variety = 0;
	Uint8 animation = 0;
	// Inline stock for single-yield resources; cached total for multi-yield cells.
	Uint32 amount = 0;
	Resource() = default;
	constexpr Resource(Uint16 kind, Uint8 variant, Uint32 stock, Uint8 phase)
		: type(kind), variety(variant), animation(phase), amount(stock) {}
	
	void clear() {type=NO_RES_TYPE; variety = 0;  amount = 0;  animation = 0; }
	bool operator==(const Resource&) const = default;
	Uint64 getUint64() const { return Uint64(amount) << 32 | Uint64(type) << 16 | Uint64(variety) << 8 | animation; }
	// A checksum contribution, not an injective representation or equality test.
	Uint32 getUint32() const { return Uint32(getUint64()) ^ (amount * 0x9e3779b1u); }
};
static_assert(sizeof(Resource) == 8);


#define MAX_NB_RESOURCES 15
#define MAX_RESOURCES MaterialCount
#define NO_RES -1
#define WOOD 0
#define WHEAT 1
#define PAPYRUS 2
#define STONE 3
#define ALGA 4
#define CHERRY 5
#define ORANGE 6
#define PRUNE 7
#define BASIC_COUNT 5
#define HAPPINESS_BASE 5
#define HAPPINESS_COUNT 3

// Presentation only: authored names may use an installed translation token.
std::string getResourceDisplayName(const std::string& authoredName);
