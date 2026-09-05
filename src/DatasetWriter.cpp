#include "DatasetWriter.h"

#include <algorithm>

#include "Building.h"
#include "FileManager.h"
#include "Game.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "Order.h"
#include "Player.h"
#include "Ressource.h"
#include "Team.h"
#include "TeamStat.h"
#include "Toolkit.h"
#include "Unit.h"
#include "UnitConsts.h"

DatasetWriter::DatasetWriter()
	: file(NULL), numRecords(0)
{
}

DatasetWriter::~DatasetWriter()
{
	close();
}

void DatasetWriter::writeU32(Uint32 v)
{
	fwrite(&v, 4, 1, file);
}

void DatasetWriter::writeI32(Sint32 v)
{
	fwrite(&v, 4, 1, file);
}

bool DatasetWriter::open(const std::string& path)
{
	// Match ReplayWriter's absolute-path bypass: FileManager::openFP
	// prepends every dirList entry, which turns absolute paths into
	// nonsense (~/.glob2//tmp/foo). The trainer pipeline relies on
	// arbitrary absolute paths working as written.
	if (!path.empty() && path[0] == '/')
		file = fopen(path.c_str(), "wb");
	else
		file = GAGCore::Toolkit::getFileManager()->openFP(path, "wb");

	if (!file)
		return false;

	numRecords = 0;

	// Header: magic + num_records placeholder. No version field — there's
	// only one producer and one consumer (in this repo) and regenerating
	// datasets is cheap, so we'd never need to support multiple versions
	// in flight. If the format ever changes wire-incompatibly, bump the
	// magic to "GDS2" and parsers reject by magic mismatch.
	fwrite("GDS1", 4, 1, file);
	writeU32(0); // num_records placeholder, patched in close()

	return true;
}

void DatasetWriter::writeRecord(Uint32 tick, Order& order, Game& game)
{
	if (!file)
		return;

	// Skip non-action orders, matching the criteria ReplayWriter::pushOrder
	// uses to filter what reaches the replay stream. ORDER_NULL fires every
	// tick for every player ("did nothing") — without this skip the dataset
	// is dominated by non-actions and num_records balloons (4 players ×
	// 30k ticks ≈ 120k records, of which only a few thousand are real
	// AI decisions).
	Uint8 type = order.getOrderType();
	if (type == ORDER_NULL || type == ORDER_VOICE_DATA)
		return;

	writeU32(tick);
	Uint8 sender = (Uint8)order.sender;
	fwrite(&sender, 1, 1, file);
	fwrite(&type, 1, 1, file);

	// State blob (observation features). Length-prefixed so the parser
	// can skip past it without knowing the schema. Rather than computing
	// the size up front, write a placeholder, dump the blob, then patch
	// the length back in.
	long stateLenOffset = ftell(file);
	writeU32(0);
	long stateStart = ftell(file);

	int senderTeamNum = game.players[order.sender]->team->teamNumber;
	writeStateBlob(senderTeamNum, game);

	long stateEnd = ftell(file);
	Uint32 stateLen = (Uint32)(stateEnd - stateStart);
	fseek(file, stateLenOffset, SEEK_SET);
	writeU32(stateLen);
	fseek(file, stateEnd, SEEK_SET);

	// Order payload, exactly as Order::getData() returns it.
	int payloadLen = order.getDataLength();
	writeU32((Uint32)payloadLen);
	if (payloadLen > 0)
		fwrite(order.getData(), 1, payloadLen, file);

	numRecords++;
}

void DatasetWriter::writeStateBlob(int senderTeamNum, Game& game)
{
	Team* senderTeam = game.teams[senderTeamNum];
	const TeamStat* stat = const_cast<TeamStats&>(senderTeam->stats).getLatestStat();

	// num_teams = 1 — bot-team-only by design (kyle approved). Enemy
	// internal state would leak omniscient info; the spatial grid encodes
	// visible enemy presence already. The redundant length prefix keeps
	// the layout extensible if we ever revisit.
	writeU32(1);

	// Bot-team scalars.
	writeI32(senderTeam->prestige);

	Uint32 flags = 0;
	if (senderTeam->isAlive) flags |= 1u << 0;
	if (senderTeam->hasWon)  flags |= 1u << 1;
	if (senderTeam->hasLost) flags |= 1u << 2;
	writeU32(flags);

	for (int i = 0; i < MAX_NB_RESSOURCES; i++)
		writeI32(senderTeam->teamRessources[i]);

	for (int i = 0; i < NB_UNIT_TYPE; i++)
		writeI32(stat->numberUnitPerType[i]);

	for (int i = 0; i < IntBuildingType::NB_BUILDING; i++)
		writeI32(stat->numberBuildingPerType[i]);

	// Spatial grid. Downsample from the actual map to a fixed-max
	// GRID_W × GRID_H. For maps smaller than GRID_W/GRID_H we shrink the
	// grid to match (avoids padding empty cells).
	const Map& map = game.map;
	int mapW = map.getW();
	int mapH = map.getH();
	int gridW = std::min(mapW, (int)GRID_W);
	int gridH = std::min(mapH, (int)GRID_H);
	int stepX = std::max(1, mapW / gridW);
	int stepY = std::max(1, mapH / gridH);

	writeU32((Uint32)gridW);
	writeU32((Uint32)gridH);

	// Vision mask: cells we directly see plus everything our allies share
	// with us via the three sharedVision channels (matches AI omniscience
	// boundary; rule-based AIs reason from the same masked view).
	Uint32 visionMask = senderTeam->me
		| senderTeam->sharedVisionExchange
		| senderTeam->sharedVisionFood
		| senderTeam->sharedVisionOther;

	for (int gy = 0; gy < gridH; gy++)
	{
		int y0 = gy * stepY;
		for (int gx = 0; gx < gridW; gx++)
		{
			int x0 = gx * stepX;

			// Terrain: take the top-left source cell (categorical; we'd
			// need a histogram to do better and the model can learn around
			// downsample artifacts).
			int tt = map.getTerrainType(x0, y0);
			Uint8 terrain = (tt < 0) ? 255 : (Uint8)tt;

			Uint32 resourceSum = 0;
			Uint32 myUnitCount = 0;
			Uint32 enemyUnitCount = 0;
			Uint8 myBuilding = 0;
			Uint8 enemyBuilding = 0;
			bool anyCurrentlyVisible = false;
			bool anyEverSeen = false;

			for (int dy = 0; dy < stepY; dy++)
			{
				int sy = y0 + dy;
				for (int dx = 0; dx < stepX; dx++)
				{
					int sx = x0 + dx;
					bool currentlyVisible = map.isFOWDiscovered(sx, sy, visionMask);
					bool everSeen = map.isMapDiscovered(sx, sy, visionMask);
					if (currentlyVisible) anyCurrentlyVisible = true;
					if (everSeen) anyEverSeen = true;

					if (currentlyVisible)
					{
						const Ressource& r = map.getRessource(sx, sy);
						if (r.type != NO_RES_TYPE)
							resourceSum += r.amount;
					}

					// Ground + air units in the same channel — separation
					// would burn channels for marginal signal (air is rare).
					Uint16 guid = map.getGroundUnit(sx, sy);
					Uint16 auid = map.getAirUnit(sx, sy);
					for (int pass = 0; pass < 2; pass++)
					{
						Uint16 gid = (pass == 0) ? guid : auid;
						if (gid == NOGUID) continue;
						int unitTeam = Unit::GIDtoTeam(gid);
						if (unitTeam == senderTeamNum)
							myUnitCount++;
						else if (currentlyVisible)
							enemyUnitCount++;
					}

					Uint16 bgid = map.getBuilding(sx, sy);
					if (bgid != NOGBID)
					{
						int bTeam = Building::GIDtoTeam(bgid);
						int bId = Building::GIDtoID(bgid);
						Building* b = NULL;
						if (bTeam >= 0 && bTeam < game.mapHeader.getNumberOfTeams() && bId >= 0 && bId < Building::MAX_COUNT)
							b = game.teams[bTeam]->myBuildings[bId];
						if (b)
						{
							// shortTypeNum is 0..NB_BUILDING-1; shift by 1 so
							// 0 == "no building" stays unambiguous.
							Uint8 typeId = (Uint8)(b->shortTypeNum + 1);
							if (bTeam == senderTeamNum)
							{
								// First-write-wins on collision — multiple
								// of my buildings inside one downsampled
								// cell is rare and any type is fine.
								if (myBuilding == 0) myBuilding = typeId;
							}
							else if (currentlyVisible)
							{
								if (enemyBuilding == 0) enemyBuilding = typeId;
							}
						}
					}
				}
			}

			Uint8 chTerrain = terrain;
			Uint8 chResource = (Uint8)std::min(resourceSum, (Uint32)255);
			Uint8 chMyUnits = (Uint8)std::min(myUnitCount, (Uint32)255);
			Uint8 chEnemyUnits = (Uint8)std::min(enemyUnitCount, (Uint32)255);
			Uint8 chMyBuilding = myBuilding;
			Uint8 chEnemyBuilding = enemyBuilding;
			Uint8 chDiscovery = anyCurrentlyVisible ? 2 : (anyEverSeen ? 1 : 0);

			fwrite(&chTerrain, 1, 1, file);
			fwrite(&chResource, 1, 1, file);
			fwrite(&chMyUnits, 1, 1, file);
			fwrite(&chEnemyUnits, 1, 1, file);
			fwrite(&chMyBuilding, 1, 1, file);
			fwrite(&chEnemyBuilding, 1, 1, file);
			fwrite(&chDiscovery, 1, 1, file);
		}
	}
}

void DatasetWriter::close()
{
	if (!file)
		return;

	// Patch num_records at offset 4 (right after the magic).
	fseek(file, 4, SEEK_SET);
	writeU32(numRecords);

	fclose(file);
	file = NULL;
}
