// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Game.h"
#include "Unit.h"
#include "Team.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Race.h"
#include <BinaryStream.h>
#include <FileManager.h>
#include <GzipUtil.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
namespace fs = std::filesystem;

static void save(GameGUI& gui, const fs::path& path)
{
    auto* memory = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    gui.save(&output, "Hiring study");
    std::string compressed;
    if (!GAGCore::gzipCompress(memory->takeContents(), 6, compressed)) throw std::runtime_error("Cannot compress fixture");
    std::ofstream file(path, std::ios::binary);
    file.write(compressed.data(), compressed.size());
    if (!file) throw std::runtime_error("Cannot write fixture");
}

int main(int argc, char** argv)
try
{
    if (argc != 4 || std::string(argv[1]) != "--prepare")
        throw std::runtime_error("Usage: HiringStudyHarness --prepare sustainable|contention OUTPUT_DIR");
    const std::string kind = argv[2];
    if (kind != "sustainable" && kind != "contention") throw std::invalid_argument("Unknown fixture kind");
    const fs::path directory = argv[3];
    if (fs::exists(directory)) throw std::runtime_error("Output directory already exists");
    fs::create_directories(directory);
    GlobalContainer globals; globalContainer = &globals;
    globals.runNoX = true; globals.settings.rememberUnit = false;
    globals.buildingsTypes.init(); IntBuildingType::init(); Race::loadDefault();
    GameGUI gui(false); auto& game = gui.game;
    game.map.setSize(7, 7, GRASS); game.map.setGame(&game);
    for (int t=0; t<2; ++t) { game.addTeam(t); game.teams[t]->race.loadDefault(); }
    GameHeader header; header.setNumberOfPlayers(2); header.setRandomSeed(713);
    // Both slots must supply lockstep orders when the save is replayed through
    // the real headless engine. Inactive AI supplies NullOrder without changing
    // demand; two local human slots would wait forever for the second player.
    for (int t=0; t<2; ++t) header.getBasePlayer(t)=BasePlayer(t, "hiring study", t, BasePlayer::playerTypeFromImplementationID(AI::NONE));
    game.setGameHeader(header, true); game.map.setMapDiscovered(); game.setWaitingOnMask(0);
    const int innType = game.buildingsTypes.getTypeNum("inn", 0, false);
    for (int t=0; t<2; ++t)
    {
        game.teams[t]->startPosX=t*64+4; game.teams[t]->startPosY=4;
        for (int n=0; n<24; ++n)
        {
            auto* b=game.addBuilding(t*64+3+(n%8)*7, 40+(n/8)*9, innType, t, 4, 4);
            if (!b) throw std::runtime_error("Cannot place service building");
            b->materials[WHEAT]=b->type->maxMaterial[WHEAT]/2;
            b->priority=kind=="contention" ? (n%3)-1 : 0;
            b->updateCallLists();
        }
        for (int y=85; y<120; ++y) for (int x=t*64+1; x<t*64+60; x+=3)
            for (int amount=0; amount<20; ++amount) game.map.incResourceByIndex(x,y,WHEAT,0);
        for (int n=0; n<600; ++n)
        {
            auto* u=game.addUnit(t*64+1+n%60, 4+n/60, t, WORKER, 0, n%256, 0, 0);
            if (!u) throw std::runtime_error("Cannot place worker");
            // Stagger early medical transitions so services and deliveries are
            // active during the retained window, using the stock hunger rate.
            u->hungry=u->trigHungry+u->hungriness*(16+n%128);
        }
        game.teams[t]->createLists();
    }
    save(gui, directory/"initial.game.gz");
    int minimum=1200;
    for (int tick=0; tick<4096; ++tick)
    {
        game.syncStep(0);
        int count=0; for (int t=0; t<2; ++t) count+=game.teams[t]->liveUnits.size();
        minimum=std::min(minimum,count);
        if (tick==2047) save(gui,directory/"checkpoint-2048.game.gz");
    }
    save(gui,directory/"final.game.gz");
    nlohmann::json report={{"schema_version",1},{"kind",kind},{"seed",713},{"ticks",4096},{"initial_workers",1200},{"minimum_workers",minimum}};
    std::ofstream summary(directory/"fixture.json"); summary << report.dump(2) << '\n';
    std::cout << report.dump() << '\n';
    if (minimum<1000) throw std::runtime_error("Fixture failed sustained population requirement");
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
