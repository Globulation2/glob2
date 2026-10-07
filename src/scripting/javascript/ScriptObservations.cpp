#include <bit>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptObservations.h"
#include "ScriptBuildingCapabilities.h"
#include "FileFormatVersions.h"
#include "TerrainProperties.h"
#include "TerrainPresentation.h"
#include "TerrainExperiments.h"
#include "Game.h"
#include "GameRuleOverrides.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "Stream.h"
#include <algorithm>
#include <stdexcept>
namespace Script
{
namespace
{
Value numbers(const Sint32 *values, int n)
{
	Value a = Value::array();
	for (int i = 0; i < n; ++i)
		a.items.emplace_back(values[i]);
	return a;
}
bool visible(Game &game, int team, const Unit &u)
{
	return team < 0 || u.owner->teamNumber == team ||
		   (u.insideTimeout >= 0 &&
			(game.map.isFOWDiscovered(u.posX, u.posY, game.teams[team]->me) ||
			 game.map.isFOWDiscovered(u.posX - u.dx, u.posY - u.dy, game.teams[team]->me)));
}
bool visible(Game &game, int team, const Building &b)
{
	return team < 0 || b.owner->teamNumber == team ||
		   (!b.type->isCloaked && game.map.isFOWDiscovered(b.posX, b.posY, game.teams[team]->me));
}
int arg(const std::vector<Value> &a, size_t i, int lo, int hi)
{
	if (i >= a.size())
		throw std::runtime_error("Missing query argument");
	return Value::object().set("value", a[i]).integer("value", lo, hi);
}
} // namespace
Value Observations::ref(const Unit *u) const
{
	if (!u)
		return {};
	return Value::object().set("id", unsigned(u->gid)).set("generation", u->scriptIdentity);
}
Value Observations::ref(const Building *b) const
{
	if (!b)
		return {};
	return Value::object().set("id", unsigned(b->gid)).set("generation", b->scriptIdentity);
}
Value Observations::unit(const Unit &u) const
{
	if (!visible(game, team, u) || u.isDead)
		return {};
	Value v = ref(&u);
	v.set("team", u.owner->teamNumber)
		.set("type", u.typeNum)
		.set("x", u.posX)
		.set("y", u.posY)
		.set("hp", u.hp)
		.set("maxHp", u.performance[HP])
		.set("levels", numbers(u.level, NB_ABILITY))
		.set("performance", numbers(u.performance, NB_ABILITY));
	if (team < 0 || u.owner->teamNumber == team)
	{
		v.set("experience", u.experience)
			.set("experienceLevel", u.experienceLevel)
			.set("hunger", u.hungry)
			.set("fruitCount", u.fruitCount);
		v.set("activity", int(u.activity))
			.set("movement", int(u.movement))
			.set("action", int(u.action))
			.set("medical", int(u.medical))
			.set("insideTimeout", u.insideTimeout)
			.set("carriedMaterial", u.carriedMaterial)
			.set("carriedResource", u.carriedMaterial)
			.set("speed", u.speed)
			.set("direction", u.direction)
			.set("fruitMask", u.fruitMask)
			.set("destinationPurpose", u.destinationPurpose)
			.set("targetX", u.targetX)
			.set("targetY", u.targetY);
		v.set("attachedBuilding", u.attachedBuilding && visible(game, team, *u.attachedBuilding)
									  ? ref(u.attachedBuilding)
									  : Value());
		v.set("targetBuilding", u.targetBuilding && visible(game, team, *u.targetBuilding)
									? ref(u.targetBuilding)
									: Value());
	}
	return v;
}
Value Observations::building(const Building &b) const
{
	if (!visible(game, team, b) || b.buildingState == Building::DEAD)
		return {};
	Value v = ref(&b);
	v.set("team", b.owner->teamNumber)
		.set("type", b.typeNum)
		.set("shortType", b.shortTypeNum)
		.set("key", b.type->key)
		.set("capabilities", buildingCapabilities(game, b.typeNum))
		.set("relocatable", b.type->semantics.relocatable)
		.set("interTeamExchange", b.type->semantics.market.interTeamFruitExchange)
		.set("x", b.posX)
		.set("y", b.posY)
		.set("hp", b.hp)
		.set("maxHp", b.getEffectiveMaxHp())
		.set("level", b.type->level)
		.set("virtual", bool(b.type->isVirtual))
		.set("construction", int(b.constructionResultState));
	if (team < 0 || b.owner->teamNumber == team)
	{
		v.set("workers", b.maxUnitWorking)
			.set("futureWorkers", b.getMaxUnitWorkingFuture())
			.set("priority", b.priority)
			.set("range", b.unitStayRange)
			.set("minimumLevel", b.minLevelToFlag)
			.set("requireBombing", b.explorersRequireBombing)
			.set("workerMinimumLevel", b.minWorkerLevelToFlag)
			.set("materials", numbers(b.materials, MaterialCount))
			.set("resources", numbers(b.materials, MaterialSlotCount))
			.set("wishedMaterials", numbers(b.wishedMaterials, MaterialCount))
			.set("wishedResources", numbers(b.wishedMaterials, MaterialSlotCount))
			.set("production", numbers(b.ratio, NB_UNIT_TYPE))
			.set("productionTimeout", b.productionTimeout)
			.set("receiveMask", b.receiveMaterialMask)
			.set("sendMask", b.sendMaterialMask)
			.set("bullets", b.bullets);
		Value a = Value::array();
		for (int i = 0; i < BASIC_COUNT; ++i)
			a.items.emplace_back(b.clearingMaterials[i]);
		v.set("clearingResources", a);
        for (unsigned i=BASIC_COUNT;i<MaterialCount;++i) a.items.emplace_back(b.clearingMaterials[i]);
        v.set("clearingMaterials", a);
	}
	return v;
}
const Observations::RememberedTile *Observations::lookup(unsigned index) const
{
	unsigned chunk = index / 256;
	if (chunk >= remembered.size() || !remembered[chunk])
		return nullptr;
	const auto &tile = (*remembered[chunk])[index % 256];
	return tile.known ? &tile : nullptr;
}
Observations::RememberedTile &Observations::remember(unsigned index)
{
	if (remembered.empty())
		remembered.resize((unsigned(game.map.getW()) * game.map.getH() + 255) / 256);
	auto &chunk = remembered.at(index / 256);
	if (!chunk)
		chunk = std::make_unique<Chunk>();
	auto &tile = (*chunk)[index % 256];
	if (!tile.known)
		++knownTiles;
	return tile;
}
void Observations::observe()
{
	if (team < 0 || lastTick == game.stepCounter)
		return;
	const unsigned mask = game.teams[team]->me;
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
			if (game.map.isFOWDiscovered(x, y, mask))
			{
				const auto &t = game.map.getTile(x, y);
				const auto &r = t.resource;
				remember(unsigned(game.map.coordToIndex(x, y))) = {
					game.stepCounter, t.terrain, t.fertility, game.map.terrainTypeAt(x,y), r.type, r.variety, r.amount, true};
				const auto index = unsigned(game.map.coordToIndex(x,y));
				if (r.type != NO_RES_TYPE && std::popcount(game.map.resourcePropertiesByIndex(r.type).materialMask) > 1)
					rememberedStocks[index] = game.map.materialStocksAt(index);
				else rememberedStocks.erase(index);
			}
	lastTick = game.stepCounter;
}
Value Observations::tile(int x, int y) const
{
	x &= game.map.getW() - 1;
	y &= game.map.getH() - 1;
	const bool current = team < 0 || game.map.isFOWDiscovered(x, y, game.teams[team]->me);
	Value v = Value::object().set("x", x).set("y", y).set("visible", current);
	RememberedTile t;
	if (current)
	{
		const auto &c = game.map.getTile(x, y);
		t = {game.stepCounter, c.terrain, c.fertility, game.map.terrainTypeAt(x,y),
			 c.resource.type, c.resource.variety, c.resource.amount};
	}
	else
	{
		auto *previous = lookup(unsigned(game.map.coordToIndex(x, y)));
		if (!previous)
			return v.set("explored", false);
		t = *previous;
	}
	v.set("explored", true)
		.set("observedTick", t.tick)
		.set("terrain", int(t.terrain))
		.set("terrainType", int(t.terrainType))
		.set("resource", Value::object()
							 .set("type", int(t.type))
							 .set("variety", int(t.variety))
							 .set("amount", int(t.amount)));
	Value stock = Value::array();
	const auto at = game.map.coordToIndex(x,y);
	const auto rememberedStock = rememberedStocks.find(unsigned(at));
	for (unsigned m=0; m<MaterialCount; ++m)
	{
		unsigned amount = 0;
		if (current) amount = game.map.materialAmountAtSlot(at,m);
		else if (rememberedStock != rememberedStocks.end()) amount = rememberedStock->second[m];
		else if (t.type != NO_RES_TYPE && materialIndex(game.map.resourcePropertiesByIndex(t.type).primaryMaterial)==m) amount=t.amount;
		stock.items.emplace_back(amount);
	}
	v.set("materialStocks", stock);
	if (team < 0 || profile == 2)
		v.set("fertility", int(t.fertility));
	if (current)
	{
		const auto &c = game.map.getTile(x, y);
		auto unitId = [&](unsigned id)
		{
			if (id >= Unit::MAX_COUNT * Team::MAX_COUNT)
				return 65535u;
			int owner = Unit::GIDtoTeam(id);
			if (owner >= game.mapHeader.getNumberOfTeams())
				return 65535u;
			auto *u = game.teams[owner]->myUnits[Unit::GIDtoID(id)];
			return u && !u->isDead && visible(game, team, *u) ? id : 65535u;
		};
		unsigned buildingId = c.building;
		int owner = buildingId < Building::MAX_COUNT * Team::MAX_COUNT
						? Building::GIDtoTeam(buildingId)
						: Team::MAX_COUNT;
		auto *b = owner < game.mapHeader.getNumberOfTeams()
					  ? game.teams[owner]->myBuildings[Building::GIDtoID(buildingId)]
					  : nullptr;
		if (!b || !visible(game, team, *b))
			buildingId = 65535;
		v.set("groundUnit", unitId(c.groundUnit))
			.set("airUnit", unitId(c.airUnit))
			.set("building", buildingId);
	}
	if (team >= 0)
	{
		v.set("forbidden", bool(game.map.getForbidden(x, y) & game.teams[team]->me));
		// Only present where the team painted a farm (the farm-areas experiment), so
		// tile records in games without it are unchanged.
		if (game.map.farmAreasEnabled() && game.map.isFarmArea(x, y, game.teams[team]->me))
			v.set("farmArea", true);
	}
	return v;
}
Value Observations::query(const std::string &name, const std::vector<Value> &args,
						  const QueryBudget &budget) const
{
	auto charge = [&](std::size_t nodes, std::size_t bytes = 0)
	{
		if (budget)
			budget(nodes, nodes * NativeValueCost + bytes);
	};
    if (name == "rules")
    {
        Value rules=Value::object();
        for(const auto& [key,value]:gameRuleValues(game.gameHeader))
        { charge(1,key.size()); rules.set(key,value); }
        return rules;
    }
	if (name == "experiments")
	{
		// Keys of the experiments this game carries (ExperimentalFeatures.h).
		Value a = Value::array();
		for (const auto &key : game.gameHeader.getExperiments().keys())
		{
			charge(1, key.size());
			a.items.push_back(Value(key));
		}
		return a;
	}
	if (name == "materialTypes")
	{
		Value result=Value::array(); charge(MaterialCount*2);
		for (unsigned m=0; m<MaterialCount; ++m)
			result.items.push_back(Value::object().set("id",m).set("key",std::string(MaterialKeys[m])));
		return result;
	}
	if (name == "resourceTypes")
	{
		const auto& registry=game.map.resourceRegistry();
		charge(registry.size()*48);
		const auto snapshot = game.map.frozenResourceRegistry();
		if (resourceDefinitionRegistry == snapshot) return resourceDefinitions;
		Value result=Value::array();
		for (unsigned id=0; id<registry.size(); ++id)
		{
			const auto resource=static_cast<ResourceId>(id);
			const auto& p=registry.properties(resource);
			Value yields=Value::array();
			for (unsigned m=0; m<MaterialCount; ++m)
			{
				const auto& y=registry.yields(resource)[m];
				if (y.capacity) yields.items.push_back(Value::object().set("material",m).set("capacity",y.capacity)
					.set("initial",y.initial).set("growthRate",y.growthRate).set("consumption",int(y.consumption))
					.set("seedReserve", y.seedReserve).set("destroysDeposit", y.destroysDeposit).set("placementMaximum", y.placementMaximum));
			}
			result.items.push_back(Value::object().set("id",id).set("key",registry.key(resource))
				.set("name",registry.presentation(resource).name).set("yields",yields)
				.set("blocksGround",p.blocksGround).set("blocksAir",p.blocksAir).set("blocksBuilding",p.blocksBuilding)
				.set("clearable",p.clearable).set("farmable",p.farmable).set("spreadRate",p.spreadRate)
				.set("primaryMaterial", materialIndex(p.primaryMaterial)).set("growthRate", p.growthRate)
				.set("ecology", int(p.ecology)).set("habitatMask", p.habitatMask).set("visibleToHarvest", p.visibleToHarvest)
				.set("persistsWhenEmpty", p.persistsWhenEmpty).set("stockDependentGrowth", p.stockDependentGrowth)
				.set("stockBranchDivisor", p.stockBranchDivisor).set("clearConsumption", int(p.clearConsumption))
				.set("requiresGrowthTerrain", p.requiresGrowthTerrain).set("requiresPermanentDepositsTerrain", p.requiresPermanentDepositsTerrain)
				.set("requiredExperiment", registry.requiredExperiment(resource)));
		}
		resourceDefinitionRegistry = snapshot;
		resourceDefinitions = std::move(result);
		return resourceDefinitions;
	}
    if (name == "terrainTypes")
    {
		// Registry definitions reveal no tile state. Cache per immutable registry;
		// callers receive detached, read-only JS snapshots in both profiles.
		charge(game.map.terrainRegistry().size() * (48 + game.map.resourceRegistry().size()));
		if (terrainDefinitionRegistry != game.map.frozenTerrainRegistry() ||
            terrainResourceDefinitionRegistry != game.map.frozenResourceRegistry())
		{
			terrainDefinitions = [&]
			{
				Value result = Value::array();
				for (unsigned id = 0; id < game.map.terrainRegistry().size(); ++id)
				{
					const auto type = static_cast<TerrainType>(id);
					const auto &p = game.map.terrainProperties(type);
					const auto &presentation = game.map.terrainPresentation(type);
					const auto experiment = terrainExperiment(type);
					Value resources = Value::array();
					for (unsigned resource = 0; resource < game.map.resourceRegistry().size(); ++resource)
						if (game.map.terrainSupportsResourceType(type, static_cast<ResourceId>(resource)))
							resources.items.emplace_back(resource);
					result.items.push_back(
						Value::object()
							.set("id", id)
							.set("name", presentation.name)
							.set("group", id < TERRAIN_COUNT
											 ? Value(terrainGroupDefinition(terrainGroup(type)).key)
											 : Value())
							.set("experiment", experiment
												   ? Value(experimentDefinition(*experiment).key)
												   : Value())
							.set("editorSelectable", presentation.editorSelectable)
							.set("walkable", p.walkable)
							.set("swimmable", p.swimmable)
							.set("flyable", p.flyable)
							.set("resourcesGrow", p.resourcesGrow)
							.set("fertilitySource", p.fertilitySource)
							.set("nonGrowingResources", p.nonGrowingResources)
							.set("buildable", p.buildable)
							.set("projectileBlocks", p.projectileBlocks)
							.set("shoreline", p.shoreline)
							.set("groundSpeedQ8", int(p.groundSpeedQ8))
							.set("airSpeedQ8", int(p.airSpeedQ8))
							.set("groundHealthQ8", int(p.groundHealthQ8))
							.set("airHealthQ8", int(p.airHealthQ8))
							.set("growthQ8", int(p.growthQ8))
							.set("fertilityQ8", int(p.fertilityQ8))
							.set("inhibitionQ8", int(p.inhibitionQ8))
							.set("shoreSupportQ8", int(p.shoreSupportQ8))
							.set("allowedResources", resources)
							.set("farmMaterial", p.farmMaterial == 255 ? Value() : Value(std::string(materialKey(static_cast<MaterialId>(p.farmMaterial))))));
				}
				return result;
			}();
			terrainDefinitionRegistry = game.map.frozenTerrainRegistry();
            terrainResourceDefinitionRegistry = game.map.frozenResourceRegistry();
		}
		return terrainDefinitions;
	}
	if (name == "buildingTypes")
	{
		Value a = Value::array();
		const Value options = args.empty() ? Value::object() : args[0];
        const int offset = options.get("offset").kind == Value::Null ? 0 : options.integer("offset", 0, int(game.buildingsTypes.size()));
        const int limit = options.get("limit").kind == Value::Null ? int(game.buildingsTypes.size()) : options.integer("limit", 1, int(game.buildingsTypes.size()));
        for (unsigned i = offset; i < game.buildingsTypes.size() && i < unsigned(offset + limit); ++i)
		{
			if (!game.isBuildingTypeAvailable(i)) continue;
			const auto &b = *game.buildingsTypes.get(i);
			charge(256 + NB_ABILITY * (MaterialSlotCount + 8), b.type.size() + b.key.size());
            auto service = [&](const BuildingServiceSpec& spec) {
                return Value::object().set("enabled", spec.enabled).set("unitMask", spec.unitMask)
                    .set("duration", spec.duration).set("cost", numbers(spec.cost.data(), MaterialSlotCount));
            };
            Value training = Value::array(), production = Value::array();
            for (const auto& spec : b.semantics.training)
                training.items.push_back(Value::object().set("enabled", spec.enabled).set("unitMask", spec.unitMask)
                    .set("targetLevel", spec.targetLevel).set("constructionLevel", spec.constructionLevel)
                    .set("duration", spec.duration).set("cost", numbers(spec.cost.data(), MaterialSlotCount)));
            for (const auto& spec : b.semantics.production.recipes)
                production.items.push_back(Value::object().set("enabled", spec.enabled).set("duration", spec.duration)
                    .set("cost", numbers(spec.cost.data(), MaterialSlotCount)));
			a.items.push_back(
				Value::object()
					.set("id", i)
                    .set("key", b.key).set("nextType", b.nextLevel).set("previousType", b.prevLevel)
                    .set("placeable", b.semantics.placeable).set("instantPlacement", b.semantics.instantPlacement)
                    .set("requiredWorkerLevel", b.semantics.requiredWorkerLevel)
                    .set("admittedUnitMask", b.semantics.admittedUnitMask)
                    .set("maxUnitsInside", b.maxUnitInside).set("maxRadius", b.maxUnitStayRange)
                    .set("relocatable", b.semantics.relocatable).set("occupiesGround", b.semantics.occupiesGround)
                    .set("repairable", b.semantics.repairable).set("regeneration", b.semantics.regenerationPerTick)
                    .set("feeding", service(b.semantics.feeding)).set("healing", service(b.semantics.healing))
                    .set("training", training).set("production", production)
                    .set("capabilities", buildingCapabilities(game, i))
                    .set("projectileDamage", numbers(b.semantics.projectileDamage.data(), NB_UNIT_TYPE))
                    .set("projectileRange", b.shootingRange).set("projectileSpeed", b.shootSpeed).set("projectileRhythm", b.shootRhythm)
                    .set("ammunitionMaterial", b.semantics.ammunitionMaterial).set("ammunitionResource", b.semantics.ammunitionMaterial).set("ammunitionCost", b.semantics.ammunitionCost)
                    .set("suppliesStock", b.runtimeSuppliesStock).set("fetchesStock", b.runtimeFetchesStock)
                    .set("suppliesDirectStock", b.semantics.market.suppliesDirectStock)
                    .set("exchangesFruit", b.semantics.market.interTeamFruitExchange)
                    .set("name", b.type)
					.set("shortType", b.shortTypeNum)
					.set("level", b.level)
					.set("site", bool(b.isBuildingSite))
					.set("virtual", bool(b.isVirtual))
					.set("width", b.width)
					.set("height", b.height)
					.set("maxHp", b.hpMax)
					.set("maxWorkers", b.semantics.assignmentLimit).set("usesWorkers", bool(b.maxUnitWorking))
					.set("materialCapacity", numbers(b.maxMaterial, MaterialCount))
					.set("resourceCapacity", numbers(b.maxMaterial, MaterialSlotCount)));
		}
		return a;
	}
	if (name == "teams")
	{
		Value a = Value::array();
		for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		{
			const auto &tm = *game.teams[t];
			charge(64);
			Value v = Value::object().set("id", t).set("alive", tm.isAlive);
			if (team < 0 || team == t)
				v.set("allies", tm.allies)
					.set("materials", numbers(tm.teamMaterials, MaterialCount))
					.set("resources", numbers(tm.teamMaterials, MaterialSlotCount));
			a.items.push_back(v);
		}
		return a;
	}
	if (name == "tile")
	{
		charge(64);
		return tile(arg(args, 0, -32768, 32767), arg(args, 1, -32768, 32767));
	}
	if (name == "region")
	{
		int x = arg(args, 0, -32768, 32767), y = arg(args, 1, -32768, 32767),
			w = arg(args, 2, 0, 256), h = arg(args, 3, 0, 256);
		Value a = Value::array();
		for (int yy = 0; yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx)
			{
				charge(64);
				a.items.push_back(tile(x + xx, y + yy));
			}
		return a;
	}
	if (name == "units" || name == "buildings")
	{
		int filter = -1, offset = 0, limit = 32768;
		if (!args.empty())
		{
			if (args[0].get("team").kind != Value::Null)
				filter = args[0].integer("team", 0, game.mapHeader.getNumberOfTeams() - 1);
			if (args[0].get("offset").kind != Value::Null)
				offset = args[0].integer("offset", 0, 32768);
			if (args[0].get("limit").kind != Value::Null)
				limit = args[0].integer("limit", 0, 32768);
		}
		if (!limit)
			return Value::array();
		Value a = Value::array();
		for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
			if (filter < 0 || t == filter)
				for (int i = 0; i < (name == "units" ? Unit::MAX_COUNT : Building::MAX_COUNT); ++i)
				{
					Value v;
					if (name == "units")
					{
						auto *u = game.teams[t]->myUnits[i];
						if (!u || u->isDead || !visible(game, team, *u))
							continue;
						if (offset)
						{
							--offset;
							continue;
						}
						charge(160);
						v = unit(*u);
					}
					else
					{
						auto *b = game.teams[t]->myBuildings[i];
						if (!b || b->buildingState == Building::DEAD || !visible(game, team, *b))
							continue;
						if (offset)
						{
							--offset;
							continue;
						}
						charge(160);
						v = building(*b);
					}
					a.items.push_back(std::move(v));
					if (a.items.size() == unsigned(limit))
						return a;
				}
		return a;
	}
	if (name == "unit" || name == "building")
	{
		if (args.size() != 1)
			throw std::runtime_error("Entity lookup needs a reference");
		int id = args[0].integer("id", 0, 65535);
		if (id >= (name == "unit" ? Unit::MAX_COUNT : Building::MAX_COUNT) * Team::MAX_COUNT)
			return {};
		int owner = name == "unit" ? Unit::GIDtoTeam(id) : Building::GIDtoTeam(id);
		if (owner >= game.mapHeader.getNumberOfTeams())
			return {};
		const Value &generation = args[0].get("generation");
		if (generation.kind != Value::Number)
			return {};
		if (name == "unit")
		{
			auto *u = game.teams[owner]->myUnits[Unit::GIDtoID(id)];
			if (u && !u->isDead && visible(game, team, *u) &&
				generation.number == ref(u).get("generation").number)
			{
				charge(160);
				return unit(*u);
			}
		}
		else
		{
			auto *b = game.teams[owner]->myBuildings[Building::GIDtoID(id)];
			if (b && b->buildingState != Building::DEAD && visible(game, team, *b) &&
				generation.number == ref(b).get("generation").number)
			{
				charge(160);
				return building(*b);
			}
		}
		return {};
	}
	if (team >= 0)
		throw std::runtime_error("Scenario query requires map-script capability");
	if (name == "objectives")
	{
		Value a = Value::array();
		for (int i = 0; i < game.objectives.getNumberOfObjectives(); ++i)
		{
			charge(32, game.objectives.getGameObjectiveText(i).size());
			a.items.push_back(Value::object()
								  .set("id", i)
								  .set("scriptNumber", game.objectives.getScriptNumber(i))
								  .set("text", game.objectives.getGameObjectiveText(i))
								  .set("type", int(game.objectives.getObjectiveType(i)))
								  .set("visible", game.objectives.isObjectiveVisible(i))
								  .set("complete", game.objectives.isObjectiveComplete(i))
								  .set("failed", game.objectives.isObjectiveFailed(i)));
		}
		return a;
	}
	if (name == "hints")
	{
		Value a = Value::array();
		for (int i = 0; i < game.gameHints.getNumberOfHints(); ++i)
		{
			charge(32, game.gameHints.getGameHintText(i).size());
			a.items.push_back(Value::object()
								  .set("id", i)
								  .set("scriptNumber", game.gameHints.getScriptNumber(i))
								  .set("text", game.gameHints.getGameHintText(i))
								  .set("visible", game.gameHints.isHintVisible(i)));
		}
		return a;
	}
	throw std::runtime_error("Unknown observation query");
}
void Observations::save(GAGCore::OutputStream *s) const
{
	s->writeEnterSection("scriptObservations");
	s->writeUint32(lastTick, "tick");
	s->writeUint32(knownTiles, "count");
	unsigned n = 0;
	for (unsigned index = 0; index < remembered.size() * 256; ++index)
	{
		auto *entry = lookup(index);
		if (!entry)
			continue;
		const auto &t = *entry;
		s->writeEnterSection(n++);
		s->writeUint32(index, "index");
		s->writeUint32(t.tick, "tick");
		s->writeUint16(t.terrain, "terrain");
		s->writeUint16(t.fertility, "fertility");
		s->writeUint16(t.type, "type");
		s->writeUint8(t.variety, "variety");
		s->writeUint32(t.amount, "amount");
		s->writeUint16(t.terrainType, "terrainType");
		const auto stocks=rememberedStocks.find(index);
		s->writeUint8(stocks!=rememberedStocks.end(),"multiStock");
		if (stocks != rememberedStocks.end())
		{
			s->writeEnterSection("materialStocks");
			for (unsigned m = 0; m < MaterialCount; ++m)
			{
				s->writeEnterSection(m);
				s->writeUint16(stocks->second[m], "stock");
				s->writeLeaveSection();
			}
			s->writeLeaveSection();
		}
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
}
void Observations::load(GAGCore::InputStream *s, int version)
{
	remembered.clear();
	rememberedStocks.clear();
	knownTiles = 0;
	s->readEnterSection("scriptObservations");
	lastTick = s->readUint32("tick");
	unsigned n = s->readUint32("count");
	unsigned size = unsigned(game.map.getW()) * game.map.getH();
	if (n > size)
		throw std::runtime_error("Invalid observed terrain count");
	for (unsigned i = 0; i < n; ++i)
	{
		s->readEnterSection(i);
		unsigned index = s->readUint32("index");
		RememberedTile t;
		t.tick = s->readUint32("tick");
		t.terrain = s->readUint16("terrain");
		t.fertility = s->readUint16("fertility");
		t.type = version >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES ? s->readUint16("type") : s->readUint8("type");
		if (version < FILE_FORMAT_VERSION_RUNTIME_RESOURCES && t.type==255) t.type=NO_RES_TYPE;
		t.variety = s->readUint8("variety");
		t.amount = version >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES ? s->readUint32("amount") : s->readUint8("amount");
        // Legacy clearing sometimes retained unused variety/amount bytes after
        // setting the no-resource sentinel; they are not inventory or map stock.
        if (version < FILE_FORMAT_VERSION_RUNTIME_RESOURCES && t.type == NO_RES_TYPE)
        { t.variety = 0; t.amount = 0; }
		const unsigned terrainType = version >= FILE_FORMAT_VERSION_TERRAIN_PROPERTIES
			? s->readUint16("terrainType") : unsigned(legacyTerrainType(t.terrain));
		if (!game.map.validTerrainType(terrainType))
			throw std::runtime_error("Invalid remembered terrain type");
		t.terrainType = static_cast<TerrainType>(terrainType);
		if (index >= size || lookup(index))
			throw std::runtime_error("Invalid terrain memory");
		if(t.type!=NO_RES_TYPE && !game.map.resourceRegistry().valid(t.type)) throw std::runtime_error("Invalid remembered resource");
		const auto multiStock = version >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES ? s->readUint8("multiStock") : 0;
		if (multiStock > 1) throw std::runtime_error("Invalid remembered stock representation");
		const auto* properties = t.type == NO_RES_TYPE ? nullptr : &game.map.resourcePropertiesByIndex(t.type);
		if (version >= FILE_FORMAT_VERSION_RUNTIME_RESOURCES &&
			bool(multiStock) != bool(properties && std::popcount(properties->materialMask) > 1))
			throw std::runtime_error("Remembered stock representation does not match resource yields");
		if (multiStock)
		{
			auto& stock=rememberedStocks[index];
			const auto& yields = game.map.resourceRegistry().yields(static_cast<ResourceId>(t.type));
			unsigned total = 0;
			s->readEnterSection("materialStocks");
			for (unsigned m = 0; m < MaterialCount; ++m)
			{
				s->readEnterSection(m);
				stock[m] = s->readUint16("stock");
				if (stock[m] > yields[m].capacity) throw std::runtime_error("Remembered material exceeds capacity");
				total += stock[m];
				s->readLeaveSection();
			}
			s->readLeaveSection();
			if (total != t.amount) throw std::runtime_error("Remembered resource total does not match material stocks");
		}
		else if (properties)
		{
			const auto capacity = game.map.resourceRegistry().yields(static_cast<ResourceId>(t.type))[materialIndex(properties->primaryMaterial)].capacity;
			if (t.amount > capacity) throw std::runtime_error("Remembered material exceeds capacity");
		}
		else if (t.amount || t.variety) throw std::runtime_error("Empty remembered tile contains resource stock");
		t.known = true;
		remember(index) = t;
		s->readLeaveSection();
	}
	s->readLeaveSection();
}
} // namespace Script

unsigned Script::Observations::materialStock(int x, int y, MaterialId material) const
{
	if (!validMaterial(materialIndex(material))) return 0;
	x &= game.map.getW() - 1;
	y &= game.map.getH() - 1;
	const auto index = unsigned(game.map.coordToIndex(x, y));
	if (team < 0 || game.map.isFOWDiscovered(x, y, game.teams[team]->me))
		return game.map.materialAmountAt(index, material);
	const auto* old = lookup(index);
	if (!old || old->type == NO_RES_TYPE) return 0;
	const auto stocks = rememberedStocks.find(index);
	if (stocks != rememberedStocks.end()) return stocks->second[materialIndex(material)];
	return game.map.resourcePropertiesByIndex(old->type).primaryMaterial == material ? old->amount : 0;
}

Script::Observations::Cell Script::Observations::cell(int x, int y) const
{
	x &= game.map.getW() - 1;
	y &= game.map.getH() - 1;
	Cell out;
	out.visible = team < 0 || game.map.isFOWDiscovered(x, y, game.teams[team]->me);
	if (out.visible)
	{
		const auto &tile = game.map.getTile(x, y);
		out.known = true;
		out.tick = game.stepCounter;
		out.terrain = tile.terrain;
		out.terrainType = game.map.terrainTypeAt(x,y);
		out.fertility = tile.fertility;
		out.resource = tile.resource.type;
		out.amount = tile.resource.amount;
		if (tile.building != 65535)
		{
			const auto &b = game.teams[Building::GIDtoTeam(tile.building)]
								->myBuildings[Building::GIDtoID(tile.building)];
			out.building = b && visible(game, team, *b);
		}
	}
	else if (const auto *old = lookup(game.map.coordToIndex(x, y)))
	{
		out.known = true;
		out.tick = old->tick;
		out.terrain = old->terrain;
		out.terrainType = old->terrainType;
		out.fertility = old->fertility;
		out.resource = old->type;
		out.amount = old->amount;
	}
	if (team >= 0 && out.known)
		out.forbidden = (game.map.getTile(x, y).forbidden & game.teams[team]->me) != 0;
	return out;
}

void Script::Observations::visitSpatialEntities(
	bool units, int filter, const std::function<void(const SpatialEntity &)> &visit,
	const QueryBudget &budget) const
{
	if (filter < -1 || filter >= game.mapHeader.getNumberOfTeams())
		throw std::runtime_error("Invalid spatial source team");
	const int capacity = units ? Unit::MAX_COUNT : Building::MAX_COUNT;
	for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
		if (filter < 0 || filter == t)
		{
			// Charge the fixed slot scan, independent of hidden entity counts.
			if (budget)
				budget(capacity, 0);
			for (int i = 0; i < capacity; ++i)
				if (units)
				{
					const auto *u = game.teams[t]->myUnits[i];
					if (u && !u->isDead && visible(game, team, *u))
						visit({t, u->typeNum, u->posX, u->posY, u->hp,
							   u->performance[ATTACK_STRENGTH], false});
				}
				else
				{
					const auto *b = game.teams[t]->myBuildings[i];
					if (b && b->buildingState != Building::DEAD && visible(game, team, *b))
						visit({t, b->shortTypeNum, b->posX, b->posY, b->hp, 0,
							   bool(b->type->isVirtual), b->typeNum});
				}
		}
}
