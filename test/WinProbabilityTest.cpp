// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TeamStat.h"
#include "WinProbability.h"
#include "WinningConditions.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "FileFormatVersions.h"
#include "scene/SceneExtract.h"
#include <map>
#include <sstream>

TEST_SUITE("WinProbability")
{
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
