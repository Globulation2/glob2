// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptObservations.h"
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
			.set("carriedResource", u.carriedResource)
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
			.set("resources", numbers(b.resources, MAX_NB_RESOURCES))
			.set("wishedResources", numbers(b.wishedResources, MAX_NB_RESOURCES))
			.set("production", numbers(b.ratio, NB_UNIT_TYPE))
			.set("productionTimeout", b.productionTimeout)
			.set("receiveMask", b.receiveResourceMask)
			.set("sendMask", b.sendResourceMask)
			.set("bullets", b.bullets);
		Value a = Value::array();
		for (int i = 0; i < BASIC_COUNT; ++i)
			a.items.emplace_back(b.clearingResources[i]);
		v.set("clearingResources", a);
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
    if (name == "terrainTypes")
    {
        // Static definitions reveal no map state. Build the native value once;
        // callers receive detached, read-only JS snapshots in both profiles.
        static const Value definitions=[] {
            Value result=Value::array();
            for(unsigned id=0;id<TERRAIN_COUNT;++id)
            {
                const auto type=static_cast<TerrainType>(id);
                const auto& p=terrainProperties(type);
                const auto& presentation=terrainPresentation(type);
                const auto experiment=terrainExperiment(type);
                Value resources=Value::array();
                for(unsigned resource=0;resource<MAX_NB_RESOURCES;++resource)
                    if(p.allowedResources & (1u<<resource))resources.items.emplace_back(resource);
                result.items.push_back(Value::object()
                    .set("id",id).set("name",presentation.name)
                    .set("experiment",experiment?Value(experimentDefinition(*experiment).key):Value())
                    .set("editorSelectable",presentation.editorSelectable)
                    .set("walkable",p.walkable).set("swimmable",p.swimmable).set("flyable",p.flyable)
                    .set("resourcesGrow",p.resourcesGrow).set("fertilitySource",p.fertilitySource)
                    .set("nonGrowingResources",p.nonGrowingResources).set("buildable",p.buildable)
                    .set("projectileBlocks",p.projectileBlocks).set("shoreline",p.shoreline)
                    .set("groundSpeedQ8",int(p.groundSpeedQ8)).set("airSpeedQ8",int(p.airSpeedQ8))
                    .set("groundHealthQ8",int(p.groundHealthQ8)).set("airHealthQ8",int(p.airHealthQ8))
                    .set("growthQ8",int(p.growthQ8)).set("fertilityQ8",int(p.fertilityQ8))
                    .set("inhibitionQ8",int(p.inhibitionQ8)).set("shoreSupportQ8",int(p.shoreSupportQ8))
                    .set("allowedResources",resources).set("farmCrop",int(p.farmCrop)));
            }
            return result;
        }();
        charge(TERRAIN_COUNT*48);
        return definitions;
    }
	if (name == "buildingTypes")
	{
		Value a = Value::array();
		for (unsigned i = 0; i < globalContainer->buildingsTypes.size(); ++i)
		{
			if (!game.isBuildingTypeAvailable(i)) continue;
			const auto &b = *globalContainer->buildingsTypes.get(i);
			charge(64, b.type.size());
			a.items.push_back(
				Value::object()
					.set("id", i)
					.set("name", b.type)
					.set("shortType", b.shortTypeNum)
					.set("level", b.level)
					.set("site", bool(b.isBuildingSite))
					.set("virtual", bool(b.isVirtual))
					.set("width", b.width)
					.set("height", b.height)
					.set("maxHp", b.hpMax)
					.set("maxWorkers", b.maxUnitWorking)
					.set("resourceCapacity", numbers(b.maxResource, MAX_NB_RESOURCES)));
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
					.set("resources", numbers(tm.teamResources, MAX_NB_RESOURCES));
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
		s->writeUint8(t.type, "type");
		s->writeUint8(t.variety, "variety");
		s->writeUint8(t.amount, "amount");
		s->writeUint16(t.terrainType, "terrainType");
		s->writeLeaveSection();
	}
	s->writeLeaveSection();
}
void Observations::load(GAGCore::InputStream *s, int version)
{
	remembered.clear();
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
		t.type = s->readUint8("type");
		t.variety = s->readUint8("variety");
		t.amount = s->readUint8("amount");
		const unsigned terrainType = version >= FILE_FORMAT_VERSION_TERRAIN_PROPERTIES
			? s->readUint16("terrainType") : unsigned(legacyTerrainType(t.terrain));
		if (!validTerrainType(terrainType)) throw std::runtime_error("Invalid remembered terrain type");
		t.terrainType = static_cast<TerrainType>(terrainType);
		if (index >= size || lookup(index))
			throw std::runtime_error("Invalid terrain memory");
		t.known = true;
		remember(index) = t;
		s->readLeaveSection();
	}
	s->readLeaveSection();
}
} // namespace Script

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
							   bool(b->type->isVirtual)});
				}
		}
}
