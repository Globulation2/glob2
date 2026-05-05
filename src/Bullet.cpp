// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Bullet.h"
#include <assert.h>
#include <SDL_endian.h>
#include <Stream.h>

Bullet::Bullet(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	bool good = load(stream, versionMinor);
	assert(good);
}

Bullet::Bullet(Sint32 px, Sint32 py, Sint32 speedX, Sint32 speedY, Sint32 ticksLeft, Sint32 shootDamage, Sint32 targetX, Sint32 targetY, Sint32 revealX, Sint32 revealY, Sint32 revealW, Sint32 revealH)
{
	this->px = px;
	this->py = py;
	this->speedX = speedX;
	this->speedY = speedY;
	this->ticksInitial = ticksLeft;
	this->ticksLeft = ticksLeft;
	this->shootDamage = shootDamage;
	this->targetX = targetX;
	this->targetY = targetY;
	this->revealX = revealX;
	this->revealY = revealY;
	this->revealW = revealW;
	this->revealH = revealH;
}

bool Bullet::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	px = stream->readSint32("px");
	py = stream->readSint32("py");
	speedX = stream->readSint32("speedX");
	speedY = stream->readSint32("speedY");
	ticksInitial = 0;
	ticksLeft = stream->readSint32("ticksLeft");
	shootDamage = stream->readSint32("shootDamage");
	targetX = stream->readSint32("targetX");
	targetY = stream->readSint32("targetY");

	revealX = stream->readSint32("revealX");
	revealY = stream->readSint32("revealY");
	revealW = stream->readSint32("revealW");
	revealH = stream->readSint32("revealH");
	return true;
}

void Bullet::save(GAGCore::OutputStream *stream)
{
	stream->writeSint32(px, "px");
	stream->writeSint32(py, "py");
	stream->writeSint32(speedX, "speedX");
	stream->writeSint32(speedY, "speedY");
	stream->writeSint32(ticksLeft, "ticksLeft");
	stream->writeSint32(shootDamage, "shootDamage");
	stream->writeSint32(targetX, "targetX");
	stream->writeSint32(targetY, "targetY");
	stream->writeSint32(revealX, "revealX");
	stream->writeSint32(revealY, "revealY");
	stream->writeSint32(revealW, "revealW");
	stream->writeSint32(revealH, "revealH");
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

