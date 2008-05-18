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

#ifndef __BULLET_H
#define __BULLET_H

#define SHOOTING_COOLDOWN_MAX 65536
//! Number of bit before any significant one, to avoid overflow while computing totalDefensePower in TeamStat.cpp
#define SHOOTING_COOLDOWN_MAGNITUDE 10

#include <GAGSys.h>

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

class Bullet
{
public:
	Bullet(GAGCore::InputStream *stream, Sint32 versionMinor);
	Bullet(Sint32 px, Sint32 py, Sint32 speedX, Sint32 speedY, Sint32 ticksLeft, Sint32 shootDamage, Sint32 targetX, Sint32 targetY, Sint32 revealX, Sint32 revealY, Sint32 revealW, Sint32 revealH);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
public:
	Sint32 px, py; //!< pixel precision point of x,y
	Sint32 speedX, speedY; //!< pixel precision speed.
	Sint32 ticksInitial;
	Sint32 ticksLeft;
	Sint32 shootDamage;
	Sint32 targetX, targetY;
	Sint32 revealX, revealY, revealW, revealH; //!< area of source of the bullet
public:
	void step(void);
};

struct BulletExplosion
{
	int x, y, ticksLeft;
};

#endif
 
