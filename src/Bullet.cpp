/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "Bullet.h"
#include <assert.h>
#include <SDL_endian.h>

Bullet::Bullet(SDL_RWops *stream)
{
	bool good=load(stream);
	assert(good);
}

Bullet::Bullet(Sint32 px, Sint32 py, Sint32 speedX, Sint32 speedY, Sint32 ticksLeft, Sint32 shootDamage, Sint32 targetX, Sint32 targetY)
{
	this->px=px;
	this->py=py;
	this->speedX=speedX;
	this->speedY=speedY;
	this->ticksInitial=ticksLeft;
	this->ticksLeft=ticksLeft;
	this->shootDamage=shootDamage;
	this->targetX=targetX;
	this->targetY=targetY;
}

bool Bullet::load(SDL_RWops *stream)
{
	px=SDL_ReadBE32(stream);
	py=SDL_ReadBE32(stream);
	speedX=SDL_ReadBE32(stream);
	speedY=SDL_ReadBE32(stream);
	ticksInitial=0;
	ticksLeft=SDL_ReadBE32(stream);
	shootDamage=SDL_ReadBE32(stream);
	targetX=SDL_ReadBE32(stream);
	targetY=SDL_ReadBE32(stream);
	return true;
}

void Bullet::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, px);
	SDL_WriteBE32(stream, py);
	SDL_WriteBE32(stream, speedX);
	SDL_WriteBE32(stream, speedY);
	SDL_WriteBE32(stream, ticksLeft);
	SDL_WriteBE32(stream, shootDamage);
	SDL_WriteBE32(stream, targetX);
	SDL_WriteBE32(stream, targetY);
}

void Bullet::step(void)
{
	if (ticksLeft>0)
	{
		//printf("bullet %d stepped to p=(%d, %d), tl=%d\n", (int)this, px, py, ticksLeft);
		px+=speedX;
		py+=speedY;
		ticksLeft--;
	}
}

