// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::SearchTools;

MapInfo::MapInfo(Echo& echo) : echo(echo)
{

}



int MapInfo::get_width()
{
	return echo.player->map->getW();
}



int MapInfo::get_height()
{
	return echo.player->map->getH();
}



bool MapInfo::is_forbidden_area(int x, int y)
{
	return echo.player->map->isForbidden(x, y, echo.player->team->me);
}



bool MapInfo::is_guard_area(int x, int y)
{
	return echo.player->map->isGuardArea(x, y, echo.player->team->me);
}



bool MapInfo::is_clearing_area(int x, int y)
{
	return echo.player->map->isClearArea(x, y, echo.player->team->me);
}



bool MapInfo::is_discovered(int x, int y)
{
	return echo.player->map->isMapDiscovered(x, y, echo.player->team->me);
}



bool MapInfo::is_ressource(int x, int y, int type)
{
	return echo.player->map->isRessourceTakeable(x, y, type);
}



bool MapInfo::is_ressource(int x, int y)
{
	return echo.player->map->isRessource(x, y);
}



bool MapInfo::is_water(int x, int y)
{
	return echo.player->map->isWater(x, y);
}



bool MapInfo::is_sand(int x, int y)
{
	return echo.player->map->isSand(x, y);
}



bool MapInfo::is_grass(int x, int y)
{
	return echo.player->map->isGrass(x, y);
}



bool MapInfo::backs_onto_sand(int x, int y)
{
	if(echo.player->map->hasSand(x-1, y))
		return true;
	if(echo.player->map->hasSand(x+1, y))
		return true;
	if(echo.player->map->hasSand(x-1, y-1))
		return true;
	if(echo.player->map->hasSand(x, y-1))
		return true;
	if(echo.player->map->hasSand(x+1, y-1))
		return true;
	if(echo.player->map->hasSand(x-1, y+1))
		return true;
	if(echo.player->map->hasSand(x, y+1))
		return true;
	if(echo.player->map->hasSand(x+1, y+1))
		return true;
	return false;
}



int MapInfo::get_ammount_ressource(int x, int y)
{
	return echo.player->map->getRessource(x, y).amount;
}
