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
Observations::Observations(Game &g, int t) : game(g), team(t) {}
Observations::ObservationScope::ObservationScope(const Observations& o, const AIEngine::AIWorldView& value)
    : observations(o), previous(o.view) { observations.view = &value; }
Observations::ObservationScope::ObservationScope(const Observations& o)
    : observations(o), previous(o.view)
{
    if (!previous)
    {
        owned = AIEngine::AIWorldView::capture(o.game, AIEngine::AIWorldView::captureCatalog(o.game));
        observations.view = owned.get();
    }
}
Observations::ObservationScope::~ObservationScope() { observations.view = previous; }
const AIEngine::AIWorldView& Observations::world() const
{
    if (!view) throw std::logic_error("Script observation used outside its decision scope");
    return *view;
}
namespace
{
Value numbers(const Sint32 *values, int n)
{
	Value a = Value::array();
	for (int i = 0; i < n; ++i)
		a.items.emplace_back(values[i]);
	return a;
}
bool visible(const AIEngine::AIWorldView &world, int team, const AIEngine::UnitView &u)
{
	return team < 0 || u.team == team ||
		   (u.insideTimeout >= 0 &&
			((world.visibilityAt(world.tileIndex(u.x, u.y)).visible & world.teams[team].mask) ||
			 (world.visibilityAt(world.tileIndex(u.x - u.dx, u.y - u.dy)).visible & world.teams[team].mask)));
}
bool visible(const AIEngine::AIWorldView &world, int team, const AIEngine::BuildingView &b)
{
	return team < 0 || b.team == team ||
		   (!world.catalog->at(b.type).isCloaked && (world.visibilityAt(world.tileIndex(b.x, b.y)).visible & world.teams[team].mask));
}
int arg(const std::vector<Value> &a, size_t i, int lo, int hi)
{
	if (i >= a.size())
		throw std::runtime_error("Missing query argument");
	return Value::object().set("value", a[i]).integer("value", lo, hi);
}
} // namespace
Value Observations::ref(const AIEngine::UnitView *u) const
{
	if (!u)
		return {};
	return Value::object().set("id", unsigned(u->identity.gid)).set("generation", u->identity.generation);
}
Value Observations::ref(const AIEngine::BuildingView *b) const
{
	if (!b)
		return {};
	return Value::object().set("id", unsigned(b->identity.gid)).set("generation", b->identity.generation);
}
Value Observations::unit(const AIEngine::UnitView &u) const
{
	if (!visible(world(), team, u) || u.dead)
		return {};
	Value v = ref(&u);
	v.set("team", u.team)
		.set("type", u.type)
		.set("x", u.x)
		.set("y", u.y)
		.set("hp", u.hp)
		.set("maxHp", u.performance[HP])
		.set("levels", numbers(u.levels.data(), NB_ABILITY))
		.set("performance", numbers(u.performance.data(), NB_ABILITY));
	if (team < 0 || u.team == team)
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
		v.set("attachedBuilding", world().building(u.attached) && visible(world(), team, *world().building(u.attached))
									  ? ref(world().building(u.attached))
									  : Value());
		v.set("targetBuilding", world().building(u.target) && visible(world(), team, *world().building(u.target))
									? ref(world().building(u.target))
									: Value());
	}
	return v;
}
Value Observations::building(const AIEngine::BuildingView &b) const
{
	if (!visible(world(), team, b) || b.state == Building::DEAD)
		return {};
	Value v = ref(&b);
	v.set("team", b.team)
		.set("type", b.type)
		.set("shortType", b.shortType)
		.set("key", world().catalog->at(b.type).key)
		.set("capabilities", buildingCapabilities(world(), b.type))
		.set("relocatable", world().catalog->at(b.type).semantics.relocatable)
		.set("interTeamExchange", world().catalog->at(b.type).semantics.market.interTeamFruitExchange)
		.set("x", b.x)
		.set("y", b.y)
		.set("hp", b.hp)
		.set("maxHp", b.maxHp)
		.set("level", world().catalog->at(b.type).level)
		.set("virtual", bool(world().catalog->at(b.type).isVirtual))
		.set("construction", int(b.construction));
	if (team < 0 || b.team == team)
	{
		v.set("workers", b.workers)
			.set("futureWorkers", b.futureWorkers)
			.set("priority", b.priority)
			.set("range", b.range)
			.set("minimumLevel", b.minimumLevel)
			.set("requireBombing", b.requireBombing)
			.set("workerMinimumLevel", b.minimumWorkerLevel)
			.set("resources", numbers(b.resources.data(), MAX_NB_RESOURCES))
			.set("wishedResources", numbers(b.wishedResources.data(), MAX_NB_RESOURCES))
			.set("production", numbers(b.ratios.data(), NB_UNIT_TYPE))
			.set("productionTimeout", b.productionTimeout)
			.set("receiveMask", b.receiveMask)
			.set("sendMask", b.sendMask)
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
		remembered.resize((unsigned(world().width) * world().height + 255) / 256);
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
	auto observation = captureObservation();
	if (team < 0 || lastTick == world().tick)
		return;
	const unsigned mask = world().teams[team].mask;
    for (std::size_t index = 0; index < std::size_t(world().width) * world().height; ++index)
        if (world().visibilityAt(index).visible & mask)
        {
            const auto terrain = world().terrainAt(index);
            const auto cell = world().resourceAt(index);
            const auto& r = cell.resource;
            remember(unsigned(index)) = {world().tick, terrain.legacy, cell.fertility,
                terrain.type, r.type, r.variety, r.amount, true};
        }
	lastTick = world().tick;
}
Value Observations::tile(int x, int y) const
{
	x &= world().width - 1;
	y &= world().height - 1;
    const auto index = world().tileIndex(x, y);
	const bool current = team < 0 || (world().visibilityAt(index).visible & world().teams[team].mask);
	Value v = Value::object().set("x", x).set("y", y).set("visible", current);
	RememberedTile t;
	if (current)
	{
		const auto c = world().resourceAt(index);
        const auto terrain = world().terrainAt(index);
		t = {world().tick, terrain.legacy, c.fertility, terrain.type,
			 c.resource.type, c.resource.variety, c.resource.amount};
	}
	else
	{
		auto *previous = lookup(unsigned(index));
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
		const auto c = world().occupancyAt(index);
		auto unitId = [&](unsigned id)
		{
			if (id >= Unit::MAX_COUNT * Team::MAX_COUNT)
				return 65535u;
			int owner = Unit::GIDtoTeam(id);
			if (owner >= int(world().teams.size()))
				return 65535u;
			auto *u = world().unitAtSlot(id);
			return u && !u->dead && visible(world(), team, *u) ? id : 65535u;
		};
		unsigned buildingId = c.building;
		int owner = buildingId < Building::MAX_COUNT * Team::MAX_COUNT
						? Building::GIDtoTeam(buildingId)
						: Team::MAX_COUNT;
		auto *b = owner < int(world().teams.size())
					  ? world().buildingAtSlot(buildingId)
					  : nullptr;
		if (!b || !visible(world(), team, *b))
			buildingId = 65535;
		v.set("groundUnit", unitId(c.groundUnit))
			.set("airUnit", unitId(c.airUnit))
			.set("building", buildingId);
	}
	if (team >= 0)
	{
		v.set("forbidden", bool(world().areasAt(index).forbidden & world().teams[team].mask));
		// Only present where the team painted a farm (the farm-areas experiment), so
		// tile records in games without it are unchanged.
		if (world().farmAreasEnabled && (world().areasAt(index).farm & world().teams[team].mask))
			v.set("farmArea", true);
	}
	return v;
}
Value Observations::query(const std::string &name, const std::vector<Value> &args,
						  const QueryBudget &budget) const
{
	auto observation = captureObservation();
	auto charge = [&](std::size_t nodes, std::size_t bytes = 0)
	{
		if (budget)
			budget(nodes, nodes * NativeValueCost + bytes);
	};
    if (name == "rules")
    {
        Value rules=Value::object();
        for(const auto& [key,value]:world().ruleValues)
        { charge(1,key.size()); rules.set(key,value); }
        return rules;
    }
	if (name == "experiments")
	{
		// Keys of the experiments this game carries (ExperimentalFeatures.h).
		Value a = Value::array();
		for (const auto &key : world().experimentKeys)
		{
			charge(1, key.size());
			a.items.push_back(Value(key));
		}
		return a;
	}
    if (name == "terrainTypes")
    {
		// Registry definitions reveal no tile state. Cache per immutable registry;
		// callers receive detached, read-only JS snapshots in both profiles.
		charge(world().terrain->size() * 48);
		if (terrainDefinitionRegistry != world().terrain)
		{
			terrainDefinitions = [&]
			{
				Value result = Value::array();
				for (unsigned id = 0; id < world().terrain->size(); ++id)
				{
					const auto type = static_cast<TerrainType>(id);
					const auto &p = world().terrain->properties(type);
					const auto &presentation = world().terrain->presentation(type);
					const auto experiment = terrainExperiment(type);
					Value resources = Value::array();
					for (unsigned resource = 0; resource < MAX_NB_RESOURCES; ++resource)
						if (p.allowedResources & (1u << resource))
							resources.items.emplace_back(resource);
					result.items.push_back(
						Value::object()
							.set("id", id)
							.set("name", presentation.name)
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
							.set("farmCrop", int(p.farmCrop)));
				}
				return result;
			}();
			terrainDefinitionRegistry = world().terrain;
		}
		return terrainDefinitions;
	}
	if (name == "buildingTypes")
	{
		Value a = Value::array();
		const Value options = args.empty() ? Value::object() : args[0];
        const int offset = options.get("offset").kind == Value::Null ? 0 : options.integer("offset", 0, int(world().catalog->size()));
        const int limit = options.get("limit").kind == Value::Null ? int(world().catalog->size()) : options.integer("limit", 1, int(world().catalog->size()));
        for (unsigned i = offset; i < world().catalog->size() && i < unsigned(offset + limit); ++i)
		{
			if (!world().catalog->at(i).available) continue;
			const auto &b = world().catalog->at(i);
			charge(256 + NB_ABILITY * (MAX_NB_RESOURCES + 8), b.legacyType.size() + b.key.size());
            auto service = [&](const BuildingServiceSpec& spec) {
                return Value::object().set("enabled", spec.enabled).set("unitMask", spec.unitMask)
                    .set("duration", spec.duration).set("cost", numbers(spec.cost.data(), MAX_NB_RESOURCES));
            };
            Value training = Value::array(), production = Value::array();
            for (const auto& spec : b.semantics.training)
                training.items.push_back(Value::object().set("enabled", spec.enabled).set("unitMask", spec.unitMask)
                    .set("targetLevel", spec.targetLevel).set("constructionLevel", spec.constructionLevel)
                    .set("duration", spec.duration).set("cost", numbers(spec.cost.data(), MAX_NB_RESOURCES)));
            for (const auto& spec : b.semantics.production.recipes)
                production.items.push_back(Value::object().set("enabled", spec.enabled).set("duration", spec.duration)
                    .set("cost", numbers(spec.cost.data(), MAX_NB_RESOURCES)));
			a.items.push_back(
				Value::object()
					.set("id", i)
                    .set("key", b.key).set("nextType", b.next).set("previousType", b.previous)
                    .set("placeable", b.semantics.placeable).set("instantPlacement", b.semantics.instantPlacement)
                    .set("requiredWorkerLevel", b.semantics.requiredWorkerLevel)
                    .set("admittedUnitMask", b.semantics.admittedUnitMask)
                    .set("maxUnitsInside", b.maximumInside).set("maxRadius", b.maximumRange)
                    .set("relocatable", b.semantics.relocatable).set("occupiesGround", b.semantics.occupiesGround)
                    .set("repairable", b.semantics.repairable).set("regeneration", b.semantics.regenerationPerTick)
                    .set("feeding", service(b.semantics.feeding)).set("healing", service(b.semantics.healing))
                    .set("training", training).set("production", production)
                    .set("capabilities", buildingCapabilities(world(), i))
                    .set("projectileDamage", numbers(b.semantics.projectileDamage.data(), NB_UNIT_TYPE))
                    .set("projectileRange", b.shootingRange).set("projectileSpeed", b.shootSpeed).set("projectileRhythm", b.shootRhythm)
                    .set("ammunitionResource", b.semantics.ammunitionResource).set("ammunitionCost", b.semantics.ammunitionCost)
                    .set("suppliesStock", b.suppliesStock).set("fetchesStock", b.fetchesStock)
                    .set("suppliesDirectStock", b.semantics.market.suppliesDirectStock)
                    .set("exchangesFruit", b.semantics.market.interTeamFruitExchange)
                    .set("name", b.legacyType)
					.set("shortType", b.shortTypeNum)
					.set("level", b.level)
					.set("site", bool(b.site))
					.set("virtual", bool(b.isVirtual))
					.set("width", b.width)
					.set("height", b.height)
					.set("maxHp", b.hpMax)
					.set("maxWorkers", b.semantics.assignmentLimit).set("usesWorkers", bool(b.maximumWorkers))
					.set("resourceCapacity", numbers(b.maxResource.data(), MAX_NB_RESOURCES)));
		}
		return a;
	}
	if (name == "teams")
	{
		Value a = Value::array();
		for (int t = 0; t < int(world().teams.size()); ++t)
		{
			const auto &tm = world().teams[t];
			charge(64);
			Value v = Value::object().set("id", t).set("alive", tm.alive);
			if (team < 0 || team == t)
				v.set("allies", tm.allies)
					.set("resources", numbers(tm.resources.data(), MAX_NB_RESOURCES));
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
				filter = args[0].integer("team", 0, int(world().teams.size()) - 1);
			if (args[0].get("offset").kind != Value::Null)
				offset = args[0].integer("offset", 0, 32768);
			if (args[0].get("limit").kind != Value::Null)
				limit = args[0].integer("limit", 0, 32768);
		}
		if (!limit)
			return Value::array();
		Value a = Value::array();
		for (int t = 0; t < int(world().teams.size()); ++t)
			if (filter < 0 || t == filter)
				for (int i = 0; i < (name == "units" ? Unit::MAX_COUNT : Building::MAX_COUNT); ++i)
				{
					Value v;
					if (name == "units")
					{
						auto *u = world().unitAtSlot(Unit::GIDfrom(i, t));
						if (!u || u->dead || !visible(world(), team, *u))
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
						auto *b = world().buildingAtSlot(Building::GIDfrom(i, t));
						if (!b || b->state == Building::DEAD || !visible(world(), team, *b))
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
		if (owner >= int(world().teams.size()))
			return {};
		const Value &generation = args[0].get("generation");
		if (generation.kind != Value::Number)
			return {};
		if (name == "unit")
		{
			auto *u = world().unitAtSlot(id);
			if (u && !u->dead && visible(world(), team, *u) &&
				generation.number == ref(u).get("generation").number)
			{
				charge(160);
				return unit(*u);
			}
		}
		else
		{
			auto *b = world().buildingAtSlot(id);
			if (b && b->state != Building::DEAD && visible(world(), team, *b) &&
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
	auto observation = captureObservation();
	remembered.clear();
	knownTiles = 0;
	s->readEnterSection("scriptObservations");
	lastTick = s->readUint32("tick");
	unsigned n = s->readUint32("count");
	unsigned size = unsigned(world().width) * world().height;
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
		if (!world().terrain->valid(terrainType))
			throw std::runtime_error("Invalid remembered terrain type");
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
	auto observation = captureObservation();
	x &= world().width - 1;
	y &= world().height - 1;
	Cell out;
    const auto index = world().tileIndex(x, y);
	out.visible = team < 0 || (world().visibilityAt(index).visible & world().teams[team].mask);
	if (out.visible)
	{
		const auto tile = world().resourceAt(index);
        const auto terrain = world().terrainAt(index);
        const auto occupancy = world().occupancyAt(index);
		out.known = true;
		out.tick = world().tick;
		out.terrain = terrain.legacy;
		out.terrainType = terrain.type;
		out.fertility = tile.fertility;
		out.resource = tile.resource.type;
		out.amount = tile.resource.amount;
		if (occupancy.building != 65535)
		{
			const auto &b = world().buildingAtSlot(occupancy.building);
			out.building = b && visible(world(), team, *b);
		}
	}
	else if (const auto *old = lookup(unsigned(index)))
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
		out.forbidden = (world().areasAt(index).forbidden & world().teams[team].mask) != 0;
	return out;
}

void Script::Observations::visitSpatialEntities(
	bool units, int filter, const std::function<void(const SpatialEntity &)> &visit,
	const QueryBudget &budget) const
{
	auto observation = captureObservation();
	if (filter < -1 || filter >= int(world().teams.size()))
		throw std::runtime_error("Invalid spatial source team");
	const int capacity = units ? Unit::MAX_COUNT : Building::MAX_COUNT;
	for (int t = 0; t < int(world().teams.size()); ++t)
		if (filter < 0 || filter == t)
		{
			// Charge the fixed slot scan, independent of hidden entity counts.
			if (budget)
				budget(capacity, 0);
			for (int i = 0; i < capacity; ++i)
				if (units)
				{
					const auto *u = world().unitAtSlot(Unit::GIDfrom(i, t));
					if (u && !u->dead && visible(world(), team, *u))
						visit({t, u->type, u->x, u->y, u->hp,
							   u->performance[ATTACK_STRENGTH], false});
				}
				else
				{
					const auto *b = world().buildingAtSlot(Building::GIDfrom(i, t));
					if (b && b->state != Building::DEAD && visible(world(), team, *b))
						visit({t, b->shortType, b->x, b->y, b->hp, 0,
							   bool(world().catalog->at(b->type).isVirtual), b->type});
				}
		}
}
