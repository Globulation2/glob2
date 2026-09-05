// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

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

