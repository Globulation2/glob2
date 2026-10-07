// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real terrain regeneration and resource clearing without a window.
#include "EngineFixtures.h"
#include "Version.h"
#include "FileFormatVersions.h"
#include "Building.h"
#include "Game.h"
#include "GameGUI.h"
#include "GlobalContainer.h"
#include "IntBuildingType.h"
#include "Map.h"
#include "generator/shared/ResourceSemantics.h"
#include "generator/shared/BalancedStarts.h"
#include "generator/shared/Pipeline.h"
#include "generator/shared/Resources.h"
#include "generator/shared/StartDiagnostics.h"
#include "Race.h"
#include "Unit.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <locale>
#include "online/Sha256.h"
#include <vector>

namespace
{
#define CHECK_MAP_ID_ARGUMENT(Name, Expected, Other, ...) \
    template<class Id> concept Name = requires(Map& map, Id id) { __VA_ARGS__; }; \
    static_assert(Name<Expected> && !Name<Other> && !Name<int> && !Name<decltype(Resource{}.type)>)
CHECK_MAP_ID_ARGUMENT(MaterialAmountArgument,MaterialId,ResourceId,map.materialAmountAt(0,id));
CHECK_MAP_ID_ARGUMENT(MaterialPresenceArgument,MaterialId,ResourceId,map.hasMaterialSource(id));
CHECK_MAP_ID_ARGUMENT(MaterialSetArgument,MaterialId,ResourceId,map.setMaterialAmount(0,id,1));
CHECK_MAP_ID_ARGUMENT(MaterialTakeableArgument,MaterialId,ResourceId,map.isMaterialTakeable(0,0,id));
CHECK_MAP_ID_ARGUMENT(HarvestArgument,MaterialId,ResourceId,map.takeHarvest(0,0,0,0,id,1));
CHECK_MAP_ID_ARGUMENT(MaterialHabitatArgument,MaterialId,ResourceId,map.terrainSupportsMaterialAt(0,0,id));
CHECK_MAP_ID_ARGUMENT(MaterialGrowthArgument,MaterialId,ResourceId,map.materialGrowthRateAt(0,id));
CHECK_MAP_ID_ARGUMENT(MaterialExpansionArgument,MaterialId,ResourceId,map.materialExpansionRateAt(0,id));
CHECK_MAP_ID_ARGUMENT(MaterialGradientArgument,MaterialId,ResourceId,map.getMaterialGradient(0,id,0));
CHECK_MAP_ID_ARGUMENT(MaterialAvailabilityArgument,MaterialId,ResourceId,map.materialAvailable(0,id,0,0,0));
CHECK_MAP_ID_ARGUMENT(MaterialDistanceArgument,MaterialId,ResourceId,map.materialAvailable(0,id,0,0,0,static_cast<int*>(nullptr)));
CHECK_MAP_ID_ARGUMENT(ResourceIncrementArgument,ResourceId,MaterialId,map.incResource(0,0,id,0));
CHECK_MAP_ID_ARGUMENT(ResourceSetArgument,ResourceId,MaterialId,map.setResource(0,0,id,0));
CHECK_MAP_ID_ARGUMENT(ResourcePropertiesArgument,ResourceId,MaterialId,map.resourceProperties(id));
CHECK_MAP_ID_ARGUMENT(ResourceHabitatArgument,ResourceId,MaterialId,map.terrainSupportsResourceAt(0,0,id));
#undef CHECK_MAP_ID_ARGUMENT
}

TEST_SUITE("TerrainResources")
{
TEST_CASE("terrain strokes clear incompatible resources; buildings and units")
{
	glob2test::HeadlessGlobals globals;
	const int positions[][2] = {{8, 8}, {0, 0}, {15, 0}, {0, 15}, {15, 15}};
	int strokes = 0;
	for (int type = 0; type < 8; ++type)
		for (TerrainType paint : {GRASS, SAND, WATER})
			for (const auto& position : positions)
			{
				Map map;
				map.setSize(4, 4, type == ALGA ? WATER : GRASS);
				for (int y = 0; y < 16; ++y)
					for (int x = 0; x < 16; ++x)
					{
						auto resource = map.getResource(x, y);
						resource.type = type;
						resource.amount = 3;
						map.replaceResource(x, y, resource);
					}

				// An adjacent second stroke also checks overlapping brush footprints.
				for (int offset : {0, 1})
				{
					const int px = position[0] + offset, py = position[1];
					std::vector<Resource> before;
					for (int y = 0; y < 16; ++y)
						for (int x = 0; x < 16; ++x)
							before.push_back(map.getResource(x, y));

					// The map operations used by MapEdit::handleTerrainClick.
					map.setUMatPos(px, py, paint, 1);
					map.removeUnallowedResources(px - 2, py - 2, 4, 4);
					if (paint == GRASS)
						for (int y = py - 1; y <= py; ++y)
							for (int x = px - 1; x <= px; ++x)
								map.replaceResource(x, y, Resource{});

					// Independent whole-map oracle: retain every compatible resource,
					// except the four tiles explicitly cleared by the grass brush.
					for (int y = 0; y < 16; ++y)
						for (int x = 0; x < 16; ++x)
						{
							Resource expected = before[y * 16 + x];
							const bool bareGrass = paint == GRASS &&
								(x == (px & 15) || x == ((px - 1) & 15)) &&
								(y == (py & 15) || y == ((py - 1) & 15));
							if (bareGrass || (expected.type != NO_RES_TYPE &&
								!(map.terrainPropertiesAt(x,y).allowedResources & (1u<<expected.type))))
								expected.clear();
							REQUIRE(map.getResource(x, y).getUint64() == expected.getUint64());
						}
					++strokes;
				}
			}
	MESSAGE("terrain resources: " << strokes << " strokes over all 8 resources, 3 terrains, interior and four wrapped corners");

	// Buildings and units. After a stroke a building stands iff its whole footprint is
	// still grass, a walker iff its tile is not water, an explorer always; the grass
	// brush also clears the four tiles the cell touches. The oracle knows nothing about
	// the stroke position, so it holds on every side of an entity alike.
	const int swarm = globals->buildingsTypes.getTypeNum("swarm", 0, false);
	const int swarmW = globals->buildingsTypes.get(swarm)->width;
	const int swarmH = globals->buildingsTypes.get(swarm)->height;
	constexpr int swarmX = 8, swarmY = 8;

	struct World
	{
		GameGUI gui;
		Game& game = gui.game;
		int swarmW, swarmH;
		int swarmX, swarmY, workerX, workerY, explorerX, explorerY;
		Uint16 swarmGid, workerGid, explorerGid;
		World(int swarm, int w, int h, int offsetX = 0, int offsetY = 0) : swarmW(w), swarmH(h),
            swarmX((8 + offsetX) & 15), swarmY((8 + offsetY) & 15),
            workerX((6 + offsetX) & 15), workerY((9 + offsetY) & 15),
            explorerX((11 + offsetX) & 15), explorerY((9 + offsetY) & 15)
		{
			game.map.setSize(4, 4, GRASS);
			game.map.setGame(&game);
			game.addTeam();
			Building* building = game.addBuilding(swarmX, swarmY, swarm, 0);
			Unit* worker = game.addUnit(workerX, workerY, 0, WORKER, 0, 0, 0, 0);
			Unit* explorer = game.addUnit(explorerX, explorerY, 0, EXPLORER, 0, 0, 0, 0);
			REQUIRE((building && worker && explorer && !worker->performance[SWIM]));
			swarmGid = building->gid;
			workerGid = worker->gid;
			explorerGid = explorer->gid;
		}
		bool alive(Uint16 gid, bool building) const
		{
			if (building)
				return game.teams[0]->myBuildings[Building::GIDtoID(gid)] != nullptr;
			return game.teams[0]->myUnits[Unit::GIDtoID(gid)] != nullptr;
		}
		// The map operations used by MapEdit::handleTerrainClick.
		void stroke(int x, int y, TerrainType paint)
		{
			game.map.setUMatPos(x, y, paint, 1);
			game.map.removeUnallowedResources(x - 2, y - 2, 4, 4);
			game.removeUnallowedUnitsAndBuildings(x - 2, y - 2, 4, 4);
			if (paint == GRASS)
				game.removeUnitAndBuildingAndFlags(x, y, 2, Game::DEL_BUILDING | Game::DEL_UNIT);
		}
		void check(int px, int py, TerrainType paint) const
		{
			auto touched = [&](int x, int y)
			{
				return paint == GRASS && (((x - px) & 15) == 0 || ((x - px) & 15) == 15)
                    && (((y - py) & 15) == 0 || ((y - py) & 15) == 15);
			};
			bool swarmStands = true;
			for (int y = swarmY; y < swarmY + swarmH; ++y)
				for (int x = swarmX; x < swarmX + swarmW; ++x)
					if (!game.map.isGrass(x & 15, y & 15) || touched(x, y))
						swarmStands = false;
			const bool workerStands = !game.map.isWater(workerX, workerY) && !touched(workerX, workerY);
			const bool explorerStands = !touched(explorerX, explorerY);
			REQUIRE(alive(swarmGid, true) == swarmStands);
			REQUIRE((game.map.getBuilding(swarmX, swarmY) != NOGBID) == swarmStands);
			REQUIRE(alive(workerGid, false) == workerStands);
			REQUIRE((game.map.getGroundUnit(workerX, workerY) != NOGUID) == workerStands);
			REQUIRE(alive(explorerGid, false) == explorerStands);
			REQUIRE((game.map.getAirUnit(explorerX, explorerY) != NOGUID) == explorerStands);
		}
	};

	int entityStrokes = 0;
    const int offsets[][2] = {{0, 0}, {-8, -8}, {7, -8}, {-8, 7}, {7, 7}};
    for (const auto& offset : offsets)
        for (TerrainType paint : {GRASS, SAND, WATER})
            for (int py = 4; py <= 13; ++py)
                for (int px = 4; px <= 13; ++px)
                {
                    World world(swarm, swarmW, swarmH, offset[0], offset[1]);
                    const int x = (px + offset[0]) & 15;
                    const int y = (py + offset[1]) & 15;
                    world.stroke(x, y, paint);
                    world.check(x, y, paint);
                    ++entityStrokes;
                }

	// A cell touches the tiles c-1..c; the cell 2*swarmX+swarmW-c touches their mirror
	// image across the swarm, the same distance east as c is west, and must agree.
	for (TerrainType paint : {GRASS, SAND, WATER})
		for (int c = 4; c <= swarmX; ++c)
		{
			World west(swarm, swarmW, swarmH), east(swarm, swarmW, swarmH);
			west.stroke(c, swarmY, paint);
			east.stroke(2 * swarmX + swarmW - c, swarmY, paint);
			REQUIRE(west.alive(west.swarmGid, true) == east.alive(east.swarmGid, true));
		}

	// Co-located air units survive flooding, while only swimming ground units remain.
    for (bool swimming : {false, true})
        for (const auto& offset : offsets)
        {
            World world(swarm, swarmW, swarmH, offset[0], offset[1]);
            Unit* worker = world.game.getUnit(world.workerGid);
            worker->performance[SWIM] = swimming ? worker->race->getUnitType(WORKER, 1)->performance[SWIM] : 0;
            Unit* explorer = world.game.getUnit(world.explorerGid);
            world.game.map.setAirUnit(explorer->posX, explorer->posY, NOGUID);
            explorer->posX = world.workerX;
            explorer->posY = world.workerY;
            world.game.map.setAirUnit(explorer->posX, explorer->posY, explorer->gid);
            for (int y = world.workerY; y <= world.workerY + 1; ++y)
                for (int x = world.workerX; x <= world.workerX + 1; ++x)
                    world.stroke(x & 15, y & 15, WATER);
            REQUIRE(world.game.map.isWater(world.workerX, world.workerY));
            REQUIRE(world.alive(world.workerGid, false) == swimming);
            REQUIRE(world.game.map.getGroundUnit(world.workerX, world.workerY) == (swimming ? world.workerGid : NOGUID));
            REQUIRE(world.alive(world.explorerGid, false));
            REQUIRE(world.game.map.getAirUnit(world.workerX, world.workerY) == world.explorerGid);
            ++entityStrokes;
        }

    // A single deletion flag addresses exactly one occupancy layer.
    for (unsigned flag : {unsigned(Game::DEL_GROUND_UNIT), unsigned(Game::DEL_AIR_UNIT)})
    {
        World world(swarm, swarmW, swarmH);
        Unit* explorer = world.game.getUnit(world.explorerGid);
        world.game.map.setAirUnit(explorer->posX, explorer->posY, NOGUID);
        explorer->posX = world.workerX;
        explorer->posY = world.workerY;
        world.game.map.setAirUnit(explorer->posX, explorer->posY, explorer->gid);
        world.game.removeUnitAndBuildingAndFlags(world.workerX, world.workerY, 1, flag);
        REQUIRE(world.alive(world.workerGid, false) == (flag == Game::DEL_AIR_UNIT));
        REQUIRE(world.alive(world.explorerGid, false) == (flag == Game::DEL_GROUND_UNIT));
        REQUIRE(world.game.map.getGroundUnit(world.workerX, world.workerY) == (flag == Game::DEL_AIR_UNIT ? world.workerGid : NOGUID));
        REQUIRE(world.game.map.getAirUnit(world.workerX, world.workerY) == (flag == Game::DEL_GROUND_UNIT ? world.explorerGid : NOGUID));
        REQUIRE(world.alive(world.swarmGid, true));
    }
    MESSAGE("terrain entities: " << entityStrokes << " strokes, 3 terrains, interior and four wrapped corners, swimmers and separate occupancy layers");
}
}

#include <nlohmann/json.hpp>

TEST_SUITE("RuntimeResources")
{
TEST_CASE("material harvesting consumes stock and never invents a delivery")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(4,4,GRASS);
    REQUIRE(map.incResourceByIndex(8,8,WHEAT,0));
    const auto tile=map.coordToIndex(8,8);
    CHECK(map.hasMaterialSource(MaterialId::Food));
    REQUIRE(map.takeHarvest(7,8,1,0,MaterialId::Food,1));
    CHECK(map.getResource(tile).type==NO_RES_TYPE);
    CHECK_FALSE(map.hasMaterialSource(MaterialId::Food));
    CHECK_FALSE(map.takeHarvest(7,8,1,0,MaterialId::Food,1));
    REQUIRE(map.incResourceByIndex(8,8,STONE,0));
    for (int n=0;n<12;++n) REQUIRE(map.takeHarvest(7,8,1,0,MaterialId::Stone,1));
    CHECK(map.materialAmountAt(tile,MaterialId::Stone)==1);
}

TEST_CASE("growth at capacity stays capped and persistent fruit depletes")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(4,4,GRASS);
    REQUIRE(map.incResourceByIndex(8,8,WHEAT,0));
    const auto tile=map.coordToIndex(8,8);
    map.setResourceAmount(tile,5);
    CHECK_FALSE(map.growResourceStock(tile));
    CHECK(map.getResource(tile).amount==5);
    map.replaceResource(tile,Resource{});
    REQUIRE(map.incResourceByIndex(8,8,CHERRY,0));
    REQUIRE(map.takeHarvest(7,8,1,0,MaterialId::Cherries,1));
    CHECK(map.getResource(tile).type==CHERRY);
    CHECK(map.materialAmountAt(tile,MaterialId::Cherries)==0);
    CHECK_FALSE(map.isMaterialTakeableSlot(8,8,CHERRY));
    CHECK_FALSE(map.takeHarvest(7,8,1,0,MaterialId::Cherries,1));
    REQUIRE(map.growResourceStock(tile));
    CHECK(map.materialAmountAt(tile,MaterialId::Cherries)==1);
}

TEST_CASE("custom compound deposit has independent material stock and destructive harvest")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(4,4,GRASS);
    using Json=nlohmann::json;
    auto definition=Json::parse(map.resourceRegistry().serialize())["resources"][1];
    definition["key"]="compound-test";
    definition["properties"]["blocksGround"]=false;
    definition["properties"]["blocksAir"]=true;
    definition["yields"]["wood"]={{"capacity",4},{"initial",3},{"growthRate",196608},{"consumption","all"}};
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
    const auto id=map.resourceRegistry().find("compound-test");
    REQUIRE(id.has_value());
    REQUIRE(map.incResourceByIndex(8,8,resourceIndex(*id),0));
    const auto tile=map.coordToIndex(8,8);
    CHECK(map.materialAmountAt(tile,MaterialId::Food)==1);
    CHECK(map.materialAmountAt(tile,MaterialId::Wood)==3);
    CHECK(map.getResource(tile).amount==4);
    CHECK_FALSE(map.resourceBlocksGround(tile));
    CHECK(map.resourceBlocksAir(tile));
    CHECK_FALSE(map.isFreeForAirUnit(8,8));
    REQUIRE(map.takeHarvest(7,8,1,0,MaterialId::Food,1));
    CHECK(map.materialAmountAt(tile,MaterialId::Wood)==3);
    CHECK(map.getResource(tile).amount==3);
    map.setMaterialAmount(tile,MaterialId::Wood,2);
    const auto stocks=map.materialStocksAt(tile);
    const auto available=map.materialMaskAt(tile),renewable=map.resourceMaterialMaskAt(tile);
    auto visual=map.getTile(tile);
    visual.resource.variety=2;
    visual.resource.animation=1;
    map.replaceTile(tile,visual);
    CHECK(map.materialStocksAt(tile)==stocks);
    CHECK(map.materialMaskAt(tile)==available);
    CHECK(map.resourceMaterialMaskAt(tile)==renewable);
    CHECK(map.getResource(tile).amount==2);
    CHECK(map.getResource(tile).variety==2);
    CHECK(map.getResource(tile).animation==1);
    // A deliberate replacement still reinitializes the compound definition.
    map.replaceResource(tile,map.getResource(tile));
    CHECK(map.materialAmountAt(tile,MaterialId::Food)==1);
    CHECK(map.materialAmountAt(tile,MaterialId::Wood)==3);
    REQUIRE(map.takeHarvest(7,8,1,0,MaterialId::Wood,1));
    CHECK(map.getResource(tile).type==NO_RES_TYPE);
    CHECK_FALSE(map.hasMaterialSource(MaterialId::Wood));
    CHECK_FALSE(map.hasMaterialSource(MaterialId::Food));
}
}

#include <BinaryStream.h>
#include <StreamBackend.h>

TEST_SUITE("RuntimeResources")
{
TEST_CASE("resource identities above byte range preserve compound stocks through save and continuation")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame original({.loadDefaultRace=true,.header=true,.seed=741});
    auto& map=original.game.map;
    using Json=nlohmann::json;
    const auto prototype=Json::parse(map.resourceRegistry().serialize())["resources"][1];
    Json definitions=Json::array();
    for (int n=0;n<260;++n)
    {
        auto resource=prototype;
        resource["key"]="fixture-resource-"+std::to_string(1000+n);
        resource["properties"]["persistsWhenEmpty"]=true;
        resource["yields"]["wood"]={{"capacity",5},{"initial",2},{"growthRate",196608},{"consumption","one"}};
        definitions.push_back(std::move(resource));
    }
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",definitions}}.dump());
    const auto id=*map.resourceRegistry().find("fixture-resource-1259");
    REQUIRE(resourceIndex(id)>255);
    REQUIRE(map.incResourceByIndex(12,12,resourceIndex(id),0));
    const auto at=map.coordToIndex(12,12);
    map.setMaterialAmount(at,MaterialId::Food,0);
    map.setMaterialAmount(at,MaterialId::Wood,4);
    CHECK(map.getResource(at).amount==4);
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    original.game.save(&output,false,"Compound resource continuation");
    output.flush();
    auto* copy=new GAGCore::MemoryStreamBackend(*bytes);
    copy->seekFromStart(0);
    GAGCore::BinaryInputStream input(copy);
    glob2test::HeadlessGame restored({.loadDefaultRace=true,.header=true});
    REQUIRE(restored.game.load(&input));
    auto& loaded=restored.game.map;
    CHECK(loaded.resourceRegistry().digest()==map.resourceRegistry().digest());
    CHECK(loaded.getResource(at).type==resourceIndex(id));
    CHECK(loaded.materialStocksAt(at)==map.materialStocksAt(at));
    CHECK(loaded.checkSum(true)==map.checkSum(true));
    for (int tick=0;tick<32;++tick)
    {
        { auto random=original.game.bindRandom(); original.game.syncStep(0); }
        { auto random=restored.game.bindRandom(); restored.game.syncStep(0); }
        CHECK(original.game.checkSum(nullptr,nullptr,nullptr,true)==restored.game.checkSum(nullptr,nullptr,nullptr,true));
    }
}

TEST_CASE("last finite source extinction and persistent source regrowth update material presence")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(4,4,GRASS);
    REQUIRE(map.incResourceByIndex(8,8,WHEAT,0));
    const auto at=map.coordToIndex(8,8);
    map.setMaterialAmount(at,MaterialId::Food,0);
    CHECK(map.getResource(at).type==NO_RES_TYPE);
    CHECK_FALSE(map.hasMaterialSource(MaterialId::Food));
    REQUIRE(map.incResourceByIndex(8,8,CHERRY,0));
    map.setMaterialAmount(at,MaterialId::Cherries,0);
    CHECK(map.hasMaterialSource(MaterialId::Cherries));
    CHECK(map.materialMaskAt(at)==0);
    REQUIRE(map.growResourceStock(at));
    CHECK(map.materialMaskAt(at)==materialBit(MaterialId::Cherries));
    map.replaceResource(at,Resource{});
    CHECK_FALSE(map.hasMaterialSource(MaterialId::Cherries));
}
}

TEST_SUITE("RuntimeResources")
{
TEST_CASE("shared starting supply checks accept material sources above resource id 255")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.loadDefaultRace=true});
    auto& game=fixture.game;
    auto& map=game.map;
    using Json=nlohmann::json;
    const auto builtins=Json::parse(map.resourceRegistry().serialize())["resources"];
    Json definitions=Json::array();
    for (unsigned i=0;i<300;++i)
    {
        auto spec=builtins[WHEAT];
        spec["key"]="source-"+std::to_string(i);
        definitions.push_back(spec);
    }
    auto food=builtins[WHEAT]; food["key"]="zzz-food";
    auto wood=builtins[WOOD]; wood["key"]="zzz-wood";
    definitions.push_back(food); definitions.push_back(wood);
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",definitions}}.dump());
    const auto foodId=*map.resourceRegistry().find("zzz-food");
    const auto woodId=*map.resourceRegistry().find("zzz-wood");
    REQUIRE(resourceIndex(foodId)>255); REQUIRE(resourceIndex(woodId)>255);
    for(int y : {8,24}) for(int x : {8,24})
    {
        REQUIRE(map.incResource(x,y,ResourceId(WHEAT),0));
        REQUIRE(map.incResource(x+2,y,ResourceId(WOOD),0));
    }
    GenerationRequest request; request.nbTeams=2; request.nbWorkers=4;
    GenerationContext builtinContext(request), customContext(request);
    REQUIRE(MapGeneration::chooseBalancedStarts(game,builtinContext,64));
    for(int y : {8,24}) for(int x : {8,24})
    {
        map.replaceResource(x,y,Resource{}); map.replaceResource(x+2,y,Resource{});
        REQUIRE(map.incResource(x,y,foodId,0));
        REQUIRE(map.incResource(x+2,y,woodId,0));
    }
    REQUIRE(MapGeneration::chooseBalancedStarts(game,customContext,64));
    CHECK(customContext.bootX==builtinContext.bootX);
    CHECK(customContext.bootY==builtinContext.bootY);
    fixture.addUnit(WORKER,7,8);
    const std::vector<MapGeneration::MaterialAccessRule> rules={{MaterialId::Food,4,"food"},{MaterialId::Wood,4,"wood"}};
    CHECK(MapGeneration::startingAccessFailure(map,1,rules,0).empty());
    CHECK_FALSE(MapGeneration::startingAccessFailure(map,1,{{MaterialId::Gold,4,"gold"}},0).empty());
    map.setMaterialAmount(map.coordToIndex(8,8),MaterialId::Food,0);
    CHECK_FALSE(MapGeneration::startingAccessFailure(map,1,rules,0).empty());
    // A secondary yield also satisfies supply; identity and primary yield do not.
    wood["yields"]["food"]={{"capacity",3},{"initial",2},{"consumption","one"}};
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({wood})}}.dump());
    CHECK(MapGeneration::startingAccessFailure(map,1,rules,0).empty());
}
TEST_CASE("starting crop guarantees and adjacent supply use live material yields")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.loadDefaultRace=true});
    auto& map=fixture.game.map;
    using Json=nlohmann::json;
    auto custom=Json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
    custom["key"]="test:composite-starter";
    custom["properties"]["growthRate"]=0;
    custom["properties"]["persistsWhenEmpty"]=true;
    custom["yields"]["wood"]={{"capacity",4},{"initial",2},{"consumption","one"}};
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({custom})}}.dump());
    const auto id=*map.resourceRegistry().find("test:composite-starter");
    map.setResource(9,8,id,1);
    const auto at=map.coordToIndex(9,8);
    std::vector<int> reached(size_t(map.getW())*map.getH(),-1);
    reached[map.coordToIndex(8,8)]=0;
    auto crops=MapGeneration::cropsBesideReach(map,reached);
    CHECK(crops.food); CHECK(crops.wood); CHECK(crops.missing().empty());
    GenerationRequest request; request.nbTeams=1;
    GenerationContext context(request); context.bootX[0]=8; context.bootY[0]=8;
    const auto before=map.materialStocksAt(at);
    MapGeneration::guaranteeStartingResources(fixture.game,context,12,12,0);
    CHECK(context.namedStreams().empty());
    CHECK(map.materialStocksAt(at)==before);
    // A retained deposit and another positive yield must not disguise exhausted food.
    map.setMaterialAmount(at,MaterialId::Food,0);
    crops=MapGeneration::cropsBesideReach(map,reached);
    CHECK_FALSE(crops.food); CHECK(crops.wood);
    CHECK(crops.missing()=="cannot walk to food.");
    CHECK_FALSE(map.hasMaterialSource(MaterialId::Food));
    MapGeneration::guaranteeStartingResources(fixture.game,context,12,12,0);
    CHECK(map.hasMaterialSource(MaterialId::Food));
    CHECK(map.getResource(at).type==resourceIndex(id));
    CHECK(map.materialAmountAt(at,MaterialId::Wood)==before[materialIndex(MaterialId::Wood)]);
}

TEST_CASE("bounded starter crop replacement uses surplus materials and respects clearability")
{
    glob2test::HeadlessGlobals globals;
    for (bool clearable : {false,true})
    {
        CAPTURE(clearable);
        glob2test::HeadlessGame fixture({.loadDefaultRace=true});
        auto& map=fixture.game.map;
        using Json=nlohmann::json;
        auto custom=Json::parse(map.resourceRegistry().serialize())["resources"][WOOD];
        custom["key"]="test:surplus-timber";
        custom["properties"]["clearable"]=clearable;
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({custom})}}.dump());
        const auto id=*map.resourceRegistry().find("test:surplus-timber");
        map.setResource(9,8,id,1);
        GenerationRequest request; request.nbTeams=1;
        GenerationContext context(request); context.bootX[0]=8; context.bootY[0]=8;
        std::vector<unsigned char> allowed(size_t(map.getW())*map.getH(),0);
        allowed[map.coordToIndex(9,8)]=1;
        MapGeneration::guaranteeStartingResources(fixture.game,context,12,12,0,nullptr,&allowed);
        CHECK(map.hasMaterialSource(MaterialId::Food)==clearable);
        CHECK((map.getResource(9,8).type==resourceIndex(id))==!clearable);
    }
}

TEST_CASE("material frontage counts secondary stocks and prospective renewable supply")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.loadDefaultRace=true});
    auto& map=fixture.game.map;
    using Json=nlohmann::json;
    auto custom=Json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
    custom["key"]="test:frontage";
    custom["properties"]["ecology"]="uniform";
    custom["properties"]["growthRate"]=ResourceRateScale;
    custom["properties"]["spreadRate"]=0;
    custom["properties"]["stockDependentGrowth"]=false;
    custom["properties"]["persistsWhenEmpty"]=true;
    custom["yields"]={{"food",{{"capacity",4},{"initial",4},{"growthRate",ResourceRateScale},{"consumption","one"}}},
                      {"wood",{{"capacity",4},{"initial",2},{"growthRate",0},{"consumption","infinite"}}}};
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({custom})}}.dump());
    const auto id=*map.resourceRegistry().find("test:frontage");
    map.setResource(9,8,id,1);
    const auto at=map.coordToIndex(9,8);
    const MapGeneration::Torus torus(map);
    const auto access=MapGeneration::floodFrom(torus,MapGeneration::tileMask(torus,{int(map.coordToIndex(8,8))}),MapGeneration::groundUnitTiles(map),0);
    CHECK(map.materialGrowthRateAt(at,MaterialId::Food)==0);
    REQUIRE(map.materialRenewalPotentialAt(at,MaterialId::Food)>0);
    auto frontage=MapGeneration::materialFrontages(map,access,0,true);
    CHECK(frontage[MaterialId::Food].edges==1);
    CHECK(frontage[MaterialId::Food].renewableEdges==1);
    CHECK(frontage[MaterialId::Wood].edges==1);
    CHECK(frontage[MaterialId::Wood].renewableEdges==1);
    CHECK(frontage[MaterialId::Food].nearestStep==0);
    CHECK(frontage.count(MaterialId::Stone)==0);
    map.setMaterialAmount(at,MaterialId::Food,0);
    frontage=MapGeneration::materialFrontages(map,access,0,true);
    CHECK(frontage.count(MaterialId::Food)==0);
    CHECK(frontage[MaterialId::Wood].edges==1);
    custom["yields"]["wood"]["consumption"]="one";
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({custom})}}.dump());
    frontage=MapGeneration::materialFrontages(map,access,0,true);
    CHECK(frontage[MaterialId::Wood].renewableEdges==0);
}

TEST_CASE("start diagnostics distinguish configured renewal from spreading pressure")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
    auto& map=fixture.game.map;
    fixture.addUnit(WORKER,8,8);
    using Json=nlohmann::json;
    auto custom=Json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
    custom["key"]="test:diagnostic-supply";
    custom["properties"]["ecology"]="uniform";
    custom["properties"]["growthRate"]=ResourceRateScale;
    custom["properties"]["spreadRate"]=0;
    custom["properties"]["stockDependentGrowth"]=false;
    custom["properties"]["persistsWhenEmpty"]=true;
    custom["yields"]={{"food",{{"capacity",4},{"initial",4},{"growthRate",0},{"consumption","one"}}},
                      {"wood",{{"capacity",4},{"initial",2},{"growthRate",0},{"consumption","one"}}}};
    const auto install=[&] { map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({custom})}}.dump()); };
    const auto diagnose=[&] {
        auto report=MapGeneration::diagnoseStarts(fixture.game,1);
        REQUIRE(report.measured); REQUIRE(report.colonies.size()==1);
        return report.colonies.front();
    };
    install();
    const auto id=*map.resourceRegistry().find("test:diagnostic-supply");
    map.setResource(10,8,id,1);
    auto metrics=diagnose();
    CHECK(metrics.renewableFood==0); CHECK(metrics.encroachingWood==0);
    CHECK(metrics.threatenedBuildSites==0);
    custom["yields"]["food"]["growthRate"]=ResourceRateScale;
    install();
    metrics=diagnose();
    REQUIRE(metrics.renewableFood>0);
    CHECK(metrics.encroachingWood==0); CHECK(metrics.threatenedBuildSites==0);
    const auto normalRenewal=metrics.renewableFood;
    fixture.game.gameHeader.setResourceScarcityLevel(1);
    CHECK(diagnose().renewableFood==doctest::Approx(normalRenewal/2));
    fixture.game.gameHeader.setResourceScarcityLevel(0);
    custom["properties"]["spreadRate"]=ResourceRateScale;
    install();
    metrics=diagnose();
    CHECK(metrics.encroachingWood>0); CHECK(metrics.threatenedBuildSites>0);
    fixture.game.gameHeader.setResourceGrowthDisabled(true);
    metrics=diagnose();
    CHECK(metrics.renewableFood==0); CHECK(metrics.encroachingWood==0);
    CHECK(metrics.threatenedBuildSites==0);
    custom["yields"]["food"]["consumption"]="infinite";
    install();
    CHECK(diagnose().renewableFood>0);
    map.setMaterialAmount(map.coordToIndex(10,8),MaterialId::Food,0);
    CHECK(diagnose().renewableFood==0);
}

TEST_CASE("fixed property ablations preserve harvest growth clearing and movement invariants")
{
    glob2test::HeadlessGlobals globals;
    using Json=nlohmann::json;
    // Frozen generated cases. Bits independently toggle mobility, persistence,
    // clearing, stock growth, and destructive/infinite harvest combinations.
    constexpr std::array<unsigned,16> cases={0x014,0x172,0x2a3,0x3cf,0x4d1,0x58a,0x623,0x7e0,0x86f,0x931,0xa75,0xb98,0xc02,0xded,0xe46,0xfba};
    const auto prototype=Json::parse(ResourceRegistry::builtins()->serialize())["resources"][1];
    for (const auto bits:cases)
    {
        CAPTURE(bits);
        Map map;
        map.setSize(4,4,GRASS);
        auto resource=prototype;
        resource["key"]="ablation";
        auto& properties=resource["properties"];
        properties["blocksGround"]=bool(bits&1);
        properties["blocksAir"]=bool(bits&2);
        properties["blocksBuilding"]=bool(bits&4);
        properties["clearable"]=true;
        properties["clearConsumption"]=(bits&8) ? "one" : "all";
        properties["persistsWhenEmpty"]=bool(bits&16);
        properties["stockDependentGrowth"]=bool(bits&32);
        properties["farmable"]=false;
        properties["ecology"]="uniform";
        const unsigned capacity=2+((bits>>6)&3);
        const char* consume=((bits>>8)%3)==0 ? "all" : ((bits>>8)%3)==1 ? "one" : "infinite";
        resource["yields"]["food"]={{"capacity",capacity},{"initial",1},{"growthRate",(bits&64)?196608:0},{"consumption",consume}};
        resource["yields"]["wood"]={{"capacity",capacity+1},{"initial",2},{"growthRate",(bits&128)?196608:0},{"consumption","one"}};
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({resource})}}.dump());
        const auto id=resourceIndex(*map.resourceRegistry().find("ablation"));
        REQUIRE(map.incResourceByIndex(8,8,id,0));
        const auto index=map.coordToIndex(8,8);
        CHECK(map.resourceBlocksGround(index)==bool(bits&1));
        CHECK(map.resourceBlocksAir(index)==bool(bits&2));
        CHECK(map.resourceBlocksBuilding(index)==bool(bits&4));
        CHECK_FALSE(MapGeneration::permanentResourceBarrier(map,index));
        resource["properties"]["clearable"]=false;
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({resource})}}.dump());
        const bool permanent=bool(bits&1) && std::string_view(consume)!="all"
            && (bool(bits&16) || std::string_view(consume)=="infinite");
        CHECK(MapGeneration::permanentResourceBarrier(map,index)==permanent);
        resource["properties"]["clearable"]=true;
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({resource})}}.dump());
        REQUIRE(map.takeHarvest(7,8,1,0,MaterialId::Food,1));
        if (std::string_view(consume)=="all")
        {
            CHECK(map.getResource(index).type==NO_RES_TYPE);
            CHECK_FALSE(map.takeHarvest(7,8,1,0,MaterialId::Wood,1));
            continue;
        }
        CHECK(map.materialAmountAt(index,MaterialId::Food)==(std::string_view(consume)=="infinite" ? 1 : 0));
        CHECK(map.materialAmountAt(index,MaterialId::Wood)==2);
        for (int step=0;step<8;++step)
        {
            map.growResourceStock(index);
            const auto stocks=map.materialStocksAt(index);
            CHECK(stocks[materialIndex(MaterialId::Food)]<=capacity);
            CHECK(stocks[materialIndex(MaterialId::Wood)]<=capacity+1);
            CHECK(map.getResource(index).amount==stocks[0]+stocks[1]);
        }
        const auto before=map.getResource(index).amount;
        map.decResource(8,8);
        CHECK(map.getResource(index).amount==((bits&8) ? before-1 : 0));
    }
}
}

TEST_SUITE("RuntimeResources")
{
TEST_CASE("editor catalog replacement preserves materials across layouts and rejects invalid imports atomically")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(4,4,GRASS);
    REQUIRE(map.incResourceByIndex(8,8,WHEAT,0));
    const auto index=map.coordToIndex(8,8);
    map.setMaterialAmount(index,MaterialId::Food,4);
    using Json=nlohmann::json;
    auto spec=Json::parse(map.resourceRegistry().serialize())["resources"][1];
    const auto install=[&](const Json& definition) {
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
    };
    spec["yields"]["wood"]={{"capacity",5},{"initial",2},{"consumption","one"}};
    install(spec);
    CHECK(map.materialAmountAt(index,MaterialId::Food)==4);
    CHECK(map.materialAmountAt(index,MaterialId::Wood)==2);
    CHECK(map.getResource(index).amount==6);
    spec["yields"].erase("food");
    spec["properties"]["primaryMaterial"]="wood";
    install(spec);
    CHECK(map.materialAmountAt(index,MaterialId::Food)==0);
    CHECK(map.materialAmountAt(index,MaterialId::Wood)==2);
    CHECK(map.getResource(index).amount==2);
    const auto digest=map.resourceRegistry().digest();
    const auto checksum=map.checkSum(true);
    spec["yields"]["wood"]["capacity"]=-1;
    CHECK_THROWS(install(spec));
    CHECK(map.resourceRegistry().digest()==digest);
    CHECK(map.checkSum(true)==checksum);
}

TEST_CASE("redefined builtin resource follows declarative habitat and terrain whitelist intersection")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(4,4,GRASS);
    using Json=nlohmann::json;
    auto spec=Json::parse(map.resourceRegistry().serialize())["resources"][1];
    spec["properties"]["habitatMask"]=ResourceAquatic;
    spec["properties"]["ecology"]="shore";
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({spec})}}.dump());
    CHECK_FALSE(map.terrainSupportsResourceAtByIndex(8,8,WHEAT));
    map.setCellTerrain(8,8,WATER);
    CHECK(map.terrainSupportsResourceAtByIndex(8,8,WHEAT));
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"fixture:whitelist","name":"Whitelist","base":"grass","appearance":"grass","properties":{},"allowedResourceKeys":["wheat"]}]})");
    map.setCellTerrain(9,8,*map.terrainRegistry().find("fixture:whitelist"));
    CHECK_FALSE(map.terrainSupportsResourceAtByIndex(9,8,WHEAT));
    const auto oldRegistry=map.frozenTerrainRegistry();
    const auto checksum=map.checkSum(true);
    CHECK_THROWS(map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"fixture:invalid-whitelist","name":"Invalid","base":"grass","appearance":"grass","properties":{},"allowedResourceKeys":["missing-resource"]}]})"));
    CHECK(map.frozenTerrainRegistry()==oldRegistry);
    CHECK(map.checkSum(true)==checksum);
}
}

TEST_SUITE("RuntimeResources")
{
TEST_CASE("destructive resources cannot bridge pooled farms even without reserves or regrowth")
{
    glob2test::HeadlessGlobals globals;
    for (bool disabled : {false,true})
    for (bool all : {false,true})
    {
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto& map=fixture.game.map;
        fixture.game.gameHeader.setResourceGrowthDisabled(disabled);
        using Json=nlohmann::json;
        auto spec=Json::parse(map.resourceRegistry().serialize())["resources"][1];
        spec["key"]="destructive-farm";
        spec["yields"]["food"]["seedReserve"]=0;
        spec["yields"]["food"]["consumption"]=all ? "all" : "one";
        spec["yields"]["food"]["destroysDeposit"]=true;
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({spec})}}.dump());
        const auto id=*map.resourceRegistry().find("destructive-farm");
        REQUIRE(map.incResource(9,8,id,0));
        REQUIRE(map.incResourceByIndex(10,8,WHEAT,0));
        map.setMaterialAmount(map.coordToIndex(10,8),MaterialId::Food,5);
        map.addFarmArea(9,8,0);
        map.addFarmArea(10,8,0);
        CHECK_FALSE(map.pickFarmHarvestTileSlot(8,8,int(MaterialId::Food),1).has_value());
        CHECK(map.materialAmountAt(map.coordToIndex(9,8),MaterialId::Food)==1);
    }
}
TEST_CASE("infinite farm stocks remain harvestable at their seed reserve")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.header=true; options.loadDefaultRace=true;
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame fixture(options);
    auto& map=fixture.game.map;
    using Json=nlohmann::json;
    auto spec=Json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
    spec["key"]="infinite-farm";
    spec["yields"]["food"]["initial"]=1;
    spec["yields"]["food"]["seedReserve"]=1;
    spec["yields"]["food"]["consumption"]="infinite";
    spec["yields"]["food"]["destroysDeposit"]=false;
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({spec})}}.dump());
    const auto id=*map.resourceRegistry().find("infinite-farm");
    REQUIRE(map.incResource(9,8,id,0));
    map.addFarmArea(9,8,0);
    for (unsigned harvest=0;harvest<4;++harvest)
    {
        CHECK(map.takeHarvest(8,8,1,0,MaterialId::Food,1));
        CHECK(map.materialAmountAt(map.coordToIndex(9,8),MaterialId::Food)==1);
    }
}

TEST_CASE("farm harvest uses the touched material policy before pooling neighboring stock")
{
    glob2test::HeadlessGlobals globals;
    using Json=nlohmann::json;
    for (bool all : {false,true})
    {
        glob2test::GameOptions options;
        options.header=true; options.loadDefaultRace=true;
        options.experiments.set(ExperimentId::FarmAreas,true);
        glob2test::HeadlessGame fixture(options);
        auto& map=fixture.game.map;
        auto spec=Json::parse(map.resourceRegistry().serialize())["resources"][WHEAT];
        spec["key"]="mixed-farm-harvest";
        spec["yields"]["food"]["initial"]=1;
        spec["yields"]["food"]["seedReserve"]=1;
        spec["yields"]["gold"]={{"capacity",4},{"initial",3},
            {"consumption",all ? "all" : "one"},{"destroysDeposit",!all}};
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({spec})}}.dump());
        const auto id=*map.resourceRegistry().find("mixed-farm-harvest");
        REQUIRE(map.incResource(9,8,id,0));
        map.addFarmArea(9,8,0);
        const auto target=map.coordToIndex(9,8);
        // The same mixed deposit must still withhold its renewable food seed.
        CHECK_FALSE(map.takeHarvest(8,8,1,0,MaterialId::Food,1));
        CHECK(map.materialAmountAt(target,MaterialId::Food)==1);
        REQUIRE(map.takeHarvest(8,8,1,0,MaterialId::Gold,1));
        CHECK(map.getResource(target).type==NO_RES_TYPE);

        spec["yields"].erase("gold");
        spec["yields"]["food"]["consumption"]=all ? "all" : "one";
        spec["yields"]["food"]["destroysDeposit"]=!all;
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({spec})}}.dump());
        REQUIRE(map.incResource(9,8,id,0));
        REQUIRE(map.incResourceByIndex(8,9,WHEAT,0));
        const auto neighbor=map.coordToIndex(8,9);
        map.setMaterialAmount(neighbor,MaterialId::Food,5);
        map.addFarmArea(8,9,0);
        // A nearby poolable crop must not replace the explicitly touched target.
        REQUIRE(map.takeHarvest(8,8,1,0,MaterialId::Food,1));
        CHECK(map.getResource(target).type==NO_RES_TYPE);
        CHECK(map.materialAmountAt(neighbor,MaterialId::Food)==5);
        // A vanished target cannot redirect an already-started harvest.
        CHECK_FALSE(map.takeHarvest(8,8,1,0,MaterialId::Food,1));
        CHECK(map.materialAmountAt(neighbor,MaterialId::Food)==5);

        spec["properties"]["farmable"]=false;
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({spec})}}.dump());
        REQUIRE(map.incResource(9,8,id,0));
        REQUIRE(map.takeHarvest(8,8,1,0,MaterialId::Food,1));
        CHECK(map.getResource(target).type==NO_RES_TYPE);
        CHECK(map.materialAmountAt(neighbor,MaterialId::Food)==5);

        spec["properties"]["farmable"]=true;
        spec["yields"]["food"]["consumption"]="one";
        spec["yields"]["food"]["destroysDeposit"]=false;
        spec["yields"]["gold"]={{"capacity",4},{"initial",3},{"consumption","one"}};
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({spec})}}.dump());
        REQUIRE(map.incResource(9,8,id,0));
        map.setMaterialAmount(target,MaterialId::Food,0);
        CHECK_FALSE(map.takeHarvest(8,8,1,0,MaterialId::Food,1));
        CHECK(map.materialAmountAt(target,MaterialId::Gold)==3);
        CHECK(map.materialAmountAt(neighbor,MaterialId::Food)==5);
    }
}
}

// Run through the native test registry: each case starts a fresh process, so a
// previously cached default registry cannot make this regression pass.
TEST_CASE("embedded resource save starts with an invalid installed catalog" * doctest::test_suite("RuntimeResources"))
{
    const auto frozen = ResourceRegistry::loadFile((glob2test::sourceRoot() / "data/resources/registry.json").string());
    const auto directory = glob2test::profileDir() / "data/resources";
    std::filesystem::create_directories(directory);
    const auto path = directory / "registry.json";
    struct Remove { std::filesystem::path path; ~Remove() { std::filesystem::remove(path); } } remove{path};
    { std::ofstream file(path); file << "{invalid installed resource definitions"; }
    glob2test::HeadlessGlobals globals;
    CHECK_THROWS(ResourceRegistry::builtins());
    CHECK(ResourceRegistry::availableDefaults()->size() == 0);
    Map empty;
    CHECK_THROWS(empty.setSize(4,4,GRASS));

    GameGUI original(false);
    original.game.map.installResourceDefinitions(frozen->serialize());
    original.game.map.setSize(4,4,GRASS);
    original.game.map.setGame(&original.game);
    original.game.addTeam(0);
    original.game.teams[0]->race.loadDefault();
    GameHeader header;
    header.setNumberOfPlayers(1);
    header.getBasePlayer(0) = BasePlayer(0,"test",0,BasePlayer::P_LOCAL);
    original.game.setGameHeader(header,true);
    REQUIRE(original.game.map.incResourceByIndex(6,6,resourceIndex(*original.game.map.resourceRegistry().find("wheat")),0));
    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    original.game.save(&output,false,"Embedded resources without installed defaults");
    output.flush();
    auto* copied = new GAGCore::MemoryStreamBackend(*bytes);
    copied->seekFromStart(0);
    GAGCore::BinaryInputStream input(copied);
    GameGUI restored(false);
    REQUIRE(restored.game.load(&input));
    CHECK(restored.game.map.resourceRegistry().digest() == original.game.map.resourceRegistry().digest());
    CHECK(restored.game.map.checkSum(true) == original.game.map.checkSum(true));
    CHECK_THROWS(ResourceRegistry::builtins());
    // Old files carry numeric IDs only. Their adapter must be frozen too, not
    // fall back to the missing or invalid default catalog at process startup.
    const auto legacyBytes=glob2test::readFile(glob2test::inflated("javascript/profile1-initial.game.gz"));
    GAGCore::BinaryInputStream oldInput(new GAGCore::MemoryStreamBackend(legacyBytes.data(),legacyBytes.size()));
    oldInput.seekFromStart(0);
    GameGUI oldGame(false);
    REQUIRE(oldGame.game.load(&oldInput));
    CHECK(oldGame.game.map.resourceRegistry().digest()==ResourceRegistry::legacy()->digest());
}

TEST_SUITE("RuntimeResources")
{
TEST_CASE("renamed deposits and sprite variants preserve stochastic stock trajectories")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame original({.wDec=4,.hDec=4,.header=true,.seed=92417});
    glob2test::HeadlessGame renamed({.wDec=4,.hDec=4,.header=true,.seed=92417});
    using Json=nlohmann::json;
    auto definition=Json::parse(original.game.map.resourceRegistry().serialize())["resources"][1];
    definition["key"]="trajectory-original";
    definition["properties"]["ecology"]="uniform";
    definition["properties"]["growthRate"]=ResourceRateScale;
    definition["properties"]["spreadRate"]=ResourceRateScale/4;
    definition["properties"]["stockDependentGrowth"]=false;
    definition["properties"]["blocksGround"]=false;
    definition["properties"]["persistsWhenEmpty"]=true;
    definition["yields"]["food"]["growthRate"]=ResourceRateScale/2;
    definition["yields"]["wood"]={{"capacity",7},{"initial",2},{"growthRate",ResourceRateScale/3},{"consumption","one"}};
    original.game.map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
    definition["key"]="trajectory-renamed";
    definition["presentation"]["name"]="Different artwork";
    definition["presentation"]["sprite"]="data/gfx/missing-metamorphic-art";
    definition["presentation"]["levels"]=Json::array({{{"stock",0},{"variants",Json::array({{{"frame",0},{"weight",1}},{{"frame",7},{"weight",3}}})}}});
    renamed.game.map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
    const auto leftId=*original.game.map.resourceRegistry().find("trajectory-original");
    const auto rightId=*renamed.game.map.resourceRegistry().find("trajectory-renamed");
    REQUIRE(original.game.map.resourceRegistry().digest()!=renamed.game.map.resourceRegistry().digest());
    REQUIRE(original.game.map.incResource(8,8,leftId,0));
    REQUIRE(renamed.game.map.incResource(8,8,rightId,0));
    // Copy the actual engine state; binding must not reseed either game from
    // the process-global stream between ticks.
    renamed.game.syncRandom=original.game.syncRandom;
    unsigned occupied=0;
    for (unsigned tick=0;tick<192;++tick)
    {
        CAPTURE(tick);
        { auto random=original.game.bindRandom(); original.game.map.growResources(); }
        { auto random=renamed.game.bindRandom(); renamed.game.map.growResources(); }
        if (tick%3==0)
        {
            CHECK(original.game.map.takeHarvest(8,8,0,0,MaterialId::Food,1)
                ==renamed.game.map.takeHarvest(8,8,0,0,MaterialId::Food,1));
        }
        occupied=0;
        for (size_t index=0;index<256;++index)
        {
            const auto& left=original.game.map.getResource(index);
            const auto& right=renamed.game.map.getResource(index);
            CHECK((left.type==NO_RES_TYPE)==(right.type==NO_RES_TYPE));
            CHECK(left.amount==right.amount);
            CHECK(original.game.map.materialStocksAt(index)==renamed.game.map.materialStocksAt(index));
            if (left.type!=NO_RES_TYPE)
            {
                ++occupied;
                CHECK(left.type==resourceIndex(leftId));
                CHECK(right.type==resourceIndex(rightId));
                // Rendering may ask for arbitrarily many variants between ticks.
                renamed.game.map.resourceRegistry().presentation(rightId).frame(right.amount,index%16,index/16,tick);
            }
        }
        CHECK(original.game.syncRandom==renamed.game.syncRandom);
    }
    CHECK(occupied>1); // The fixture exercised spreading as well as in-place stock.
}
}

TEST_CASE("frozen seeded resource compositions preserve invariants and exact simulation continuation [golden]" * doctest::test_suite("RuntimeResources"))
{
    glob2test::HeadlessGlobals globals;
    using Json=nlohmann::json;
    constexpr std::array<Uint32,3> seeds={0x1875a203u,0x4bfa021du,0xb037a19fu};
    std::ostringstream trace;
    trace.imbue(std::locale::classic());
    const auto record=[&](Uint32 seed,Game& game) {
        std::ostringstream randomState;
        randomState.imbue(std::locale::classic());
        randomState << game.syncRandom;
        trace << seed << ' ' << game.stepCounter << ' '
              << game.checkSum(nullptr,nullptr,nullptr,true) << ' '
              << Online::Sha256::hex(randomState.str()) << '\n';
    };
    for (const auto seed:seeds)
    {
        CAPTURE(seed);
        glob2test::GameOptions options;
        options.wDec=options.hDec=5; options.header=true; options.loadDefaultRace=true; options.seed=seed;
        options.experiments.set(ExperimentId::FarmAreas,true);
        glob2test::HeadlessGame original(options);
        auto& map=original.game.map;
        Uint32 state=seed;
        const auto next=[&]() { state=state*1664525u+1013904223u; return state; };
        Json definitions=Json::array();
        constexpr unsigned count=24;
        for (unsigned n=0;n<count;++n)
        {
            const auto bits=next();
            const bool persistent=n%3==0, empty=persistent && n%2==0;
            const unsigned habitat=n%4;
            const auto habitatMask=habitat==0 ? ResourceAquatic : habitat==1 ? ResourceShore : ResourceLand;
            Json yields=Json::object();
            const unsigned materials=n%2 ? 1 : 3;
            for (unsigned m=0;m<materials;++m)
            {
                const auto material=m==0 ? MaterialId::Food : m==1 ? MaterialId::Gold : MaterialId::Fabric;
                const unsigned capacity=2+((bits>>(m*3))&3);
                const auto mode=(n+m)%3;
                yields[std::string(materialKey(material))]={{"capacity",capacity},{"initial",empty ? 0 : 1},
                    {"seedReserve",(n+m)%3},{"growthRate",(bits & (1u<<(12+m))) ? ResourceRateScale : 0},
                    {"consumption",mode==0 ? "one" : mode==1 ? "all" : "infinite"},
                    {"destroysDeposit",mode==0 && n%5==0}};
            }
            definitions.push_back({{"key","fixture:composition-"+std::to_string(n)},
                {"properties",{{"primaryMaterial","food"},{"habitatMask",habitatMask},
                    {"ecology",habitat==0 ? "shore" : "uniform"},
                    {"growthRate",(bits&16) ? 4*ResourceRateScale : 0},
                    {"spreadRate",(bits&32) ? ResourceRateScale : 0},
                    {"stockDependentGrowth",bool(bits&1)},{"stockBranchDivisor",2},
                    {"persistsWhenEmpty",persistent},{"farmable",bool(bits&64)},
                    {"blocksGround",bool(bits&2)},{"blocksAir",bool(bits&4)},
                    {"blocksBuilding",bool(bits&8)},{"clearable",true},
                    {"clearConsumption",n%2 ? "one" : "all"}}},
                {"yields",yields},
                {"presentation",{{"name","Composition fixture"},{"sprite","data/gfx/ressource"},
                    {"minimap",{40,80,20}}, {"levels",Json::array({{{"stock",0},{"variants",Json::array({{{"frame",0}}})}}})}}}});
        }
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",definitions}}.dump());
        for (unsigned n=0;n<count;++n)
        {
            const int x=3+(n%8)*3,y=4+(n/8)*7;
            map.setCellTerrain(x,y,n%4==0 ? WATER : n%4==1 ? SAND : GRASS);
            if(n%4==0) map.setCellTerrain(x+1,y,SAND);
            const auto id=*map.resourceRegistry().find("fixture:composition-"+std::to_string(n));
            REQUIRE(map.incResource(x,y,id,0));
            if(map.resourcePropertiesByIndex(resourceIndex(id)).farmable) map.addFarmArea(x,y,0);
        }
        const auto invariant=[](Map& current) {
            std::array<std::uint64_t,MaterialCount> totals{};
            for (int y=0;y<current.getH();++y) for (int x=0;x<current.getW();++x)
            {
                const auto index=current.coordToIndex(x,y);
                const auto& deposit=current.getResource(index);
                const auto stocks=current.materialStocksAt(index);
                unsigned sum=0; MaterialMask mask=0;
                for(unsigned m=0;m<MaterialCount;++m)
                {
                    sum+=stocks[m]; totals[m]+=stocks[m];
                    if(stocks[m]) mask|=materialBit(static_cast<MaterialId>(m));
                    if(deposit.type!=NO_RES_TYPE)
                        REQUIRE(stocks[m]<=current.resourceRegistry().yields(static_cast<ResourceId>(deposit.type))[m].capacity);
                }
                REQUIRE(sum==deposit.amount);
                REQUIRE(mask==current.materialMaskAt(index));
                if(deposit.type==NO_RES_TYPE) REQUIRE(sum==0);
                else
                {
                    REQUIRE(current.terrainSupportsResourceAtByIndex(x,y,deposit.type));
                    const auto& p=current.resourcePropertiesByIndex(deposit.type);
                    REQUIRE(current.resourceBlocksGround(index)==p.blocksGround);
                    REQUIRE(current.resourceBlocksAir(index)==p.blocksAir);
                    REQUIRE(current.resourceBlocksBuilding(index)==p.blocksBuilding);
                    if(!sum) REQUIRE(p.persistsWhenEmpty);
                }
            }
            return totals;
        };
        const auto advance=[&](Game& game,unsigned tick) {
            auto random=game.bindRandom();
            auto& current=game.map;
            const unsigned n=(tick*7+tick/3+seed)%count;
            const int x=3+(n%8)*3,y=4+(n/8)*7;
            const auto index=current.coordToIndex(x,y);
            const auto before=invariant(current);
            if(tick%4==0)
            {
                // A material absent from every generated definition cannot be
                // delivered, including from a farm or a persistent empty tile.
                CHECK_FALSE(current.takeHarvest(x-1,y,1,0,MaterialId::Metal,1));
                CHECK(invariant(current)==before);
            }
            else if(tick%4==1 && !current.isFarmArea(x,y,1))
            {
                const auto deposit=current.getResource(index);
                const auto stocks=current.materialStocksAt(index);
                const auto stock=stocks[materialIndex(MaterialId::Food)];
                const bool delivered=current.takeHarvest(x-1,y,1,0,MaterialId::Food,1);
                CHECK(delivered==(stock>0));
                auto expected=before;
                if(delivered)
                {
                    const auto& yield=current.resourceRegistry().yields(static_cast<ResourceId>(deposit.type))[materialIndex(MaterialId::Food)];
                    if(yield.consumption==ResourceConsumption::All || yield.destroysDeposit)
                        for(unsigned m=0;m<MaterialCount;++m) expected[m]-=stocks[m];
                    else if(yield.consumption==ResourceConsumption::One) --expected[materialIndex(MaterialId::Food)];
                }
                CHECK(invariant(current)==expected);
            }
            else if(tick%4==2)
            {
                const auto selected=current.pickFarmHarvestTileSlot(x-1,y,materialIndex(MaterialId::Food),1);
                if(selected)
                {
                    const auto& deposit=current.getResource(*selected);
                    const auto& yield=current.resourceRegistry().yields(static_cast<ResourceId>(deposit.type))[materialIndex(MaterialId::Food)];
                    CHECK(yield.consumption!=ResourceConsumption::All);
                    CHECK_FALSE(yield.destroysDeposit);
                    const auto selectedStock=current.materialAmountAt(*selected,MaterialId::Food);
                    // Infinite stock is usable even below the configured reserve:
                    // harvesting cannot deplete either the stock or the reserve.
                    if(yield.consumption==ResourceConsumption::Infinite) CHECK(selectedStock>0);
                    else CHECK(selectedStock>yield.seedReserve);
                    REQUIRE(current.takeHarvest(x-1,y,1,0,MaterialId::Food,1));
                    if(yield.consumption==ResourceConsumption::Infinite)
                        CHECK(current.materialAmountAt(*selected,MaterialId::Food)==selectedStock);
                    else CHECK(current.materialAmountAt(*selected,MaterialId::Food)>=yield.seedReserve);
                }
            }
            else if(tick%8==7)
            {
                const auto deposit=current.getResource(index);
                current.decResource(x,y);
                const auto after=invariant(current);
                std::uint64_t removed=0;
                for(unsigned m=0;m<MaterialCount;++m) { REQUIRE(after[m]<=before[m]); removed+=before[m]-after[m]; }
                if(deposit.type!=NO_RES_TYPE)
                    CHECK(removed==(current.resourcePropertiesByIndex(deposit.type).clearConsumption==ResourceConsumption::All ? deposit.amount : std::min<Uint32>(1,deposit.amount)));
                else CHECK(removed==0);
            }
            else if(tick%4==3) current.growResourceStock(index);
            game.syncStep(0);
            invariant(current);
        };
        record(seed,original.game);
        for(unsigned tick=0;tick<17;++tick) { advance(original.game,tick); record(seed,original.game); }
        auto* memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(memory);
        original.game.save(&output,false,"Frozen generated resource composition"); output.flush();
        auto* copied=new GAGCore::MemoryStreamBackend(*memory); copied->seekFromStart(0);
        GAGCore::BinaryInputStream input(copied);
        glob2test::HeadlessGame restored(options);
        REQUIRE(restored.game.load(&input));
        CHECK(restored.game.map.resourceRegistry().digest()==map.resourceRegistry().digest());
        for(unsigned tick=17;tick<49;++tick)
        {
            CAPTURE(tick);
            advance(original.game,tick); advance(restored.game,tick);
            REQUIRE(original.game.checkSum(nullptr,nullptr,nullptr,true)==restored.game.checkSum(nullptr,nullptr,nullptr,true));
            CHECK(original.game.syncRandom==restored.game.syncRandom);
            record(seed,original.game);
        }
    }
    glob2test::writeFile(glob2test::artifactDir()/"seeded-compositions.trace",trace.str());
    glob2test::expectGolden("resources/seeded-compositions.trace",trace.str());
}

TEST_CASE("legacy resource saves ignore reordered modified installed defaults in a fresh process" * doctest::test_suite("RuntimeResources"))
{
    using Json=nlohmann::json;
    auto installed=Json::parse(ResourceRegistry::loadFile((glob2test::sourceRoot()/"data/resources/registry.json").string())->serialize());
    std::reverse(installed["resources"].begin(),installed["resources"].end());
    for(auto& resource:installed["resources"])
        if(resource["key"]=="trees") resource["properties"]["growthRate"]=0;
    const auto directory=glob2test::profileDir()/"data/resources";
    std::filesystem::create_directories(directory);
    const auto path=directory/"registry.json";
    struct Remove { std::filesystem::path path; ~Remove() { std::filesystem::remove(path); } } remove{path};
    glob2test::writeFile(path,installed.dump());
    glob2test::HeadlessGlobals globals;
    const auto defaults=ResourceRegistry::builtins();
    REQUIRE(defaults->properties(*defaults->find("trees")).growthRate==0);
    REQUIRE(resourceIndex(*defaults->find("trees"))==0);
    const auto bytes=glob2test::readFile(glob2test::inflated("javascript/profile1-initial.game.gz"));
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    GameGUI restored(false);
    REQUIRE(restored.game.load(&input));
    const auto& legacy=restored.game.map.resourceRegistry();
    REQUIRE(legacy.size()==8);
    CHECK(legacy.properties(*legacy.find("trees")).growthRate==ResourceRateScale);
    CHECK(legacy.digest()==ResourceRegistry::legacy()->digest());
}

TEST_CASE("custom ground and air obstruction independently govern routes across toroidal barriers" * doctest::test_suite("RuntimeResources"))
{
    glob2test::HeadlessGlobals globals;
    using Json=nlohmann::json;
    for (unsigned bits=0;bits<4;++bits)
    {
        CAPTURE(bits);
        glob2test::HeadlessGame fixture({.wDec=4,.hDec=4,.clearImmobile=true,.header=true});
        auto& map=fixture.game.map;
        auto definition=Json::parse(map.resourceRegistry().serialize())["resources"][0];
        definition["key"]="route-obstruction";
        definition["properties"]["blocksGround"]=bool(bits&1);
        definition["properties"]["blocksAir"]=bool(bits&2);
        map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
        const auto id=*map.resourceRegistry().find("route-obstruction");
        // Two complete columns prevent a blocked route wrapping around the torus.
        for(int y=0;y<map.getH();++y) for(int x:{1,5}) map.setResource(x,y,id,0);
        const auto route=[&](bool air) {
            int x=4,y=8;
            for(int steps=0;steps<32;++steps)
            {
                if(x==8 && y==8) return true;
                int dx=0,dy=0;
                const bool found=air ? map.pathfindAirPointToPoint(x,y,8,8,&dx,&dy)
                    : map.pathfindPointToPoint(x,y,8,8,&dx,&dy,0,1,32);
                if(!found) return false;
                REQUIRE((dx || dy));
                const int nx=(x+dx+map.getW())%map.getW(),ny=(y+dy+map.getH())%map.getH();
                REQUIRE(air ? map.isFreeForAirUnit(nx,ny) : map.isFreeForGroundUnit(nx,ny,false,1));
                x=nx;y=ny;
            }
            FAIL("A valid route must reach its goal within the bounded fixture");
            return false;
        };
        CHECK(route(false)==!(bits&1));
        CHECK(route(true)==!(bits&2));
        map.replaceResource(5,8,Resource{});
        CHECK(route(false));
        CHECK(route(true));
    }
}


TEST_CASE("released terrain-seed format 138 retains legacy resource and material continuation" * doctest::test_suite("RuntimeResources"))
{
    glob2test::HeadlessGlobals globals;
    const auto manifest = nlohmann::json::parse(glob2test::readFile(
        glob2test::sourceRoot()/"test/fixtures/resources/terrain-seed138.game.manifest.json"));
    const auto bytes = glob2test::readFile(glob2test::inflated("resources/terrain-seed138.game.gz"));
    // Undo the documented, sole format138 addition to compare the same authentic
    // format137 checkpoint. Never reinterpret an unpublished resource138 draft.
    auto legacyBytes = bytes;
    const size_t seedOffset = manifest["terrain_seed_offset"].get<size_t>();
    const size_t minorOffset = manifest["header_minor_offset"].get<size_t>();
    REQUIRE(seedOffset + 4 <= legacyBytes.size());
    REQUIRE(minorOffset + 4 <= legacyBytes.size());
    legacyBytes.erase(seedOffset, 4);
    legacyBytes[minorOffset + 3] = 137;
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size()));
    GAGCore::BinaryInputStream legacyInput(new GAGCore::MemoryStreamBackend(legacyBytes.data(), legacyBytes.size()));
    // The copying MemoryStreamBackend constructor writes the bytes, leaving its
    // cursor at the end. Rewind both complete game streams before loading.
    input.seekFromStart(0);
    legacyInput.seekFromStart(0);
    GameGUI restored(false), legacy(false);
    REQUIRE(restored.game.load(&input));
    REQUIRE(legacy.game.load(&legacyInput));
    auto &map = restored.game.map;
    CHECK(map.terrainSeed() == manifest["terrain_seed"].get<Uint32>());
    CHECK(legacy.game.map.terrainSeed() == 0);
    REQUIRE(map.resourceRegistry().size() == 8);
    CHECK(map.resourceRegistry().digest() == ResourceRegistry::legacy()->digest());
    CHECK(restored.game.stepCounter == 8192);
    unsigned deposits = 0, pendingHarvests = 0;
    for (size_t i = 0; i < size_t(map.w)*map.h; ++i)
    {
        CHECK(map.materialStocksAt(i) == legacy.game.map.materialStocksAt(i));
        deposits += map.getResource(i).type != NO_RES_TYPE;
        for (unsigned material = 8; material < MaterialCount; ++material)
            CHECK(map.materialAmountAtSlot(i, material) == 0);
    }
    REQUIRE(deposits > 0);
    for (int t = 0; t < restored.game.mapHeader.getNumberOfTeams(); ++t)
    {
        for (unsigned u = 0; u < Unit::MAX_COUNT; ++u)
            if (const auto *unit = restored.game.teams[t]->myUnits[u])
            {
                const auto *oldUnit = legacy.game.teams[t]->myUnits[u];
                REQUIRE(oldUnit != nullptr);
                CHECK(unit->movement == oldUnit->movement);
                CHECK(unit->destinationPurpose == oldUnit->destinationPurpose);
                CHECK(unit->carriedMaterial == oldUnit->carriedMaterial);
                pendingHarvests += unit->movement == Unit::MOV_HARVESTING;
            }
        for (unsigned b = 0; b < Building::MAX_COUNT; ++b)
            if (const auto *building = restored.game.teams[t]->myBuildings[b])
            {
                const auto *oldBuilding = legacy.game.teams[t]->myBuildings[b];
                REQUIRE(oldBuilding != nullptr);
                for (unsigned material = 0; material < MaterialCount; ++material)
                {
                    CHECK(building->materials[material] == oldBuilding->materials[material]);
                    if (material >= 8) CHECK(building->materials[material] == 0);
                }
            }
    }
    REQUIRE(pendingHarvests > 0);
    REQUIRE(restored.game.mapHeader.getVersionMinor() == 138);
    REQUIRE(legacy.game.mapHeader.getVersionMinor() == 137);
    // MapHeader equality deliberately excludes file versions. Verify all other
    // header metadata before normalizing that non-simulation checksum input.
    REQUIRE(restored.game.mapHeader == legacy.game.mapHeader);
    restored.game.mapHeader = legacy.game.mapHeader;
    // Full checksums include cached routing and pending unit state; terrain seed
    // is presentation-only and must not change continued simulation.
    for (unsigned tick = 0; tick < 32; ++tick)
    {
        CHECK(restored.game.checkSum(nullptr,nullptr,nullptr,true) == legacy.game.checkSum(nullptr,nullptr,nullptr,true));
        { auto random = restored.game.bindRandom(); restored.game.syncStep(0); }
        { auto random = legacy.game.bindRandom(); legacy.game.syncStep(0); }
    }
    CHECK(restored.game.checkSum(nullptr,nullptr,nullptr,true) == legacy.game.checkSum(nullptr,nullptr,nullptr,true));
}

TEST_CASE("released observation format 139 imports legacy stocks and preserves current save continuation" * doctest::test_suite("RuntimeResources"))
{
    glob2test::HeadlessGlobals globals;
    const auto bytes=glob2test::readFile(glob2test::inflated("resources/observation139.game.gz"));
    const auto manifest=nlohmann::json::parse(glob2test::readFile(
        glob2test::sourceRoot()/"test/fixtures/resources/observation139.game.manifest.json"));
    REQUIRE(Online::Sha256::hex(bytes)==manifest["decompressed_sha256"].get<std::string>());
    GAGCore::BinaryInputStream legacyInput(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    legacyInput.seekFromStart(0);
    GameGUI original(false),restored(false);
    REQUIRE(original.game.load(&legacyInput));
    REQUIRE(original.game.mapHeader.getVersionMinor()==139);
    REQUIRE(original.game.stepCounter==8193);
    CHECK(original.game.map.resourceRegistry().digest()==ResourceRegistry::legacy()->digest());
    REQUIRE(original.game.map.resourceRegistry().size()==8);
    unsigned pendingHarvests=0;
    for(int team=0;team<original.game.mapHeader.getNumberOfTeams();++team)
    {
        for(unsigned u=0;u<Unit::MAX_COUNT;++u)
            if(const auto* unit=original.game.teams[team]->myUnits[u])
                pendingHarvests+=unit->movement==Unit::MOV_HARVESTING;
        for(unsigned b=0;b<Building::MAX_COUNT;++b)
            if(const auto* building=original.game.teams[team]->myBuildings[b])
                for(unsigned material=8;material<MaterialCount;++material)
                    CHECK(building->materials[material]==0);
    }
    REQUIRE(pendingHarvests>0);
    auto* memory=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    original.game.save(&output,false,original.game.mapHeader.getMapName()); output.flush();
    auto* copy=new GAGCore::MemoryStreamBackend(*memory); copy->seekFromStart(0);
    GAGCore::BinaryInputStream currentInput(copy);
    REQUIRE(restored.game.load(&currentInput));
    REQUIRE(restored.game.mapHeader.getVersionMinor()==VERSION_MINOR);
    // File-format versions contribute to checksums but are not simulation state.
    // Verify the remaining header metadata before normalizing only that boundary.
    REQUIRE(original.game.mapHeader==restored.game.mapHeader);
    original.game.mapHeader=restored.game.mapHeader;
    for(unsigned tick=0;tick<32;++tick)
    {
        CHECK(original.game.checkSum(nullptr,nullptr,nullptr,true)==restored.game.checkSum(nullptr,nullptr,nullptr,true));
        original.game.syncStep(0); restored.game.syncStep(0);
    }
    CHECK(original.game.checkSum(nullptr,nullptr,nullptr,true)==restored.game.checkSum(nullptr,nullptr,nullptr,true));
}
