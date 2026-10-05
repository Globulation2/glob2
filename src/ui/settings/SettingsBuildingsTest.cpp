// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Settings.h"
#include "GameGUIDefaultAssignManager.h"
#include "FileFormatVersions.h"
#include <FileManager.h>
#include <Toolkit.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

TEST_SUITE("SettingsBuildings")
{
TEST_CASE("catalog preferences isolate reused stock keys and persist assignment and radius")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    auto& stock=globals->buildingsTypes;
    const auto stockFingerprint=stock.fingerprint();
    const int inn=stock.getTypeNum("inn",0,false);
    const int flag=stock.getTypeNum("warflag",0,false);
    Settings settings;
    settings.setBuildingAssignment(stockFingerprint,*stock.get(inn),9);
    settings.setBuildingRadius(stockFingerprint,*stock.get(flag),7);
    auto changed=nlohmann::json::parse(stock.snapshotJson());
    changed["variants"][inn]["properties"]["hpMax"]=stock.get(inn)->hpMax+1;
    world.game.buildingsTypes.loadSnapshotJson(changed.dump());
    const auto customFingerprint=world.game.buildingsTypes.fingerprint();
    REQUIRE(customFingerprint!=stockFingerprint);
    auto& custom=*world.game.buildingsTypes.get(inn);
    CHECK(custom.key==stock.get(inn)->key);
    CHECK(settings.buildingAssignment(customFingerprint,custom)==custom.presentation.defaultAssigned);
    settings.setBuildingAssignment(customFingerprint,custom,14);
    CHECK(settings.buildingAssignment(stockFingerprint,*stock.get(inn))==9);
    REQUIRE(settings.save("building-preferences-test.txt"));
    Settings loaded;loaded.load("building-preferences-test.txt");
    CHECK(loaded.buildingAssignment(stockFingerprint,*stock.get(inn))==9);
    CHECK(loaded.buildingAssignment(customFingerprint,custom)==14);
    CHECK(loaded.buildingRadius(stockFingerprint,*stock.get(flag))==7);
    globals->settings=loaded;
    GameGUIDefaultAssignManager manager(world.game);
    manager.setDefaultAssignedUnits(inn,12);
    CHECK(manager.getDefaultAssignedUnits(inn)==12);
    CHECK(globals->settings.buildingAssignment(stockFingerprint,*stock.get(inn))==9);
    CHECK(globals->settings.buildingAssignment(customFingerprint,custom)==12);
}
TEST_CASE("version one preferences import only into frozen stock catalog")
{
    glob2test::HeadlessGlobals globals;
    const auto filename="legacy-building-preferences.txt";
    const auto path=std::filesystem::path(GAGCore::Toolkit::getFileManager()->getDir(0))/filename;
    {
        std::ofstream file(path);
        file << "version=1\ndefaultUnitsAssigned[1][1]=11\ndefaultUnitsAssigned[1][2]=13\ndefaultFlagRadius[1]=9\n";
    }
    Settings settings;settings.load(filename);
    BuildingsTypes frozen;frozen.initLegacy();
    const auto fingerprint=frozen.fingerprint();
    REQUIRE(fingerprint==globals->buildingsTypes.fingerprint());
    CHECK(settings.buildingAssignment(fingerprint,*frozen.getByType("inn",0,false))==11);
    CHECK(settings.buildingAssignment(fingerprint,*frozen.getByType("inn",1,true))==13);
    const auto& flag=*frozen.getByType("warflag",0,false);
    CHECK(settings.buildingRadius(fingerprint,flag)==std::min(9,flag.maxUnitStayRange));
    REQUIRE(settings.save(filename));
    const auto saved=glob2test::readFile(path);
    CHECK(saved.find("defaultUnitsAssigned[")==std::string::npos);
    CHECK(saved.find("defaultFlagRadius[")==std::string::npos);
    CHECK(saved.find("version=2")!=std::string::npos);
}
TEST_CASE("per game assignment overrides use stable keys and import old numeric saves")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    auto& game=world.game;
    const int type=game.buildingsTypes.getTypeNum("inn",0,false);
    GameGUIDefaultAssignManager original(game);original.setDefaultAssignedUnits(type,7);
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);original.save(&output);output.flush();
    const auto data=backend->takeContents();
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(data.data(),data.size()));
    input.seekFromStart(0);
    GameGUIDefaultAssignManager loaded(game);loaded.load(&input,FILE_FORMAT_VERSION_BUILDING_CATALOG);
    CHECK(loaded.getDefaultAssignedUnits(type)==7);
    auto* oldBackend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream old(oldBackend);
    old.writeEnterSection("GameGUIDefaultAssignManager");old.writeEnterSection("unitCount");old.writeUint32(1,"size");old.writeEnterSection(0);
    old.writeSint32(type,"building_type");old.writeSint32(8,"default_assigned");
    old.writeLeaveSection();old.writeLeaveSection();old.writeLeaveSection();old.flush();
    const auto oldData=oldBackend->takeContents();
    GAGCore::BinaryInputStream oldInput(new GAGCore::MemoryStreamBackend(oldData.data(),oldData.size()));
    oldInput.seekFromStart(0);
    loaded.load(&oldInput,135);CHECK(loaded.getDefaultAssignedUnits(type)==8);
}
}
