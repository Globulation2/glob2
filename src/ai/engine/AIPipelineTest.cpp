// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AI.h"
#include "AIImplementation.h"
#include "AIOrderScheduler.h"
#include "Version.h"
#include "Player.h"
#include "Order.h"
#include "RessourceType.h"
#include "Marshaling.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <array>

namespace
{
constexpr std::array controllers{AI::NUMBI, AI::CASTOR, AI::WARRUSH,
    AI::ECONO, AI::NICOWAR, AI::CORTEX, AI::CABINO, AI::MAXIMA, AI::JAVASCRIPT};
constexpr std::array<unsigned, 9> actors{0,1,2,3,4,5,6,7,8};
constexpr unsigned ticks = 20;

void populate(glob2test::HeadlessGame& fixture, unsigned delay, unsigned workers)
{
    auto& game = fixture.game;
    game.gameHeader.setAIOrderDelay(delay);
    game.map.configureCompute(workers ? workers : 1, workers ? Map::ComputeAI : 0);
    for (unsigned player : actors)
    {
        const int x = (player % 3) * 40, y = (player / 3) * 40;
        auto* swarm = fixture.addBuilding("swarm", x+3,y+3,0,player);
        auto* inn = fixture.addBuilding("inn", x+10,y+3,0,player);
        for (auto* building : {swarm, inn}) {
            building->resources[WHEAT] = building->type->maxResource[WHEAT];
            building->update();
        }
        for (int i=0; i<16; ++i) fixture.addUnit(WORKER,x+3+i%12,y+12+i/12,player);
        for (int i=0; i<3; ++i) fixture.addUnit(WARRIOR,x+3+i,y+16,player);
        for (int yy=y+21; yy<y+27; ++yy) for (int xx=x+3; xx<x+29; ++xx) {
            game.map.setResource(xx,yy,WHEAT,0);
            game.map.setResourceAmount(game.map.coordToIndex(xx,yy),globalContainer->resourcesTypes.get(WHEAT)->sizesCount);
        }
        for (int xx=x+3; xx<x+29; ++xx) for (auto [row,resource] : {std::pair{30,WOOD},std::pair{31,STONE}}) {
            game.map.setResource(xx,y+row,resource,0);
            game.map.setResourceAmount(game.map.coordToIndex(xx,y+row),globalContainer->resourcesTypes.get(resource)->sizesCount);
        }
        auto* team = game.teams[player];
        team->startPosX=x+3; team->startPosY=y+3; team->startPosSet=Team::START_POS_FROM_UNIT;
        team->stats.step(team);
        if (controllers[player] == AI::JAVASCRIPT)
            game.gameHeader.setAIConfig(player,Script::config(
                "function step(c){const b=c.game.buildings({team:c.myTeam})[0];"
                "return {type:'workers',building:b,workers:1+Math.floor(c.random()*4)};}"));
        game.players[player]->makeItAI(controllers[player]);
    }
    game.map.setMapDiscovered();
    game.setWaitingOnMask(0);
}

std::string wire(Order& order)
{
    std::string bytes(1,char(order.getOrderType()));
    if (order.getDataLength()) bytes.append(reinterpret_cast<const char*>(order.getData()),order.getDataLength());
    return bytes;
}
std::vector<Uint32> state(Game& game)
{
    std::vector<Uint32> result, buildings, units;
    game.checkSum(&result,&buildings,&units,true);
    // Loading records the current save version in the map header; it is not
    // simulated state. Compare every other game, building and unit checksum.
    result.erase(result.begin());
    result.insert(result.end(),buildings.begin(),buildings.end());
    result.insert(result.end(),units.begin(),units.end());
    return result;
}
struct Tick
{
    std::vector<std::string> orders;
    std::vector<Uint32> checksums;
    bool operator==(const Tick&) const = default;
};
void compare(const Tick& actual, const Tick& expected)
{
    REQUIRE(actual.orders.size()==expected.orders.size());
    for (unsigned player=0; player<actual.orders.size(); ++player) {
        CAPTURE(player); CAPTURE(controllers[player]);
        CHECK(actual.orders[player]==expected.orders[player]);
    }
    REQUIRE(actual.checksums.size()==expected.checksums.size());
    for (unsigned index=0; index<actual.checksums.size(); ++index)
        if (actual.checksums[index]!=expected.checksums[index]) {
            CAPTURE(index);
            CHECK(actual.checksums[index]==expected.checksums[index]);
            break;
        }
}
Tick advance(Game& game)
{
    const auto outputs = game.prepareAIOrders(actors,false);
    REQUIRE(outputs.size()==actors.size());
    Tick result;
    for (unsigned index=0; index<outputs.size(); ++index) {
        REQUIRE(outputs[index].first==actors[index]);
        auto order=game.validateAIOrder(outputs[index].second,actors[index]);
        REQUIRE(order);
        order->sender=actors[index];
        result.orders.push_back(wire(*order));
        game.executeOrder(order,0);
    }
    game.syncStep(0);
    result.checksums=state(game);
    return result;
}
std::string save(Game& game)
{
    auto* memory=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    game.save(&output,false,"AI pipeline continuation"); output.flush();
    return memory->takeContents();
}
void load(Game& game, const std::string& bytes, unsigned workers)
{
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0); REQUIRE(game.load(&input));
    game.setWaitingOnMask(0);
    game.map.configureCompute(workers ? workers : 1, workers ? Map::ComputeAI : 0);
}
}

TEST_SUITE("AIPipeline")
{
TEST_CASE("every shipped controller preserves orders and state across worker counts and all delays")
{
    glob2test::HeadlessGlobals globals;
    for (unsigned delay=0; delay<=8; ++delay) {
        CAPTURE(delay);
        std::vector<Tick> expected;
        for (unsigned workers : {0u,1u,4u}) {
            CAPTURE(workers);
            setSyncRandSeed(0xA171);
            glob2test::HeadlessGame fixture(glob2test::GameOptions{.wDec=7,.hDec=7,.teams=9,
                .loadDefaultRace=true,.header=true,.seed=0xA171});
            populate(fixture,delay,workers);
            for (unsigned tick=0; tick<ticks; ++tick) {
                CAPTURE(tick);
                auto actual=advance(fixture.game);
                if (!workers) expected.push_back(std::move(actual));
                else compare(actual,expected[tick]);
            }
            fixture.game.drainAI();
        }
    }
}
TEST_CASE("save continuation retains controller caches receipts and future commands at pipeline boundaries")
{
    glob2test::HeadlessGlobals globals;
    for (unsigned delay=0; delay<=8; ++delay) for (unsigned workers : {0u,1u,4u}) {
        CAPTURE(delay); CAPTURE(workers);
        setSyncRandSeed(0xA171);
        glob2test::HeadlessGame fixture(glob2test::GameOptions{.wDec=7,.hDec=7,.teams=9,
            .loadDefaultRace=true,.header=true,.seed=0xA171});
        populate(fixture,delay,workers);
        std::vector<Tick> expected;
        std::vector<std::pair<unsigned,std::string>> checkpoints;
        for (unsigned tick=0; tick<ticks; ++tick) {
            expected.push_back(advance(fixture.game));
            if (tick==0 || tick==delay+1 || tick==13)
                checkpoints.emplace_back(tick+1,save(fixture.game));
        }
        fixture.game.drainAI();
        for (const auto& [start,bytes] : checkpoints) {
            CAPTURE(start);
            GameGUI restored(false);
            load(restored.game,bytes,workers);
            for (unsigned tick=start; tick<ticks; ++tick) {
                CAPTURE(tick);
                compare(advance(restored.game),expected[tick]);
            }
            restored.game.drainAI();
        }
    }
}
TEST_CASE("pause and repeated boundary calls cannot execute a published command twice")
{
    glob2test::HeadlessGlobals globals;
    for (unsigned delay : {0u,4u,8u}) {
        CAPTURE(delay);
        glob2test::HeadlessGame fixture(glob2test::GameOptions{.loadDefaultRace=true,.header=true});
        auto& game=fixture.game;
        fixture.addBuilding("swarm",4,4);
        fixture.addUnit(WORKER,12,12);
        game.gameHeader.setAIOrderDelay(delay);
        game.gameHeader.setAIConfig(0,Script::config(
            "function step(c){return {type:'workers',building:c.game.buildings({team:c.myTeam})[0],workers:3};}"));
        game.players[0]->makeItAI(AI::JAVASCRIPT);
        game.map.configureCompute(4,Map::ComputeAI);
        constexpr std::array<unsigned,1> player{0};
        for (unsigned tick=0; tick<=delay; ++tick) {
            auto first=game.prepareAIOrders(player,false)[0].second;
            auto repeated=game.prepareAIOrders(player,false)[0].second;
            CHECK(wire(*first)==wire(*repeated));
            if (tick==delay) {
                REQUIRE(first->getOrderType()==ORDER_MODIFY_BUILDING);
                // This checkpoint is after publication but before admission:
                // reload must return the same command, without another poll.
                const auto checkpoint=save(game);
                GameGUI restored(false);
                load(restored.game,checkpoint,4);
                auto savedOrder=restored.game.prepareAIOrders(player,false)[0].second;
                CHECK(wire(*savedOrder)==wire(*first));
                savedOrder=restored.game.validateAIOrder(savedOrder,0);
                savedOrder->sender=0;restored.game.executeOrder(savedOrder,0);
                first=game.validateAIOrder(first,0);first->sender=0;game.executeOrder(first,0);
                CHECK(state(restored.game)==state(game));
                CHECK(restored.game.prepareAIOrders(player,false)[0].second->getOrderType()==ORDER_NULL);
                CHECK(game.prepareAIOrders(player,true)[0].second->getOrderType()==ORDER_NULL);
                CHECK(game.prepareAIOrders(player,false)[0].second->getOrderType()==ORDER_NULL);
                break;
            }
            CHECK(first->getOrderType()==ORDER_NULL);
            game.syncStep(0);
        }
        game.drainAI();
    }
}
TEST_CASE("replacement cancels an old output without rejecting identical new wire bytes")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture(glob2test::GameOptions{.loadDefaultRace=true,.header=true});
    auto& game=fixture.game;
    fixture.addBuilding("swarm",4,4);
    fixture.addUnit(WORKER,12,12);
    game.gameHeader.setAIConfig(0,Script::config(
        "function step(c){return {type:'workers',building:c.game.buildings({team:c.myTeam})[0],workers:3};}"));
    game.players[0]->makeItAI(AI::JAVASCRIPT);
    game.map.configureCompute(4,Map::ComputeAI);
    constexpr std::array<unsigned,1> player{0};
    auto old=game.prepareAIOrders(player,false)[0].second;
    REQUIRE(old->getOrderType()==ORDER_MODIFY_BUILDING);
    game.players[0]->makeItAI(AI::JAVASCRIPT);
    CHECK(game.validateAIOrder(old,0)->getOrderType()==ORDER_NULL);
    game.syncStep(0);
    auto replacement=game.prepareAIOrders(player,false)[0].second;
    REQUIRE(replacement->getOrderType()==ORDER_MODIFY_BUILDING);
    CHECK(wire(*replacement)==wire(*old));
    CHECK(replacement->aiGeneration!=old->aiGeneration);
    CHECK(game.validateAIOrder(replacement,0)->getOrderType()==ORDER_MODIFY_BUILDING);
    CHECK(game.validateAIOrder(old,0)->getOrderType()==ORDER_NULL);
    replacement->sender=0;game.executeOrder(replacement,0);
    game.drainAI();
}
TEST_CASE("team death cancels submitted work and saves receipts for a revived controller")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture(glob2test::GameOptions{.loadDefaultRace=true,.header=true});
    auto& game=fixture.game;game.setWaitingOnMask(0);
    fixture.addBuilding("swarm",4,4);fixture.addUnit(WORKER,10,10);
    game.gameHeader.setAIOrderDelay(1);
    game.gameHeader.setAIConfig(0,Script::config(
        "function step(c){return {type:'workers',building:c.game.buildings({team:c.myTeam})[0],workers:3};}"));
    game.players[0]->makeItAI(AI::JAVASCRIPT);
    constexpr std::array<unsigned,1> player{0};
    CHECK(game.prepareAIOrders(player,false)[0].second->getOrderType()==ORDER_NULL);
    game.teams[0]->isAlive=false;
    CHECK(game.prepareAIOrders({},false).empty());
    const auto checkpoint=save(game);
    glob2test::HeadlessGame restored(glob2test::GameOptions{});
    load(restored.game,checkpoint,1);
    game.teams[0]->isAlive=true;restored.game.teams[0]->isAlive=true;
    game.syncStep(0);restored.game.syncStep(0);
    CHECK(game.prepareAIOrders(player,false)[0].second->getOrderType()==ORDER_NULL);
    CHECK(restored.game.prepareAIOrders(player,false)[0].second->getOrderType()==ORDER_NULL);
    game.syncStep(0);restored.game.syncStep(0);
    const auto due=game.prepareAIOrders(player,false)[0].second;
    const auto resumed=restored.game.prepareAIOrders(player,false)[0].second;
    REQUIRE(due->getOrderType()==ORDER_MODIFY_BUILDING);
    CHECK(wire(*due)==wire(*resumed));
    CHECK(state(game)==state(restored.game));
    game.drainAI();restored.game.drainAI();
}
TEST_CASE("a submitted deadline is delivered even when no new poll is eligible")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture(glob2test::GameOptions{.loadDefaultRace=true,.header=true});
    auto& game=fixture.game;
    fixture.addBuilding("swarm",4,4);
    fixture.addUnit(WORKER,10,10);
    game.setWaitingOnMask(0);
    game.gameHeader.setAIOrderDelay(1);
    game.gameHeader.setAIConfig(0,Script::config(
        "function step(c){return {type:'workers',building:c.game.buildings({team:c.myTeam})[0],workers:3};}"));
    game.players[0]->makeItAI(AI::JAVASCRIPT);
    constexpr std::array<unsigned,1> player{0};
    CHECK(game.prepareAIOrders(player,false)[0].second->getOrderType()==ORDER_NULL);
    game.syncStep(0);
    const auto due=game.prepareAIOrders({},false);
    REQUIRE(due.size()==1);
    CHECK(due[0].first==0);
    REQUIRE(due[0].second->getOrderType()==ORDER_MODIFY_BUILDING);
    CHECK(game.prepareAIOrders({},false).empty());
    auto order=game.validateAIOrder(due[0].second,0);order->sender=0;game.executeOrder(order,0);
    game.syncStep(0);
    CHECK(game.prepareAIOrders({},false).empty());
    game.drainAI();
}
}

TEST_CASE("wrapper preemption preserves controller rejection feedback and pending release identity across save" * doctest::test_suite("AIPipeline"))
{
    class ReceiptProbe : public AIImplementation
    {
    public:
        unsigned polls=0;
        std::vector<AIEngine::ExecutionReceipt> feedback;
        bool load(GAGCore::InputStream*,Player*,Sint32) override {return true;}
        void save(GAGCore::OutputStream*) override {}
        bool supportsObservation() const override {return true;}
        std::shared_ptr<Order> getOrder() override {return std::make_shared<NullOrder>();}
        std::shared_ptr<Order> getOrder(const AIEngine::DecisionContext& context) override {
            ++polls;feedback=context.receipts;return getOrder();
        }
    };
    glob2test::HeadlessGlobals globals;
    for(auto status:{AIEngine::ExecutionStatus::Accepted,AIEngine::ExecutionStatus::Rejected,AIEngine::ExecutionStatus::Canceled}) {
        CAPTURE(unsigned(status));
        glob2test::HeadlessGame fixture(glob2test::GameOptions{.header=true});
        auto& game=fixture.game;auto* training=fixture.addBuilding("school",4,4);
        REQUIRE(training);training->maxUnitWorking=3;game.gameHeader.setUnitUpgradesDisabled(true);
        AI original(AI::NONE,game.players[0]);delete original.aiImplementation;
        auto* probe=new ReceiptProbe;original.aiImplementation=probe;original.prepareDecision();
        auto observed=AIEngine::AIWorldView::capture(game,AIEngine::AIWorldView::captureCatalog(game));
        OrderConstruction controllerOrder(training->gid,1,1);
        auto controllerCommand=AIEngine::Command::capture(controllerOrder,*observed);
        AIEngine::ExecutionReceipt rejected{{0,1,0,0,0},AIEngine::ExecutionStatus::Rejected,0,0,
            controllerCommand.bytes,controllerCommand.target};
        const std::vector<AIEngine::ExecutionReceipt> receipt{rejected},empty;
        auto invoke=[&](AI& ai,const std::shared_ptr<const AIEngine::AIWorldView>& view,
                Uint64 sequence,const std::vector<AIEngine::ExecutionReceipt>& feedback) {
            AIEngine::DecisionContext context{*view,0,0,feedback,view};
            context.pollSequence=sequence;context.controllerGeneration=1;
            return ai.decide(context);
        };
        auto release=invoke(original,observed,1,receipt);
        REQUIRE(std::dynamic_pointer_cast<OrderModifyBuilding>(release.decode()));
        CHECK(probe->polls==0); // The rejection must wait through wrapper preemption.
        auto* memory=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(memory);
        original.save(&output);output.flush();const auto bytes=memory->takeContents();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));input.seekFromStart(0);
        AI restored(&input,game.players[0],VERSION_MINOR);delete restored.aiImplementation;
        auto* resumed=new ReceiptProbe;restored.aiImplementation=resumed;restored.prepareDecision();
        // Pending same-incarnation release suppresses duplicate release orders.
        CHECK(invoke(original,observed,2,empty).bytes==invoke(restored,observed,2,empty).bytes);
        for(auto* value:{probe,resumed}) {
            REQUIRE(value->polls==1);REQUIRE(value->feedback.size()==1);
            CHECK(value->feedback.front().request==rejected.request);
            CHECK(value->feedback.front().status==AIEngine::ExecutionStatus::Rejected);
            CHECK(value->feedback.front().selectedTarget==rejected.selectedTarget);
        }
        training->maxUnitWorking=0;
        auto settled=AIEngine::AIWorldView::capture(game,observed->catalog);
        const std::vector<AIEngine::ExecutionReceipt> wrapperReceipt{
            {{0,1,0,1,0},status,0,0,release.bytes,release.target}};
        CHECK(invoke(original,settled,3,wrapperReceipt).bytes==invoke(restored,settled,3,wrapperReceipt).bytes);
        for(auto* value:{probe,resumed}) {
            CHECK(value->polls==2);CHECK(value->feedback.empty()); // Wrapper outcomes are private.
        }
    }
}
