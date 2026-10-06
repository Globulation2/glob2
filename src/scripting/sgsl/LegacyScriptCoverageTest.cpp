// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "SGSL.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <array>
#include "AI.h"
#include "Player.h"
#include <nlohmann/json.hpp>

namespace
{
struct World
{
    glob2test::HeadlessGame world{glob2test::GameOptions{
        .teams = 2, .clearImmobile=true, .loadDefaultRace=true, .header=true}};
    MapScriptSGSL script;
    World()
    {
        world.gui.init();
        world.game.map.areaNames[0] = "zone";
        for (int y = 4; y < 8; ++y)
            for (int x = 4; x < 8; ++x) world.game.map.setPoint(0,x,y);
    }
    ErrorReport compile(const std::string& source)
    {
        script.sourceCode=source;
        return script.compileScript(&world.game);
    }
    void step(int count = 1) { while (count--) script.syncStep(world.game, world.gui, world.gui.clientRequests); }
};
int units(const Team* team)
{
    int result = 0;
    for (int id = 0; id<Unit::MAX_COUNT; ++id) if (team->myUnits[id]) ++result;
    return result;
}
std::string save(MapScriptSGSL& script, Game& game)
{
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(bytes);
    script.save(&out,&game);
    out.flush();
    return bytes->takeContents();
}
}

TEST_SUITE("LegacyScriptCoverage")
{
    TEST_CASE("supported statements and wait forms compile")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        for (const char* source : {
            "summonUnits(\"zone\",30,Worker,3,0)",
            "summonUnits(\"zone\",0,Explorer,0,1)",
            "summonUnits(\"zone\",1,Warrior,1,0)",
            "setArea(\"old\",10,10,4) summonUnits(\"old\",2,Worker,0,0)",
            "summonFlag(\"attack\",3,5,4,2,0) destroyFlag(\"attack\")",
            "alliance(0,1,3) guiDisable(BuildingTab) guiEnable(FlagTab)",
            "show(\"hello\") hide timer(3) win(0) loose(1)",
            "label(\"start\") wait(1) jump(\"start\")",
            "wait(isdead(1))", "wait(area(\"zone\",0))",
            "wait(not(area(\"zone\",enemy(0))))", "wait(area(\"zone\",ally(0)))",
            "wait(Worker(0) > 2)", "wait(Explorer(0) = 0)", "wait(Warrior(1) < 3)",
            "wait(Inn(0,0) > 0)", "wait(only Swarm(0,1) = 1)",
            "wait(not(only Hospital(1,2) < 1))", "space", "wait(1) story wait(2)"})
        {
            INFO(source);
            REQUIRE(w.compile(source).type == ErrorReport::ET_OK);
        }
    }

    TEST_CASE("invalid references and value boundaries report typed errors")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        struct Invalid { const char* source; ErrorReport::ErrorType error; };
        for (const auto& entry : {
            Invalid{"summonUnits(\"missing\",1,Worker,0,0)",ErrorReport::ET_UNDEFINED_AREA_NAME},
            Invalid{"summonUnits(\"zone\",31,Worker,0,0)",ErrorReport::ET_INVALID_VALUE},
            Invalid{"summonUnits(\"zone\",1,Worker,4,0)",ErrorReport::ET_INVALID_VALUE},
            Invalid{"summonUnits(\"zone\",1,Worker,0,2)",ErrorReport::ET_INVALID_TEAM},
            Invalid{"summonUnits(\"zone\",1,Inn,0,0)",ErrorReport::ET_SYNTAX_ERROR},
            Invalid{"setArea(\"old\",1,1,0)",ErrorReport::ET_INVALID_VALUE},
            Invalid{"setArea(\"old\",1,1,2) setArea(\"old\",2,2,2)",ErrorReport::ET_DUPLICATED_AREA_NAME},
            Invalid{"alliance(0,1,4)",ErrorReport::ET_INVALID_ALLIANCE_LEVEL},
            Invalid{"alliance(2,0,1)",ErrorReport::ET_INVALID_TEAM},
            Invalid{"jump(\"missing\")",ErrorReport::ET_UNDEFINED_LABEL},
            Invalid{"wait(0)",ErrorReport::ET_INVALID_VALUE},
            Invalid{"wait(only Worker(0) = 1)",ErrorReport::ET_INVALID_ONLY},
            Invalid{"wait(Inn(0,6) > 0)",ErrorReport::ET_INVALID_VALUE},
            Invalid{"wait(area(\"missing\",0))",ErrorReport::ET_UNDEFINED_AREA_NAME},
            Invalid{"wait(Worker(2) = 0)",ErrorReport::ET_INVALID_TEAM},
            Invalid{"win(2)",ErrorReport::ET_INVALID_TEAM}})
        {
            CAPTURE(entry.source);
            const auto report=w.compile(std::string("\n")+entry.source);
            CHECK(report.type == entry.error);
            CHECK(report.line >= 1);
            CHECK(report.pos <= w.script.sourceCode.size());
        }
    }

    TEST_CASE("truncated arguments fail without accepting incomplete statements")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        for (const std::string source : {
            "summonUnits(\"zone\",2,Worker,0,0)", "setArea(\"old\",1,2,3)",
            "summonFlag(\"f\",1,2,3,4,0)", "destroyFlag(\"f\")", "alliance(0,1,2)",
            "wait(not(area(\"zone\",enemy(0))))", "wait(only Inn(0,0) > 1)",
            "timer(2)", "show(\"text\")", "guiDisable(BuildingTab)", "win(0)"})
        {
            for (size_t length=source.find('('); length<source.size(); ++length)
            {
                INFO(source); CAPTURE(length);
                CHECK(w.compile(source.substr(0,length)).type != ErrorReport::ET_OK);
            }
        }
    }

    TEST_CASE("summons respect painted area capacity and unit attributes")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        REQUIRE(w.compile("summonUnits(\"zone\",30,Warrior,2,1)").type == ErrorReport::ET_OK);
        w.step();
        CHECK(units(w.world.game.teams[0]) == 0);
        CHECK(units(w.world.game.teams[1]) == 16);
        for (int id = 0; id<Unit::MAX_COUNT; ++id) if (auto* unit=w.world.game.teams[1]->myUnits[id])
        {
            CHECK(unit->typeNum == WARRIOR);
            CHECK(w.world.game.map.isPointSet(0,unit->posX,unit->posY));
            CHECK(unit->level[ATTACK_STRENGTH] == 2);
        }
        w.step();
        CHECK(units(w.world.game.teams[1]) == 16);
    }

    TEST_CASE("alliance levels preserve unrelated teams and apply vision masks")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        for (int level = 0; level < 4; ++level)
        {
            REQUIRE(w.compile("alliance(0,1,"+std::to_string(level)+")").type == ErrorReport::ET_OK);
            w.step();
            const auto* team=w.world.team;
            CHECK(bool(team->allies & 2) == (level== 3));
            CHECK(bool(team->enemies & 2) == (level== 0));
            CHECK(bool(team->sharedVisionExchange & 2) == (level>= 1));
            CHECK(bool(team->sharedVisionFood & 2) == (level>= 2));
            CHECK(bool(team->sharedVisionOther & 2) == (level== 3));
            CHECK((team->allies & 1) == 1);
        }
    }

    TEST_CASE("area waits observe team alliance and negation instead of unrelated units")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        for (const char* who : {"0", "ally(0)", "enemy(1)"})
        {
            REQUIRE(w.compile(std::string("wait(area(\"zone\",")+who+")) win(0)").type == ErrorReport::ET_OK);
            w.step();
            CHECK_FALSE(w.script.hasTeamWon(0));
            auto* unit=w.world.addUnit(WORKER,4,4);
            w.step();
            CHECK(w.script.hasTeamWon(0));
            // Move off the scripted area without changing the team queried.
            w.world.game.map.setGroundUnit(4,4,NOGUID);
            w.world.game.map.setGroundUnit(20,20,unit->gid);
            unit->posX = 20; unit->posY = 20;
        }
        REQUIRE(w.compile("wait(not(area(\"zone\",0))) loose(1)").type == ErrorReport::ET_OK);
        w.step();
        CHECK(w.script.hasTeamLost(1));
    }

    TEST_CASE("count waits evaluate all building classes exact levels and unit comparisons")
    {
        glob2test::HeadlessGlobals globals;
        const std::pair<const char*,const char*> classes[] = {
            {"Swarm","swarm"},{"Inn","inn"},{"Hospital","hospital"},
            {"Racetrack","racetrack"},{"Pool","swimmingpool"},{"Camp","barracks"},
            {"School","school"},{"Tower","defencetower"},{"Market","market"},
            {"Wall","stonewall"},{"ExplorationFlag","explorationflag"},
            {"WarFlag","warflag"},{"ClearingFlag","clearingflag"}};
        for (const auto& [token,type] : classes)
        {
            INFO(std::string(token));
            World w;
            REQUIRE(w.compile(std::string("wait(only ")+token+"(0,1) = 1) win(0)").type==ErrorReport::ET_OK);
            w.step(); CHECK_FALSE(w.script.hasTeamWon(0));
            w.world.addBuilding(type,4,4);
            for (int sample=0; sample<TeamStats::STATS_SMOOTH_SIZE; ++sample) w.world.team->stats.step(w.world.team);
            w.step(); CHECK(w.script.hasTeamWon(0));
            REQUIRE(w.compile(std::string("wait(")+token+"(0,0) > 0) win(0)").type==ErrorReport::ET_OK);
            w.step(); CHECK(w.script.hasTeamWon(0));
            REQUIRE(w.compile(std::string("wait(not(only ")+token+"(0,0) < 1)) win(0)").type==ErrorReport::ET_OK);
            w.step(); CHECK_FALSE(w.script.hasTeamWon(0));
        }
        World w;
        for (const auto& [token,type] : {std::pair{"Worker",WORKER},std::pair{"Explorer",EXPLORER},std::pair{"Warrior",WARRIOR}})
        {
            REQUIRE(w.compile(std::string("wait(")+token+"(0) > 0) win(0)").type==ErrorReport::ET_OK);
            w.step(); CHECK_FALSE(w.script.hasTeamWon(0));
            w.world.addUnit(type); for (int sample=0; sample<TeamStats::STATS_SMOOTH_SIZE; ++sample) w.world.team->stats.step(w.world.team);
            w.step(); CHECK(w.script.hasTeamWon(0));
        }
        REQUIRE(w.compile("wait(isdead(1)) win(0)").type==ErrorReport::ET_OK);
        w.step(); CHECK_FALSE(w.script.hasTeamWon(0));
        w.world.game.teams[1]->isAlive=false;
        w.step(); CHECK(w.script.hasTeamWon(0));
    }

    TEST_CASE("flag lifecycle and GUI commands change their real game objects")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        REQUIRE(w.compile("summonFlag(\"attack\",4,4,5,7,0) wait(1) destroyFlag(\"attack\") win(0)").type==ErrorReport::ET_OK);
        w.step(); REQUIRE(w.script.flags.size()==1);
        auto* flag=w.script.flags.at("attack");
        CHECK(flag->posX==4); CHECK(flag->posY==4); CHECK(flag->unitStayRange==5);
        CHECK(flag->maxUnitWorking==7); CHECK(flag->maxUnitWorkingPreferred==7);
        w.step(2); CHECK(w.script.flags.empty()); CHECK(w.script.hasTeamWon(0));
        for (const auto& [token,name] : {std::pair{"Inn","inn"},std::pair{"WarFlag","warflag"}})
        {
            REQUIRE(w.compile(std::string("guiDisable(")+token+") wait(1) guiEnable("+token+")").type==ErrorReport::ET_OK);
            w.step();
            if (std::string(token)=="Inn") CHECK_FALSE(w.world.gui.isBuildingEnabled(name));
            else CHECK_FALSE(w.world.gui.isFlagEnabled(name));
            w.step(2);
            if (std::string(token)=="Inn") CHECK(w.world.gui.isBuildingEnabled(name));
            else CHECK(w.world.gui.isFlagEnabled(name));
        }
        REQUIRE(w.compile("label(\"again\") wait(1) jump(\"again\") win(0)").type==ErrorReport::ET_OK);
        w.step(12); CHECK_FALSE(w.script.hasTeamWon(0));
    }

    TEST_CASE("authored flag aliases resolve game catalog IDs and respect missing definitions")
    {
        glob2test::HeadlessGlobals globals;
        for (const bool missing : {false, true})
        {
            World w;
            auto catalog=nlohmann::json::parse(w.world.game.buildingsTypes.snapshotJson());
            const int old=w.world.game.buildingsTypes.getTypeNum("warflag",0,false);
            auto flag=catalog["variants"][old]; flag["id"]=0;
            flag["semantics"]["assignmentLimit"]=3;
            flag["presentation"]["defaultAssigned"]=3;
            flag["properties"]["maxUnitStayRange"]=6;
            if (missing) flag["properties"]["type"]="different-authored-alias";
            catalog["variants"]=nlohmann::json::array({flag});
            catalog["startingBuilding"]="";
            w.world.game.buildingsTypes.loadSnapshotJson(catalog.dump());
            w.world.game.configureBuildingCatalog();
            REQUIRE(w.compile("summonFlag(\"attack\",4,4,99,99,0) wait(1)").type==ErrorReport::ET_OK);
            w.step();
            if (missing) CHECK(w.script.flags.empty());
            else
            {
                REQUIRE(w.script.flags.size()==1);
                auto* placed=w.script.flags.at("attack");
                CHECK(placed->typeNum==0);
                CHECK(placed->unitStayRange==6);
                CHECK(placed->maxUnitWorking==3);
                CHECK(placed->maxUnitWorkingPreferred==3);
            }
        }
    }

    TEST_CASE("independent stories timers space and saved suspension resume exactly")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        REQUIRE(w.compile("show(\"begin\") wait(3) hide win(0) story wait(1) loose(1)").type == ErrorReport::ET_OK);
        w.step();
        CHECK(w.script.isTextShown);
        CHECK_FALSE(w.script.hasTeamWon(0));
        const auto bytes=save(w.script,w.world.game);
        MapScriptSGSL restored;
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        input.seekFromStart(0);
        REQUIRE(restored.load(&input,&w.world.game));
        for (int tick = 0; tick < 5; ++tick)
        {
            w.step(); restored.syncStep(w.world.game, w.world.gui, w.world.gui.clientRequests);
            CHECK(save(w.script,w.world.game) == save(restored,w.world.game));
        }
        CHECK(w.script.hasTeamWon(0));
        CHECK(w.script.hasTeamLost(1));
        CHECK_FALSE(w.script.isTextShown);
        REQUIRE(w.compile("timer(3) win(0) space loose(1)").type == ErrorReport::ET_OK);
        w.step(); CHECK_FALSE(w.script.hasTeamWon(0));
        w.step(3); CHECK(w.script.hasTeamWon(0)); CHECK_FALSE(w.script.hasTeamLost(1));
        w.world.gui.setIsSpaceSet(true);
        w.step(); CHECK(w.script.hasTeamLost(1)); CHECK_FALSE(w.world.gui.isSpaceSet());
    }
    TEST_CASE("resetAI changes only the chosen player and rejects unsafe indices")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        const auto other=w.world.game.players[1]->type;
        REQUIRE(w.compile("resetAI(0,2)").type==ErrorReport::ET_OK);
        w.step();
        REQUIRE(w.world.game.players[0]->ai);
        CHECK(w.world.game.players[0]->ai->implementationID==AI::CASTOR);
        CHECK(w.world.game.players[1]->type==other);
        for (const char* source : {"resetAI(2,2)","resetAI(999,2)","resetAI(0,10)","resetAI(0,999)"})
        {
            INFO(source);
            CHECK(w.compile(source).type==ErrorReport::ET_INVALID_VALUE);
        }
    }

    TEST_CASE("resetAI authoring accepts valid slots before an editor map has a player header")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame w(glob2test::GameOptions{.teams=2,.clearImmobile=true,.loadDefaultRace=true});
        REQUIRE(w.game.gameHeader.getNumberOfPlayers()==0);
        MapScriptSGSL script; script.sourceCode="resetAI(0,2)";
        CHECK(script.compileScript(&w.game).type==ErrorReport::ET_OK);
        script.syncStep(w.game,w.gui,w.gui.clientRequests); // No active player means no runtime replacement.
        CHECK(w.game.players[0]==nullptr);
        script.sourceCode="resetAI(16,2)";
        CHECK(script.compileScript(&w.game).type==ErrorReport::ET_INVALID_VALUE);
    }

    TEST_CASE("legacy resetAI tokens with invalid player or implementation leave players intact")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        for (const auto invalid : {std::pair{999,2},std::pair{0,999},std::pair{-1,2},std::pair{0,-1}})
        {
            REQUIRE(w.compile("resetAI(0,2)").type==ErrorReport::ET_OK);
            REQUIRE(w.script.stories.size()==1);
            auto& tokens=w.script.stories.front().line;
            REQUIRE(tokens.size()>=3);
            tokens[1].value=invalid.first; tokens[2].value=invalid.second;
            const auto first=w.world.game.players[0]->type;
            const auto second=w.world.game.players[1]->type;
            w.step();
            CHECK(w.world.game.players[0]->type==first);
            CHECK(w.world.game.players[1]->type==second);
        }
    }

    TEST_CASE("objectives and hints resolve script numbers and persist in game snapshots")
    {
        glob2test::HeadlessGlobals globals;
        World w;
        auto& goals=w.world.game.objectives; auto& hints=w.world.game.gameHints;
        goals.removeObjective(0); // Fresh games install the default victory objective.
        goals.addNewObjective("other",false,false,false,GameObjectives::Primary,3);
        goals.addNewObjective("target",true,false,false,GameObjectives::Secondary,7);
        hints.addNewHint("other hint",false,3); hints.addNewHint("target hint",true,8);
        REQUIRE(w.compile("objectiveVisible(7) hintVisible(8)").type==ErrorReport::ET_OK);
        w.step(); CHECK(goals.isObjectiveVisible(1)); CHECK(hints.isHintVisible(1));
        CHECK_FALSE(goals.isObjectiveComplete(1));
        REQUIRE(w.compile("objectiveComplete(7)").type==ErrorReport::ET_OK);
        w.step(); CHECK(goals.isObjectiveComplete(1)); CHECK_FALSE(goals.isObjectiveFailed(1));
        REQUIRE(w.compile("objectiveFailed(7) objectiveHidden(7) hintHidden(8)").type==ErrorReport::ET_OK);
        w.step(); CHECK_FALSE(goals.isObjectiveVisible(1)); CHECK_FALSE(hints.isHintVisible(1));
        CHECK_FALSE(goals.isObjectiveComplete(1)); CHECK(goals.isObjectiveFailed(1));
        CHECK(goals.isObjectiveVisible(0)); CHECK_FALSE(goals.isObjectiveComplete(0));
        CHECK_FALSE(goals.isObjectiveFailed(0)); CHECK(hints.isHintVisible(0));
        REQUIRE(w.compile("objectiveComplete(999) objectiveVisible(999) hintHidden(999)").type==ErrorReport::ET_OK);
        w.step(); CHECK_FALSE(goals.isObjectiveComplete(0)); CHECK(hints.isHintVisible(0));
        auto* backend=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream out(backend);
        w.world.game.save(&out,false,"script objectives"); out.flush();
        const auto bytes=backend->takeContents(); GameGUI restored(false);
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        in.seekFromStart(0); REQUIRE(restored.game.load(&in));
        CHECK_FALSE(restored.game.objectives.isObjectiveComplete(1));
        CHECK(restored.game.objectives.isObjectiveFailed(1));
        CHECK_FALSE(restored.game.objectives.isObjectiveVisible(1));
        CHECK(restored.game.objectives.getScriptNumber(1)==7);
        CHECK_FALSE(restored.game.gameHints.isHintVisible(1));
        CHECK(restored.game.gameHints.getGameHintText(1)=="target hint");
    }

    TEST_CASE("named highlights target distinct GUI elements and unhighlight restores state")
    {
        glob2test::HeadlessGlobals globals; World w;
        for (const auto& entry : {
            std::pair{"main menu icon",GameGUI::HighlightMainMenuIcon},
            std::pair{"right side panel",GameGUI::HighlightRightSidePanel},
            std::pair{"under minimap icons",GameGUI::HighlightUnderMinimapIcon},
            std::pair{"units assigned bar",GameGUI::HighlightUnitsAssignedBar},
            std::pair{"units ratio bar",GameGUI::HighlightRatioBar},
            std::pair{"workers working free stat",GameGUI::HighlightWorkersWorkingFreeStat},
            std::pair{"explorers working free stat",GameGUI::HighlightExplorersWorkingFreeStat},
            std::pair{"warriors working free stat",GameGUI::HighlightWarriorsWorkingFreeStat},
            std::pair{"forbidden zone on panel",GameGUI::HighlightForbiddenZoneOnPanel},
            std::pair{"guard zone on panel",GameGUI::HighlightGuardZoneOnPanel},
            std::pair{"clearing zone on panel",GameGUI::HighlightClearingZoneOnPanel},
            std::pair{"brush selector",GameGUI::HighlightBrushSelector}})
        {
            INFO(entry.first);
            const std::string argument=std::string("(\"")+entry.first+"\")";
            REQUIRE(w.compile("hilightItem"+argument).type==ErrorReport::ET_OK);
            w.step(); CHECK(w.world.gui.highlights==std::set<int>{entry.second});
            REQUIRE(w.compile("unhilightItem"+argument).type==ErrorReport::ET_OK);
            w.step(); CHECK(w.world.gui.highlights.empty());
        }
        REQUIRE(w.compile("hilightItem(\"unknown item\")").type==ErrorReport::ET_OK);
        w.step(); CHECK(w.world.gui.highlights.empty());
    }

    TEST_CASE("unit and building highlights use map and panel channels independently")
    {
        glob2test::HeadlessGlobals globals; World w;
        REQUIRE(w.compile("hilightUnits(Worker) hilightBuildings(Inn) hilightBuildingOnPanel(Inn)").type==ErrorReport::ET_OK);
        w.step(); CHECK(w.world.gui.highlights.size()==3);
        CHECK(w.world.gui.highlights.count(GameGUI::HighlightWorkers)==1);
        CHECK(w.world.gui.highlights.count(GameGUI::HighlightBuildingOnMap+IntBuildingType::FOOD_BUILDING)==1);
        CHECK(w.world.gui.highlights.count(GameGUI::HighlightBuildingOnPanel+IntBuildingType::FOOD_BUILDING)==1);
        REQUIRE(w.compile("unhilightUnits(Worker) unhilightBuildings(Inn) unhilightBuildingOnPanel(Inn)").type==ErrorReport::ET_OK);
        w.step(); CHECK(w.world.gui.highlights.empty());
    }

}
