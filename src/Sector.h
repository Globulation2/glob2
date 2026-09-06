// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <list>

class Map;
class Game;
class Bullet;

//! A 16x16-tile piece of Map. Pure simulation state: the bullets list is
//! ticked in step() and contributes to Map::checkSum via the damage it
//! applies. Render-only state (bullet explosions, unit death animations)
//! used to live here too and is now on GameAnimations — see
//! src/render/GameAnimations.h.
class Sector
{
public:
	// === Sector geometry (cross-slice) ===
	//! Bit-shift converting a tile coordinate to its sector index, i.e.
	//! log2 of SECTOR_TILES. Used by Map::getSector and Map.cpp's sector
	//! grid math.
	static constexpr int SECTOR_SHIFT = 4;
	//! Side length of a sector in tiles (1 << SECTOR_SHIFT). A sector is
	//! the unit of bullet bookkeeping.
	static constexpr int SECTOR_TILES = 16;

	Sector() {}
	Sector(Game *);
	virtual ~Sector(void);
	// !This call is needed to use the Sector!
	void setGame(Game *game);

	void free(void);

	std::list<Bullet *> bullets;

	void save(GAGCore::OutputStream *stream);
	bool load(GAGCore::InputStream *stream, Game *game, Sint32 versionMinor);

	// Server needs only load and save from this class.
#ifndef YOG_SERVER_ONLY
	void step(void);
#endif  // !YOG_SERVER_ONLY
private:
	Map *map;
	Game *game;
};
