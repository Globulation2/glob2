// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "WarriorTacticsTrace.h"
#include "sim/snapshot/WorldSnapshot.h"
#include <fstream>
#include <sstream>

namespace
{
struct TraceFixture
{
    SimulationSnapshot::Handle world;
    std::shared_ptr<SimulationSnapshot::Entities> entities=std::make_shared<SimulationSnapshot::Entities>();
    TraceFixture()
    {
        world.worldIdentity=1;world.width=world.height=128;world.entities=entities;
        auto teams=std::make_shared<SimulationSnapshot::Teams>();
        teams->values.resize(2);teams->values[0].alive=true;teams->values[1].number=1;
        world.teams=teams;
        auto catalogs=std::make_shared<SimulationSnapshot::Catalogs>();
        auto kinds=std::make_shared<std::vector<SimulationSnapshot::BuildingKindView>>(2);
        kinds->at(0).isVirtual=true;kinds->at(0).zonable[WARRIOR]=1;
        catalogs->buildings=kinds;world.catalogs=catalogs;
        SimulationSnapshot::BuildingView flag{};flag.identity={0,10};flag.gid=0;
        flag.posX=20;flag.posY=20;flag.unitStayRange=3;
        entities->buildings.push_back(flag);entities->buildingSlotIndices={0};
        SimulationSnapshot::UnitView unit{};unit.identity={0,11};unit.gid=0;
        unit.capabilityFlags=UnitRuntimeTraits::Melee;unit.performance[ATTACK_SPEED]=1;
        unit.performance[HP]=100;unit.hp=100;unit.realAttackStrength=10;
        unit.attached=flag.identity;unit.displacement=UnitState::DIS_GOING_TO_FLAG;
        entities->units.push_back(unit);
    }
};
std::vector<std::vector<std::string>> rows(const std::filesystem::path& path)
{
    std::ifstream input(path);std::vector<std::vector<std::string>> result;
    std::string line;std::getline(input,line);
    while(std::getline(input,line))
    {
        std::istringstream stream(line);std::vector<std::string> row;
        std::string value;while(std::getline(stream,value,',')) row.push_back(value);
        result.push_back(row);
    }
    return result;
}
}
TEST_SUITE("WarriorTacticsTrace")
{
TEST_CASE("accounts exact intervals through travel contact and medical detachment [artifacts]")
{
    const auto dir=glob2test::artifactDir()/"warrior-accounting";
    std::filesystem::create_directories(dir);TraceFixture f;
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());
    trace.capture(f.world);trace.capture(f.world); // duplicate observations add no time
    f.world.tick=1;f.entities->units[0].posX=1;trace.capture(f.world);
    f.world.tick=2;f.entities->units[0].movement=UnitState::MOV_ATTACKING_TARGET;trace.capture(f.world);
    f.world.tick=3;f.entities->units[0].movement=UnitState::MOV_GOING_TARGET;
    f.entities->units[0].attached={};f.entities->units[0].medical=UnitState::MED_HUNGRY;
    trace.capture(f.world);
    f.world.tick=4;trace.capture(f.world);trace.finish();trace.finish();
    const auto data=rows(dir/"warrior-episodes.csv");REQUIRE(data.size()==3);
    CHECK(data[0][7]=="to_flag");CHECK(data[0][19]=="2");CHECK(data[0][20]=="1");
    CHECK(data[1][7]=="fighting");CHECK(data[1][19]=="1");
    CHECK(data[2][7]=="to_food");CHECK(data[2][13]=="0");CHECK(data[2][14]=="10");
    CHECK(data[2][19]=="1");CHECK(data[2][23]=="right_censored");
    const auto journeys=rows(dir/"warrior-journeys.csv");REQUIRE(journeys.size()==1);
    // Fighting on the way is contact, not arrival at the distant flag.
    CHECK(journeys[0][14]=="-1");CHECK(journeys[0][15]=="2");CHECK(journeys[0][17]=="detached");
    const auto windows=rows(dir/"warrior-windows.csv");REQUIRE(windows.size()==4);
    CHECK(windows[2][2]=="0");CHECK(windows[2][3]=="4");
    CHECK(windows[2][11]=="1");CHECK(windows[2][12]=="2");CHECK(windows[2][14]=="1");
    CHECK(windows[2][21]=="4");
}
TEST_CASE("flag moves ownership changes and recycled slots close distinct journeys [artifacts]")
{
    const auto dir=glob2test::artifactDir()/"warrior-identity";
    std::filesystem::create_directories(dir);TraceFixture f;
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());trace.capture(f.world);
    f.world.tick=1;f.entities->buildings[0].posX=40;trace.capture(f.world);
    f.world.tick=2;f.entities->units[0].team=1;trace.capture(f.world);
    f.world.tick=3;f.entities->units[0].identity.generation=12;trace.capture(f.world);
    f.world.tick=4;f.entities->units.clear();f.entities->buildings.clear();trace.capture(f.world);
    trace.finish();const auto data=rows(dir/"warrior-episodes.csv");REQUIRE(data.size()==4);
    CHECK(data[0][10]=="20");CHECK(data[1][10]=="40");CHECK(data[1][23]=="converted");
    CHECK(data[2][4]=="11");CHECK(data[2][23]=="disappeared");
    CHECK(data[3][4]=="12");CHECK(data[3][19]=="1");
    const auto flags=rows(dir/"warrior-flags.csv");REQUIRE(flags.size()==3);
    CHECK(flags[1][3]=="changed");CHECK(flags[2][3]=="removed");
}
TEST_CASE("identity replacing conversion is a disappearance without invented attribution [artifacts]")
{
    const auto dir=glob2test::artifactDir()/"warrior-identity-replacement";
    std::filesystem::create_directories(dir);TraceFixture f;
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());trace.capture(f.world);
    // Actual conversion allocates a new team slot and generation. Snapshots do
    // not include the conversion event, so these identities must stay separate.
    f.world.tick=1;
    auto& unit=f.entities->units[0];
    unit.team=1;unit.gid=1024;unit.identity={1024,12};unit.attached={};
    unit.medical=UnitState::MED_HUNGRY;
    trace.capture(f.world);
    f.world.tick=2;trace.capture(f.world);trace.finish();
    const auto data=rows(dir/"warrior-episodes.csv");REQUIRE(data.size()==2);
    CHECK(data[0][2]=="0");CHECK(data[0][19]=="1");
    CHECK(data[0][23]=="disappeared");
    CHECK(data[1][2]=="1");CHECK(data[1][3]=="1024");
    CHECK(data[1][13]=="65535");CHECK(data[1][19]=="1");
    CHECK(data[1][23]=="right_censored");
    const auto journeys=rows(dir/"warrior-journeys.csv");REQUIRE(journeys.size()==1);
    CHECK(journeys[0][17]=="disappeared");
}
TEST_CASE("arrival uses wrapped circular flag radius and survives combat state changes [artifacts]")
{
    const auto dir=glob2test::artifactDir()/"warrior-arrival";
    std::filesystem::create_directories(dir);TraceFixture f;
    f.entities->buildings[0].posX=0;f.entities->buildings[0].posY=0;
    f.entities->units[0].posX=124;f.entities->units[0].posY=0;
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());trace.capture(f.world);
    f.world.tick=1;f.entities->units[0].posX=126;trace.capture(f.world);
    f.world.tick=2;f.entities->units[0].movement=UnitState::MOV_ATTACKING_TARGET;trace.capture(f.world);
    f.world.tick=3;trace.capture(f.world);trace.finish();
    const auto journeys=rows(dir/"warrior-journeys.csv");REQUIRE(journeys.size()==1);
    CHECK(journeys[0][14]=="1");CHECK(journeys[0][15]=="2");CHECK(journeys[0][17]=="right_censored");
}
TEST_CASE("continuations are left censored and gaps never silently inflate durations [artifacts]")
{
    const auto dir=glob2test::artifactDir()/"warrior-continuation";
    std::filesystem::create_directories(dir);TraceFixture f;f.world.tick=500;
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());trace.capture(f.world);
    f.world.tick=501;trace.capture(f.world);f.world.tick=503;
    CHECK_THROWS_AS(trace.capture(f.world),std::runtime_error);
    trace.finish();const auto data=rows(dir/"warrior-episodes.csv");REQUIRE(data.size()==1);
    CHECK(data[0][5]=="500");CHECK(data[0][6]=="501");CHECK(data[0][22]=="1");
}
TEST_CASE("effective melee capabilities and actual attachment determine inclusion [artifacts]")
{
    const auto dir=glob2test::artifactDir()/"warrior-capabilities";
    std::filesystem::create_directories(dir);TraceFixture f;
    auto kinds=std::make_shared<std::vector<SimulationSnapshot::BuildingKindView>>(*f.world.catalogs->buildings);
    kinds->at(0).zonable[WARRIOR]=0;kinds->at(0).zonable[WORKER]=1;
    auto catalogs=std::make_shared<SimulationSnapshot::Catalogs>(*f.world.catalogs);
    catalogs->buildings=kinds;f.world.catalogs=catalogs;
    f.entities->units[0].typeNum=7; // No dependence on the built-in warrior ID.
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());trace.capture(f.world);
    f.world.tick=1;trace.capture(f.world);trace.finish();
    const auto data=rows(dir/"warrior-episodes.csv");REQUIRE(data.size()==1);
    CHECK(data[0][7]=="to_flag");CHECK(data[0][8]=="0");
}
TEST_CASE("service travel remains feeding or healing after the medical threshold clears [artifacts]")
{
    const auto dir=glob2test::artifactDir()/"warrior-service-travel";
    std::filesystem::create_directories(dir);TraceFixture f;
    auto& unit=f.entities->units[0];unit.attached={};
    unit.activity=UnitState::ACT_UPGRADING;unit.medical=UnitState::MED_FREE;
    unit.displacement=UnitState::DIS_GOING_TO_BUILDING;unit.destinationPurpose=HEAL;
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());trace.capture(f.world);
    f.world.tick=1;unit.destinationPurpose=FEED;trace.capture(f.world);
    f.world.tick=2;unit.destinationPurpose=ATTACK_SPEED;trace.capture(f.world);
    f.world.tick=3;trace.capture(f.world);trace.finish();
    const auto data=rows(dir/"warrior-episodes.csv");REQUIRE(data.size()==3);
    CHECK(data[0][7]=="to_heal");CHECK(data[1][7]=="to_food");CHECK(data[2][7]=="training");
}
TEST_CASE("window attrition counters use melee catalog classes and preserve cumulative history [artifacts]")
{
    glob2test::HeadlessGlobals globals;TraceFixture f;
    auto catalogs=std::make_shared<SimulationSnapshot::Catalogs>(*f.world.catalogs);
    catalogs->units=UnitCatalog::builtins();f.world.catalogs=catalogs;
    const auto warrior=catalogs->units->find("warrior"),worker=catalogs->units->find("worker");
    REQUIRE(warrior);REQUIRE(worker);
    auto statistics=std::make_shared<SimulationSnapshot::StatisticsInputs>();
    statistics->events.resize(2);f.world.statisticsInputs=statistics;
    auto& events=statistics->events[0];
    events.deaths[*warrior][GameplayMeasurements::COMBAT]=11;
    events.deaths[*warrior][GameplayMeasurements::STARVATION]=5;
    events.conversionsIn[*warrior]=7;events.conversionsOut[*warrior]=3;
    events.conversionsIn[*worker]=99; // The non-melee class must not enter these totals.
    const auto dir=glob2test::artifactDir()/"warrior-attrition";
    std::filesystem::create_directories(dir);
    GameDiagnostics::WarriorTacticsTrace trace(dir.string());trace.capture(f.world);
    f.world.tick=1;++events.deaths[*warrior][GameplayMeasurements::STARVATION];
    ++events.conversionsOut[*warrior];trace.capture(f.world);trace.finish();
    const auto data=rows(dir/"warrior-windows.csv");REQUIRE(data.size()==4);
    REQUIRE(data[0].size()==28);CHECK(data[0][0]=="2");
    CHECK(data[0][24]=="11");CHECK(data[0][25]=="5");
    CHECK(data[0][26]=="7");CHECK(data[0][27]=="3");
    CHECK(data[2][25]=="6");CHECK(data[2][27]=="4");
}
}
