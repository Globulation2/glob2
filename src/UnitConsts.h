// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __UNIT_CONSTS_H
#define __UNIT_CONSTS_H

#include <assert.h>
#include <string>

enum Abilities
{
	STOP_WALK=0,
	STOP_SWIM=1,
	STOP_FLY=2,
	
	WALK=3,
	SWIM=4,
	FLY=5,
	BUILD=6,
	HARVEST=7,
	ATTACK_SPEED=8,
	ATTACK_STRENGTH=9,
	
	MAGIC_ATTACK_AIR=10,
	MAGIC_ATTACK_GROUND=11,
	MAGIC_CREATE_WOOD=12,
	MAGIC_CREATE_CORN=13,
	MAGIC_CREATE_ALGA=14,
	
	ARMOR=15, /* old 10 */
	HP=16, /* old 11 */
	
	HEAL=17, /* old 12 */
	FEED=18 /* old 13 */
};
const int NB_MOVE=9;
const int NB_ABILITY=17;

const int WORKER=0;
const int EXPLORER=1;
const int WARRIOR=2;
const int NB_UNIT_TYPE=3;

const int NB_UNIT_LEVELS=4;

std::string getUnitName(int type);

#endif
 
