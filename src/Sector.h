// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#ifndef __SECTOR_H
#define __SECTOR_H

#include <list>

class Map;
class Game;
class Bullet;
struct BulletExplosion;
class Explosion;
class Team;

#ifndef YOG_SERVER_ONLY
struct UnitDeathAnimation
{
	UnitDeathAnimation(int x, int y, Team *team);
	int x, y, ticksLeft;
	Team *team;
};
#endif  // !YOG_SERVER_ONLY

// a 16x16 piece of Map
class Sector
{
public:
	Sector() {}
	Sector(Game *);
	virtual ~Sector(void);
	// !This call is needed to use the Sector!
	void setGame(Game *game);

	void free(void);

	std::list<Bullet *> bullets;
	std::list<BulletExplosion *> explosions;
#ifndef YOG_SERVER_ONLY
	std::list<UnitDeathAnimation *> deathAnimations;
#endif  // !YOG_SERVER_ONLY

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

#endif
 
