// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL.h>

#include <string>

//! No resource identifier. This correspond to resource type 255. On this case, variety, amount and animation are zero.
#define NO_RES_TYPE 0xFF

//! Either a resource type is NO_RES_TYPE and all others fields are zero, or the resource has a valid type and amount is NOT zero. This constraint does not apply if a resource is eternal.
struct Resource
{
	Uint8 type = NO_RES_TYPE;
	Uint8 variety = 0;
	Uint8 amount = 0;
	Uint8 animation = 0;
	
	void clear() {type=NO_RES_TYPE; variety = 0;  amount = 0;  animation = 0; }
	Uint32 getUint32() const { return animation | (amount<<8) | (variety<<16) | (type<<24); }
};

std::string getResourceName(int type);

#define MAX_NB_RESOURCES 15
#define MAX_RESOURCES 8
#define NO_RES -1
#define WOOD 0
#define CORN 1
#define PAPYRUS 2
#define STONE 3
#define ALGA 4
#define CHERRY 5
#define ORANGE 6
#define PRUNE 7
#define BASIC_COUNT 5
#define HAPPINESS_BASE 5
#define HAPPINESS_COUNT (MAX_RESOURCES-BASIC_COUNT)

