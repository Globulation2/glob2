// SPDX-License-Identifier: GPL-3.0-or-later
// `glob2 dev dump-wheat`, reached through AIRegistry::dumpWheatPlan so the entry
// point does not name Cortex.
#include "CortexFoodSources.h"
#include "Map.h"
#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "MapHeader.h"
#include "Team.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <Stream.h>
#include <Toolkit.h>
#include <iostream>
#include <string>
#include <vector>

namespace Cortex
{
// Headless tooling (AI wheat-protection eyeball): run the Cortex wheat scan over
// one team's territory on a freshly-loaded map and print the checkerboard it
// WOULD paint, swept over the open-margin range N=0..2. No Orders are emitted —
// this is the isolated geometry/reconcile core (ai/cortex/CortexFoodSources.*).
//
// A loaded .map has no colony and is fully fogged, so this differs from the live
// path in two debug-only ways, both documented inline: fog is bypassed
// (ignoreFOW), and the territory region is faked as the start/colony bounding box
// padded generously (the live path uses the real colony bbox + margin).
int dumpWheatPlan(const std::string& mapName, int team)
{
	using namespace GAGCore;
	InputStream* stream = new BinaryInputStream(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), mapName));
	if (stream->isEndOfStream())
	{
		std::cerr << "dump-wheat: cannot open " << mapName << std::endl;
		delete stream;
		return 3;
	}
	Game game(NULL);
	bool ok = game.load(stream);
	delete stream;
	if (!ok)
	{
		std::cerr << "dump-wheat: failed to load " << mapName << std::endl;
		return 3;
	}

	Map& map = game.map;
	const int w = map.getW();
	const int h = map.getH();
	const int teamCount = game.mapHeader.getNumberOfTeams();
	if (team < 0 || team >= teamCount || game.teams[team] == NULL)
	{
		std::cerr << "dump-wheat: team " << team << " out of range (teams=" << teamCount << ")" << std::endl;
		return 3;
	}

	Team* tm = game.teams[team];
	const Uint32 teamMask = Team::teamNumberToMask(team);

	// Consumer seeds = feeding-building (inn) tiles; the colony bounding box grows
	// over every real building. A freshly-loaded .map usually has no buildings, so
	// fall back to the team start position as the single consumer seed.
	std::vector<int> seeds;
	std::vector<bool> seedBit(static_cast<size_t>(w) * h, false);
	int bbMinX = w, bbMinY = h, bbMaxX = -1, bbMaxY = -1;
	for (const Building* b : tm->liveBuildings.entries())
	{
		if (b == NULL || b->buildingState == Building::DEAD)
			continue;
		if (b->posX < bbMinX) bbMinX = b->posX;
		if (b->posX > bbMaxX) bbMaxX = b->posX;
		if (b->posY < bbMinY) bbMinY = b->posY;
		if (b->posY > bbMaxY) bbMaxY = b->posY;
		if (b->type && b->type->canFeedUnit)
		{
			const int idx = static_cast<int>(map.coordToIndex(b->posX, b->posY));
			seeds.push_back(idx);
			seedBit[idx] = true;
		}
	}
	const int startX = tm->startPosX;
	const int startY = tm->startPosY;
	if (startX < bbMinX) bbMinX = startX;
	if (startX > bbMaxX) bbMaxX = startX;
	if (startY < bbMinY) bbMinY = startY;
	if (startY > bbMaxY) bbMaxY = startY;
	if (seeds.empty())
	{
		const int idx = static_cast<int>(map.coordToIndex(startX, startY));
		seeds.push_back(idx);
		seedBit[idx] = true;
	}

	// Fake territory region: the start/colony bbox padded enough to reach a
	// starter field across its land gap (live path uses the colony bbox + a
	// smaller WHEAT_REGION_MARGIN instead).
	const int DEBUG_REGION_HALF = 18;
	int boxMinX = bbMinX - DEBUG_REGION_HALF;
	int boxMinY = bbMinY - DEBUG_REGION_HALF;
	int boxMaxX = bbMaxX + DEBUG_REGION_HALF;
	int boxMaxY = bbMaxY + DEBUG_REGION_HALF;
	if (boxMinX < 0) boxMinX = 0;
	if (boxMinY < 0) boxMinY = 0;
	if (boxMaxX > w - 1) boxMaxX = w - 1;
	if (boxMaxY > h - 1) boxMaxY = h - 1;

	std::cout << "Wheat-plan dump " << mapName << " : " << w << "x" << h
	          << ", team " << team << " start=(" << startX << "," << startY << ")"
	          << ", consumer seeds=" << seeds.size()
	          << ", region=(" << boxMinX << "," << boxMinY << ")-(" << boxMaxX << "," << boxMaxY << ")"
	          << " [fog bypassed]" << std::endl;
	std::cout << "  legend: ~=water #=blocked .=land c=food-source(unreached) o=open-margin"
	             " +=harvest-half X=forbidden S=seed " << team << "=start" << std::endl;

	for (int N = 0; N <= 2; N++)
	{
		FoodSourceScanResult r = scanFoodSourcesForbidden(
			map, teamMask, team, seeds,
			boxMinX, boxMinY, boxMaxX, boxMaxY,
			/*openMargin=*/N, /*ignoreFOW=*/true, /*wantDebug=*/true);

		std::cout << "=== team " << team << ", N=" << N << " ===  field=" << r.fieldTileCount
		          << " components=" << r.componentCount << " open=" << r.openCount
		          << " forbidden=" << r.forbiddenCount
		          << " add=" << r.addCount << " del=" << r.delCount << std::endl;

		for (int y = boxMinY; y <= boxMaxY; y++)
		{
			std::string row;
			for (int x = boxMinX; x <= boxMaxX; x++)
			{
				const int idx = static_cast<int>(map.coordToIndex(x, y));
				const Uint8 cls = r.classOf.empty() ? (Uint8)WC_NONE : r.classOf[idx];
				char c;
				if (x == startX && y == startY)            c = (char)('0' + team);
				else if (seedBit[idx])                     c = 'S';
				else if (cls == WC_OPEN_MARGIN)    c = 'o';
				else if (cls == WC_FORBIDDEN)      c = 'X';
				else if (cls == WC_CHECKER_OPEN)   c = '+';
				else if (map.materialAmountAt(map.coordToIndex(x, y), MaterialId::Food) > 0) c = 'c';
				else if (map.isWater(x, y))                c = '~';
				else if (!map.isFreeForGroundUnitNoForbidden(x, y, false)) c = '#';
				else                                       c = '.';
				row += c;
			}
			std::cout << row << std::endl;
		}
		std::cout << std::endl;
	}
	return 0;
}
}
