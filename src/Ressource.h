// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __RESSOURCE_H
#define __RESSOURCE_H

#include <SDL.h>

#include <string>

//! No ressource identifier. This correspond to ressource type 255. On this case, variety, amout and animation are zero.
#define NO_RES_TYPE 0xFF

//! Either a ressource type is NO_RES_TYPE and all others fields are zero, or the ressource has a valid type and amount is NOT zero. This constraint does not apply if a ressource is eternal.
struct Ressource
{
	Uint8 type = NO_RES_TYPE;
	Uint8 variety = 0;
	Uint8 amount = 0;
	Uint8 animation = 0;
	
	void clear() {type=NO_RES_TYPE; variety = 0;  amount = 0;  animation = 0; }
	//void setUint32(Uint32 i) { animation=i&0xFF; amount=(i>>8)&0xFF; variety=(i>>16)&0xFF; type=(i>>24)&0xFF; }
	Uint32 getUint32() const { return animation | (amount<<8) | (variety<<16) | (type<<24); }
};

std::string getRessourceName(int type);

#define MAX_NB_RESSOURCES 15
#define MAX_RESSOURCES 8
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
#define HAPPYNESS_BASE 5
#define HAPPYNESS_COUNT (MAX_RESSOURCES-BASIC_COUNT)

#endif
