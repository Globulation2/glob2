// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "sgsl/SGSL.h"

TEST_SUITE("LegacyAreaWait")
{
    TEST_CASE("legacy setArea waits retain the area name across team-selector tokens")
    {
        glob2test::HeadlessGlobals globals;
        for (const char* selector : {"0","ally(0)","enemy(1)"})
        {
            INFO(selector);
            glob2test::HeadlessGame world(glob2test::GameOptions{
                .teams=2,.clearImmobile=true,.loadDefaultRace=true,.header=true});
            MapScriptSGSL script;
            script.sourceCode=std::string("setArea(\"legacy\",6,6,2) wait(area(\"legacy\",")+selector+")) win(0)";
            REQUIRE(script.compileScript(&world.game).type==ErrorReport::ET_OK);
            world.addUnit(WORKER,20,20);
            script.syncStep(&world.gui);
            CHECK_FALSE(script.hasTeamWon(0));
            world.addUnit(WORKER,4,4);
            script.syncStep(&world.gui);
            CHECK(script.hasTeamWon(0));
        }
    }
    TEST_CASE("legacy area negation checks the selected team and wraps map edges")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world(glob2test::GameOptions{
            .teams=2,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        world.addUnit(WORKER,31,31);
        world.addUnit(WORKER,0,0,1);
        MapScriptSGSL script;
        script.sourceCode="setArea(\"edge\",0,0,2) wait(not(area(\"edge\",0))) win(0)";
        REQUIRE(script.compileScript(&world.game).type==ErrorReport::ET_OK);
        script.syncStep(&world.gui);
        CHECK_FALSE(script.hasTeamWon(0));
        world.game.map.setGroundUnit(31,31,NOGUID);
        script.syncStep(&world.gui);
        CHECK(script.hasTeamWon(0));
    }
}
