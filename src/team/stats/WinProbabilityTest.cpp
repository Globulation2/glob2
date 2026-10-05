#include <nlohmann/json.hpp>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ScopedEnvironment.h"
#include <iostream>
#include "TeamStat.h"
#include "WinProbability.h"
#include "WinningConditions.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "FileFormatVersions.h"
#include "render/scene/SceneExtract.h"
#include <map>
#include <array>
#include <algorithm>
#include <sstream>

TEST_SUITE("WinProbability")
{
    TEST_CASE("combat facilities count concrete providers once, including explicit construction targets")
    {
        glob2test::HeadlessGlobals globals;
        for(int admitted:{7,1,4}) {
        CAPTURE(admitted);
        glob2test::HeadlessGame world({.header=true});
        auto& catalog=world.game.buildingsTypes;
        const int finished=catalog.getFinishedTypeNum("barracks");
        const int site=catalog.getPlaceableTypeNum("barracks");
        const int passive=catalog.getFinishedTypeNum("stonewall");
        auto snapshot=nlohmann::json::parse(catalog.snapshotJson());
        snapshot["variants"][finished]["properties"]["shortTypeNum"]=11;
        snapshot["variants"][passive]["properties"]["shortTypeNum"]=5;
        snapshot["variants"][finished]["semantics"]["admittedUnitMask"]=admitted;
        catalog.loadSnapshotJson(snapshot.dump());world.game.configureBuildingCatalog();
        auto* stat=world.team->stats.getLatestStat();
        stat->buildingCountByVariant.assign(catalog.size(),0);
        stat->buildingCountByVariant[finished]=2;
        stat->buildingCountByVariant[site]=3;
        stat->buildingCountByVariant[passive]=7;
        // Two combat services on each facility still describe one building.
        std::vector<int> alliances;
        auto slots=WinProbability::slotsOf(world.game,alliances);
        REQUIRE(slots.size()==1);
        CHECK(slots[0].barracks==((admitted&(1u<<WARRIOR))?5:0));
        if(!(admitted&(1u<<WARRIOR)))continue;
        world.game.stepCounter=512;
        std::ostringstream log;
        {
            glob2test::ScopedEnvironment enabled("GLOB2_TEAM_TIMELINE","1");
            struct Capture {std::streambuf* previous;~Capture(){std::cout.rdbuf(previous);}} capture{std::cout.rdbuf(log.rdbuf())};
            world.team->stats.step(world.team);
        }
        CHECK(log.str().find(" barracks=5 ")!=std::string::npos);
        CHECK(log.str().find(" variant_"+std::to_string(passive)+"=7")!=std::string::npos);
        }
    }

    TEST_CASE("equal alliances share chances and eliminated allies contribute nothing")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams = 3, .header = true});
        auto &game = world.game;
        game.gameHeader.setAllyTeamNumber(0, 1);
        game.gameHeader.setAllyTeamNumber(1, 1);
        game.gameHeader.setAllyTeamNumber(2, 2);
        for (int t = 0; t < 3; ++t)
            game.teams[t]->stats.getLatestStat()->totalUnit = t == 2 ? 20 : 10;
        std::vector<int> allianceOf;
        auto slots = WinProbability::slotsOf(game, allianceOf);
        CHECK((allianceOf == std::vector<int>{0, 0, 1}));
        CHECK((WinProbability::permille(slots) == std::vector<int>{500, 500}));
        game.teams[1]->hasLost = true;
        slots = WinProbability::slotsOf(game, allianceOf);
        CHECK(slots[0].units == 10);
        CHECK(slots[0].alive);
        Scene scene;
        SceneExtractor().extract(game, SceneRequest{}, scene);
        REQUIRE(scene.panels.hud.winChances.size() == 3);
        CHECK(scene.panels.hud.winChances[0].permille == scene.panels.hud.winChances[1].permille);
        CHECK(scene.panels.hud.winChances[2].permille > scene.panels.hud.winChances[0].permille);
    }

    TEST_CASE("the rule waits for the minimum tick and sample boundary then ends all teams")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams = 3, .header = true});
        auto &game = world.game;
        game.gameHeader.setAllyTeamNumber(0, 1);
        game.gameHeader.setAllyTeamNumber(1, 1);
        game.gameHeader.setAllyTeamNumber(2, 2);
        game.teams[0]->prestige = 1000;
        WinningConditionWinProbability condition;
        game.stepCounter = WinProbability::MINIMUM_DECISION_TICK - 512;
        CHECK_FALSE(condition.hasTeamWon(0, &game));
        game.stepCounter = WinProbability::MINIMUM_DECISION_TICK + 1;
        CHECK_FALSE(condition.hasTeamWon(0, &game));
        game.stepCounter = WinProbability::MINIMUM_DECISION_TICK;
        CHECK(condition.hasTeamWon(0, &game));
        CHECK(condition.hasTeamWon(1, &game));
        CHECK(condition.hasTeamLost(2, &game));
        CHECK_FALSE(condition.hasTeamLost(0, &game));
        game.gameHeader.getWinningConditions().clear();
        WinningCondition::setWinProbabilityWinCondition(game.gameHeader.getWinningConditions(), 970);
        game.wonSyncStep();
        CHECK(game.isGameEnded);
        CHECK(game.teams[0]->hasWon);
        CHECK(game.teams[1]->hasWon);
        CHECK(game.teams[2]->hasLost);
    }

    TEST_CASE("calling an early loser cannot change later teams' decision inputs")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams = 3, .header = true});
        auto &game = world.game;
        std::array<int, 3> roles = {0, 1, 2};
        do
        {
            int winner = -1;
            for (int t = 0; t < 3; ++t)
            {
                auto *team = game.teams[t];
                team->prestige = roles[t] == 0 ? 200 : 0;
                team->stats.getLatestStat()->totalUnit = roles[t] == 0 ? 1 : roles[t] == 1 ? 1000 : 100;
                team->stats.getLatestStat()->needFoodCritical = roles[t] == 1 ? 1000 : 0;
                team->hasLost = team->hasWon = false;
                team->winCondition = WCUnknown;
                if (roles[t] == 0) winner = t;
            }
            std::vector<int> allianceOf;
            const auto initial = WinProbability::permille(WinProbability::slotsOf(game, allianceOf));
            REQUIRE(initial[winner] >= 970);
            game.gameHeader.getWinningConditions().clear();
            WinningCondition::setWinProbabilityWinCondition(game.gameHeader.getWinningConditions(), 970);
            game.stepCounter = WinProbability::MINIMUM_DECISION_TICK;
            game.wonSyncStep();
            CHECK(game.isGameEnded);
            for (int t = 0; t < 3; ++t)
            {
                CHECK(game.teams[t]->hasWon == (t == winner));
                CHECK(game.teams[t]->hasLost == (t != winner));
            }
        } while (std::next_permutation(roles.begin(), roles.end()));
    }

    TEST_CASE("rule serialization gates the new tag and rejects ambiguous thresholds")
    {
        using namespace GAGCore;
        for (const Uint32 threshold : {0u, 500u, 501u, 970u, 1000u, 1001u})
        {
            auto *backend = new MemoryStreamBackend;
            BinaryOutputStream output(backend);
            WinningConditionWinProbability original;
            original.thresholdPermille = threshold;
            original.encodeData(&output);
            const auto bytes = backend->getBuffer();
            const auto size = backend->getPosition();
            BinaryInputStream input(new MemoryStreamBackend(bytes, size));
            input.seekFromStart(0);
            auto decoded = WinningCondition::getWinningCondition(&input, FILE_FORMAT_VERSION_WIN_PROBABILITY_RULE);
            CHECK(bool(decoded) == (threshold >= 501 && threshold <= 1000));
            if (decoded)
                CHECK(static_cast<WinningConditionWinProbability&>(*decoded).thresholdPermille == threshold);
            BinaryInputStream legacy(new MemoryStreamBackend(bytes, size));
            legacy.seekFromStart(0);
            CHECK_FALSE(WinningCondition::getWinningCondition(&legacy, FILE_FORMAT_VERSION_WIN_PROBABILITY_RULE - 1));
        }
    }
    TEST_CASE("enabled rule preserves the per-tick checksum trace across its decision boundary [golden][artifacts]")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame world({.teams = 2, .loadDefaultRace = true, .header = true, .seed = 42});
        world.addBuilding("inn", 4, 4, 0, 0);
        world.addBuilding("inn", 16, 4, 0, 1);
        world.addUnit(WORKER, 8, 8, 0);
        world.addUnit(WORKER, 20, 8, 1);
        auto &game = world.game;
        // Keep the header at the format used to record this fixture: the map
        // version contributes to Game::checkSum even when gameplay is unchanged.
        game.mapHeader.versionMinor = FILE_FORMAT_VERSION_WIN_PROBABILITY_RULE;
        game.teams[0]->prestige = 1000;
        game.gameHeader.getWinningConditions().clear();
        WinningCondition::setWinProbabilityWinCondition(game.gameHeader.getWinningConditions(), 970);
        game.stepCounter = WinProbability::MINIMUM_DECISION_TICK - 64;
        std::ostringstream trace;
        for (int tick = 0; tick < 128; ++tick)
        {
            world.step();
            trace << game.stepCounter << ' ' << world.checksum() << ' '
                  << game.teams[0]->hasWon << ' ' << game.teams[1]->hasLost << '\n';
            if (tick < 64)
                CHECK_FALSE(game.isGameEnded);
            if (game.isGameEnded)
                break;
        }
        CHECK(game.isGameEnded);
        CHECK(game.teams[0]->hasWon);
        CHECK(game.teams[1]->hasLost);
        glob2test::expectGolden("win-probability/rule-trace.txt", trace.str());
        glob2test::writeFile(glob2test::artifactDir() / "rule-checksums.txt", trace.str());
    }

    TEST_CASE("native arithmetic reproduces every published calibration sample")
    {
        std::map<int, std::vector<WinProbability::Slot>> samples;
        std::map<int, std::vector<int>> expected;
        std::istringstream input(glob2test::readFile(glob2test::sourceRoot() / "test/data/win-probability-engine-samples.txt"));
        std::string line;
        while (std::getline(input, line))
        {
            std::istringstream row(line);
            std::string token;
            row >> token;
            REQUIRE(token == "GLOB2_WINPROB");
            std::map<std::string, int> fields;
            while (row >> token)
            {
                const auto equal = token.find('=');
                REQUIRE(equal != std::string::npos);
                fields[token.substr(0, equal)] = std::stoi(token.substr(equal + 1));
            }
            WinProbability::Slot slot;
            slot.alive = fields.at("alive");
            slot.units = fields.at("units");
            slot.prestige = fields.at("prestige");
            slot.barracks = fields.at("barracks");
            slot.explorers = fields.at("explorers");
            slot.foodCritical = fields.at("foodCritical");
            slot.attack = fields.at("attack");
            REQUIRE(fields.at("alliance") == int(samples[fields.at("tick")].size()));
            samples[fields.at("tick")].push_back(slot);
            expected[fields.at("tick")].push_back(fields.at("permille"));
        }
        REQUIRE(samples.size() == 10);
        for (const auto &[tick, slots] : samples)
        {
            INFO(tick);
            CHECK(WinProbability::permille(slots) == expected.at(tick));
        }
    }

}
