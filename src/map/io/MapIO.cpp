// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingGradientSearch.h"
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "FileFormatVersions.h"
#include "MapInternal.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"
#include "RessourceType.h"

#include "render/GameAnimations.h"

#include <algorithm>
#include <stdexcept>
#include <Stream.h>
#include <BinaryStream.h>
#include <PackedArray.h>
#include <limits>
#include <memory>
#include <stdexcept>


bool Map::load(GAGCore::InputStream *stream, MapHeader& header, Game *game)
{
    return loadTask(stream, header, game).run();
}

GAGCore::CooperativeTask Map::loadTask(GAGCore::InputStream *stream, MapHeader& header, Game *game)
try
{
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	assert(header.getVersionMinor()>=16);

	Sint32 versionMinor = header.getVersionMinor();
    const bool packed=versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE && GAGCore::PackedArray::binary(stream);

	clear();
    co_await GAGCore::CooperativeTask::checkpoint("[Loading terrain]");

	stream->readEnterSection("Map");

	char signature[4];
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature, "MapB", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the beginning of Map.\n");
		co_return false;
	}

	// We load and compute size:
	wDec = stream->readSint32("wDec");
	hDec = stream->readSint32("hDec");
	if (!supportedDimensions(wDec, hDec))
		co_return false;
	w = 1<<wDec;
	h = 1<<hDec;
	wMask = w-1;
	hMask = h-1;
	size = w*h;

	// We allocate memory:
	mapDiscovered.resize(size);
	fogOfWarA.assign(size, 0);
	fogOfWarB.assign(size, 0);
	fogOfWar = &fogOfWarA[0];
	displayedForbiddenView.resize(size, false);
	displayedGuardAreaView.resize(size, false);
	displayedClearAreaView.resize(size, false);
	tiles.resize(size);
	undermap = new Uint8[size];
	listedAddr = new Uint8*[size];
	aStarPoints=new AStarAlgorithmPoint[size];
	immobileUnits = new Uint8[size];
	memset(immobileUnits, 255, size*sizeof(Uint8));

	// We read what's inside the map:
	if (packed) GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){undermap[i]=v;});
    else stream->read(undermap, size, "undermap");
	for (size_t i = 0; i < size; ++i)
		if (undermap[i] > GRASS) co_return false;
	stream->readEnterSection("cases");
    if(packed)
    {
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){mapDiscovered[i]=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){tiles[i].terrain=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){tiles[i].building=v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){tiles[i].resource.type=v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){tiles[i].resource.variety=v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){tiles[i].resource.amount=v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){tiles[i].resource.animation=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){tiles[i].groundUnit=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){tiles[i].airUnit=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){tiles[i].forbidden=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){tiles[i].guardArea=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){tiles[i].clearArea=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){tiles[i].scriptAreas=v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){tiles[i].canResourcesGrow=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){tiles[i].fertility=v;});
    }
	for (size_t i=0; i<size; i++)
	{
        if (i % 512 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		stream->readEnterSection(i);
		if (!packed) mapDiscovered[i] = stream->readUint32("mapDiscovered");

		if (!packed) tiles[i].terrain = stream->readUint16("terrain");
		if (tiles[i].terrain >= 272) co_return false;
		if (!packed) tiles[i].building = stream->readUint16("building");
		if (tiles[i].building != NOGBID && tiles[i].building >= Building::MAX_COUNT * header.getNumberOfTeams())
			co_return false;

		if (!packed) stream->read(&(tiles[i].resource), 4, "ressource");
		if (tiles[i].resource.type != NO_RES_TYPE && tiles[i].resource.type >= MAX_RESOURCES)
			co_return false;
		if (tiles[i].resource.type != NO_RES_TYPE)
		{
			const auto* type = ResourcesTypes().get(tiles[i].resource.type);
			const auto& resource = tiles[i].resource;
			if (resource.variety >= type->varietiesCount || resource.amount > type->sizesCount || (!type->eternal && resource.amount == 0))
				throw std::runtime_error("Invalid saved resource sprite state: " + std::to_string(resource.type) + "/" + std::to_string(resource.variety) + "/" + std::to_string(resource.amount));
		}
		if (!packed) tiles[i].groundUnit = stream->readUint16("groundUnit");
		if (!packed) tiles[i].airUnit = stream->readUint16("airUnit");
		if ((tiles[i].groundUnit != NOGUID && tiles[i].groundUnit >= Unit::MAX_COUNT * header.getNumberOfTeams()) ||
			(tiles[i].airUnit != NOGUID && tiles[i].airUnit >= Unit::MAX_COUNT * header.getNumberOfTeams()))
			co_return false;
		if (!packed) tiles[i].forbidden = stream->readUint32("forbidden");
		if(!packed && versionMinor < 62)
			stream->readUint32("hiddenForbidden");
		if (!packed) tiles[i].guardArea = stream->readUint32("guardArea");
		if (!packed) tiles[i].clearArea = stream->readUint32("clearArea");
		if (!packed) tiles[i].scriptAreas = stream->readUint16("scriptAreas");
		if (!packed) tiles[i].canResourcesGrow = stream->readUint8("canRessourcesGrow");
		if(!packed && versionMinor >= 63)
			tiles[i].fertility = stream->readUint16("fertility");
		fertilityMaximum = std::max(fertilityMaximum, tiles[i].fertility);

		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	for(int n=0; n<9; ++n)
	{
		stream->readEnterSection(n);
		setAreaName(n, stream->readText("areaname"));
		stream->readLeaveSection();
	}

	const bool restoreExploredArea = header.getIsSavedGame() && versionMinor >= EXPLORED_AREA_SAVED_VERSION_MINOR;
	if (restoreExploredArea)
		loadExploredArea(stream, header.getNumberOfTeams(), game != NULL, versionMinor);

	this->game = game;

	// We load sectors:
	wSector = stream->readSint32("wSector");
	hSector = stream->readSint32("hSector");
	if (wSector != (w >> Sector::SECTOR_SHIFT) || hSector != (h >> Sector::SECTOR_SHIFT))
		co_return false;
	sizeSector = wSector*hSector;
	assert(sectors == NULL);
	sectors = new Sector[sizeSector];

	// Map::setGame is bypassed on the loaded-game path (Game::load uses
	// Map::load directly and the game pointer is set inline above), so
	// the per-sector render buckets must be sized here too.
	if (game)
		game->animations->resize(sizeSector);

	arraysBuilt = true;

	stream->readEnterSection("sectors");
	for (int i=0; i<sizeSector; i++)
	{
        if (i % 512 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		stream->readEnterSection(i);
		if (!sectors[i].load(stream, this->game, versionMinor))
		{
			stream->readLeaveSection(3);
			co_return false;
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();

	if (memcmp(signature, "MapE", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the end of Map.\n");
		co_return false;
	}

	if (game)
	{
                /* Must set game field before following action as they
                   may need it (in particular
                   makeDiscoveredAreasExplored uses it). */
		this->game=game;

		// This is a game; the gradients are built when a unit first asks for them.
		for (int t=0; t<header.getNumberOfTeams(); t++)
		{
			if (!restoreExploredArea)
			{
				assert(exploredArea[t] == NULL);
				exploredArea[t] = new Uint8[size];
				initExploredArea(t);
				makeDiscoveredAreasExplored(t);
			}
			assert(exploredArea[t]);

			clearingAreaClaims[t] = new Uint16[size];
			memset(clearingAreaClaims[t], NOGUID, size*sizeof(Uint16));
		}
	}

	co_return true;
}
catch (const std::ios_base::failure& error)
{
	std::cerr << "Map::load: " << error.what() << std::endl;
	clear();
	co_return false;
}


void Map::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Map");
	stream->write("MapB", 4, "signatureStart");
	
	// We save size:
	stream->writeSint32(wDec, "wDec");
	stream->writeSint32(hDec, "hDec");

	// We write what's inside the map:
	if(GAGCore::PackedArray::binary(stream)) GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return undermap[i];});
    else stream->write(undermap, size, "undermap");
	stream->writeEnterSection("cases");
    if(GAGCore::PackedArray::binary(stream))
    {
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return mapDiscovered[i];});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return tiles[i].terrain;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return tiles[i].building;});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return tiles[i].resource.type;});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return tiles[i].resource.variety;});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return tiles[i].resource.amount;});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return tiles[i].resource.animation;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return tiles[i].groundUnit;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return tiles[i].airUnit;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return tiles[i].forbidden;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return tiles[i].guardArea;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return tiles[i].clearArea;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return tiles[i].scriptAreas;});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return tiles[i].canResourcesGrow;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return tiles[i].fertility;});
    }
    else for (size_t i=0; i<size ;i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(mapDiscovered[i], "mapDiscovered");

		stream->writeUint16(tiles[i].terrain, "terrain");
		stream->writeUint16(tiles[i].building, "building");
		
		stream->write(&(tiles[i].resource), 4, "ressource");
		
		stream->writeUint16(tiles[i].groundUnit, "groundUnit");
		stream->writeUint16(tiles[i].airUnit, "airUnit");
		stream->writeUint32(tiles[i].forbidden, "forbidden");
		stream->writeUint32(tiles[i].guardArea, "guardArea");
		stream->writeUint32(tiles[i].clearArea, "clearArea");
		stream->writeUint16(tiles[i].scriptAreas, "scriptAreas");
		stream->writeUint8(tiles[i].canResourcesGrow, "canRessourcesGrow");
		stream->writeUint16(tiles[i].fertility, "fertility");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	//Save area names
	for(int n=0; n<9; ++n)
	{
		stream->writeEnterSection(n);
		stream->writeText(getAreaName(n), "areaname");
		stream->writeLeaveSection();
	}

	assert(game);
	if (game->mapHeader.getIsSavedGame())
		saveExploredArea(stream, game->mapHeader.getNumberOfTeams());

	// We save sectors:
	stream->writeSint32(wSector, "wSector");
	stream->writeSint32(hSector, "hSector");
	stream->writeEnterSection("sectors");
	for (int i=0; i<sizeSector; i++)
	{
		stream->writeEnterSection(i);
		sectors[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->write("MapE", 4, "signatureEnd");
	stream->writeLeaveSection();
}


void Map::addTeam(void)
{
    addTeamTask().run();
}

GAGCore::CooperativeTask Map::addTeamTask(void)
{
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	int oldNumberOfTeam=numberOfTeam-1;
	assert(numberOfTeam>0);
	
	int t=oldNumberOfTeam;
	for (int r=0; r<MAX_RESOURCES; r++)
		for (int s=0; s<SWIM_CLASS_COUNT; s++)
			assert(resourcesGradient[t][r][s]==NULL);
	
	assert(exploredArea[t] == NULL);
	exploredArea[t] = new Uint8[size];
	initExploredArea(t);
	
	assert(clearingAreaClaims[t] == NULL);
	clearingAreaClaims[t] = new Uint16[size];
	memset(clearingAreaClaims[t], NOGUID, size*sizeof(Uint16));
    co_return true;
}

void Map::removeTeam(void)
{
	gradientRuntime->pipeline.reset();
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	assert(numberOfTeam<Team::MAX_COUNT);
	
	int t=numberOfTeam;
	for (int s=0; s<SWIM_CLASS_COUNT; s++)
	{
		for (int r=0; r<MAX_RESOURCES; r++)
		{
			delete[] resourcesGradient[t][r][s];
			resourcesGradient[t][r][s]=NULL;
		}
		delete[] forbiddenGradient[t][s];
		forbiddenGradient[t][s]=NULL;
		delete[] guardAreasGradient[t][s];
		guardAreasGradient[t][s]=NULL;
		delete[] clearAreasGradient[t][s];
		clearAreasGradient[t][s]=NULL;
	}

	
	assert(exploredArea[t] != NULL);
	delete[] exploredArea[t];
	exploredArea[t]=NULL;
	
	assert(clearingAreaClaims[t] != NULL);
	delete[] clearingAreaClaims[t];
	clearingAreaClaims[t]=NULL;
}

// TODO: completely recreate:



namespace
{
bool loadFlag(GAGCore::InputStream *stream, const char *name)
{
	const auto value=stream->readUint8(name);
	if (value>1) throw std::runtime_error("Invalid saved routing flag");
	return value != 0;
}
void saveGradient(GAGCore::OutputStream *stream, const Uint16 *field, size_t size)
{
    stream->writeUint8(field != nullptr, "present");
    if(field) {
        if(GAGCore::PackedArray::binary(stream)) GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return field[i];});
        else stream->writeUint16Sections(field,size,"value");
    }
}
void loadGradient(GAGCore::InputStream *stream, Uint16 *&field, size_t size, bool packed)
{
	const bool present=loadFlag(stream,"present");
	std::unique_ptr<Uint16[]> restored;
	if (present)
	{
		restored=std::make_unique<Uint16[]>(size);
        if(packed) GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){restored[i]=v;});
        else for (size_t i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			restored[i]=stream->readUint16("value");
			stream->readLeaveSection();
		}
	}
	delete[] field;
	field=restored.release();
}
}

// Cached routing fields deliberately lag map edits. Recomputing them on load
// changes decisions before their scheduled refresh, even with an identical RNG.
void Map::saveRuntimeState(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("mapRuntime");
	stream->writeUint8(fogOfWar == fogOfWarA.data(), "fogIsA");
	stream->writeUint32(topologyGeneration, "topologyGeneration");
	stream->writeEnterSection("cells");
    if(GAGCore::PackedArray::binary(stream))
    {
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return immobileUnits[i];});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return fogOfWarA[i];});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return fogOfWarB[i];});
    }
    else for (size_t i=0; i<size; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint8(immobileUnits[i], "immobileUnit");
		stream->writeUint32(fogOfWarA[i], "fogA");
		stream->writeUint32(fogOfWarB[i], "fogB");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("teams");
	for (int t=0; t<game->teamsCount(); ++t)
	{
		stream->writeEnterSection(t);
		stream->writeEnterSection("claims");
        if(GAGCore::PackedArray::binary(stream)) GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return clearingAreaClaims[t][i];});
        else for (size_t i=0; i<size; ++i)
		{
			stream->writeEnterSection(i);
			stream->writeUint16(clearingAreaClaims[t][i], "claim");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
		stream->writeEnterSection("swimClasses");
		for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
		{
			stream->writeEnterSection(sw);
			stream->writeEnterSection("resources");
			for (int r=0; r<MAX_NB_RESOURCES; ++r)
			{
				stream->writeEnterSection(r);
				saveGradient(stream, resourcesGradient[t][r][sw], size);
				stream->writeUint8(gradientUpdated[t][r][sw], "updated");
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
			stream->writeEnterSection("forbidden");
			saveGradient(stream, forbiddenGradient[t][sw], size);
			stream->writeLeaveSection();
			stream->writeEnterSection("guard");
			saveGradient(stream, guardAreasGradient[t][sw], size);
			stream->writeUint8(guardGradientUpdated[t][sw], "updated");
			stream->writeLeaveSection();
			stream->writeEnterSection("clear");
			saveGradient(stream, clearAreasGradient[t][sw], size);
			stream->writeUint8(clearGradientUpdated[t][sw], "updated");
			stream->writeLeaveSection();
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
		stream->writeEnterSection("buildings");
		for (int b=0; b<Building::MAX_COUNT; ++b) if (auto *building=game->teams[t]->myBuildings[b])
		{
			stream->writeEnterSection(b);
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				stream->writeEnterSection(sw);
				// Materialize the old full-field representation from its frozen inputs.
				// This changes no routing answers, timestamps, RNG or save bytes.
				finishBuildingGradient(building, sw);
				saveGradient(stream, building->globalGradient[sw], size);
				stream->writeUint8(building->dirtyGradient[sw], "dirty");
				stream->writeUint32(building->lastGlobalGradientUpdateStepCounter[sw], "lastUpdate");
				stream->writeUint32(building->gradientGeneration[sw], "generation");
				stream->writeLeaveSection();
			}
			stream->writeEnterSection("roundTrip");
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				stream->writeEnterSection(sw);
				stream->writeUint32(building->globalGradientUsedStep[sw], "usedStep");
				for (int r=0; r<MAX_NB_RESOURCES; ++r)
				{
					stream->writeEnterSection(r);
					saveGradient(stream, building->roundTripGradient[r][sw], size);
					stream->writeUint32(building->roundTripGradientStep[r][sw], "step");
					stream->writeUint32(building->roundTripGradientUsedStep[r][sw], "usedStep");
					stream->writeLeaveSection();
				}
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
			stream->writeEnterSection("access");
			for (int sw=0; sw<SWIM_VARIANT_COUNT; ++sw)
			{
				stream->writeEnterSection(sw);
				stream->writeUint8(building->locked[sw], "locked");
				stream->writeUint8(building->anyResourceToClear[sw], "resourceState");
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("gradientPipeline");
	stream->writeUint8(gradientRuntime->pipeline.enabled() ? gradientRuntime->pipeline.delayTicks() : 8, "delay");
	stream->writeUint8(gradientRuntime->pipeline.pendingCount(), "count");
	unsigned index=0;
	gradientRuntime->pipeline.visitPendingSnapshots([&](const GradientPipeline::PendingSnapshot &snapshot) {
		int destination=-1;
		for (int t=0; t<game->teamsCount(); ++t)
			for (int kind=0; kind<MAX_NB_RESOURCES+2; ++kind)
				for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw) {
					auto *slot=kind<MAX_NB_RESOURCES ? &resourcesGradient[t][kind][sw]
						: kind==MAX_NB_RESOURCES ? &guardAreasGradient[t][sw] : &clearAreasGradient[t][sw];
					if (slot==snapshot.slot) destination=(t*(MAX_NB_RESOURCES+2)+kind)*SWIM_CLASS_COUNT+sw;
				}
		if (destination<0) throw std::runtime_error("Unknown pending gradient destination");
		stream->writeEnterSection(index++);
		stream->writeUint16(destination, "destination");
		stream->writeUint8(snapshot.remaining, "remaining");
		stream->writeUint8(snapshot.superseded, "superseded");
		saveGradient(stream, snapshot.data, size);
		stream->writeLeaveSection();
	});
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}

void Map::loadRuntimeState(GAGCore::InputStream *stream, Sint32 versionMinor)
{
    const bool packed=versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE && GAGCore::PackedArray::binary(stream);
	gradientRuntime->pipeline.reset();
	stream->readEnterSection("mapRuntime");
	const bool fogIsA=loadFlag(stream,"fogIsA");
	if (versionMinor>=FILE_FORMAT_VERSION_TOPOLOGY_GENERATION)
		topologyGeneration=stream->readUint32("topologyGeneration");
	fogOfWar=fogIsA ? fogOfWarA.data() : fogOfWarB.data();
	stream->readEnterSection("cells");
    if(packed)
    {
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){immobileUnits[i]=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){fogOfWarA[i]=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){fogOfWarB[i]=v;});
    }
    else for (size_t i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		immobileUnits[i]=stream->readUint8("immobileUnit");
		fogOfWarA[i]=stream->readUint32("fogA");
		fogOfWarB[i]=stream->readUint32("fogB");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readEnterSection("teams");
	for (int t=0; t<game->teamsCount(); ++t)
	{
		stream->readEnterSection(t);
		stream->readEnterSection("claims");
        if(packed) GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){clearingAreaClaims[t][i]=v;});
        else for (size_t i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			clearingAreaClaims[t][i]=stream->readUint16("claim");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readEnterSection("swimClasses");
		for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
		{
			stream->readEnterSection(sw);
			stream->readEnterSection("resources");
			for (int r=0; r<MAX_NB_RESOURCES; ++r)
			{
				stream->readEnterSection(r);
				loadGradient(stream, resourcesGradient[t][r][sw], size, packed);
				gradientUpdated[t][r][sw]=loadFlag(stream,"updated");
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
			stream->readEnterSection("forbidden");
			loadGradient(stream, forbiddenGradient[t][sw], size, packed);
			stream->readLeaveSection();
			stream->readEnterSection("guard");
			loadGradient(stream, guardAreasGradient[t][sw], size, packed);
			guardGradientUpdated[t][sw]=loadFlag(stream,"updated");
			stream->readLeaveSection();
			stream->readEnterSection("clear");
			loadGradient(stream, clearAreasGradient[t][sw], size, packed);
			clearGradientUpdated[t][sw]=loadFlag(stream,"updated");
			stream->readLeaveSection();
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readEnterSection("buildings");
		for (int b=0; b<Building::MAX_COUNT; ++b) if (auto *building=game->teams[t]->myBuildings[b])
		{
			stream->readEnterSection(b);
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				stream->readEnterSection(sw);
				// Existing saves contain complete fields; discard any previous queue
				// before replacing its buffer, including when reusing a loaded object.
				building->globalGradientSearch[sw].reset();
				loadGradient(stream, building->globalGradient[sw], size, packed);
				building->dirtyGradient[sw]=loadFlag(stream,"dirty");
				building->lastGlobalGradientUpdateStepCounter[sw]=stream->readUint32("lastUpdate");
				// An older save restored its fields as current; keep them so.
				building->gradientGeneration[sw]=versionMinor>=FILE_FORMAT_VERSION_TOPOLOGY_GENERATION
					? stream->readUint32("generation") : topologyGeneration;
				stream->readLeaveSection();
			}
			if (versionMinor >= FILE_FORMAT_VERSION_ROUND_TRIP_FIELDS)
			{
				stream->readEnterSection("roundTrip");
				for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
				{
					stream->readEnterSection(sw);
					building->globalGradientUsedStep[sw]=stream->readUint32("usedStep");
					for (int r=0; r<MAX_NB_RESOURCES; ++r)
					{
						stream->readEnterSection(r);
						loadGradient(stream, building->roundTripGradient[r][sw], size, packed);
						building->roundTripGradientStep[r][sw]=stream->readUint32("step");
						building->roundTripGradientUsedStep[r][sw]=stream->readUint32("usedStep");
						stream->readLeaveSection();
					}
					stream->readLeaveSection();
				}
				stream->readLeaveSection();
			}
			stream->readEnterSection("access");
			for (int sw=0; sw<SWIM_VARIANT_COUNT; ++sw)
			{
				stream->readEnterSection(sw);
				building->locked[sw]=loadFlag(stream,"locked");
				building->anyResourceToClear[sw]=stream->readUint8("resourceState");
				if (building->anyResourceToClear[sw]>2) throw std::runtime_error("Invalid saved resource state");
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if (versionMinor>=FILE_FORMAT_VERSION_GRADIENT_PIPELINE) {
		stream->readEnterSection("gradientPipeline");
		const unsigned delay=stream->readUint8("delay"), count=stream->readUint8("count");
		if (delay<1 || delay>16 || count>delay) throw std::runtime_error("Invalid saved gradient queue size");
		configureGradientPipeline(1, delay);
		for (unsigned index=0; index<count; ++index) {
			stream->readEnterSection(index);
			const unsigned destination=stream->readUint16("destination");
			const unsigned sw=destination%SWIM_CLASS_COUNT;
			const unsigned kind=(destination/SWIM_CLASS_COUNT)%(MAX_NB_RESOURCES+2);
			const unsigned team=destination/(SWIM_CLASS_COUNT*(MAX_NB_RESOURCES+2));
			if (team>=static_cast<unsigned>(game->teamsCount())) throw std::runtime_error("Invalid saved gradient team");
			auto *slot=kind<MAX_NB_RESOURCES ? &resourcesGradient[team][kind][sw]
				: kind==MAX_NB_RESOURCES ? &guardAreasGradient[team][sw] : &clearAreasGradient[team][sw];
			const unsigned remaining=stream->readUint8("remaining");
			const bool superseded=loadFlag(stream,"superseded");
			Uint16 *field=nullptr;
			loadGradient(stream, field, size, packed);
			gradientRuntime->pipeline.restoreCompleted({slot, static_cast<int>(sw), remaining,
				superseded, std::unique_ptr<Uint16[]>(field)});
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
}
