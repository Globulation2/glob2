// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "AIWarrush.h"
#include "AINumbi.h"
#include "AINicowar.h"
#include "AICabino.h"
#include "CabinoObservationFixture.h"
#include "cortex/AICortex.h"
#include "Player.h"
#include "AI.h"
#include "Version.h"
#include "Order.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <functional>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <algorithm>
#include <cstdlib>

namespace {
using namespace GAGCore;
constexpr Uint32 sentinel=0x1234abcd;
std::string write(bool text, const std::function<void(OutputStream*)>& body)
{
    auto* storage=new MemoryStreamBackend;
    std::unique_ptr<OutputStream> out(text ? static_cast<OutputStream*>(new TextOutputStream(storage))
        : static_cast<OutputStream*>(new BinaryOutputStream(storage)));
    body(out.get()); out->flush();
    return storage->takeContents();
}
void read(const std::string& bytes, bool text, const std::function<void(InputStream*)>& body)
{
    MemoryStreamBackend storage(bytes.data(),bytes.size()); storage.seekFromStart(0);
    std::unique_ptr<InputStream> in(text ? static_cast<InputStream*>(new TextInputStream(&storage))
        : static_cast<InputStream*>(new BinaryInputStream(new MemoryStreamBackend(storage))));
    body(in.get());
}
struct World {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world{glob2test::GameOptions{.header=true}};
    Player* player() { return world.game.players[0]; }
};
std::string wire(const std::shared_ptr<Order>& order)
{
    std::string result(1,char(order->getOrderType()));
    if (order->getDataLength()) result.append(reinterpret_cast<const char*>(order->getData()),order->getDataLength());
    return result;
}
}

TEST_SUITE("LegacyAIState") {
TEST_CASE("Warrush timers and constructors round-trip at boundaries")
{
    World w;
    for (bool text : {false,true}) for (int building : {0,1,30}) for (int area : {0,1,16,17,33,34,50}) {
        CAPTURE(text); CAPTURE(building); CAPTURE(area);
        AIWarrush original(w.player()); original.buildingDelay=building; original.areaUpdatingDelay=area;
        const auto bytes=write(text,[&](auto* out){ original.save(out); out->writeUint32(sentinel,"sentinel"); });
        read(bytes,text,[&](auto* in){
            AIWarrush loaded(w.player()); REQUIRE(loaded.load(in,w.player(),VERSION_MINOR));
            CHECK(loaded.buildingDelay==building); CHECK(loaded.areaUpdatingDelay==area);
            CHECK(in->readUint32("sentinel")==sentinel);
        });
        read(bytes,text,[&](auto* in){
            AIWarrush loaded(in,w.player(),VERSION_MINOR);
            CHECK(loaded.buildingDelay==building); CHECK(loaded.areaUpdatingDelay==area);
            CHECK(in->readUint32("sentinel")==sentinel);
        });
    }
}
TEST_CASE("Numbi phase clock survives and advances on the original poll")
{
    World w;
    for (bool text : {false,true}) for (int timer : {0,1,31,32,1024}) {
        AINumbi original(w.player()); original.phaseTime=1024; original.timer=timer;
        original.phase=3; original.attackPhase=2; original.mainBuilding[4]=11;
        const auto bytes=write(text,[&](auto* out){ original.save(out); out->writeUint32(sentinel,"sentinel"); });
        read(bytes,text,[&](auto* in){
            AINumbi loaded(in,w.player(),VERSION_MINOR);
            CHECK(loaded.timer==timer); CHECK(loaded.phase==3); CHECK(loaded.attackPhase==2);
            CHECK(loaded.mainBuilding[4]==11); CHECK(in->readUint32("sentinel")==sentinel);
        });
        read(bytes,text,[&](auto* in){
            AINumbi loaded(w.player()); REQUIRE(loaded.load(in,w.player(),VERSION_MINOR));
            CHECK(loaded.timer==timer); CHECK(in->readUint32("sentinel")==sentinel);
        });
    }
    AINumbi original(w.player()); original.timer=original.phaseTime;
    const auto bytes=write(false,[&](auto* out){original.save(out);});
    read(bytes,false,[&](auto* in){
        AINumbi loaded(in,w.player(),VERSION_MINOR);
        MersenneTwister left(17), right(17); original.setRandomEngine(left); loaded.setRandomEngine(right);
        original.getOrder(); loaded.getOrder();
        CHECK(loaded.phase==original.phase); CHECK(loaded.timer==original.timer);
        CHECK(loaded.phase==1);
    });
}
TEST_CASE("old layouts default omitted clocks without consuming the following field")
{
    World w;
    for (bool text : {false,true}) for (int version : {58,121,127,128,129,130,131}) {
        const auto warrush=write(text,[&](auto* out){out->writeUint32(sentinel,"sentinel");});
        read(warrush,text,[&](auto* in){
            AIWarrush loaded(in,w.player(),version);
            CHECK(loaded.buildingDelay==0); CHECK(loaded.areaUpdatingDelay==0);
            CHECK(in->readUint32("sentinel")==sentinel);
        });
        // Write the genuine historical Numbi layout; it has no timer field.
        const auto numbi=write(text,[&](auto* out){
            out->writeEnterSection("AINumbi");
            for (const char* name : {"phase","attackPhase","phaseTime","critticalWarriors","critticalTime","attackTimer"})
                out->writeSint32(7,name);
            for (int i=0;i<IntBuildingType::NB_BUILDING;++i)
                out->writeSint32(i,"mainBuilding["+std::to_string(i)+"]");
            out->writeLeaveSection(); out->writeUint32(sentinel,"sentinel");
        });
        read(numbi,text,[&](auto* in){
            AINumbi loaded(in,w.player(),version); CHECK(loaded.timer==0);
            CHECK(loaded.phase==7); CHECK(loaded.mainBuilding[static_cast<unsigned>(AIPlanning::BuildingIntent::TrainSwim)]==4);
            CHECK(in->readUint32("sentinel")==sentinel);
        });
    }
}
TEST_CASE("new timer records reject out-of-range missing and malformed values")
{
    World w;
    for (bool text : {false,true}) for (auto values : {std::pair{-1,10},std::pair{31,10},std::pair{10,-1},std::pair{10,51}}) {
        const auto bytes=write(text,[&](auto* out){
            out->writeEnterSection("AIWarrush"); out->writeSint32(values.first,"buildingDelay");
            out->writeSint32(values.second,"areaUpdatingDelay"); out->writeLeaveSection();
        });
        read(bytes,text,[&](auto* in){AIWarrush loaded(w.player()); CHECK_FALSE(loaded.load(in,w.player(),VERSION_MINOR));});
        read(bytes,text,[&](auto* in){CHECK_THROWS(AIWarrush(in,w.player(),VERSION_MINOR));});
    }
    for (bool text : {false,true}) {
        const auto missing=write(text,[&](auto* out){out->writeEnterSection("AIWarrush");out->writeSint32(4,"buildingDelay");out->writeLeaveSection();});
        read(missing,text,[&](auto* in){AIWarrush loaded(w.player()); CHECK_THROWS(loaded.load(in,w.player(),VERSION_MINOR));});
    }
    for (const char* value : {"", "invalid", "2147483648", "1 trailing"}) {
        const auto bytes=write(true,[&](auto* out){out->writeEnterSection("AIWarrush");out->writeText(value,"buildingDelay");out->writeSint32(2,"areaUpdatingDelay");out->writeLeaveSection();});
        read(bytes,true,[&](auto* in){AIWarrush loaded(w.player()); CHECK_THROWS(loaded.load(in,w.player(),VERSION_MINOR));});
    }
    AINumbi original(w.player());
    for (int timer : {-1,original.phaseTime+1}) {
        original.timer=timer;
        for (bool text : {false,true}) {
            const auto bytes=write(text,[&](auto* out){original.save(out);});
            read(bytes,text,[&](auto* in){AINumbi loaded(w.player());CHECK_FALSE(loaded.load(in,w.player(),VERSION_MINOR));});
        }
    }
}
TEST_CASE("pre-fix format 127 writers remain aligned for Cabino Nicowar and Cortex")
{
    World w;
    for (bool text : {false,true}) for (const char* name : {"cabino","nicowar","cortex"}) {
        CAPTURE(text); CAPTURE(name);
        std::ifstream file(glob2test::fixture(std::string("legacy-ai-127/")+name+(text ? ".txt" : ".bin")),std::ios::binary);
        REQUIRE(file.good());
        const std::string bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        read(bytes,text,[&](auto* in){
            if (std::string(name)=="cabino") {
                Cabino::AICabino loaded(in,w.player(),127);
                CHECK(loaded.timer==77); CHECK(loaded.orders.empty());
                CHECK(loaded.gradient_manager.gradients.empty());
                auto* happiness=dynamic_cast<Cabino::HappinessHandler*>(loaded.getOtherModule("HappinessHandler"));
                REQUIRE(happiness); CHECK_FALSE(happiness->is_fruit_trees_computed);
            } else if (std::string(name)=="nicowar") {
                NewNicowar loaded; REQUIRE(loaded.load(in,w.player(),127));
                CHECK(loaded.timer==77); CHECK_FALSE(loaded.defend_explorers);
                CHECK_FALSE(loaded.explorer_attack_preparation_phase); CHECK_FALSE(loaded.explorer_attack_phase);
            } else {
                AICortex loaded(in,w.player(),127); CHECK(loaded.timer==77);
            }
            CHECK(in->readUint32("sentinel")==sentinel);
        });
    }
}
TEST_CASE("Warrush wrapper preserves the next decision and its random stream")
{
    World w; w.world.addBuilding("swarm",4,4);
    w.player()->makeItAI(AI::WARRUSH);
    auto* ai=w.player()->ai;
    ai->getOrder(false); ai->getOrder(false);
    auto* original=dynamic_cast<AIWarrush*>(ai->aiImplementation); REQUIRE(original);
    const int building=original->buildingDelay, area=original->areaUpdatingDelay;
    const auto before=original->snapshotRandom();
    const auto bytes=write(false,[&](auto* out){ai->save(out);});
    const auto expected=wire(ai->getOrder(false)); const auto random=original->snapshotRandom();
    read(bytes,false,[&](auto* in){REQUIRE(ai->load(in,VERSION_MINOR));});
    auto* loaded=dynamic_cast<AIWarrush*>(ai->aiImplementation); REQUIRE(loaded);
    CHECK(loaded->buildingDelay==building); CHECK(loaded->areaUpdatingDelay==area);
    CHECK(loaded->snapshotRandom()==before);
    CHECK(wire(ai->getOrder(false))==expected); CHECK(loaded->snapshotRandom()==random);
}
TEST_CASE("Numbi wrapper preserves an active phase clock and the next decision")
{
    World w; w.world.addBuilding("swarm",4,4);
    for (bool text : {false,true}) {
        w.player()->makeItAI(AI::NUMBI);
        auto* ai=w.player()->ai;
        ai->getOrder(false); // Initialize the wrapper's controller stream.
        auto* original=dynamic_cast<AINumbi*>(ai->aiImplementation); REQUIRE(original);
        original->timer=original->phaseTime;
        const auto before=original->snapshotRandom();
        const auto bytes=write(text,[&](auto* out){ai->save(out);});
        const auto expected=wire(ai->getOrder(false)); const auto random=original->snapshotRandom();
        const int phase=original->phase, timer=original->timer;
        read(bytes,text,[&](auto* in){REQUIRE(ai->load(in,VERSION_MINOR));});
        auto* loaded=dynamic_cast<AINumbi*>(ai->aiImplementation); REQUIRE(loaded);
        CHECK(loaded->snapshotRandom()==before);
        CHECK(wire(ai->getOrder(false))==expected); CHECK(loaded->snapshotRandom()==random);
        CHECK(loaded->phase==phase); CHECK(loaded->timer==timer);
    }
}
TEST_CASE("Nicowar explorer phases and full construction counts survive")
{
    World w;
    for (bool text : {false,true}) {
        NewNicowar original; NicowarStrategyLoader strategies; original.strategy=strategies.getParticularStrategy("default");
        original.timer=127; original.defend_explorers=true;
        original.explorer_attack_preparation_phase=true; original.explorer_attack_phase=true;
        original.starving_recovery_inns=300; original.buildings_under_construction_per_type[0]=400;
        original.placement_queue.push_back(NewNicowar::RegularInn);
        const auto bytes=write(text,[&](auto* out){original.save(out);out->writeUint32(sentinel,"sentinel");});
        read(bytes,text,[&](auto* in){
            NewNicowar loaded; REQUIRE(loaded.load(in,w.player(),VERSION_MINOR));
            CHECK(loaded.defend_explorers); CHECK(loaded.explorer_attack_preparation_phase); CHECK(loaded.explorer_attack_phase);
            CHECK(loaded.starving_recovery_inns==300); CHECK(loaded.buildings_under_construction_per_type[0]==400);
            CHECK(loaded.placement_queue.size()==1); CHECK(in->readUint32("sentinel")==sentinel);
        });
        NewNicowar loaded;
        for (int i=0;i<2;++i) read(bytes,text,[&](auto* in){REQUIRE(loaded.load(in,w.player(),VERSION_MINOR));CHECK(loaded.placement_queue.size()==1);});
    }
}
TEST_CASE("Cabino saving leaves its pending FIFO intact")
{
    World w;
    for (bool text : {false,true}) {
        Cabino::AICabino original(w.player());
        original.orders.push(std::make_shared<OrderModifyBuilding>(17,5));
        original.orders.push(std::make_shared<NullOrder>());
        original.orders.push(std::make_shared<OrderModifyBuilding>(23,9));
        original.orders.front()->sender=3; original.orders.front()->gameCheckSum=sentinel;
        const auto before=original.orders;
        const auto bytes=write(text,[&](auto* out){original.save(out);});
        CHECK(original.orders.size()==before.size());
        // Decoding the historical broken nonempty FIFO is deliberately not a
        // baseline assertion: the old writer did not store a complete record.
        if (original.orders.size()!=before.size()) continue;
        read(bytes,text,[&](auto* in){
            Cabino::AICabino loaded(in,w.player(),VERSION_MINOR); REQUIRE(loaded.orders.size()==before.size());
            CHECK(loaded.orders.front()->sender==3); CHECK(loaded.orders.front()->gameCheckSum==sentinel);
            auto expected=before;
            while (!expected.empty()) { CHECK(wire(loaded.getOrder())==wire(expected.front()));expected.pop(); }
        });
    }
}
TEST_CASE("Cabino construction polls preserve high material and combined source masks")
{
    using namespace Cabino;
    using Poll=DistributedNewConstructionManager::GradientPoll;
    static_assert(std::is_same_v<decltype(Poll{}.source),unsigned>);
    static_assert(sizeof(Gradient::Sources)==sizeof(unsigned));
    static_assert(alignof(Gradient::Sources)==alignof(unsigned));
    World w;
    auto& game=w.world.game;
    game.map.setResourceByIndex(13,15,PRUNE,1);
    game.map.setResourceByIndex(20,16,WHEAT,1);
    REQUIRE((game.map.materialMaskAt(game.map.coordToIndex(13,15)) & materialBit(MaterialId::Prunes))!=0);
    const unsigned highMask=1u<<(8+materialIndex(MaterialId::Prunes));
    const unsigned combinedMask=highMask|(1u<<(8+materialIndex(MaterialId::Food)))|Gradient::VillageCenter;
    Poll high(highMask,Gradient::None,4);
    Poll combined(combinedMask,Gradient::None,-1,-1);
    CHECK(high.source==highMask);
    CHECK(combined.source==combinedMask);
    Poll assigned(Gradient::Stone,Gradient::None,1);
    assigned.source=combinedMask; // the construction planner's late-bound assignment
    CHECK(assigned.source==combined.source);
    AICabino original(w.player());
    MersenneTwister controllerRandom(713);original.setRandomEngine(controllerRandom);
    const auto controllerBefore=controllerRandom,gameBefore=game.syncRandom;
    const int width=game.map.getW(),height=game.map.getH();
    const auto distance=[&](int x,int y,int sourceX,int sourceY) {
        int dx=std::abs(x-sourceX),dy=std::abs(y-sourceY);
        return std::max(std::min(dx,width-dx),std::min(dy,height-dy));
    };
    const auto checkFields=[&](AICabino& ai) {
        glob2test::withCabinoObservation(ai,game,[&] {
            auto& highField=ai.getGradientManager().getGradient(high.source,high.obstacle);
            auto& combinedField=ai.getGradientManager().getGradient(assigned.source,combined.obstacle);
            for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                CHECK(highField.getHeight(x,y)==1+distance(x,y,13,15));
                const int nearest=std::min({distance(x,y,13,15),distance(x,y,20,16),
                    distance(x,y,int(ai.getCenterX()),int(ai.getCenterY()))});
                CHECK(combinedField.getHeight(x,y)==1+nearest);
            }
        });
    };
    checkFields(original);
    glob2test::withCabinoObservation(original,game,[&]{original.getGradientManager().updateGradients();});
    for(bool text:{false,true}) {
        CAPTURE(text);
        const auto bytes=write(text,[&](auto* out){original.getGradientManager().save(out);out->writeUint32(sentinel,"sentinel");});
        AICabino restored(w.player());
        read(bytes,text,[&](auto* in) {
            REQUIRE(glob2test::withCabinoObservation(restored,game,[&]{return restored.getGradientManager().load(in);}));
            CHECK(in->readUint32("sentinel")==sentinel);
        });
        CHECK(write(text,[&](auto* out){restored.getGradientManager().save(out);out->writeUint32(sentinel,"sentinel");})==bytes);
        checkFields(restored);
        glob2test::withCabinoObservation(restored,game,[&]{restored.getGradientManager().updateGradients();});
        checkFields(restored);
    }
    CHECK(controllerRandom==controllerBefore);
    CHECK(game.syncRandom==gameBefore);
}
TEST_CASE("Cabino material mask placement resumes without changing RNG")
{
    using namespace Cabino;
    World w;
    auto& game=w.world.game;
    game.map.setMapDiscovered();
    REQUIRE(w.world.addBuilding("swarm",4,4));
    game.map.setResourceByIndex(20,20,STONE,1);
    const int tower=game.buildingsTypes.findByKey("defencetower.0.finished");
    REQUIRE(tower>=0);
    const auto* type=game.buildingsTypes.get(tower);
    REQUIRE(type->shootingRange>0);
    REQUIRE(type->semantics.ammunitionMaterial==int(materialIndex(MaterialId::Stone)));
    REQUIRE(type->semantics.ammunitionCost>0);
    AICabino original(w.player());
    MersenneTwister controllerRandom(713);original.setRandomEngine(controllerRandom);
    const auto controllerBefore=controllerRandom,gameBefore=game.syncRandom;
    const auto place=[&](AICabino& ai) {
        return glob2test::withCabinoObservation(ai,game,[&] {
            auto* construction=dynamic_cast<DistributedNewConstructionManager*>(ai.getNewConstructionModule());
            REQUIRE(construction);
            construction->updateImap();
            return construction->findBestPlace(DefendWithProjectiles,unsigned(tower));
        });
    };
    const auto expected=place(original);
    REQUIRE(expected.x!=NO_POSITION);
    REQUIRE(expected.y!=NO_POSITION);
    CHECK(expected.x<unsigned(game.map.getW()));
    CHECK(expected.y<unsigned(game.map.getH()));
    const unsigned stoneMask=1u<<(8+materialIndex(MaterialId::Stone));
    bool hasMaterialGradient=false;
    // Inspect the already selected rows; calling getGradient here would create
    // a missing row and could hide a placement-mask regression.
    const auto selectedRows=write(false,[&](auto* out){original.getGradientManager().save(out);});
    glob2test::withCabinoObservation(original,game,[&] {
        read(selectedRows,false,[&](auto* in) {
            in->readEnterSection("GradientManager");
            const auto count=in->readUint32("size");
            REQUIRE(count>0);
            for(Uint32 row=0;row<count;++row) {
                in->readEnterSection(row);
                const unsigned source=in->readUint32("sources");
                const unsigned obstacles=in->readUint32("obstacles");
                Gradient field;
                REQUIRE(field.load(in,original));
                if(source==stoneMask) {
                    CHECK(obstacles==unsigned(Gradient::None));
                    CHECK(field.getHeight(20,20)==1);
                    hasMaterialGradient=true;
                }
                in->readLeaveSection();
            }
            in->readLeaveSection();
            CHECK(in->isValid());
        });
    });
    REQUIRE(hasMaterialGradient); // exercises the exact 2048 mask in the sanitizer report
    for(bool text:{false,true}) {
        CAPTURE(text);
        const auto bytes=write(text,[&](auto* out){original.save(out);out->writeUint32(sentinel,"sentinel");});
        AICabino restored(w.player());
        MersenneTwister restoredRandom(713);restored.setRandomEngine(restoredRandom);
        read(bytes,text,[&](auto* in){REQUIRE(restored.load(in,w.player(),VERSION_MINOR));CHECK(in->readUint32("sentinel")==sentinel);});
        CHECK(write(text,[&](auto* out){restored.getGradientManager().save(out);})
            ==write(text,[&](auto* out){original.getGradientManager().save(out);}));
        const auto resumed=place(restored);
        CHECK(resumed.x==expected.x);CHECK(resumed.y==expected.y);
        CHECK(write(text,[&](auto* out){restored.getGradientManager().save(out);})
            ==write(text,[&](auto* out){original.getGradientManager().save(out);}));
        CHECK(restoredRandom==controllerBefore);
    }
    CHECK(controllerRandom==controllerBefore);
    CHECK(game.syncRandom==gameBefore);
}
TEST_CASE("Cabino preserves stale gradients module latches and fruit memories")
{
    World w;
    using namespace Cabino;
    for (bool text : {false,true}) {
        AICabino original(w.player());
        auto* defense=dynamic_cast<SimpleBuildingDefense*>(original.getDefenseModule()); REQUIRE(defense);
        defense->building_health[17]=43;
        auto* explore=dynamic_cast<ExplorationManager*>(original.getOtherModule("ExplorationManager")); REQUIRE(explore);
        explore->original_explorers_wanted=8;
        auto* happy=dynamic_cast<HappinessHandler*>(original.getOtherModule("HappinessHandler")); REQUIRE(happy);
        happy->is_fruit_trees_computed=true;
        happy->fruit_trees.push_back({12,14,6,4,1});
        happy->exploring_fruit_trees.push_back({17,8,9,3});
        auto* farmer=dynamic_cast<Farmer*>(original.getOtherModule("Farmer")); REQUIRE(farmer);
        auto& manager=original.getGradientManager();
        glob2test::withCabinoObservation(original,w.world.game,[&] {
            farmer->water_gradient.reset(original,Gradient::Water,Gradient::None);
            field::Frontier frontier; farmer->water_gradient.update(frontier); farmer->water_gradient.gradient[7]=123;
            farmer->is_water_gradient_computed=true;
            manager.getGradient(Gradient::Wood,Gradient::Resource).gradient[5]=77;
            manager.getGradient(Gradient::Wheat,Gradient::Building).gradient[6]=88;
            manager.updateGradients(); // rotate the refresh FIFO away from map order.
        });
        const auto bytes=write(text,[&](auto* out){original.save(out);});
        AICabino loaded(w.player());
        for (int attempt=0;attempt<2;++attempt) read(bytes,text,[&](auto* in){
            REQUIRE(loaded.load(in,w.player(),VERSION_MINOR));
            CHECK(loaded.modules.size()==original.modules.size());
            auto* d=dynamic_cast<SimpleBuildingDefense*>(loaded.getDefenseModule()); CHECK(d->building_health==defense->building_health);
            auto* e=dynamic_cast<ExplorationManager*>(loaded.getOtherModule("ExplorationManager")); CHECK(e->original_explorers_wanted==8);
            auto* h=dynamic_cast<HappinessHandler*>(loaded.getOtherModule("HappinessHandler")); CHECK(h->is_fruit_trees_computed);
            REQUIRE(h->fruit_trees.size()==1); CHECK(h->fruit_trees[0].fruit_tree_max_x==12);
            REQUIRE(h->exploring_fruit_trees.size()==1); CHECK(h->exploring_fruit_trees[0].flag==17);
            auto* f=dynamic_cast<Farmer*>(loaded.getOtherModule("Farmer")); CHECK(f->is_water_gradient_computed);
            CHECK(f->water_gradient.gradient==farmer->water_gradient.gradient);
            auto& gm=loaded.getGradientManager(); REQUIRE(gm.gradients.size()==manager.gradients.size());
            auto expected=manager.update_queue, actual=gm.update_queue;
            while (!expected.empty()) {
                CHECK(actual.front()->first.sources==expected.front()->first.sources);
                CHECK(actual.front()->first.obstacles==expected.front()->first.obstacles);
                CHECK(actual.front()->second.gradient==expected.front()->second.gradient);
                actual.pop(); expected.pop();
            }
        });
    }
}
TEST_CASE("Cortex saved policy and model supersede the current process selection")
{
    World w;
    for (bool text : {false,true}) for (int mode : {0,1,2}) {
        AICortex original(w.player());
        original.policy.mlSwarmCaps_=mode==1; original.policy.mlDecide_=mode==2;
        if (mode) {
            auto& net=mode==1 ? original.policy.swarmNet_ : original.policy.decisionNet_;
            const int input=mode==1 ? 16 : 48, output=mode==1 ? 20 : 18;
            std::vector<Uint8> blob;
            auto word=[&](Uint32 v){for (int n=0;n<32;n+=8) blob.push_back(Uint8(v>>n));};
            word(0x434E5831); word(1); word(16); word(1); word(2);
            word(input); word(output); word(input); word(output);
            for(int i=0;i<input*output;++i) word(0);
            for(int i=0;i<output;++i) word(Uint32(i*65536));
            REQUIRE(net.loadFromMemory(blob.data(),blob.size(),input,output));
        }
        const auto bytes=write(text,[&](auto* out){original.save(out);});
        read(bytes,text,[&](auto* in){
            AICortex loaded(w.player());
            loaded.policy.mlSwarmCaps_=true; loaded.policy.mlDecide_=false;
            REQUIRE(loaded.load(in,w.player(),VERSION_MINOR));
            CHECK(loaded.policy.mlSwarmCaps_==original.policy.mlSwarmCaps_);
            CHECK(loaded.policy.mlDecide_==original.policy.mlDecide_);
            if (mode) {
                auto& expected=mode==1 ? original.policy.swarmNet_ : original.policy.decisionNet_;
                auto& actual=mode==1 ? loaded.policy.swarmNet_ : loaded.policy.decisionNet_;
                CHECK(actual.snapshotBlob()==expected.snapshotBlob());
            }
        });
    }
}

TEST_CASE("shared runtime reload replaces queues and registrations in binary and text")
{
    World w;
    using namespace AISharedRuntime;
    for (bool text : {false,true}) {
        Runtime original(new Econo,w.player());
        Runtime::OwnerObservationScope originalObservation(original);
        original.push_order(std::make_shared<OrderModifyBuilding>(17,5));
        original.push_order(std::make_shared<NullOrder>());
        original.add_building_order(new Construction::BuildingOrder(IntBuildingType::FOOD_BUILDING,3));
        original.add_management_order(new Management::AssignWorkers(4,17));
        const auto bytes=write(text,[&](auto* out){original.save(out);out->writeUint32(sentinel,"sentinel");});
        Runtime loaded(new Econo,w.player());
        Runtime::OwnerObservationScope loadedObservation(loaded);
        for (int attempt=0;attempt<2;++attempt) read(bytes,text,[&](auto* in){
            REQUIRE(loaded.load(in,w.player(),VERSION_MINOR));
            REQUIRE(loaded.orders.size()==2); CHECK(wire(loaded.orders.front())==wire(original.orders.front()));
            CHECK(loaded.management_orders.size()==1); CHECK(loaded.building_orders.size()==1);
            CHECK(loaded.br.pending_buildings.size()==original.br.pending_buildings.size());
            CHECK(in->readUint32("sentinel")==sentinel);
            // Values absent from the saved image must disappear on the next load.
            loaded.orders.push_back(std::make_shared<NullOrder>());
            loaded.br.register_building();
        });
    }
}

}
