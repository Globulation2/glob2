// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include "GlobalContainer.h"
#include "Game.h"
#include "GameGUI.h"
#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"
#include "FileManager.h"
#include "Version.h"
#include <SDL.h>
#include <fstream>
#include <string>
#include <utility>
#include <initializer_list>
#include <stdexcept>
#include "TextStream.h"
#include "Unit.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void compare(TeamStats& expected, TeamStats& actual)
{
    const auto& a = *expected.getLatestStat();
    const auto& b = *actual.getLatestStat();
    require(a.totalUnit == b.totalUnit && a.totalHP == b.totalHP,
            "latest population and health survive loading and sampling");
    require(a.needFood == b.needFood && a.needFoodCritical == b.needFoodCritical &&
            a.needFoodNoInns == b.needFoodNoInns && a.needHeal == b.needHeal &&
            a.needNothing == b.needNothing, "Numbi medical-demand snapshot is unchanged");
    require(a.totalFree == b.totalFree && a.totalNeeded == b.totalNeeded,
            "smoothed labor totals are unchanged");
    for (int type = 0; type < NB_UNIT_TYPE; ++type)
        require(expected.getTotalUnits(type) == actual.getTotalUnits(type) &&
                expected.getFreeUnits(type) == actual.getFreeUnits(type), "per-type population and smoothing match");
    for (int level = 0; level < NB_UNIT_LEVELS; ++level)
        require(expected.getWorkersLevel(level) == actual.getWorkersLevel(level) &&
                a.totalNeededPerLevel[level] == b.totalNeededPerLevel[level], "per-level labor matches");
    require(expected.getWorkersBalance() == actual.getWorkersBalance(), "labor balance matches");
    const auto& historyA = expected.getEndOfGameStats();
    const auto& historyB = actual.getEndOfGameStats();
    require(historyA.size() == historyB.size(), "loading does not append an end-game sample");
    for (size_t i = 0; i < historyA.size(); ++i)
        for (int field = 0; field < EndOfGameStat::TYPE_NB_STATS; ++field)
            require(historyA[i].value[field] == historyB[i].value[field], "end-game history matches");
}

static void sample(Game& game, unsigned tick)
{
    auto* unit = game.teams[0]->myUnits[0];
    unit->hungry = (tick % 11 < 5) ? 0 : unit->trigHungry + 100;
    unit->medical = (tick % 17 < 8) ? Unit::MED_DAMAGED : Unit::MED_FREE;
    game.stepCounter = tick;
    game.teams[0]->stats.step(game.teams[0]);
}

static std::unique_ptr<GameGUI> roundTrip(Game& game)
{
    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream writer(bytes);
    game.save(&writer, false, "team statistics regression");
    auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
    copy->seekFromStart(0);
    GAGCore::BinaryInputStream reader(copy);
    auto loaded = std::make_unique<GameGUI>();
    require(loaded->game.load(&reader), "binary game save loads");
    return loaded;
}

class LocatedOutput : public GAGCore::BinaryOutputStream
{
public:
    size_t statsPosition = 0, smoothingPosition = 0;
    explicit LocatedOutput(GAGCore::StreamBackend* backend) : BinaryOutputStream(backend) {}
    void writeSint32(Sint32 value, const std::string name) override
    {
        if (name == "statsIndex") statsPosition = getPosition();
        if (name == "smoothedIndex") smoothingPosition = getPosition();
        BinaryOutputStream::writeSint32(value, name);
    }
};

static void malformedStats(Game& game)
{
    auto* bytes = new GAGCore::MemoryStreamBackend;
    LocatedOutput writer(bytes);
    game.save(&writer, false, "invalid statistics regression");
    require(writer.statsPosition && writer.smoothingPosition, "new statistics indices are in the save");
    bytes->seekFromEnd(0);
    const std::string original(bytes->getBuffer(), bytes->getPosition());
    const std::pair<size_t, int> cases[] = {{writer.statsPosition, -1}, {writer.statsPosition, 128},
        {writer.smoothingPosition, -1}, {writer.smoothingPosition, 32}};
    for (const auto& entry : cases)
    {
        auto* corrupt = new GAGCore::MemoryStreamBackend(original.data(), original.size());
        GAGCore::BinaryOutputStream patch(corrupt);
        patch.seekFromStart(entry.first);
        patch.writeSint32(entry.second, "invalidIndex");
        auto* copy = new GAGCore::MemoryStreamBackend(*corrupt);
        copy->seekFromStart(0);
        GAGCore::BinaryInputStream reader(copy);
        GameGUI loaded;
        bool rejected = false;
        try { loaded.game.load(&reader); }
        catch (const std::runtime_error& error)
        { rejected = std::string(error.what()) == "Invalid team statistics sampling index"; }
        require(rejected, "invalid sampling index reaches the statistics validator");
    }
    for (const size_t length : {writer.statsPosition + 2, writer.smoothingPosition + 2,
                               writer.smoothingPosition + 6})
    {
        GAGCore::BinaryInputStream reader(new GAGCore::MemoryStreamBackend(original.data(), length));
        reader.seekFromStart(0);
        GameGUI loaded;
        bool rejected = false;
        try { loaded.game.load(&reader); }
        catch (const std::runtime_error& error)
        { rejected = std::string(error.what()).find("Incomplete binary field:") == 0; }
        require(rejected, "truncated statistics reach the checked field reader");
    }
}

static void textRoundTrip()
{
    // Exercise the new named fields independently of the legacy end-game text labels.
    GameGUI gui;
    Game& game = gui.game;
    game.map.setSize(5, 5, GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.teams[0]->race.loadDefault();
    require(game.addUnit(5, 5, 0, WORKER, 0, 0, 0, 0) != nullptr, "text fixture worker exists");
    for (unsigned tick = 1; tick <= 77; ++tick) sample(game, tick);
    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::TextOutputStream writer(bytes);
    game.teams[0]->save(&writer);
    writer.flush();
    auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
    copy->seekFromStart(0);
    GAGCore::TextInputStream reader(copy);
    GameGUI loaded;
    loaded.game.map.setSize(5, 5, GRASS);
    loaded.game.map.setGame(&loaded.game);
    loaded.game.addTeam();
    require(loaded.game.teams[0]->load(&reader, &globalContainer->buildingsTypes, VERSION_MINOR), "text team save loads");
    compare(game.teams[0]->stats, loaded.game.teams[0]->stats);
    const unsigned start = game.stepCounter;
    for (unsigned i = 1; i <= 65; ++i)
    {
        sample(game, start + i);
        sample(loaded.game, start + i);
        compare(game.teams[0]->stats, loaded.game.teams[0]->stats);
    }
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    require(argc == 3 || argc == 5, "usage: harness PROFILE ROOT [--write-fixture FILE | --legacy FILE]");
    require(std::string(argv[1]).find("glob2-stats-test-") == 0, "disposable profile required");
    GlobalContainer globals(argv[1]);
    globals.fileManager->addDir(argv[2]);
    globalContainer = &globals;
    globals.runNoX = true;
    globals.settings.rememberUnit = false;
    globals.buildingsTypes.init();
    IntBuildingType::init();
    GameGUIKeyActions::init();
    MapEditKeyActions::init();
    if (argc == 5 && std::string(argv[3]) == "--legacy")
    {
        FILE* file = std::fopen(argv[4], "rb");
        require(file != nullptr, "open legacy fixture");
        GAGCore::BinaryInputStream reader(new GAGCore::FileStreamBackend(file));
        GameGUI restored;
        require(restored.game.load(&reader), "legacy save loads");
        Game& game = restored.game;
        require(game.mapHeader.getVersionMinor() <= 88, "fixture is a genuine legacy format");
        std::printf("LEGACY version=%d teams=%d\n", game.mapHeader.getVersionMinor(), game.mapHeader.getNumberOfTeams());
        for (unsigned step = 0; step < 65; ++step)
        {
            for (int t = 0; t < game.mapHeader.getNumberOfTeams(); ++t)
            {
                auto& stats = game.teams[t]->stats;
                const auto& stat = *stats.getLatestStat();
                std::printf("%u %d %d %d %d %d %d %d %d %zu\n", step, t,
                    stat.totalUnit, stat.totalHP, stat.needFood, stat.needFoodCritical,
                    stat.needHeal, stat.totalFree, stat.totalNeeded, stats.getEndOfGameStats().size());
                stats.step(game.teams[t]);
            }
            ++game.stepCounter;
        }
        return 0;
    }
    GameGUI gui;
    Game& game = gui.game;
    game.map.setSize(5, 5, GRASS);
    game.map.setGame(&game);
    game.addTeam();
    game.teams[0]->race.loadDefault();
    require(game.addUnit(5, 5, 0, WORKER, 0, 0, 0, 0) != nullptr, "fixture worker exists");
    // Fill and wrap the 128-snapshot ring before testing every smoothing phase.
    for (unsigned tick = 0; tick < 4096; ++tick)
        sample(game, tick);
    if (argc == 5 && std::string(argv[3]) == "--write-fixture")
    {
        for (unsigned tick = 4096; tick < 4107; ++tick) sample(game, tick);
        auto* bytes = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream writer(bytes);
        game.save(&writer, false, "team statistics legacy fixture");
        bytes->seekFromEnd(0);
        std::ofstream file(argv[4], std::ios::binary);
        file.write(bytes->getBuffer(), bytes->getPosition());
        file.close();
        require(!file.fail(), "write saved fixture");
        std::printf("Wrote save format %d fixture\n", VERSION_MINOR);
        return 0;
    }
    for (unsigned phase = 0; phase < 32; ++phase)
    {
        auto loaded = roundTrip(game);
        compare(game.teams[0]->stats, loaded->game.teams[0]->stats);
        // Repeated loads must not advance the sampling position either.
        auto reloaded = roundTrip(loaded->game);
        compare(game.teams[0]->stats, reloaded->game.teams[0]->stats);
        const unsigned start = game.stepCounter;
        for (unsigned delta = 1; delta <= 65; ++delta)
        {
            sample(game, start + delta);
            sample(loaded->game, start + delta);
            sample(reloaded->game, start + delta);
            compare(game.teams[0]->stats, loaded->game.teams[0]->stats);
            compare(game.teams[0]->stats, reloaded->game.teams[0]->stats);
        }
    }
    malformedStats(game);
    textRoundTrip();
    std::puts("Team statistics save regressions passed: 32 sampling phases, ring wrap, repeated loads, text streams and corruption controls");
    return 0;
}
