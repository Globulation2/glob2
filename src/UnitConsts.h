/*
  Copyright (C) 2001-2006 Stephane Magnenat & Luc-Olivier de Charriere
  for any question or comment contact us at nct at ysagoon dot com or
  nuage at ysagoon dot com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __UNIT_CONSTS_H
#define __UNIT_CONSTS_H

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

namespace UnitConsts
{
	int GIDtoID(Uint16 gid)
	{
		assert(gid < 32768);
		return (gid % 1024);
	}
	
	int GIDtoTeam(Uint16 gid)
	{
		assert(gid < 32768);
		return (gid / 1024);
	}
	
	Uint16 GIDfrom(int id, int team)
	{
		assert(id >= 0);
		assert(id < 1024);
		assert(team >= 0);
		assert(team < 32);
		return id + team * 1024;
	}
}

#endif
