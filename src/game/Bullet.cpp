// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Bullet.h"
#include "FileFormatVersions.h"
#include "Team.h"
#include <BinaryStream.h>
#include <stdexcept>
#include <limits>
#include <assert.h>
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
	unitDamage.fill(shootDamage);
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
	ticksLeft = stream->readSint32("ticksLeft");
	if (versionMinor >= 85)
		ticksInitial = stream->readSint32("ticksInitial");
	else
		ticksInitial = ticksLeft;
	shootDamage = stream->readSint32("shootDamage");
	targetX = stream->readSint32("targetX");
	targetY = stream->readSint32("targetY");

	revealX = stream->readSint32("revealX");
	revealY = stream->readSint32("revealY");
	revealW = stream->readSint32("revealW");
	revealH = stream->readSint32("revealH");
	const auto endpointX = int64_t(px) + int64_t(speedX) * ticksLeft;
	const auto endpointY = int64_t(py) + int64_t(speedY) * ticksLeft;
	if (ticksLeft < 0 || ticksInitial < ticksLeft || shootDamage < 0 ||
		endpointX < std::numeric_limits<Sint32>::min() || endpointX > std::numeric_limits<Sint32>::max() ||
		endpointY < std::numeric_limits<Sint32>::min() || endpointY > std::numeric_limits<Sint32>::max() ||
		revealW < 0 || revealW > 32767 || revealH < 0 || revealH > 32767 ||
		int64_t(revealX)+revealW > std::numeric_limits<Sint32>::max() ||
		int64_t(revealY)+revealH > std::numeric_limits<Sint32>::max())
		throw std::runtime_error("Invalid saved bullet trajectory");
	sourceTeam = -1;
	if (versionMinor >= FILE_FORMAT_VERSION_GAMEPLAY_STATS)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		sourceTeam = stream->readSint32("sourceTeam");
		if (sourceTeam < -1 || sourceTeam >= Team::MAX_COUNT)
			throw std::runtime_error("Invalid bullet source team");
	}
	unitDamage.fill(shootDamage);
	if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		for (int u = 0; u < NB_UNIT_TYPE; ++u)
		{
			unitDamage[u] = stream->readSint32("unitDamage" + std::to_string(u));
			if (unitDamage[u] < 0 || unitDamage[u] > 1000000)
				throw std::runtime_error("Invalid saved bullet damage");
		}
	}
	return true;
}

void Bullet::save(GAGCore::OutputStream *stream)
{
	stream->writeSint32(px, "px");
	stream->writeSint32(py, "py");
	stream->writeSint32(speedX, "speedX");
	stream->writeSint32(speedY, "speedY");
	stream->writeSint32(ticksLeft, "ticksLeft");
	stream->writeSint32(ticksInitial, "ticksInitial");
	stream->writeSint32(shootDamage, "shootDamage");
	stream->writeSint32(targetX, "targetX");
	stream->writeSint32(targetY, "targetY");
	stream->writeSint32(revealX, "revealX");
	stream->writeSint32(revealY, "revealY");
	stream->writeSint32(revealW, "revealW");
	stream->writeSint32(revealH, "revealH");
	stream->writeSint32(sourceTeam, "sourceTeam");
	for (int u = 0; u < NB_UNIT_TYPE; ++u)
		stream->writeSint32(unitDamage[u], "unitDamage" + std::to_string(u));
}

void Bullet::step(void)
{
	if (ticksLeft>0)
	{
		px+=speedX;
		py+=speedY;
		ticksLeft--;
	}
}


Uint32 Bullet::checkSum() const
{
	Uint32 checksum = 0;
	const auto mix = [&](Sint32 value) {
		checksum = (checksum << 1) | (checksum >> 31);
		checksum ^= static_cast<Uint32>(value);
	};
	for (const Sint32 value : {px, py, speedX, speedY, ticksInitial, ticksLeft,
		shootDamage, targetX, targetY, revealX, revealY, revealW, revealH, sourceTeam}) mix(value);
	for (const Sint32 value : unitDamage) mix(value);
	return checksum;
}
