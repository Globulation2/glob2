// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingGradientSearch.h"
#include "Map.h"
#include "TerrainCompatibility.h"
#include "gradient/GradientRuntime.h"
#include "FileFormatVersions.h"
#include "Version.h"
#include "MapInternal.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"

#include "render/GameAnimations.h"

#include <algorithm>
#include <bit>
#include <Stream.h>
#include <BinaryStream.h>
#include <PackedArray.h>
#include <limits>
#include <memory>
#include <stdexcept>


namespace
{
// Format 136 stores bounded raw chunks, avoiding the binary stream string limit
// and text-stream quoting differences. Keep these wire sizes stable.
constexpr std::size_t RegistryChunkBytes = 64 * 1024;
constexpr std::size_t MaximumRegistryChunks =
	TerrainRegistry::MaximumDefinitionBytes / RegistryChunkBytes;
}

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
	terrainRegistryValue = TerrainRegistry::builtins();
    resourceRegistryValue = versionMinor >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES
        ? ResourceRegistry::empty() : ResourceRegistry::legacy();
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

	// Files older than the catalogue were written with seven built-ins: their custom
	// definitions and tile IDs start at 7 and move behind the current built-ins.
	const unsigned savedBuiltins = TerrainRegistry::savedBuiltinCount(versionMinor);
	// Returns a value the registry rejects when the renumbered ID would not fit.
	auto remapTerrainId = [savedBuiltins](Uint16 v) -> unsigned
	{ return v < savedBuiltins ? v : unsigned(v) - savedBuiltins + TERRAIN_COUNT; };
	if (versionMinor >= FILE_FORMAT_VERSION_RUNTIME_TERRAIN)
	{
		stream->readEnterSection("terrainRegistry");
		const auto chunks = stream->readUint32("chunks");
		if (!chunks || chunks > MaximumRegistryChunks)
			throw std::ios_base::failure("Invalid terrain registry size");
		std::string definitions;
		for (unsigned i = 0; i < chunks; ++i)
		{
			stream->readEnterSection(i);
			const auto length = stream->readUint32("length");
			if (length > RegistryChunkBytes)
				throw std::ios_base::failure("Invalid terrain registry chunk");
			const auto offset = definitions.size();
			definitions.resize(offset + length);
			stream->read(definitions.data() + offset, length, "definitions");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		try
		{
			terrainRegistryValue = TerrainRegistry::deserialize(definitions, savedBuiltins);
		}
		catch (const std::exception &error)
		{
			throw std::ios_base::failure(std::string("Invalid terrain registry: ") + error.what());
		}
	}
	terrainSeedValue = versionMinor >= FILE_FORMAT_VERSION_TERRAIN_SEED
						   ? stream->readUint32("terrainSeed")
						   : 0;
    if (versionMinor >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
    {
        stream->readEnterSection("resourceRegistry");
        const auto chunks=stream->readUint32("chunks");
        if (!chunks || chunks>ResourceRegistry::MaximumDefinitionBytes/RegistryChunkBytes) throw std::ios_base::failure("Invalid resource registry size");
        std::string definitions;
        for (unsigned i=0;i<chunks;++i)
        {
            stream->readEnterSection(i);
            const auto length=stream->readUint32("length");
            if (length>RegistryChunkBytes) throw std::ios_base::failure("Invalid resource registry chunk");
            const auto offset=definitions.size(); definitions.resize(offset+length);
            stream->read(definitions.data()+offset,length,"definitions");
            stream->readLeaveSection();
        }
        stream->readLeaveSection();
        try { resourceRegistryValue=ResourceRegistry::deserialize(definitions); }
        catch (const std::exception& error)
        {
            throw std::ios_base::failure(std::string("Invalid resource registry: ")+error.what());
        }
    }
	terrainCounts.assign(terrainRegistry().size(), 0);

	// We allocate memory:
	mapDiscovered.resize(size);
	fogOfWarA.assign(size, 0);
	fogOfWarB.assign(size, 0);
	fogOfWar = &fogOfWarA[0];
	displayedForbiddenView.resize(size, false);
	displayedGuardAreaView.resize(size, false);
	displayedClearAreaView.resize(size, false);
	displayedFarmAreaView.resize(size, false);
	resourceCells.resize(size);
	occupancyCells.resize(size);
	areaCells.resize(size);
	legacyTerrain.resize(size);
	scriptAreaCells.resize(size);
	for (auto &cell : resourceCells) cell.mayGrow = 1;
	for (auto &cell : occupancyCells) cell.immobileUnit = 255;
	terrainIds.assign(size, GRASS);
	undermap = new Uint8[size];
	listedAddr = new Uint8*[size];
	aStarPoints=new AStarAlgorithmPoint[size];


	// We read what's inside the map:
	if (packed) GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){undermap[i]=v;});
    else stream->read(undermap, size, "undermap");
	for (size_t i = 0; i < size; ++i)
		if (undermap[i] > GRASS) co_return false;
	stream->readEnterSection("cases");
    if(packed)
    {
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){mapDiscovered[i]=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){legacyTerrain[i]=v;});
        if (versionMinor >= FILE_FORMAT_VERSION_TERRAIN_PROPERTIES)
            GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){ const auto id=remapTerrainId(v); if (!validTerrainType(id)) throw std::ios_base::failure("Unknown terrain identity"); terrainIds[i]=static_cast<TerrainType>(id); });
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){occupancyCells[i].building=v;});
        if (versionMinor>=FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
            GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){resourceCells[i].resource.type=v;});
        else GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){resourceCells[i].resource.type=v==255 ? NO_RES_TYPE : v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){resourceCells[i].resource.variety=v;});
        if (versionMinor>=FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
            GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){resourceCells[i].resource.amount=v;});
        else GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){resourceCells[i].resource.amount=v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){resourceCells[i].resource.animation=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){occupancyCells[i].groundUnit=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){occupancyCells[i].airUnit=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){areaCells[i].forbidden=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){areaCells[i].guard=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){areaCells[i].clear=v;});
        if (versionMinor >= FILE_FORMAT_VERSION_FARM_AREA)
            GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){areaCells[i].farm=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){scriptAreaCells[i]=v;});
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){resourceCells[i].mayGrow=v;});
        GAGCore::PackedArray::read<Uint16>(stream,size,[&](size_t i,Uint16 v){resourceCells[i].fertility=v;});
    }
	for (size_t i=0; i<size; i++)
	{
        if (i % 512 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		stream->readEnterSection(i);
		if (!packed) mapDiscovered[i] = stream->readUint32("mapDiscovered");

		if (!packed) legacyTerrain[i] = stream->readUint16("terrain");
		if (versionMinor < FILE_FORMAT_VERSION_TERRAIN_PROPERTIES)
		{
			if (legacyTerrain[i] >= 272) co_return false;
			terrainIds[i] = legacyTerrainType(legacyTerrain[i]);
		}
		else
		{
			if (!packed)
			{
				const auto id = remapTerrainId(stream->readUint16("terrainType"));
				if (!validTerrainType(id)) co_return false;
				terrainIds[i] = static_cast<TerrainType>(id);
			}
			const auto& visual = terrainRegistry().compatibility(terrainIds[i]);
			if (legacyTerrain[i] < visual.firstFrame || legacyTerrain[i] >= visual.firstFrame + visual.variants) co_return false;
		}
		if (!packed) occupancyCells[i].building = stream->readUint16("building");
		if (occupancyCells[i].building != NOGBID && occupancyCells[i].building >= Building::MAX_COUNT * header.getNumberOfTeams())
			co_return false;

        if (!packed)
        {
            auto& r=resourceCells[i].resource;
            if (versionMinor>=FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
            {
                r.type=stream->readUint16("resourceType");
                r.variety=stream->readUint8("resourceVariety");
                r.amount=stream->readUint32("resourceStock");
                r.animation=stream->readUint8("resourceAnimation");
            }
            else
            {
                Uint8 legacy[4]; stream->read(legacy,4,"ressource");
                r.type=legacy[0]==255 ? NO_RES_TYPE : legacy[0];
                r.variety=legacy[1]; r.amount=legacy[2]; r.animation=legacy[3];
            }
        }
        if (resourceCells[i].resource.type==NO_RES_TYPE)
        {
            auto& r=resourceCells[i].resource;
            // Historical clearing only changed the type byte. Unused stock and
            // animation bytes in supported saves are not a surviving deposit.
            if (versionMinor<FILE_FORMAT_VERSION_RUNTIME_RESOURCES) r.clear();
            else if (r.amount || r.variety || r.animation) throw std::ios_base::failure("Invalid empty resource state");
        }
        else
        {
            const auto& r=resourceCells[i].resource;
            if (!resourceRegistry().valid(r.type)) throw std::ios_base::failure("Unknown saved resource");
            Uint32 maximum=0;
            for (const auto& y:resourceRegistry().yields(static_cast<ResourceId>(r.type))) maximum+=y.capacity;
            if (r.amount>maximum || (!r.amount && !resourcePropertiesByIndex(r.type).persistsWhenEmpty)) throw std::ios_base::failure("Invalid saved resource stock");
        }
		if (!packed) occupancyCells[i].groundUnit = stream->readUint16("groundUnit");
		if (!packed) occupancyCells[i].airUnit = stream->readUint16("airUnit");
		if ((occupancyCells[i].groundUnit != NOGUID && occupancyCells[i].groundUnit >= Unit::MAX_COUNT * header.getNumberOfTeams()) ||
			(occupancyCells[i].airUnit != NOGUID && occupancyCells[i].airUnit >= Unit::MAX_COUNT * header.getNumberOfTeams()))
			co_return false;
		if (!packed) areaCells[i].forbidden = stream->readUint32("forbidden");
		if(!packed && versionMinor < 62)
			stream->readUint32("hiddenForbidden");
		if (!packed) areaCells[i].guard = stream->readUint32("guardArea");
		if (!packed) areaCells[i].clear = stream->readUint32("clearArea");
		if (!packed && versionMinor >= FILE_FORMAT_VERSION_FARM_AREA)
			areaCells[i].farm = stream->readUint32("farmArea");
		if (!packed) scriptAreaCells[i] = stream->readUint16("scriptAreas");
		if (!packed) resourceCells[i].mayGrow = stream->readUint8("canRessourcesGrow");
		if(!packed && versionMinor >= 63)
			resourceCells[i].fertility = stream->readUint16("fertility");
		fertilityMaximum = std::max(fertilityMaximum, resourceCells[i].fertility);

		stream->readLeaveSection();
	}
	stream->readLeaveSection();

    std::vector<Uint32> savedMultiTotals;
    if (versionMinor>=FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
        for (const auto& cell:resourceCells)
            if (cell.resource.type!=NO_RES_TYPE && !std::has_single_bit(resourcePropertiesByIndex(cell.resource.type).materialMask)) savedMultiTotals.push_back(cell.resource.amount);
    rebuildResourceHabitats();
    rebuildResourceState();
    if (versionMinor>=FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
    {
        stream->readEnterSection("resourceStocks");
        const auto count=stream->readUint32("count");
        if (count>size) throw std::ios_base::failure("Too many multi-material deposits");
        std::vector<bool> seen(size,false);
        size_t expected=0; for (auto slot:resourceStockIndices) if (slot) ++expected;
        if (count!=expected) throw std::ios_base::failure("Missing multi-material stocks");
        for (unsigned n=0;n<count;++n)
        {
            stream->readEnterSection(n);
            const auto i=stream->readUint32("tile");
            if (i>=size || seen[i] || resourceStockIndices.empty() || !resourceStockIndices[i]) throw std::ios_base::failure("Invalid material stock tile");
            seen[i]=true;
            const auto before=resourceMaterialMaskAt(i);
            auto& stocks=resourceStocks[resourceStockIndices[i]-1];
            const auto& yields=resourceRegistry().yields(static_cast<ResourceId>(resourceCells[i].resource.type));
            for (unsigned m=0;m<MaterialCount;++m)
            {
                stream->readEnterSection(m); stocks[m]=stream->readUint16("stock"); stream->readLeaveSection();
                if (stocks[m]>yields[m].capacity) throw std::ios_base::failure("Material stock exceeds capacity");
            }
            refreshResourceTotal(i);
            if (resourceCells[i].resource.amount!=savedMultiTotals[resourceStockIndices[i]-1]) throw std::ios_base::failure("Inconsistent resource total stock");
            materialStockChanged(i,before);
            if (!resourceCells[i].resource.amount && !resourcePropertiesByIndex(resourceCells[i].resource.type).persistsWhenEmpty) throw std::ios_base::failure("Empty finite resource");
            stream->readLeaveSection();
        }
        stream->readLeaveSection();
    }

	for(int n=0; n<9; ++n)
	{
		stream->readEnterSection(n);
		setAreaName(n, stream->readText("areaname"));
		stream->readLeaveSection();
	}

	const bool restoreExploredArea = header.getIsSavedGame() && versionMinor >= EXPLORED_AREA_SAVED_VERSION_MINOR;
	if (restoreExploredArea)
		loadExploredArea(stream, header.getNumberOfTeams(), game != NULL, versionMinor);

	rebuildTerrainCounts();
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
	preparePendingGradient();
	stream->writeEnterSection("Map");
	stream->write("MapB", 4, "signatureStart");
	
	// We save size:
	stream->writeSint32(wDec, "wDec");
	stream->writeSint32(hDec, "hDec");

	{
		const auto definitions = terrainRegistry().serialize();
		stream->writeEnterSection("terrainRegistry");
		stream->writeUint32((definitions.size() + RegistryChunkBytes - 1) / RegistryChunkBytes, "chunks");
		for (std::size_t i = 0; i < definitions.size(); i += RegistryChunkBytes)
		{
			stream->writeEnterSection(i / RegistryChunkBytes);
			const auto length = std::min(RegistryChunkBytes, definitions.size() - i);
			stream->writeUint32(length, "length");
			stream->write(definitions.data() + i, length, "definitions");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}
	stream->writeUint32(terrainSeedValue, "terrainSeed");

    {
        const auto definitions=resourceRegistry().serialize();
        stream->writeEnterSection("resourceRegistry");
        stream->writeUint32((definitions.size()+RegistryChunkBytes-1)/RegistryChunkBytes,"chunks");
        for (size_t i=0;i<definitions.size();i+=RegistryChunkBytes)
        {
            stream->writeEnterSection(i/RegistryChunkBytes);
            const auto length=std::min(RegistryChunkBytes,definitions.size()-i);
            stream->writeUint32(length,"length"); stream->write(definitions.data()+i,length,"definitions");
            stream->writeLeaveSection();
        }
        stream->writeLeaveSection();
    }

	// We write what's inside the map:
	if(GAGCore::PackedArray::binary(stream)) GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return undermap[i];});
    else stream->write(undermap, size, "undermap");
	stream->writeEnterSection("cases");
    if(GAGCore::PackedArray::binary(stream))
    {
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return mapDiscovered[i];});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return legacyTerrain[i];});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return static_cast<Uint16>(terrainIds[i]);});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return occupancyCells[i].building;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return resourceCells[i].resource.type;});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return resourceCells[i].resource.variety;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return resourceCells[i].resource.amount;});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return resourceCells[i].resource.animation;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return occupancyCells[i].groundUnit;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return occupancyCells[i].airUnit;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return areaCells[i].forbidden;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return areaCells[i].guard;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return areaCells[i].clear;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return areaCells[i].farm;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return scriptAreaCells[i];});
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return resourceCells[i].mayGrow;});
        GAGCore::PackedArray::write<Uint16>(stream,size,[&](size_t i){return resourceCells[i].fertility;});
    }
    else for (size_t i=0; i<size ;i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(mapDiscovered[i], "mapDiscovered");

		stream->writeUint16(legacyTerrain[i], "terrain");
		stream->writeUint16(static_cast<Uint16>(terrainIds[i]), "terrainType");
		stream->writeUint16(occupancyCells[i].building, "building");
		
		stream->writeUint16(resourceCells[i].resource.type,"resourceType");
        stream->writeUint8(resourceCells[i].resource.variety,"resourceVariety");
        stream->writeUint32(resourceCells[i].resource.amount,"resourceStock");
        stream->writeUint8(resourceCells[i].resource.animation,"resourceAnimation");
		
		stream->writeUint16(occupancyCells[i].groundUnit, "groundUnit");
		stream->writeUint16(occupancyCells[i].airUnit, "airUnit");
		stream->writeUint32(areaCells[i].forbidden, "forbidden");
		stream->writeUint32(areaCells[i].guard, "guardArea");
		stream->writeUint32(areaCells[i].clear, "clearArea");
		stream->writeUint32(areaCells[i].farm, "farmArea");
		stream->writeUint16(scriptAreaCells[i], "scriptAreas");
		stream->writeUint8(resourceCells[i].mayGrow, "canRessourcesGrow");
		stream->writeUint16(resourceCells[i].fertility, "fertility");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

    stream->writeEnterSection("resourceStocks");
    size_t multiCount=0; for (auto slot:resourceStockIndices) if (slot) ++multiCount;
    stream->writeUint32(multiCount,"count");
    unsigned entry=0;
    for (size_t i=0;i<resourceStockIndices.size();++i)
        if (resourceStockIndices[i])
        {
            stream->writeEnterSection(entry++); stream->writeUint32(i,"tile");
            const auto& stocks=resourceStocks[resourceStockIndices[i]-1];
            for (unsigned m=0;m<MaterialCount;++m)
            { stream->writeEnterSection(m); stream->writeUint16(stocks[m],"stock"); stream->writeLeaveSection(); }
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
	for (int r=0; r<MaterialCount; r++)
		for (int s=0; s<SWIM_CLASS_COUNT; s++)
		{
			assert(materialGradients[t][r][s]==NULL);
			assert(marketMaterialGradients[t][r][s]==NULL);
		}
	
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
	gradientRuntime->preparation={};
	gradientRuntime->pipeline.reset();
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	assert(numberOfTeam<Team::MAX_COUNT);
	
	int t=numberOfTeam;
	for (auto it=gradientRuntime->materialFields.begin(); it!=gradientRuntime->materialFields.end();) {
		if (it->second.team==t) { gradientRuntime->materialLru.erase(it->second.lru); it=gradientRuntime->materialFields.erase(it); }
		else ++it;
	}
	gradientRuntime->stockRevision[t]={};
	for (int s=0; s<SWIM_CLASS_COUNT; s++)
	{
		for (int r=0; r<MaterialCount; r++)
		{
			delete[] materialGradients[t][r][s];
			materialGradients[t][r][s]=NULL;
			delete[] marketMaterialGradients[t][r][s];
			marketMaterialGradients[t][r][s]=NULL;
			marketGradientDirty[t][r][s]=false;
			marketGradientUpdated[t][r][s]=false;
		}
		delete[] forbiddenGradient[t][s];
		forbiddenGradient[t][s]=NULL;
		delete[] guardAreasGradient[t][s];
		guardAreasGradient[t][s]=NULL;
		delete[] clearAreasGradient[t][s];
		clearAreasGradient[t][s]=NULL;
	}
	rebuildPlaneRegistry();

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
	const_cast<Map*>(this)->preparePendingGradient();
	stream->writeEnterSection("mapRuntime");
	stream->writeUint8(fogOfWar == fogOfWarA.data(), "fogIsA");
	stream->writeUint32(topologyGeneration, "topologyGeneration");
	stream->writeEnterSection("cells");
    if(GAGCore::PackedArray::binary(stream))
    {
        GAGCore::PackedArray::write<Uint8>(stream,size,[&](size_t i){return occupancyCells[i].immobileUnit;});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return fogOfWarA[i];});
        GAGCore::PackedArray::write<Uint32>(stream,size,[&](size_t i){return fogOfWarB[i];});
    }
    else for (size_t i=0; i<size; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint8(occupancyCells[i].immobileUnit, "immobileUnit");
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
			for (int r=0; r<MaterialSlotCount; ++r)
			{
				stream->writeEnterSection(r);
				saveGradient(stream, materialGradients[t][r][sw], size);
				stream->writeUint8(gradientUpdated[t][r][sw], "updated");
				// The "with markets" twin is runtime state like its plain gradient:
				// a resumed game must walk the same field it left. Its own section:
				// the text format keys tiles by section name, and the plain
				// gradient's tiles live in this one.
				stream->writeEnterSection("markets");
				saveGradient(stream, marketMaterialGradients[t][r][sw], size);
				stream->writeUint8(marketGradientDirty[t][r][sw], "dirty");
				stream->writeUint8(marketGradientUpdated[t][r][sw], "updated");
				stream->writeLeaveSection();
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
				finishBuildingGradient(building, sw, BuildingRoute::Footprint);
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
				for (int r=0; r<MaterialSlotCount; ++r)
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
			stream->writeEnterSection("routes");
			for (int profile=1; profile<BUILDING_ROUTE_COUNT; ++profile)
			{
				stream->writeEnterSection(profile);
				for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
				{
					stream->writeEnterSection(sw);
					const int slot=profile*SWIM_CLASS_COUNT+sw;
					finishBuildingGradient(building, sw, BuildingRoute(profile));
					saveGradient(stream, building->globalGradient[slot], size);
					stream->writeUint8(building->dirtyGradient[slot], "dirty");
					stream->writeUint32(building->lastGlobalGradientUpdateStepCounter[slot], "lastUpdate");
					stream->writeUint32(building->gradientGeneration[slot], "generation");
					stream->writeUint32(building->globalGradientUsedStep[slot], "usedStep");
					stream->writeLeaveSection();
				}
				for (int sw=0; sw<SWIM_VARIANT_COUNT; ++sw)
					stream->writeUint8(building->locked[profile*SWIM_VARIANT_COUNT+sw], sw ? "swimLocked" : "walkLocked");
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
			for (int kind=0; kind<2*MaterialSlotCount+2; ++kind)
				for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw) {
					auto *slot=kind<MaterialSlotCount ? &materialGradients[t][kind][sw]
						: kind==MaterialSlotCount ? &guardAreasGradient[t][sw]
						: kind==MaterialSlotCount+1 ? &clearAreasGradient[t][sw] : &marketMaterialGradients[t][kind-MaterialSlotCount-2][sw];
					if (slot==snapshot.slot) destination=kind<MaterialSlotCount+2
						? (t*(MaterialSlotCount+2)+kind)*SWIM_CLASS_COUNT+sw
						: Team::MAX_COUNT*(MaterialSlotCount+2)*SWIM_CLASS_COUNT+(t*MaterialSlotCount+kind-MaterialSlotCount-2)*SWIM_CLASS_COUNT+sw;
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
	saveMaterialRoutingCache(stream);
	stream->writeLeaveSection();
}

void Map::loadRuntimeState(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	invalidateResourceSeeds();
    const bool packed=versionMinor>=FILE_FORMAT_VERSION_COMPACT_STATE && GAGCore::PackedArray::binary(stream);
	gradientRuntime->preparation={};
	gradientRuntime->pipeline.reset();
	stream->readEnterSection("mapRuntime");
	const bool fogIsA=loadFlag(stream,"fogIsA");
	if (versionMinor>=FILE_FORMAT_VERSION_TOPOLOGY_GENERATION)
		topologyGeneration=stream->readUint32("topologyGeneration");
	fogOfWar=fogIsA ? fogOfWarA.data() : fogOfWarB.data();
	stream->readEnterSection("cells");
    if(packed)
    {
        GAGCore::PackedArray::read<Uint8>(stream,size,[&](size_t i,Uint8 v){occupancyCells[i].immobileUnit=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){fogOfWarA[i]=v;});
        GAGCore::PackedArray::read<Uint32>(stream,size,[&](size_t i,Uint32 v){fogOfWarB[i]=v;});
    }
    else for (size_t i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		occupancyCells[i].immobileUnit=stream->readUint8("immobileUnit");
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
			for (int r=0; r<MaterialSlotCount; ++r)
			{
				stream->readEnterSection(r);
				loadGradient(stream, materialGradients[t][r][sw], size, packed);
				gradientUpdated[t][r][sw]=loadFlag(stream,"updated");
				if (versionMinor >= FILE_FORMAT_VERSION_MARKET_GRADIENTS)
				{
					stream->readEnterSection("markets");
					loadGradient(stream, marketMaterialGradients[t][r][sw], size, packed);
					marketGradientDirty[t][r][sw]=loadFlag(stream,"dirty");
					marketGradientUpdated[t][r][sw]=loadFlag(stream,"updated");
					stream->readLeaveSection();
					if (!marketsV2Enabled() && (marketMaterialGradients[t][r][sw] || marketGradientDirty[t][r][sw] || marketGradientUpdated[t][r][sw]))
						throw std::runtime_error("Market routing state requires Markets V2");
				}
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
			const BuildingRoute savedRoute = versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG ? BuildingRoute::Footprint : BuildingRoute::Automatic;
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				stream->readEnterSection(sw);
				// Existing saves contain complete fields; discard any previous queue
				// before replacing its buffer, including when reusing a loaded object.
				building->globalGradientSearch[building->routeSlot(sw, savedRoute)].reset();
				loadGradient(stream, building->globalGradient[building->routeSlot(sw, savedRoute)], size, packed);
				building->dirtyGradient[building->routeSlot(sw, savedRoute)]=loadFlag(stream,"dirty");
				building->lastGlobalGradientUpdateStepCounter[building->routeSlot(sw, savedRoute)]=stream->readUint32("lastUpdate");
				// An older save restored its fields as current; keep them so.
				building->gradientGeneration[building->routeSlot(sw, savedRoute)]=versionMinor>=FILE_FORMAT_VERSION_TOPOLOGY_GENERATION
					? stream->readUint32("generation") : topologyGeneration;
				stream->readLeaveSection();
			}
			if (versionMinor >= FILE_FORMAT_VERSION_ROUND_TRIP_FIELDS)
			{
				stream->readEnterSection("roundTrip");
				for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
				{
					stream->readEnterSection(sw);
					building->globalGradientUsedStep[building->routeSlot(sw, savedRoute)]=stream->readUint32("usedStep");
					for (int r=0; r<MaterialSlotCount; ++r)
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
				building->locked[building->routeAccess(sw, savedRoute)]=loadFlag(stream,"locked");
				building->anyResourceToClear[sw]=stream->readUint8("resourceState");
				if (building->anyResourceToClear[sw]>2) throw std::runtime_error("Invalid saved resource state");
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
			if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG)
			{
				stream->readEnterSection("routes");
				for (int profile=1; profile<BUILDING_ROUTE_COUNT; ++profile)
				{
					stream->readEnterSection(profile);
					for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
					{
						stream->readEnterSection(sw);
						const int slot=profile*SWIM_CLASS_COUNT+sw;
						building->globalGradientSearch[slot].reset();
						loadGradient(stream, building->globalGradient[slot], size, packed);
						building->dirtyGradient[slot]=loadFlag(stream,"dirty");
						building->lastGlobalGradientUpdateStepCounter[slot]=stream->readUint32("lastUpdate");
						building->gradientGeneration[slot]=stream->readUint32("generation");
						building->globalGradientUsedStep[slot]=stream->readUint32("usedStep");
						stream->readLeaveSection();
					}
					for (int sw=0; sw<SWIM_VARIANT_COUNT; ++sw)
						building->locked[profile*SWIM_VARIANT_COUNT+sw]=loadFlag(stream, sw ? "swimLocked" : "walkLocked");
					stream->readLeaveSection();
				}
				stream->readLeaveSection();
			}
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
		configureGradientPipeline(2, delay);
		for (unsigned index=0; index<count; ++index) {
			stream->readEnterSection(index);
			const unsigned destination=stream->readUint16("destination");
			const unsigned sw=destination%SWIM_CLASS_COUNT;
			const unsigned marketBase=Team::MAX_COUNT*(MaterialSlotCount+2)*SWIM_CLASS_COUNT;
			const bool market=destination>=marketBase;
			if (market && (versionMinor<FILE_FORMAT_VERSION_MARKET_GRADIENTS || !marketsV2Enabled())) throw std::runtime_error("Invalid saved gradient destination");
			const unsigned encoded=market ? destination-marketBase : destination;
			const unsigned kinds=market ? MaterialSlotCount : MaterialSlotCount+2;
			const unsigned kind=(encoded/SWIM_CLASS_COUNT)%kinds;
			const unsigned team=encoded/(SWIM_CLASS_COUNT*kinds);
			if (team>=static_cast<unsigned>(game->teamsCount())) throw std::runtime_error("Invalid saved gradient team");
			auto *slot=market ? &marketMaterialGradients[team][kind][sw] : kind<MaterialSlotCount ? &materialGradients[team][kind][sw]
				: kind==MaterialSlotCount ? &guardAreasGradient[team][sw] : &clearAreasGradient[team][sw];
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
	if (versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG) loadMaterialRoutingCache(stream,packed,versionMinor);
	stream->readLeaveSection();
    if (versionMinor < FILE_FORMAT_VERSION_HAZARD_ROUTING && hasTerrainHealthEffects()) {
        // Consume the complete old state first, then discard only route caches.
        // Unit health, claims, fog and scheduling unrelated to routing survive.
        gradientRuntime->preparation={};
        const unsigned delay = gradientRuntime->pipeline.delayTicks();
        gradientRuntime->pipeline.reset();
        if (delay) configureGradientPipeline(2,delay);
        gradientRuntime->materialFields.clear();
        gradientRuntime->materialLru.clear();
        for (int t=0; t<game->teamsCount(); ++t) {
            for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw) {
                for (int r=0; r<MaterialSlotCount; ++r) {
                    delete[] materialGradients[t][r][sw]; materialGradients[t][r][sw]=nullptr;
                    gradientUpdated[t][r][sw]=false;
                    delete[] marketMaterialGradients[t][r][sw]; marketMaterialGradients[t][r][sw]=nullptr;
                    marketGradientDirty[t][r][sw]=marketGradientUpdated[t][r][sw]=false;
                }
                delete[] forbiddenGradient[t][sw]; forbiddenGradient[t][sw]=nullptr;
                delete[] guardAreasGradient[t][sw]; guardAreasGradient[t][sw]=nullptr;
                delete[] clearAreasGradient[t][sw]; clearAreasGradient[t][sw]=nullptr;
                guardGradientUpdated[t][sw]=clearGradientUpdated[t][sw]=false;
            }
            for (int b=0; b<Building::MAX_COUNT; ++b)
                if (auto* building=game->teams[t]->myBuildings[b]) building->freeGradients();
        }
    }
    // Loaded and discarded slots bypassed publishPlane.
    rebuildPlaneRegistry();
}


void Map::saveMaterialRoutingCache(GAGCore::OutputStream* stream) const
{
    const auto& cache=*gradientRuntime;
    const auto write64=[&](Uint64 value,const char* name) {
        stream->writeEnterSection(name); stream->writeUint32(value>>32,"high"); stream->writeUint32(value,"low"); stream->writeLeaveSection();
    };
    stream->writeEnterSection("resourceRoutingCache");
    write64(std::max<Uint64>(cache.materialCacheBudget,Uint64(size)*sizeof(Uint16)),"budget"); write64(cache.materialCacheClock,"clock");
    stream->writeEnterSection("revisions");
    for (int team=0; team<Team::MAX_COUNT; ++team) {
        stream->writeEnterSection(team);
        for (int resource=0; resource<MaterialCount; ++resource) {
            stream->writeEnterSection(resource); write64(cache.stockRevision[team][resource],"revision"); stream->writeLeaveSection();
        }
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
    stream->writeUint32(cache.materialFields.size(),"count");
    unsigned index=0;
    // Saving least-to-most recently used retains eviction and refresh behavior.
    for (const auto key : cache.materialLru) {
        const auto& entry=cache.materialFields.at(key);
        stream->writeEnterSection(index++);
        stream->writeSint32(entry.consumer,"consumer"); stream->writeSint32(entry.type,"type");
        stream->writeSint32(entry.x,"x"); stream->writeSint32(entry.y,"y");
        stream->writeUint32(entry.identity,"identity"); stream->writeUint32(entry.topology,"topology");
        stream->writeUint32(entry.builtStep,"builtStep"); write64(entry.sourceRevision,"sourceRevision"); write64(entry.recency,"recency");
        stream->writeUint8(entry.team,"team"); stream->writeUint8(entry.resource,"resource");
        stream->writeUint8(entry.swim,"swim"); stream->writeUint8(entry.modes,"modes");
        saveGradient(stream,entry.cells.get(),size);
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
}

void Map::loadMaterialRoutingCache(GAGCore::InputStream* stream, bool packed, int versionMinor)
{
    auto& cache=*gradientRuntime;
    const auto read64=[&](const char* name) {
        stream->readEnterSection(name); const Uint64 high=stream->readUint32("high"), low=stream->readUint32("low"); stream->readLeaveSection();
        return (high<<32)|low;
    };
    stream->readEnterSection("resourceRoutingCache");
    const Uint64 budget=read64("budget"), clock=read64("clock");
    const Uint64 fieldBytes=Uint64(size)*sizeof(Uint16), maxBudget=std::max<Uint64>(fieldBytes,64ull*1024*1024);
    if (budget<fieldBytes || budget>maxBudget || clock==std::numeric_limits<Uint64>::max()) throw std::runtime_error("Invalid resource routing cache budget or clock");
    cache.materialFields.clear(); cache.materialLru.clear(); cache.materialCacheBudget=budget; cache.materialCacheClock=clock;
    stream->readEnterSection("revisions");
    for (int team=0; team<Team::MAX_COUNT; ++team) {
        stream->readEnterSection(team);
        for (int resource=0; resource<(versionMinor>=FILE_FORMAT_VERSION_RUNTIME_RESOURCES ? int(MaterialCount) : 8); ++resource) {
            stream->readEnterSection(resource); cache.stockRevision[team][resource]=read64("revision"); stream->readLeaveSection();
        }
        stream->readLeaveSection();
    }
    stream->readLeaveSection();
    const Uint32 count=stream->readUint32("count");
    if (count>budget/fieldBytes) throw std::runtime_error("Resource routing cache exceeds budget");
    Uint64 prior=0;
    for (Uint32 index=0; index<count; ++index) {
        stream->readEnterSection(index);
        GradientRuntime::MaterialField entry;
        entry.consumer=stream->readSint32("consumer"); entry.type=stream->readSint32("type");
        entry.x=stream->readSint32("x"); entry.y=stream->readSint32("y");
        entry.identity=stream->readUint32("identity"); entry.topology=stream->readUint32("topology");
        entry.builtStep=stream->readUint32("builtStep"); entry.sourceRevision=read64("sourceRevision"); entry.recency=read64("recency");
        entry.team=stream->readUint8("team"); entry.resource=stream->readUint8("resource");
        entry.swim=stream->readUint8("swim"); entry.modes=stream->readUint8("modes");
        if (entry.modes<1 || entry.modes>3 || entry.team>=game->teamsCount() || entry.resource>=MaterialCount || entry.swim>=SWIM_CLASS_COUNT
            || entry.consumer < -1 || entry.consumer>=Team::MAX_COUNT*Building::MAX_COUNT
            || (entry.consumer>=0 && Building::GIDtoTeam(entry.consumer)!=entry.team)
            || entry.type < -1 || entry.type>=int(game->buildingsTypes.size())
            || !entry.recency || entry.recency<=prior || entry.recency>clock)
            throw std::runtime_error("Invalid resource routing cache entry");
        prior=entry.recency;
        const Uint64 key=((((Uint64(entry.consumer+1)*Team::MAX_COUNT+entry.team)*MaterialCount+entry.resource)*SWIM_CLASS_COUNT+entry.swim)*4)+entry.modes;
        if (cache.materialFields.contains(key)) throw std::runtime_error("Duplicate resource routing cache entry");
        Uint16* field=nullptr; loadGradient(stream,field,size,packed); entry.cells.reset(field);
        if (!field) throw std::runtime_error("Missing resource routing cache field");
        cache.materialLru.push_back(key); entry.lru=std::prev(cache.materialLru.end());
        cache.materialFields.emplace(key,std::move(entry));
        stream->readLeaveSection();
    }
    stream->readLeaveSection();
}
