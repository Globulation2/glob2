// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameHeader.h"

#include "FileFormatVersions.h"
#include "BuildingType.h"
#include "BuildingArtwork.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <array>
#include <string_view>

#include <algorithm>
#include <ctime>

namespace
{
constexpr std::size_t CatalogChunkBytes = 256 * 1024;
constexpr Uint32 MaxCatalogChunks = 32;
// Artwork uses the same bounded byte transport as catalog JSON. Chunking also
// keeps binary and text streams below their individual string-length limits.
constexpr Uint32 MaxArtworkChunks = BuildingArtwork::MaxBytes / CatalogChunkBytes;

// Format 145 was allocated independently by the released artwork engine and the
// earlier compact-growth branch. Nonempty artwork has a fixed uppercase magic;
// catalog experiment keys are lowercase. Empty artwork is followed by bounded
// resource definitions and enabled keys. An older header reaches its GaBe game
// signature (or EOF) before that extra field can be read. Probe without mutating
// GameHeader; the normal readers still validate every selected payload.
bool hasBuildingArtwork145(GAGCore::InputStream* stream)
{
    if (auto* text = dynamic_cast<GAGCore::TextInputStream*>(stream))
        return text->hasField("buildingArtwork.chunks");
    if (!dynamic_cast<GAGCore::BinaryInputStream*>(stream) || !stream->canSeek())
        throw std::ios_base::failure("Cannot identify format-145 game header");
    const auto start = stream->getPosition();
    const auto word = [&]() {
        const auto before = stream->getPosition();
        const auto value = stream->readUint32("layoutProbe");
        if (stream->getPosition() != before + 4) throw std::runtime_error("Truncated layout probe");
        return value;
    };
    const auto skipText = [&](Uint32 limit) {
        const auto count = word();
        if (count > limit) throw std::runtime_error("Invalid layout probe length");
        std::array<char, 4096> bytes{};
        const auto before = stream->getPosition();
        stream->read(bytes.data(), count, "layoutProbe");
        if (stream->getPosition() != before + count) throw std::runtime_error("Truncated layout probe text");
    };
    bool artwork = false;
    try {
        const auto chunks = word();
        if (chunks) {
            const auto size = word();
            std::array<char, 8> magic{};
            const auto before = stream->getPosition();
            stream->read(magic.data(), magic.size(), "layoutProbe");
            artwork = chunks <= MaxArtworkChunks && size >= magic.size() && size <= CatalogChunkBytes &&
                stream->getPosition() == before + magic.size() && std::string_view(magic.data(), magic.size()) == "G2BA0001";
        } else {
            const auto definitions = word();
            if (definitions > ExperimentSet::MAX_STORED) throw std::runtime_error("Invalid layout probe count");
            for (Uint32 i = 0; i < definitions; ++i) { skipText(128); skipText(512); skipText(4096); }
            const auto enabled = word();
            if (enabled > ExperimentSet::MAX_STORED) throw std::runtime_error("Invalid layout probe experiments");
            for (Uint32 i = 0; i < enabled; ++i) skipText(128);
            artwork = true;
        }
    } catch (const std::runtime_error&) {
        artwork = false;
    }
    stream->seekFromStart(start);
    return artwork;
}

std::string readCatalog(GAGCore::InputStream* stream, const char* section="buildingCatalog", Uint32 maxChunks=MaxCatalogChunks)
{
	stream->readEnterSection(section);
	const auto count = stream->readUint32("chunks");
	if (count > maxChunks) throw std::runtime_error("Building catalog is too large");
	std::string snapshot;
	for (Uint32 i=0; i<count; ++i)
	{
		stream->readEnterSection(i);
		const Uint32 size=stream->readUint32("size");
		if (!size || size>CatalogChunkBytes)
			throw std::runtime_error("Invalid building catalog chunk");
		const size_t offset=snapshot.size();
		snapshot.resize(offset+size);
		stream->read(snapshot.data()+offset,size,"data");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return snapshot;
}

void writeCatalog(GAGCore::OutputStream* stream, const std::string& snapshot, const char* section="buildingCatalog", Uint32 maxChunks=MaxCatalogChunks)
{
	const auto count = (snapshot.size() + CatalogChunkBytes - 1) / CatalogChunkBytes;
	if (count > maxChunks) throw std::runtime_error("Building catalog is too large");
	stream->writeEnterSection(section);
	stream->writeUint32(static_cast<Uint32>(count), "chunks");
	for (Uint32 i=0; i<count; ++i)
	{
		stream->writeEnterSection(i);
		const size_t offset=i*CatalogChunkBytes;
		const Uint32 size=static_cast<Uint32>(std::min(CatalogChunkBytes,snapshot.size()-offset));
		stream->writeUint32(size,"size");
		stream->write(snapshot.data()+offset,size,"data");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}
}

GameHeader::GameHeader()
{
	reset();
}

void GameHeader::reset()
{
	++observationRevisionValue; aiOrderDelay = 8; buildingGradientDelay = DEFAULT_BUILDING_GRADIENT_DELAY;
	buildingCatalogSnapshot.clear();
	buildingArtwork.reset();
	buildingCatalogExperimentKeys.clear();
	resourceCatalogExperiments.clear();
	//These are the default game options
	numberOfPlayers = 0;
	gameLatency = 0;
	orderRate = 1;
	//Seed is random by default
	seed = std::time(NULL);
	//If needed, seed can be fixed, default value, 5489
	//seed = 5489;
	
	for (Uint8 i=0; i<Team::MAX_COUNT; ++i)
	{
		players[i] = BasePlayer();
		aiConfig[i].clear();
		allyTeamNumbers[i] = i+1;
	}
	allyTeamsFixed=true;
	winningConditions = WinningCondition::getDefaultWinningConditions();
	mapDiscovered=false;
	resourceGrowthDisabled=false;
	resourceScarcityLevel=0;
	instantConstruction=false;
	stockpileStartLevel=0;
	hungerDisabled=false;
	unitUpgradesDisabled=false;
	glassCannonLevel=0;
	unitsFearless=false;
	permadeathDisabled=false;
	peacefulMode=false;
	buildingHpLevel=0;
	experiments.clear();
}

void GameHeader::setBuildingCatalogSnapshot(const std::string& snapshot)
{
	++observationRevisionValue;
	if (snapshot.empty())
	{
		buildingCatalogSnapshot.clear();
		buildingArtwork.reset();
		buildingCatalogExperimentKeys.clear();
		return;
	}
	if (snapshot == buildingCatalogSnapshot) return;
	BuildingsTypes catalog;
	catalog.loadSnapshotJson(snapshot);
	std::vector<std::string> keys;
	for (const auto& experiment : catalog.experiments()) keys.push_back(experiment.key);
	buildingArtwork.reset();
	buildingCatalogSnapshot = catalog.snapshotJson();
	buildingCatalogExperimentKeys = std::move(keys);
}

void GameHeader::setBuildingArtwork(const std::string& bytes)
{
    if(bytes.empty() && buildingCatalogSnapshot.empty()) { buildingArtwork.reset(); return; }
    if(buildingCatalogSnapshot.empty()) throw std::runtime_error("Artwork requires a building catalog");
    if(buildingArtwork && buildingArtwork->bytes()==bytes)return;
    BuildingsTypes catalog;
    catalog.loadSnapshotJson(buildingCatalogSnapshot);
    auto decoded=BuildingArtwork::decode(bytes,catalog);
    if(!decoded && !buildingArtwork)return;
    buildingArtwork=std::move(decoded);
    ++observationRevisionValue;
}

void GameHeader::setResourceExperiments(const std::vector<CatalogExperimentDefinition>& definitions)
{
	validateCatalogExperiments(definitions);
	resourceCatalogExperiments = definitions;
}
std::vector<std::string> GameHeader::catalogExperimentKeys() const
{
	auto keys = buildingCatalogExperimentKeys;
	for (const auto& definition : resourceCatalogExperiments) keys.push_back(definition.key);
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	return keys;
}



namespace
{
	// 1-based ally-team IDs. reset() seeds allyTeamNumbers[i] = i+1, so
	// team 1 and team 2 are the lowest two groups available.
	constexpr Uint8 HUMAN_ALLY_TEAM = 1;
	constexpr Uint8 ENEMY_ALLY_TEAM = 2;
}

void GameHeader::setDefaultAlliances(std::optional<int> humanColor, const std::vector<int>& aiColors)
{
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		setAllyTeamNumber(i, i + 1);
	if (!humanColor)
		return;
	setAllyTeamNumber(*humanColor, HUMAN_ALLY_TEAM);
	for (int aiColor : aiColors)
		if (aiColor != *humanColor)
			setAllyTeamNumber(aiColor, ENEMY_ALLY_TEAM);
}



bool GameHeader::load(GAGCore::InputStream *stream, Sint32 versionMinor, Sint32 historicalGrowthVersion)
{
	stream->readEnterSection("GameHeader");
	gameLatency = stream->readSint32("gameLatency");
	orderRate = stream->readUint8("orderRate");
	aiOrderDelay = versionMinor >= FILE_FORMAT_VERSION_AI_PIPELINE ? stream->readUint8("aiOrderDelay") : 0;
	if (aiOrderDelay > 8) throw std::runtime_error("Invalid saved AI order delay");
	buildingGradientDelay = versionMinor >= FILE_FORMAT_VERSION_BUILDING_GRADIENT_PIPELINE ? stream->readUint8("buildingGradientDelay") : DEFAULT_BUILDING_GRADIENT_DELAY;
	if (buildingGradientDelay < 1 || buildingGradientDelay > 8) throw std::runtime_error("Invalid saved building gradient delay");
	if (gameLatency < 0 || gameLatency > 65535 || orderRate == 0) throw std::runtime_error("Invalid saved network rate or latency");
	numberOfPlayers = stream->readSint32("numberOfPlayers");
	if (numberOfPlayers < 0 || numberOfPlayers > Team::MAX_COUNT)
	{
		return false;
	}
	stream->readEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->readEnterSection(i);
		if (i < Team::MAX_COUNT)
		{
			if (!players[i].load(stream, versionMinor))
			{
				stream->readLeaveSection();
				stream->readLeaveSection();
				stream->readLeaveSection();
				return false;
			}
		}
		else
		{
			BasePlayer scratch;
			// Trailing on-disk slots beyond Team::MAX_COUNT are padding; their
			// teamNumber field is never consumed, so a bad value here is not a
			// crash hazard. Discard validation failures.
			scratch.load(stream, versionMinor);
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if(versionMinor >= FILE_FORMAT_VERSION_ALLIES_AND_WIN_CONDITIONS)
	{
		stream->readEnterSection("allyTeamNumbers");
		for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
		{
			// Binary sections carry no bytes. Text fields need a unique slot;
			// repeating the same key made every alliance read as the last one.
			if (versionMinor >= FILE_FORMAT_VERSION_COUNTED_TEAM_STATE)
				stream->readEnterSection(i);
			Uint8 v = stream->readUint8("allyTeamNumber");
			if (i < Team::MAX_COUNT)
				allyTeamNumbers[i] = v;
			if (versionMinor >= FILE_FORMAT_VERSION_COUNTED_TEAM_STATE)
				stream->readLeaveSection();
		}
		stream->readLeaveSection();
		allyTeamsFixed = stream->readUint8("allyTeamsFixed");

		if (!WinningCondition::loadWinningConditions(stream, versionMinor, winningConditions))
			return false;
	}
	if(versionMinor >= FILE_FORMAT_VERSION_UNIFIED_SEED)
		seed = stream->readUint32("seed");
	if(versionMinor >=  FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG)
		mapDiscovered = stream->readUint8("mapDiscovered");
	if (!loadAIConfig(stream, versionMinor)) return false;
	if(versionMinor >= FILE_FORMAT_VERSION_ECONOMY_RULES)
	{
		resourceGrowthDisabled = stream->readUint8("resourceGrowthDisabled");
		// Clamped to the tier lookup tables' range (Game.cpp's stockpileAmount[],
		// Map::growResources's scarcityDivisor[]): a corrupted save or a
		// malicious network peer could otherwise supply any Uint8 (0-255) and
		// trigger an out-of-bounds array read wherever these are used to index.
		resourceScarcityLevel = std::min<Uint8>(stream->readUint8("resourceScarcityLevel"), 3);
		instantConstruction = stream->readUint8("instantConstruction");
		stockpileStartLevel = std::min<Uint8>(stream->readUint8("stockpileStartLevel"), 3);
		hungerDisabled = stream->readUint8("hungerDisabled");
	}
	if(versionMinor >= FILE_FORMAT_VERSION_COMBAT_RULES)
	{
		unitUpgradesDisabled = stream->readUint8("unitUpgradesDisabled");
		// Clamped to the tier lookup tables' range (this class's own
		// getGlassCannonScale()/getBuildingHpMultiplier()): a corrupted save
		// or a malicious network peer could otherwise supply any Uint8
		// (0-255) and trigger an out-of-bounds array read wherever these are
		// used to index.
		glassCannonLevel = std::min<Uint8>(stream->readUint8("glassCannonLevel"), 2);
		unitsFearless = stream->readUint8("unitsFearless");
		permadeathDisabled = stream->readUint8("permadeathDisabled");
		peacefulMode = stream->readUint8("peacefulMode");
		buildingHpLevel = std::min<Uint8>(stream->readUint8("buildingHpLevel"), 2);
	}
	if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG)
		setBuildingCatalogSnapshot(readCatalog(stream));
	else
		setBuildingCatalogSnapshot({});
	if (historicalGrowthVersion != 146 && (versionMinor >= 146 ||
        (versionMinor == FILE_FORMAT_VERSION_BUILDING_ARTWORK && hasBuildingArtwork145(stream))))
        setBuildingArtwork(readCatalog(stream,"buildingArtwork",MaxArtworkChunks));
    else setBuildingArtwork({});
	if (versionMinor >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
        setResourceExperiments(loadCatalogExperimentDefinitions(stream));
    else resourceCatalogExperiments.clear();
    if (!experiments.load(stream, versionMinor, false, catalogExperimentKeys())) return false;
	stream->readLeaveSection();
	return true;
}



void GameHeader::save(GAGCore::OutputStream *stream) const
{
    if(!buildingArtwork && !buildingCatalogSnapshot.empty()) {BuildingsTypes catalog;catalog.loadSnapshotJson(buildingCatalogSnapshot);BuildingArtwork::decode({},catalog);}

	stream->writeEnterSection("GameHeader");
	stream->writeSint32(gameLatency, "gameLatency");
	stream->writeUint8(orderRate, "orderRate");
	stream->writeUint8(aiOrderDelay, "aiOrderDelay");
	stream->writeUint8(buildingGradientDelay, "buildingGradientDelay");
	stream->writeSint32(numberOfPlayers, "numberOfPlayers");
	stream->writeEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->writeEnterSection(i);
		if (i < Team::MAX_COUNT)
			players[i].save(stream);
		else
			BasePlayer().save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("allyTeamNumbers");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->writeEnterSection(i);
		const Uint8 v = (i < Team::MAX_COUNT) ? allyTeamNumbers[i] : static_cast<Uint8>(i + 1);
		stream->writeUint8(v, "allyTeamNumber");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint8(allyTeamsFixed, "allyTeamsFixed");
	stream->writeEnterSection("winningConditions");
	stream->writeUint32(winningConditions.size(), "size");
	int n=0;
	for(std::list<std::shared_ptr<WinningCondition> >::const_iterator i=winningConditions.begin(); i!=winningConditions.end(); ++i)
	{
		stream->writeEnterSection(n);
		(*i)->encodeData(stream);
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	stream->writeUint32(seed, "seed");
	stream->writeUint8(mapDiscovered, "mapDiscovered");
	saveAIConfig(stream);
	stream->writeUint8(resourceGrowthDisabled, "resourceGrowthDisabled");
	stream->writeUint8(resourceScarcityLevel, "resourceScarcityLevel");
	stream->writeUint8(instantConstruction, "instantConstruction");
	stream->writeUint8(stockpileStartLevel, "stockpileStartLevel");
	stream->writeUint8(hungerDisabled, "hungerDisabled");
	stream->writeUint8(unitUpgradesDisabled, "unitUpgradesDisabled");
	stream->writeUint8(glassCannonLevel, "glassCannonLevel");
	stream->writeUint8(unitsFearless, "unitsFearless");
	stream->writeUint8(permadeathDisabled, "permadeathDisabled");
	stream->writeUint8(peacefulMode, "peacefulMode");
	stream->writeUint8(buildingHpLevel, "buildingHpLevel");
	writeCatalog(stream, buildingCatalogSnapshot);
	writeCatalog(stream, buildingArtwork ? buildingArtwork->bytes() : std::string{}, "buildingArtwork",MaxArtworkChunks);
	saveCatalogExperimentDefinitions(stream, resourceCatalogExperiments);
	experiments.save(stream);
	stream->writeLeaveSection();
}



bool GameHeader::loadWithoutPlayerInfo(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameHeader");
	gameLatency = stream->readSint32("gameLatency");
	orderRate = stream->readUint8("orderRate");
	aiOrderDelay = versionMinor >= FILE_FORMAT_VERSION_AI_PIPELINE ? stream->readUint8("aiOrderDelay") : 0;
	if (aiOrderDelay > 8) throw std::runtime_error("Invalid saved AI order delay");
	buildingGradientDelay = versionMinor >= FILE_FORMAT_VERSION_BUILDING_GRADIENT_PIPELINE ? stream->readUint8("buildingGradientDelay") : DEFAULT_BUILDING_GRADIENT_DELAY;
	if (buildingGradientDelay < 1 || buildingGradientDelay > 8) throw std::runtime_error("Invalid saved building gradient delay");
	if (gameLatency < 0 || gameLatency > 65535 || orderRate == 0) throw std::runtime_error("Invalid saved network rate or latency");
	if(versionMinor >= FILE_FORMAT_VERSION_ALLIES_AND_WIN_CONDITIONS)
	{
		stream->readEnterSection("allyTeamNumbers");
		for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
		{
			// Binary sections carry no bytes. Text fields need a unique slot;
			// repeating the same key made every alliance read as the last one.
			if (versionMinor >= FILE_FORMAT_VERSION_COUNTED_TEAM_STATE)
				stream->readEnterSection(i);
			Uint8 v = stream->readUint8("allyTeamNumber");
			if (i < Team::MAX_COUNT)
				allyTeamNumbers[i] = v;
			if (versionMinor >= FILE_FORMAT_VERSION_COUNTED_TEAM_STATE)
				stream->readLeaveSection();
		}
		stream->readLeaveSection();
		allyTeamsFixed = stream->readUint8("allyTeamsFixed");

		if (!WinningCondition::loadWinningConditions(stream, versionMinor, winningConditions))
			return false;
	}
	if(versionMinor >= FILE_FORMAT_VERSION_UNIFIED_SEED)
		seed = stream->readUint32("seed");
	if(versionMinor >=  FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG)
		mapDiscovered = stream->readUint8("mapDiscovered");
	if (!loadAIConfig(stream, versionMinor)) return false;
	if(versionMinor >= FILE_FORMAT_VERSION_ECONOMY_RULES)
	{
		resourceGrowthDisabled = stream->readUint8("resourceGrowthDisabled");
		resourceScarcityLevel = std::min<Uint8>(stream->readUint8("resourceScarcityLevel"), 3);
		instantConstruction = stream->readUint8("instantConstruction");
		stockpileStartLevel = std::min<Uint8>(stream->readUint8("stockpileStartLevel"), 3);
		hungerDisabled = stream->readUint8("hungerDisabled");
	}
	if(versionMinor >= FILE_FORMAT_VERSION_COMBAT_RULES)
	{
		unitUpgradesDisabled = stream->readUint8("unitUpgradesDisabled");
		// Clamped to the tier lookup tables' range (this class's own
		// getGlassCannonScale()/getBuildingHpMultiplier()): a corrupted save
		// or a malicious network peer could otherwise supply any Uint8
		// (0-255) and trigger an out-of-bounds array read wherever these are
		// used to index.
		glassCannonLevel = std::min<Uint8>(stream->readUint8("glassCannonLevel"), 2);
		unitsFearless = stream->readUint8("unitsFearless");
		permadeathDisabled = stream->readUint8("permadeathDisabled");
		peacefulMode = stream->readUint8("peacefulMode");
		buildingHpLevel = std::min<Uint8>(stream->readUint8("buildingHpLevel"), 2);
	}
	if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG)
		setBuildingCatalogSnapshot(readCatalog(stream));
	else
		setBuildingCatalogSnapshot({});
	if (versionMinor >= 146 ||
        (versionMinor == FILE_FORMAT_VERSION_BUILDING_ARTWORK && hasBuildingArtwork145(stream)))
        setBuildingArtwork(readCatalog(stream,"buildingArtwork",MaxArtworkChunks));
    else setBuildingArtwork({});
	if (versionMinor >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES)
        setResourceExperiments(loadCatalogExperimentDefinitions(stream));
    else resourceCatalogExperiments.clear();
    if (!experiments.load(stream, versionMinor, false, catalogExperimentKeys())) return false;
	stream->readLeaveSection();
	return true;
}



void GameHeader::saveWithoutPlayerInfo(GAGCore::OutputStream *stream) const
{
    if(!buildingArtwork && !buildingCatalogSnapshot.empty()) {BuildingsTypes catalog;catalog.loadSnapshotJson(buildingCatalogSnapshot);BuildingArtwork::decode({},catalog);}

	stream->writeEnterSection("GameHeader");
	stream->writeSint32(gameLatency, "gameLatency");
	stream->writeUint8(orderRate, "orderRate");
	stream->writeUint8(aiOrderDelay, "aiOrderDelay");
	stream->writeUint8(buildingGradientDelay, "buildingGradientDelay");
	stream->writeEnterSection("allyTeamNumbers");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->writeEnterSection(i);
		const Uint8 v = (i < Team::MAX_COUNT) ? allyTeamNumbers[i] : static_cast<Uint8>(i + 1);
		stream->writeUint8(v, "allyTeamNumber");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint8(allyTeamsFixed, "allyTeamsFixed");
	stream->writeEnterSection("winningConditions");
	stream->writeUint32(winningConditions.size(), "size");
	int n=0;
	for(std::list<std::shared_ptr<WinningCondition> >::const_iterator i=winningConditions.begin(); i!=winningConditions.end(); ++i)
	{
		stream->writeEnterSection(n);
		(*i)->encodeData(stream);
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	stream->writeUint32(seed, "seed");
	stream->writeUint8(mapDiscovered, "mapDiscovered");
	saveAIConfig(stream);
	stream->writeUint8(resourceGrowthDisabled, "resourceGrowthDisabled");
	stream->writeUint8(resourceScarcityLevel, "resourceScarcityLevel");
	stream->writeUint8(instantConstruction, "instantConstruction");
	stream->writeUint8(stockpileStartLevel, "stockpileStartLevel");
	stream->writeUint8(hungerDisabled, "hungerDisabled");
	stream->writeUint8(unitUpgradesDisabled, "unitUpgradesDisabled");
	stream->writeUint8(glassCannonLevel, "glassCannonLevel");
	stream->writeUint8(unitsFearless, "unitsFearless");
	stream->writeUint8(permadeathDisabled, "permadeathDisabled");
	stream->writeUint8(peacefulMode, "peacefulMode");
	stream->writeUint8(buildingHpLevel, "buildingHpLevel");
	writeCatalog(stream, buildingCatalogSnapshot);
	writeCatalog(stream, buildingArtwork ? buildingArtwork->bytes() : std::string{}, "buildingArtwork",MaxArtworkChunks);
	saveCatalogExperimentDefinitions(stream, resourceCatalogExperiments);
	experiments.save(stream);
	stream->writeLeaveSection();
}



bool GameHeader::loadPlayerInfo(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameHeader");
	numberOfPlayers = stream->readSint32("numberOfPlayers");
	if (numberOfPlayers < 0 || numberOfPlayers > Team::MAX_COUNT) return false;
	stream->readEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->readEnterSection(i);
		if (i < Team::MAX_COUNT)
		{
			if (!players[i].load(stream, versionMinor))
			{
				stream->readLeaveSection();
				stream->readLeaveSection();
				stream->readLeaveSection();
				return false;
			}
		}
		else
		{
			BasePlayer scratch;
			// Trailing on-disk slots beyond Team::MAX_COUNT are padding; their
			// fields are never consumed, so a bad value here is not a crash
			// hazard. Discard validation failures.
			scratch.load(stream, versionMinor);
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if (!loadAIConfig(stream, versionMinor)) return false;
	stream->readLeaveSection();
	return true;
}



void GameHeader::savePlayerInfo(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GameHeader");
	stream->writeSint32(numberOfPlayers, "numberOfPlayers");
	stream->writeEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->writeEnterSection(i);
		if (i < Team::MAX_COUNT)
			players[i].save(stream);
		else
			BasePlayer().save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	saveAIConfig(stream);
	stream->writeLeaveSection();
}


bool GameHeader::loadAIConfig(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	for (auto &values : aiConfig) values.clear();
	if (versionMinor < 101) return true;
	stream->readEnterSection("aiConfig");
	const Uint32 count = stream->readUint32("count");
	if (count > Team::MAX_COUNT) return false;
	for (Uint32 i=0; i<count; ++i)
	{
		stream->readEnterSection(i);
		aiConfig[i] = stream->readText("values");
		if (aiConfig[i].size() > 262144) return false;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return true;
}

void GameHeader::saveAIConfig(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("aiConfig");
	stream->writeUint32(Team::MAX_COUNT, "count");
	for (int i=0; i<Team::MAX_COUNT; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeText(aiConfig[i], "values");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}
