// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Engine.h"
#include "sim/presentation/SceneInputs.h"
#include "field/GradientConstants.h"
#include <BackgroundFileWriter.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <ThreadSupport.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <nlohmann/json.hpp>

TEST_SUITE("SharedWorkerLifecycle") {
TEST_CASE("background saves finish on a worker and remain readable after teardown") {
    glob2test::HeadlessGlobals globals;
    const auto path = std::filesystem::path(SDL_getenv("GLOB2_USER_DATA_DIR")) / "worker-save.txt";
    const auto submittingThread = std::this_thread::get_id();
    std::thread::id writingThread;
    {
        GAGCore::BackgroundFileWriter writer(globals->fileManager);
        writer.write(path.string(), "finished", [&](std::string &bytes) {
            writingThread = std::this_thread::get_id();
            bytes += " on worker";
        });
        writer.waitUntilIdle();
    }
    std::ifstream file(path);
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    REQUIRE(bytes == "finished on worker");
    REQUIRE((writingThread != submittingThread) == GAGCore::ThreadSupport::available);
}
TEST_CASE("resource catalog import drains deferred material preparation before replacing stocks") {
    glob2test::HeadlessGlobals globals;
    GameGUI source;
    GameHeader header;
    header.setNumberOfPlayers(1);
    header.setRandomSeed(123456);
    header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
    auto mapHeader = Engine::loadMapHeader("maps/balanced.map");
    REQUIRE(source.loadFromHeaders(mapHeader, header, true, true));
    REQUIRE(source.game.stepCounter == 0);
    auto &map = source.game.map;
    using Json = nlohmann::json;
    const auto trees = *map.resourceRegistry().find("trees");
    auto definition = Json::parse(map.resourceRegistry().serialize())["resources"][resourceIndex(trees)];
    definition["key"] = "test:deferred-compound";
    definition["properties"]["primaryMaterial"] = "gold";
    definition["properties"]["persistsWhenEmpty"] = true;
    definition["yields"] = {{"gold", {{"capacity", 4}, {"initial", 2}, {"consumption", "one"}}},
        {"food", {{"capacity", 4}, {"initial", 3}, {"consumption", "one"}}}};
    auto install = [&] {
        map.installResourceDefinitions(Json{{"schemaVersion", 1}, {"resources", Json::array({definition})}}.dump());
    };
    install();
    const auto resource = *map.resourceRegistry().find("test:deferred-compound");
    size_t index = 0;
    for (; index < size_t(map.getW()) * map.getH(); ++index) {
        const auto &tile = map.getTile(index % map.getW(), index / map.getW());
        if (tile.building == NOGBID && tile.groundUnit == NOGUID && !tile.forbidden && map.terrainTypeAt(index) == GRASS) break;
    }
    REQUIRE(index < size_t(map.getW()) * map.getH());
    map.setResource(index % map.getW(), index / map.getW(), resource, 0);
    map.setMapDiscovered(index % map.getW(), index / map.getW(), Team::teamNumberToMask(0));
    REQUIRE(map.materialAmountAt(index, MaterialId::Gold) == 2);
    REQUIRE(map.materialAmountAt(index, MaterialId::Food) == 3);
    map.getMaterialGradient(0, MaterialId::Gold, 0);
    map.configureGradientPipeline(2, 3);
    map.advanceGradientPipeline();
    map.stagePeriodicGradientPreparation();
    REQUIRE(map.hasPendingGradientPreparation());
    REQUIRE(map.gradientPipelineStatus().pending == 1);
    definition["yields"]["gold"]["capacity"] = 1;
    definition["yields"]["gold"]["initial"] = 1;
    install();
    CHECK_FALSE(map.hasPendingGradientPreparation());
    CHECK(map.materialAmountAt(index, MaterialId::Gold) == 1);
    CHECK(map.materialAmountAt(index, MaterialId::Food) == 3);
    // Finish respects publication deadlines; advancing safely publishes the
    // prepared goal after the immutable definition snapshot was replaced.
    for (int tick = 0; tick < 3; ++tick) map.advanceGradientPipeline();
    CHECK(map.getMaterialGradient(0, MaterialId::Gold, 0)[index] == GRADIENT_AT_GOAL);
}
TEST_CASE("pending gradient work survives a save and continues at the same deadlines") {
    glob2test::HeadlessGlobals globals;
    GameGUI source;
    GameHeader header;
    header.setNumberOfPlayers(1);
    header.setRandomSeed(123456);
    header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
    auto mapHeader = Engine::loadMapHeader("maps/balanced.map");
    REQUIRE(source.loadFromHeaders(mapHeader, header, true, true));
    auto &map = source.game.map;
    map.getClearAreasGradient(0, 0);
    map.configureCompute(4);
    map.configureGradientPipeline(2, 3);
    map.advanceGradientPipeline();
    map.syncStep(0, false);
    map.stagePeriodicGradientPreparation();
    REQUIRE(map.hasPendingGradientPreparation());
    REQUIRE(map.gradientPipelineStatus().jobs > 0);
    auto *storage = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream writer(storage);
    source.save(&writer, "pending worker continuation");
    CHECK_FALSE(map.hasPendingGradientPreparation());
    const std::string checkpoint(storage->getBuffer(), storage->getPosition());
    const auto simulationState = [](Game &game) {
        std::vector<Uint32> state, buildings, units;
        game.checkSum(&state, &buildings, &units, true);
        // Saving upgrades the map format header; compare all simulation state.
        state.erase(state.begin());
        state.insert(state.end(), buildings.begin(), buildings.end());
        state.insert(state.end(), units.begin(), units.end());
        return state;
    };
    std::vector<std::vector<Uint32>> checksums;
    for (unsigned tick=0; tick<24; ++tick) {
        source.game.syncStep(0);
        checksums.push_back(simulationState(source.game));
    }
    GameGUI restored;
    GAGCore::BinaryInputStream reader(new GAGCore::MemoryStreamBackend(checkpoint.data(), checkpoint.size()));
    reader.seekFromStart(0);
    REQUIRE(restored.load(&reader));
    for (const auto &expected : checksums) {
        restored.game.syncStep(0);
        REQUIRE(simulationState(restored.game) == expected);
    }
}
}

#include "ReadOnlyPhase.h"
TEST_SUITE("SimulationReadPhase") {
TEST_CASE("direct completed steps match deferred preparation across worker counts") {
    glob2test::HeadlessGlobals globals;
    GameHeader header;
    header.setNumberOfPlayers(1);
    header.setRandomSeed(123456);
    header.getBasePlayer(0) = BasePlayer(0, "Test", 0, BasePlayer::P_LOCAL);
    auto mapHeader = Engine::loadMapHeader("maps/balanced.map");
    GameGUI direct, deferred;
    REQUIRE(direct.loadFromHeaders(mapHeader, header, true, true));
    REQUIRE(deferred.loadFromHeaders(mapHeader, header, true, true));
    for (Game *game : {&direct.game, &deferred.game}) {
        game->map.getClearAreasGradient(0, 0);
        game->map.getMaterialGradient(0, MaterialId::Wood, 0);
        game->map.configureGradientPipeline(0, 8);
    }
    for (unsigned threads : {1, 4}) {
        deferred.game.map.configureCompute(threads);
        for (unsigned tick = 0; tick < 64; ++tick) {
            direct.game.syncStep(0);
            CHECK_FALSE(direct.game.map.hasPendingGradientPreparation());
            deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
            REQUIRE(deferred.game.map.hasPendingGradientPreparation());
            ReadOnlyPhase phase;
            // Submission belongs to the owner; the deferred seed job is itself read-only work.
            deferred.game.map.preparePendingWorld(deferred.game.captureReadBoundary({},false));
            auto read = [&](size_t) { deferred.game.map.getMaterialGradient(0, MaterialId::Wood, 0); };
            phase.add(1, read);
            phase.run(deferred.game.map.computeExecutor());
            CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
            CHECK(direct.game.checkSum(nullptr, nullptr, nullptr, true) == deferred.game.checkSum(nullptr, nullptr, nullptr, true));
        }
    }
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    REQUIRE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.map.configureCompute(1);
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    deferred.game.map.setGradientWorkerCount(2);
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    const auto waitingTick = deferred.game.stepCounter;
    deferred.game.anyPlayerWaited = true;
    deferred.game.syncStep(0);
    CHECK(deferred.game.stepCounter == waitingTick);
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
    deferred.game.anyPlayerWaited = false;
    deferred.game.syncStep(0, Game::PreparationCompletion::Deferred);
    deferred.game.map.clear(); // Reserved jobs must not outlive their destination.
    CHECK_FALSE(deferred.game.map.hasPendingGradientPreparation());
}
}

TEST_CASE("no-AI completed ticks and saved continuation agree across diagnostic modes and worker counts [artifacts]" * doctest::test_suite("SharedWorkerLifecycle"))
{
    glob2test::HeadlessGlobals globals;
    GameGUI source;
    GameHeader header;
    header.setNumberOfPlayers(1); header.setRandomSeed(123456);
    header.getWinningConditions().clear(); // Retain a no-AI fixture that runs to the headless tick cap.
    header.getBasePlayer(0)=BasePlayer(0,"Test",0,BasePlayer::P_LOCAL);
    auto mapHeader=Engine::loadMapHeader("maps/balanced.map");
    REQUIRE(source.loadFromHeaders(mapHeader,header,true,true));
    source.game.map.getMaterialGradient(0,MaterialId::Food,0);
    source.game.map.getGuardAreasGradient(0,0);
    source.game.map.getClearAreasGradient(0,0);
    auto* storage=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream writer(storage);
    source.save(&writer,"no-AI gradient fixture"); writer.flush();
    const std::string checkpoint(storage->getBuffer(),storage->getPosition());
    const auto fixture=glob2test::artifactDir()/"no-ai.game";
    std::ofstream(fixture,std::ios::binary).write(checkpoint.data(),checkpoint.size());
    std::vector<Uint32> expected;
    for (auto mode : {ComputeTelemetryMode::Off, ComputeTelemetryMode::Waits, ComputeTelemetryMode::Trace})
    for (unsigned threads : {1,2,4,8}) {
        CAPTURE(mode); CAPTURE(threads);
        GameGUI game;
        GAGCore::BinaryInputStream reader(new GAGCore::MemoryStreamBackend(checkpoint.data(),checkpoint.size()));
        reader.seekFromStart(0);
        REQUIRE(game.load(&reader));
        game.game.map.configureCompute(threads);
        game.game.map.computeExecutor().configureTelemetry(mode);
        std::vector<Uint32> actual;
        for (unsigned tick=0; tick<96; ++tick) {
            game.game.syncStep(0,Game::PreparationCompletion::Deferred);
            REQUIRE(game.game.prepareAIOrders({},false).empty());
            actual.push_back(game.game.checkSum(nullptr,nullptr,nullptr));
        }
        game.game.map.finishGradientPipeline();
        REQUIRE(game.game.map.gradientPipelineStatus().published>0);
        if (expected.empty()) expected=actual; else CHECK(actual==expected);
        const auto tracePath=glob2test::artifactDir()/("compute-"+std::string(ComputeTelemetry::ModeNames[unsigned(mode)])+"-"+std::to_string(threads)+".trace");
        std::ofstream trace(tracePath);
        for (std::size_t i=0;i<actual.size();++i) trace<<i<<' '<<actual[i]<<'\n';
        game.game.map.finishResourceGrowth();
        auto* saveStorage=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream saved(saveStorage);
        game.save(&saved,"compute diagnostics continuation"); saved.flush();
        const std::string bytes(saveStorage->getBuffer(),saveStorage->getPosition());
        GameGUI restored;
        GAGCore::BinaryInputStream continuation(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
        continuation.seekFromStart(0); REQUIRE(restored.load(&continuation));
        restored.game.map.configureCompute(threads);
        restored.game.map.computeExecutor().configureTelemetry(mode);
        CHECK(restored.game.map.computeExecutor().telemetrySnapshot().summary.advancingTicks==0);
        for(unsigned tick=0;tick<16;++tick) {
            game.game.syncStep(0); restored.game.syncStep(0);
            CHECK(game.game.checkSum(nullptr,nullptr,nullptr,true)==restored.game.checkSum(nullptr,nullptr,nullptr,true));
        }
    }
}

TEST_CASE("presentation borrows the published no-AI read boundary without recapture" * doctest::test_suite("SimulationReadPhase"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.clearImmobile=true,.loadDefaultRace=true});
    auto& game=fixture.game;
    auto* unit=fixture.addUnit(WORKER,10,10);
    const auto identity=Game::refOf(unit);
    SceneRequest request;
    request.selectedUnit=identity;
    const auto required=SceneExtractor::requirements(request);
    const auto captures=game.snapshots().metrics.captures;
    const auto published=game.captureReadBoundary({},false,required);
    REQUIRE(game.snapshots().metrics.captures==captures+1);
    REQUIRE(game.prepareAIOrders({},false,nullptr,&published).empty());
    SceneExtractor preparer;
    PresentationFrame frame;
    preparer.prepare(SceneInputs{published.project(required),request},frame);
    CHECK(game.snapshots().metrics.captures==captures+1);
    CHECK(frame.entities.units.data()==published.entities->units.data());
    CHECK(frame.entities.buildings.data()==published.entities->buildings.data());
    CHECK(frame.entities.unitIndex.data()==published.entities->unitSlotIndices.data());
    CHECK(frame.entities.buildingIndex.data()==published.entities->buildingSlotIndices.data());
    CHECK(frame.entities.sectors.data()==published.effects->sectors.data());
    REQUIRE(frame.panels.unit.record);
    CHECK(frame.panels.unit.record==frame.entities.unit(identity.gid));
    const auto hp=frame.panels.unit.state().hp;
    unit->hp=1;
    game.snapshots().invalidateBoundary();
    const auto edited=game.captureReadBoundary({},true,required);
    CHECK(edited.observationRevision!=published.observationRevision);
    CHECK(frame.panels.unit.state().hp==hp);
    request.view.viewportX=7;
    preparer.prepare(SceneInputs{published,request},frame);
    CHECK(frame.panels.unit.state().hp==hp);
    CHECK(game.snapshots().metrics.captures==captures+2);
}

#include "AI.h"
#include "AIImplementation.h"
#include "Player.h"
#include "ai/engine/AIDecision.h"
namespace {
class BoundaryObserver final : public AIImplementation
{
public:
    SimulationSnapshot::Handle observed;
    bool load(GAGCore::InputStream*,Player*,Sint32) override { return true; }
    void save(GAGCore::OutputStream*) override {}
    bool supportsObservation() const override { return true; }
    SimulationSnapshot::Requirements observationRequirements() const override
    { return SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)|SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain); }
    std::shared_ptr<Order> getOrder() override { throw std::logic_error("live AI observation"); }
    std::shared_ptr<Order> getOrder(const AIEngine::DecisionContext& context) override
    { observed=context.world.components(); return std::make_shared<NullOrder>(); }
};
}
TEST_CASE("AI and pending gradients consume the presentation union without another capture" * doctest::test_suite("SimulationReadPhase"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
    auto& game=fixture.game;
    fixture.addBuilding("swarm",4,4); fixture.addUnit(WORKER,12,12);
    game.gameHeader.setAIOrderDelay(0);
    game.players[0]->makeItAI(AI::NUMBI);
    auto* observer=new BoundaryObserver;
    delete game.players[0]->ai->aiImplementation;
    game.players[0]->ai->aiImplementation=observer;
    game.map.configureCompute(2);
    game.map.configureGradientPipeline(0,8);
    game.map.getClearAreasGradient(0,0);
    game.syncStep(0,Game::PreparationCompletion::Deferred);
    REQUIRE(game.map.hasPendingGradientPreparation());
    const auto gradientRequirements=game.map.pendingGradientRequirements();
    REQUIRE(gradientRequirements!=0);
    constexpr std::array<unsigned,1> players{0};
    SceneRequest request;
    const auto rendering=SceneExtractor::requirements(request);
    const auto captures=game.snapshots().metrics.captures;
    const auto published=game.captureReadBoundary(players,false,rendering);
    CHECK((published.requirements&gradientRequirements)==gradientRequirements);
    REQUIRE(game.prepareAIOrders(players,false,{},&published).size()==1);
    game.drainAI();
    CHECK_FALSE(game.map.hasPendingGradientPreparation());
    REQUIRE(observer->observed.entities);
    CHECK(observer->observed.entities==published.entities);
    CHECK(observer->observed.terrain==published.terrain);
    CHECK_FALSE(observer->observed.session); // projection excludes presentation-only state
    PresentationFrame frame;
    SceneExtractor().prepare(published.project(rendering),request,frame);
    CHECK(frame.world.entities==observer->observed.entities);
    CHECK(frame.world.terrain==observer->observed.terrain);
    CHECK(game.snapshots().metrics.captures==captures+1);
}


TEST_CASE("reduced saved AI rosters retire trailing controllers and preserve continuation" * doctest::test_suite("SharedWorkerLifecycle"))
{
    glob2test::HeadlessGlobals globals;
    GameGUI source;
    GameHeader header;
    header.setNumberOfPlayers(4);header.setRandomSeed(123456);
    header.setAIOrderDelay(2);header.getWinningConditions().clear();
    for(int seat=0;seat<4;++seat)
        header.getBasePlayer(seat)=BasePlayer(seat,"Maxima",seat,
            BasePlayer::playerTypeFromImplementationID(AI::MAXIMA));
    auto mapHeader=Engine::loadMapHeader("maps/balanced.map");
    REQUIRE(mapHeader.getNumberOfTeams()>=4);
    REQUIRE(source.loadFromHeaders(mapHeader,header,true,true));
    const std::array<unsigned,4> allSeats{0,1,2,3};
    const std::array<unsigned,2> keptSeats{0,1};
    // Use actual shared AI admission/deadlines, including canceled future work.
    const auto step=[](Game& game,std::span<const unsigned> seats) {
        std::vector<std::string> wires;
        for(auto& [seat,order]:game.prepareAIOrders(seats,false))
        {
            REQUIRE(seat<unsigned(game.gameHeader.getNumberOfPlayers()));
            order->sender=int(seat);
            order=game.validateAIOrder(std::move(order),seat);
            std::string wire{char(seat),char(order->getOrderType())};
            if(order->getDataLength())
                wire.append(reinterpret_cast<const char*>(order->getData()),order->getDataLength());
            wire.push_back(char(game.executeOrder(order,0)));
            wires.push_back(std::move(wire));
        }
        game.syncStep(0);game.drainAI();
        return wires;
    };
    const auto bytes=[](auto save) {
        auto* memory=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(memory);
        save(output);output.flush();
        return memory->takeContents();
    };
    const auto saveGame=[&](GameGUI& game) {
        return bytes([&](auto& out){game.save(&out,"roster continuation");});
    };
    for(int tick=0;tick<8;++tick)step(source.game,allSeats);
    const auto fourPlayerSave=saveGame(source);
    GameGUI reduced;
    GAGCore::BinaryInputStream fourReader(new GAGCore::MemoryStreamBackend(fourPlayerSave.data(),fourPlayerSave.size()));
    fourReader.seekFromStart(0);
    REQUIRE(reduced.load(&fourReader));
    REQUIRE(reduced.game.mapHeader.getIsSavedGame());
    REQUIRE(reduced.game.gameHeader.getNumberOfPlayers()==4);
    for(int seat=0;seat<4;++seat)
    {
        REQUIRE(reduced.game.players[seat]);REQUIRE(reduced.game.players[seat]->ai);
        REQUIRE(reduced.game.players[seat]->ai->implementationID==AI::MAXIMA);
    }
    step(reduced.game,allSeats);
    const auto playerBytes=[&](Game& game,int seat) {
        return bytes([&](auto& out){game.players[seat]->save(&out);});
    };
    // Unit/building saves include their cached gameplay values, RNG streams and
    // ordered cross references. Roster/team membership is intentionally separate.
    const auto entityBytes=[&](Game& game) {
        return bytes([&](auto& out) {
            for(int team=0;team<game.mapHeader.getNumberOfTeams();++team)
            {
                for(int slot=0;slot<Unit::MAX_COUNT;++slot)
                    if(auto* unit=game.teams[team]->myUnits[slot])
                    {unit->save(&out);unit->saveCrossRef(&out);}
                for(int slot=0;slot<Building::MAX_COUNT;++slot)
                    if(auto* building=game.teams[team]->myBuildings[slot])
                    {building->save(&out);building->saveCrossRef(&out);}
            }
        });
    };
    Player* first=reduced.game.players[0];Player* second=reduced.game.players[1];
    const auto firstState=playerBytes(reduced.game,0),secondState=playerBytes(reduced.game,1);
    const auto entities=entityBytes(reduced.game);
    const auto ownerRandom=reduced.game.map.worldRandom.streams;
    const bool ownerInitialized=reduced.game.map.worldRandom.initialized;
    const auto legacyRandom=reduced.game.syncRandom;
    GameHeader smaller=reduced.game.gameHeader;smaller.setNumberOfPlayers(2);
    reduced.game.setGameHeader(smaller,true);
    CHECK(reduced.game.players[0]==first);CHECK(reduced.game.players[1]==second);
    for(int seat=2;seat<Team::MAX_COUNT;++seat)CHECK(reduced.game.players[seat]==nullptr);
    CHECK(playerBytes(reduced.game,0)==firstState);CHECK(playerBytes(reduced.game,1)==secondState);
    CHECK(entityBytes(reduced.game)==entities);
    CHECK(reduced.game.map.worldRandom.streams==ownerRandom);
    CHECK(reduced.game.map.worldRandom.initialized==ownerInitialized);
    CHECK(reduced.game.syncRandom==legacyRandom);
    CHECK(reduced.game.teams[2]->numberOfPlayer==0);CHECK(reduced.game.teams[2]->playersMask==0);
    CHECK(reduced.game.teams[3]->numberOfPlayer==0);CHECK(reduced.game.teams[3]->playersMask==0);
    const auto checkpoint=saveGame(reduced);
    GameGUI restored;
    GAGCore::BinaryInputStream reader(new GAGCore::MemoryStreamBackend(checkpoint.data(),checkpoint.size()));
    reader.seekFromStart(0);
    REQUIRE(restored.load(&reader));
    REQUIRE(restored.game.gameHeader.getNumberOfPlayers()==2);
    for(int seat=2;seat<Team::MAX_COUNT;++seat)CHECK(restored.game.players[seat]==nullptr);
    // Both sides loaded the current four-player save before reduction, so their
    // complete raw heavy checksums include the same format header; no normalization.
    for(int tick=0;tick<24;++tick)
    {
        CHECK(step(reduced.game,keptSeats)==step(restored.game,keptSeats));
        CHECK(reduced.game.checkSum(nullptr,nullptr,nullptr,true)==restored.game.checkSum(nullptr,nullptr,nullptr,true));
        CHECK(reduced.game.map.worldRandom.streams==restored.game.map.worldRandom.streams);
        CHECK(reduced.game.syncRandom==restored.game.syncRandom);
        CHECK(playerBytes(reduced.game,0)==playerBytes(restored.game,0));
        CHECK(playerBytes(reduced.game,1)==playerBytes(restored.game,1));
    }
}
