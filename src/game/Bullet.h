// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#define SHOOTING_COOLDOWN_MAX 65536
//! Number of bit before any significant one, to avoid overflow while computing totalDefensePower in TeamStat.cpp
#define SHOOTING_COOLDOWN_MAGNITUDE 10

#include <GAGSys.h>
#include <array>
#include <algorithm>
#include <vector>
#include <stdexcept>
#include "UnitCatalog.h"
#include "UnitConsts.h"

namespace GAGCore
{
	class InputStream;
	class OutputStream;
}

// Preserve allocation-free damage storage for the stock three targets.
class BulletUnitDamage
{
public:
    std::size_t size() const { return BuiltinUnitCount + extra.size(); }
    std::size_t capacityBytes() const { return extra.capacity()*sizeof(Sint32); }
    void resize(std::size_t count) {
        if (count < BuiltinUnitCount || count > UnitCatalog::Capacity)
            throw std::length_error("Invalid bullet unit count");
        extra.resize(count-BuiltinUnitCount);
    }
    void fill(Sint32 value) { builtin.fill(value); std::fill(extra.begin(),extra.end(),value); }
    Sint32& operator[](std::size_t id) { return id<BuiltinUnitCount?builtin[id]:extra.at(id-BuiltinUnitCount); }
    Sint32 operator[](std::size_t id) const { return id<BuiltinUnitCount?builtin[id]:extra.at(id-BuiltinUnitCount); }
    bool operator==(const BulletUnitDamage&) const = default;
private:
    std::array<Sint32,BuiltinUnitCount> builtin{};
    std::vector<Sint32> extra;
};

class Bullet
{
public:
	Bullet(GAGCore::InputStream *stream, Sint32 versionMinor);
	Bullet(Sint32 px, Sint32 py, Sint32 speedX, Sint32 speedY, Sint32 ticksLeft, Sint32 shootDamage, Sint32 targetX, Sint32 targetY, Sint32 revealX, Sint32 revealY, Sint32 revealW, Sint32 revealH);
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);
public:
  Sint32 sourceTeam = -1; //!< Diagnostic only; old saves have unknown attribution.
  Sint32 px, py;          //!< pixel precision point of x,y
  Sint32 speedX, speedY;  //!< pixel precision speed.
  Sint32 ticksInitial;
  Sint32 ticksLeft;
  Sint32 shootDamage; // building damage; legacy scalar retained on the wire
  BulletUnitDamage unitDamage{}; // launch-time damage, independent of source lifetime
  Sint32 targetX, targetY;
  Sint32 revealX, revealY, revealW, revealH; //!< area of source of the bullet
public:
	void step(void);
	Uint32 checkSum() const;
};

