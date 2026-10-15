// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingType.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <stdexcept>
#include <algorithm>
#include <memory>

bool Team::load(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor)
{
    return loadTask(stream, buildingstypes, versionMinor).run();
}

GAGCore::CooperativeTask Team::loadTask(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor)
{
	assert(stream);
	assert(buildingsToBeDestroyed.size()==0);
	buildingsTryToBuildingSiteRoom.clear();

	// loading base team
	if(!BaseTeam::load(stream, versionMinor, &race))
		co_return false;

	stream->readEnterSection("Team");

	// Bind immutable definitions before any entity takes a descriptor pointer.
	// Older formats already bound their saved Race while reading BaseTeam.
	if (versionMinor >= FILE_FORMAT_VERSION_RACE_FIELD)
		race.setCatalog(game->gameHeader.getUnitCatalog());
	stats.configureUnits(race.unitTypeCount());

	// Current saves enumerate exact append order. Legacy formats encode the
	// allocation table; decoding those slots bootstraps their historical order.
	clearLists();
	clearMem();
	const bool ordered = versionMinor >= FILE_FORMAT_VERSION_INTRUSIVE_POPULATION;
	stream->readEnterSection("myUnits");
	const Uint32 unitRecords = ordered ? stream->readUint32("count") : Unit::MAX_COUNT;
	if (unitRecords > Unit::MAX_COUNT) throw std::runtime_error("Invalid unit population count");
	for (Uint32 record=0; record<unitRecords; ++record)
	{
		if ((record & 31)==0) co_await GAGCore::CooperativeTask::checkpoint("[Loading units]");
		stream->readEnterSection(record);
		const Uint32 slot = ordered ? stream->readUint16("slot") : record;
		const bool used = ordered || stream->readUint32("isUsed");
		if (slot >= Unit::MAX_COUNT || myUnits[slot]) throw std::runtime_error("Invalid or duplicate unit slot");
		if (used) {
			auto unit=std::make_unique<Unit>(stream,this,versionMinor);
			if (Unit::GIDtoID(unit->gid)!=slot || Unit::GIDtoTeam(unit->gid)!=teamNumber || unit->owner!=this)
				throw std::runtime_error("Unit identity does not match owner and slot");
			attachUnit(slot,unit.release());
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	prestige=0;
	stream->readEnterSection("myBuildings");
	const Uint32 buildingRecords = ordered ? stream->readUint32("count") : Building::MAX_COUNT;
	if (buildingRecords > Building::MAX_COUNT) throw std::runtime_error("Invalid building population count");
	for (Uint32 record=0; record<buildingRecords; ++record)
	{
		if ((record & 31)==0) co_await GAGCore::CooperativeTask::checkpoint("[Loading buildings]");
		stream->readEnterSection(record);
		const Uint32 slot = ordered ? stream->readUint16("slot") : record;
		const bool used = ordered || stream->readUint32("isUsed");
		if (slot >= Building::MAX_COUNT || myBuildings[slot]) throw std::runtime_error("Invalid or duplicate building slot");
		if (used) {
			auto building=std::make_unique<Building>(stream,buildingstypes,this,versionMinor);
			if (Building::GIDtoID(building->gid)!=slot || Building::GIDtoTeam(building->gid)!=teamNumber || building->owner!=this)
				throw std::runtime_error("Building identity does not match owner and slot");
			auto* b=building.get();
			attachBuilding(slot,building.release());
			if (!b->type->semantics.production.enabledUnits.empty()) swarms.push_back(b);
			if (b->type->shootingRange) turrets.push_back(b);
			if (b->type->canExchange) canExchange.push_back(b);
			if (b->type->isVirtual) virtualBuildings.push_back(b);
			if (b->runtime->attractsRole(0)) clearingFlags.push_back(b);
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	// Cross references are encoded after both populations exist. Old slot
	// formats bootstrap ascending order, so the same walk reads both lineages.
	stream->readEnterSection("myUnits");
	for (Unit* unit : liveUnits.entries()) {
		co_await GAGCore::CooperativeTask::checkpoint("[Resolving team links]");
		stream->readEnterSection(Unit::GIDtoID(unit->gid));
		unit->loadCrossRef(stream,this,versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readEnterSection("myBuildings");
	for (Building* building : liveBuildings.entries()) {
		co_await GAGCore::CooperativeTask::checkpoint("[Resolving team links]");
		stream->readEnterSection(Building::GIDtoID(building->gid));
		building->loadCrossRef(stream,buildingstypes,this,versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if (ordered) {
		taskSwapNext=stream->readUint16("taskSwapNext");
		if (taskSwapNext!=0xffff && (taskSwapNext>=Unit::MAX_COUNT || !myUnits[taskSwapNext]))
			throw std::runtime_error("Invalid population task cursor");
	}

	allies = stream->readUint32("allies");
	enemies = stream->readUint32("enemies");
	sharedVisionExchange = stream->readUint32("sharedVisionExchange");
	sharedVisionFood = stream->readUint32("sharedVisionFood");
	sharedVisionOther = stream->readUint32("sharedVisionOther");
	me = stream->readUint32("me");
	startPosX = stream->readSint32("startPosX");
	startPosY = stream->readSint32("startPosY");
	startPosSet = stream->readSint32("startPosSet");
	unitConversionLost = stream->readSint32("unitConversionLost");
	unitConversionGained = stream->readSint32("unitConversionGained");

	stream->readEnterSection("teamRessources");
	for (unsigned int i=0; i<MaterialSlotCount; ++i)
	{
		stream->readEnterSection(i);
		teamMaterials[i] = stream->readUint32("teamRessources");
		if (teamMaterials[i]<0) throw std::runtime_error("Invalid shared inventory");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	for(int i=0; i<GESize; ++i)
		eventCooldownTimers[i]=0;

	if (!stats.load(stream, versionMinor))
	{
		stream->readLeaveSection();
		co_return false;
	}
	if (versionMinor < FILE_FORMAT_VERSION_LIVE_TEAM_STATS)
		stats.step(this, true);

	if(versionMinor >= FILE_FORMAT_VERSION_RACE_FIELD)
	{
		if(!race.load(stream, versionMinor))
		{
			stream->readLeaveSection();
			co_return false;
		}
	}
	stats.configureUnits(race.unitTypeCount());

	isAlive = true;

	if (versionMinor >= FILE_FORMAT_VERSION_SIMULATION_CONTINUATION)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		auto readBuildings = [&](auto& list, const char* name) {
			stream->readEnterSection(name);
			const Uint32 count = stream->readUint32("count");
			if (count > Building::MAX_COUNT) throw std::runtime_error("Invalid team building list size");
			list.clear();
			// Preserve multiplicity too: existing live flag lists can contain
			// repeated references, and deduplicating would change scheduling.
			for (Uint32 i=0; i<count; ++i)
			{
				stream->readEnterSection(i);
				const Uint16 gid = stream->readUint16("gid");
				if (gid >= Building::MAX_COUNT * Team::MAX_COUNT || Building::GIDtoTeam(gid) != teamNumber || !myBuildings[Building::GIDtoID(gid)])
					throw std::runtime_error(std::string("Invalid team building list reference: ")+name+" team="+std::to_string(teamNumber)+" gid="+std::to_string(gid));
				list.push_back(myBuildings[Building::GIDtoID(gid)]);
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
		};
		readBuildings(canFeedUnit, "canFeedUnit");
		readBuildings(canHealUnit, "canHealUnit");
		readBuildings(canExchange, "canExchange");
		readBuildings(swarms, "swarms");
		readBuildings(turrets, "turrets");
		readBuildings(clearingFlags, "clearingFlags");
		readBuildings(virtualBuildings, "virtualBuildings");
		readBuildings(buildingsWaitingForDestruction, "buildingsWaitingForDestruction");
		readBuildings(buildingsToBeDestroyed, "buildingsToBeDestroyed");
		readBuildings(buildingsTryToBuildingSiteRoom, "buildingsTryToBuildingSiteRoom");
		for (int i=0; i<NB_ABILITY; ++i)
		{
			stream->readEnterSection(i);
			readBuildings(canUpgrade[i], "canUpgrade");
			stream->readLeaveSection();
		}
		buildingsNeedingUnits.clear();
		const Uint32 buckets = stream->readUint32("hiringBuckets");
		if (buckets > Building::MAX_COUNT) throw std::runtime_error("Invalid hiring bucket count");
		for (Uint32 i=0; i<buckets; ++i)
		{
			stream->readEnterSection(i);
			const Sint32 priority = stream->readSint32("priority");
			if (buildingsNeedingUnits.count(priority)) throw std::runtime_error("Duplicate hiring priority");
			readBuildings(buildingsNeedingUnits[priority], "buildings");
			stream->readLeaveSection();
		}
	}

	if (versionMinor >= FILE_FORMAT_VERSION_CONSTRUCTION_COOLDOWN)
	{
		noMoreBuildingSitesCountdown = stream->readSint32("noMoreBuildingSitesCountdown");
		if (noMoreBuildingSitesCountdown < 0 || noMoreBuildingSitesCountdown > noMoreBuildingSitesCountdownMax)
			throw std::runtime_error("Invalid construction cooldown");
	}

	if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG)
	{
		std::array<bool,Unit::MAX_COUNT> visits{};
		for (Building* building : liveBuildings.entries())
				for (Unit* unit : building->unitsInside)
				{
					const int id=Unit::GIDtoID(unit->gid);
					const int purpose=unit->destinationPurpose;
					const auto& interaction=building->runtime->interaction(unit->typeNum);
					const bool validService=purpose==FEED ? interaction.has(BuildingUnitInteraction::Feeds) :
						purpose==HEAL ? interaction.has(BuildingUnitInteraction::Heals) :
						purpose>=0 && purpose<NB_ABILITY && (interaction.trainingMask & (1u<<purpose));
					// Historical saves could retain an unassigned occupancy entry
					// in unitsInside (the format-88 entering-animation reproducer
					// has ACT_RANDOM and purpose -1). Preserve that opaque entry
					// through current resaves only with private migration provenance.
					// Authored catalogs and all declared visits stay strictly checked.
					const bool legacyOpaqueVisit=unit->hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies)
						&& unit->activity==Unit::ACT_RANDOM && purpose==-1 && !unit->serviceResourcesReserved();
					if (visits[id] || unit->attachedBuilding!=building
						|| (!legacyOpaqueVisit && (unit->activity!=Unit::ACT_UPGRADING || !validService))
						|| (unit->serviceResourcesReserved() && unit->displacement==Unit::DIS_EXITING_BUILDING))
						throw std::runtime_error("Invalid saved building service membership");
					visits[id]=true;
				}
		for (Unit* unit : liveUnits.entries())
			if (unit->serviceResourcesReserved() && !visits[Unit::GIDtoID(unit->gid)])
				throw std::runtime_error("Saved unit reservation has no building service visit");
	}

	std::fill_n(reservedTeamMaterials, MaterialSlotCount, 0);
	stockSuppliers.clear();
	directStockSuppliers.clear();
	combatFlags.clear();
	for (Building* b : liveBuildings.entries())
		{
			if (b->type->runtimeSuppliesStock && b->buildingState == Building::ALIVE) stockSuppliers.push_back(b);
			if (b->type->runtimeSuppliesDirectStock && b->buildingState == Building::ALIVE) directStockSuppliers.push_back(b);
			if (b->runtime->attractsRole(2)) combatFlags.push_back(b);
			b->reservedMaterials.fill(0);
			b->restoreServiceReservations();
			b->restoreProductionReservations();
            const bool cappedPrivate=!b->usesTeamMaterials() && b->privateStockClampedOnLoad;
            if(cappedPrivate) {
                // Services and committed production retain their existing load
                // priority. Keep only construction funding still backed by stock.
                for(int r=0;r<MaterialSlotCount;++r)
                    b->constructionReserved[r]=std::min(b->constructionReserved[r],b->availableMaterial(r));
                if(b->constructionReserved!=b->constructionBudget) b->siteCompletionPending=false;
            }
			b->restoreConstructionReservations();
            if(cappedPrivate) b->updateCallLists();
            b->privateStockClampedOnLoad=false;
		}
	stream->readLeaveSection();
	co_return true;
}




void Team::save(GAGCore::OutputStream *stream)
{
	// saving base team
	BaseTeam::save(stream);

	stream->writeEnterSection("Team");

	// Entity record order is simulation state; never derive it from free IDs.
	stream->writeEnterSection("myUnits");
	stream->writeUint32(liveUnits.size(),"count");
	unsigned record=0;
	for (Unit* unit : liveUnits.entries()) {
		stream->writeEnterSection(record++);
		stream->writeUint16(Unit::GIDtoID(unit->gid),"slot");
		unit->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("myBuildings");
	stream->writeUint32(liveBuildings.size(),"count");
	record=0;
	for (Building* building : liveBuildings.entries()) {
		stream->writeEnterSection(record++);
		stream->writeUint16(Building::GIDtoID(building->gid),"slot");
		building->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("myUnits");
	for (Unit* unit : liveUnits.entries()) {
		stream->writeEnterSection(Unit::GIDtoID(unit->gid));
		unit->saveCrossRef(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("myBuildings");
	for (Building* building : liveBuildings.entries()) {
		stream->writeEnterSection(Building::GIDtoID(building->gid));
		building->saveCrossRef(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint16(taskSwapNext,"taskSwapNext");

	stream->writeUint32(allies, "allies");
	stream->writeUint32(enemies, "enemies");
	stream->writeUint32(sharedVisionOther, "sharedVisionExchange");
	stream->writeUint32(sharedVisionFood, "sharedVisionFood");
	stream->writeUint32(sharedVisionOther, "sharedVisionOther");
	stream->writeUint32(me, "me");
	stream->writeSint32(startPosX, "startPosX");
	stream->writeSint32(startPosY, "startPosY");
	stream->writeSint32(startPosSet, "startPosSet");
	stream->writeSint32(unitConversionLost, "unitConversionLost");
	stream->writeSint32(unitConversionGained, "unitConversionGained");

	stream->writeEnterSection("teamRessources");
	for (unsigned int i=0; i<MaterialSlotCount; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(teamMaterials[i], "teamRessources");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stats.save(stream);
	race.save(stream);

	auto writeBuildings = [&](const auto& list, const char* name) {
		stream->writeEnterSection(name);
		stream->writeUint32(list.size(), "count");
		unsigned i=0;
		for (const auto* building : list)
		{
			stream->writeEnterSection(i++);
			stream->writeUint16(building->gid, "gid");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	};
	writeBuildings(canFeedUnit, "canFeedUnit");
	writeBuildings(canHealUnit, "canHealUnit");
	writeBuildings(canExchange, "canExchange");
	writeBuildings(swarms, "swarms");
	writeBuildings(turrets, "turrets");
	writeBuildings(clearingFlags, "clearingFlags");
	writeBuildings(virtualBuildings, "virtualBuildings");
	writeBuildings(buildingsWaitingForDestruction, "buildingsWaitingForDestruction");
	writeBuildings(buildingsToBeDestroyed, "buildingsToBeDestroyed");
	writeBuildings(buildingsTryToBuildingSiteRoom, "buildingsTryToBuildingSiteRoom");
	for (int i=0; i<NB_ABILITY; ++i)
	{
		stream->writeEnterSection(i);
		writeBuildings(canUpgrade[i], "canUpgrade");
		stream->writeLeaveSection();
	}
	stream->writeUint32(buildingsNeedingUnits.size(), "hiringBuckets");
	unsigned bucket=0;
	for (const auto& entry : buildingsNeedingUnits)
	{
		stream->writeEnterSection(bucket++);
		stream->writeSint32(entry.first, "priority");
		writeBuildings(entry.second, "buildings");
		stream->writeLeaveSection();
	}

	stream->writeSint32(noMoreBuildingSitesCountdown, "noMoreBuildingSitesCountdown");


	stream->writeLeaveSection();
}




Uint32 Team::checkSum(std::vector<Uint32> *checkSumsVector, std::vector<Uint32> *checkSumsVectorForBuildings, std::vector<Uint32> *checkSumsVectorForUnits, bool legacy152)
{
	Uint32 cs=0;

	cs^=BaseTeam::checkSum();
	if (!legacy152) { cs=rotr1(cs)^taskSwapNext; cs=rotr1(cs)^liveUnits.size(); cs=rotr1(cs)^liveBuildings.size(); }
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [1+t*20]

	for (Unit *unit : liveUnits.entries())
	{
		cs^=unit->checkSum(checkSumsVectorForUnits,legacy152);
		cs=rotr1(cs);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [2+t*20]

	for (Building *building : liveBuildings.entries())
	{
		cs^=building->checkSum(checkSumsVectorForBuildings,legacy152);
		cs=rotr1(cs);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [3+t*20]

	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=canUpgrade[i].size();
		cs=rotr1(cs);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [4+t*20]

	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [7+t*20]

	cs^=canExchange.size();
	cs^=canFeedUnit.size();
	cs^=canHealUnit.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [8+t*20]

	cs^=buildingsToBeDestroyed.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [9+t*20]
	cs^=buildingsTryToBuildingSiteRoom.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [10+t*20]

	cs^=swarms.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [11+t*20]
	cs^=turrets.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [12+t*20]

	cs^=allies;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [13+t*20]
	cs^=enemies;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [14+t*20]
	cs^=sharedVisionExchange;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [15+t*20]
	cs^=sharedVisionFood;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [16+t*20]
	cs^=sharedVisionOther;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [17+t*20]
	cs^=me;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [18+t*20]

	cs^=noMoreBuildingSitesCountdown;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [19+t*20]

	cs^=prestige;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [20+t*20]

	return cs;
}
