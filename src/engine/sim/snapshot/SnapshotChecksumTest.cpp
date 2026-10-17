// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "SnapshotChecksum.h"
#include "Unit.h"
#include "Building.h"
#include <cstddef>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <thread>
#include <atomic>

namespace {
SimulationSnapshot::Handle captured(glob2test::HeadlessGame& fixture) {
    return SimulationSnapshot::capture(fixture.game,SimulationSnapshot::captureCatalog(fixture.game));
}
Uint32 referenceChecksum(const SimulationSnapshot::Handle& world) {
    return SimulationChecksum::snapshotChecksum(world,true);
}
}
TEST_SUITE("SnapshotChecksum") {
    TEST_CASE("saved world and paused reporting clocks share simulation scheduler priorities") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto world=captured(fixture); world.tick=100;
        ComputeExecutor executor; executor.configure(1); executor.configureTelemetry(ComputeTelemetryMode::Trace);
        SimulationChecksum::Pipeline pipeline(executor); pipeline.submit(world,0,true);
        CHECK(pipeline.collect(0,false,100).empty());
        const auto result=pipeline.collect(1,false,100); REQUIRE(result.size()==1);
        CHECK(result[0].sample.sourceTick==0); CHECK(result[0].worldTick==100);
        const auto trace=executor.telemetrySnapshot(true);
        bool submitted=false,joined=false;
        for (const auto& stream:trace.streams) for (std::size_t i=0;i<stream.used;++i) {
            const auto& event=stream.storage[i]; if (event.producer!=ComputeProducer::Checksum) continue;
            if (event.kind==ComputeTelemetry::EventKind::Submit) {
                submitted=true; CHECK(event.due==ComputeExecutor::boundaryDue(101));
                CHECK(event.observationTick==100); CHECK(event.publicationTick==101);
            }
            if (event.kind==ComputeTelemetry::EventKind::JoinInspection) {
                joined=true; CHECK(event.logicalTick==100);
            }
        }
        CHECK(submitted); CHECK(joined);
    }

    TEST_CASE("save load preserves routine state while deep hashes cover world randomness") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame original({.loadDefaultRace=true,.header=true});
        REQUIRE(original.addUnit(WORKER,12,12,0)); REQUIRE(original.addBuilding("inn",4,4,0));
        original.step(32); // Exercise periodic map, stock and occupancy coverage too.
        original.game.totalPrestige=17; // This derived total is rebuilt on the next step after loading.
        const auto before=referenceChecksum(captured(original));
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream output(backend);
        original.game.save(&output,false,"checksum continuation"); output.flush();
        const auto bytes=backend->takeContents();
        glob2test::HeadlessGame restored({.loadDefaultRace=true,.header=true});
        auto* inputBackend=new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()); inputBackend->seekFromStart(0);
        GAGCore::BinaryInputStream input(inputBackend);
        REQUIRE(restored.game.load(&input));
        CHECK(referenceChecksum(captured(restored))==before);
        const auto deep=original.game.checkSum(nullptr,nullptr,nullptr,true);
        original.game.privateRandom(RandomDomain::GrowthJobs).nextU32();
        CHECK(original.game.checkSum(nullptr,nullptr,nullptr,true)!=deep);
        CHECK(referenceChecksum(captured(original))==before);
    }
    TEST_CASE("a missed deadline waits for workers and never runs hashing on the owner") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto world=captured(fixture);
        ComputeExecutor executor; executor.configure(2);
        if (executor.threadCount()<2) return;
        executor.configureTelemetry(ComputeTelemetryMode::Waits);
        std::atomic<bool> entered=false,release=false;
        struct State { std::atomic<bool>& entered; std::atomic<bool>& release; } state{entered,release};
        const ComputeExecutor::Group blocker{1,{[](void* ptr,std::size_t) {
            auto& state=*static_cast<State*>(ptr); state.entered=true;
            while (!state.release.load()) std::this_thread::yield();
        },&state}};
        auto blocking=executor.submit(std::span(&blocker,1),0);
        while (!entered.load()) std::this_thread::yield();
        SimulationChecksum::Pipeline pipeline(executor); pipeline.submit(world,0,true);
        CHECK(pipeline.collect(0).empty());
        std::thread unblock([&] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); release=true; });
        auto result=pipeline.collect(1); unblock.join(); executor.join(blocking);
        REQUIRE(result.size()==1); CHECK(result[0].sample.value==referenceChecksum(world));
        const auto metrics=executor.telemetrySnapshot();
        const auto& waits=metrics.summary.joins[unsigned(ComputeProducer::Checksum)][unsigned(JoinReason::TickDeadline)];
        CHECK(waits.joined==1); CHECK(waits.ownerExecutions==0);
    }

    TEST_CASE("canonical fields ignore padding addresses and presentation") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addUnit(WORKER,12,12,0));
        auto world=captured(fixture);
        const auto original=referenceChecksum(world);
        CHECK(original==referenceChecksum(world.checksumView(true)));
        const auto simulation=SimulationSnapshot::capture(fixture.game,SimulationSnapshot::captureCatalog(fixture.game),SimulationSnapshot::Simulation);
        CHECK(original==referenceChecksum(simulation));
        auto catalogs=std::make_shared<SimulationSnapshot::Catalogs>(*world.catalogs);
        catalogs->buildingFingerprint="optional preference identity"; world.catalogs=catalogs;
        CHECK(original==referenceChecksum(world));
        auto teams=std::make_shared<SimulationSnapshot::Teams>(*world.teams); world.teams=teams;
        teams->values.front().won=!teams->values.front().won;
        teams->values.front().alive=!teams->values.front().alive;
        ++teams->totalPrestige;
        CHECK(original==referenceChecksum(world));
        auto entities=std::make_shared<SimulationSnapshot::Entities>(*world.entities);
        world.entities=entities;
        ++world.worldIdentity; ++world.observationRevision;
        ++entities->units.front().levelUpAnimation;
        entities->unitCargo.overflow(entities->units.front().gid); // Empty allocation disappears on load.
        CHECK(original==referenceChecksum(world));
        ++entities->units.front().hp;
        CHECK(original!=referenceChecksum(world));
        --entities->units.front().hp;
        entities->units.front().entityRandom.nextU32();
        CHECK(original!=referenceChecksum(world));
        auto resources=std::make_shared<SimulationSnapshot::Resources>(*world.resources);
        world.resources=resources;
        auto& cell=resources->cells.front();
        static_assert(offsetof(SimulationSnapshot::ResourceCell,mayGrow)+sizeof(cell.mayGrow)<sizeof(cell));
        const auto before=referenceChecksum(world);
        reinterpret_cast<unsigned char*>(&cell)[sizeof(cell)-1]^=0xff;
        CHECK(before==referenceChecksum(world));
    }
    TEST_CASE("routine coverage does not expand to authored configuration or extra snapshot fields") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addUnit(WORKER,12,12,0));
        auto world=captured(fixture);
        const auto expected=referenceChecksum(world);
        auto catalogs=std::make_shared<SimulationSnapshot::Catalogs>(*world.catalogs);
        auto buildings=std::make_shared<std::vector<SimulationSnapshot::BuildingKindView>>(*catalogs->buildings);
        ++buildings->front().hpMax; catalogs->buildings=buildings; world.catalogs=catalogs;
        auto rules=std::make_shared<SimulationSnapshot::Rules>(*world.rules);
        rules->values.hungerDisabled=!rules->values.hungerDisabled; world.rules=rules;
        auto entities=std::make_shared<SimulationSnapshot::Entities>(*world.entities);
        ++entities->units.front().hungriness;
        ++entities->units.front().scriptIdentity;
        ++entities->units.front().realAttackStrength;
        world.entities=entities;
        CHECK(referenceChecksum(world)==expected);
        ++entities->units.front().hp;
        CHECK(referenceChecksum(world)!=expected);
    }
    TEST_CASE("terrain rule interning order does not affect canonical terrain hashes") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto world=captured(fixture);
        auto a=std::make_shared<SimulationSnapshot::Terrain>(*world.terrain);
        auto b=std::make_shared<SimulationSnapshot::Terrain>(*world.terrain);
        auto vertices=std::make_shared<std::vector<TerrainType>>(*world.terrain->vertices);
        (*vertices)[0]=SAND; (*vertices)[world.width+1]=SAND;
        a->vertices=vertices; b->vertices=vertices;
        auto first=std::make_shared<CellRuleTable>(a->registry,world.catalogs->resources);
        auto second=std::make_shared<CellRuleTable>(b->registry,world.catalogs->resources);
        const auto one=CellRuleTable::key(GRASS,GRASS,GRASS,SAND);
        const auto two=CellRuleTable::key(GRASS,GRASS,SAND,SAND);
        first->intern(one); first->intern(two); second->intern(two); second->intern(one);
        CHECK(first->find(one)!=second->find(one));
        for (int y=0;y<world.height;++y) for (int x=0;x<world.width;++x) {
            const auto index=[&](int x,int y) { return (y%world.height)*world.width+x%world.width; };
            const auto key=CellRuleTable::key((*vertices)[index(x,y)],(*vertices)[index(x+1,y)],
                (*vertices)[index(x,y+1)],(*vertices)[index(x+1,y+1)]);
            const auto i=index(x,y);
            a->cellRules[i]=first->intern(key); b->cellRules[i]=second->intern(key);
            CHECK((*first)[a->cellRules[i]].corners==(*second)[b->cellRules[i]].corners);
        }
        a->rules=first; b->rules=second;
        world.terrain=a; const auto expected=referenceChecksum(world);
        world.terrain=b; CHECK(referenceChecksum(world)==expected);
    }
    TEST_CASE("resource stock compaction preserves hashes while logical stock changes are detected") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto world=captured(fixture);
        auto resources=std::make_shared<SimulationSnapshot::Resources>(*world.resources); world.resources=resources;
        resources->stockIndices.assign(resources->cells.size(),0);
        const auto empty=referenceChecksum(world);
        resources->stockIndices.clear();
        CHECK(referenceChecksum(world)==empty);
        resources->stockIndices.assign(resources->cells.size(),0);
        resources->stocks.resize(2); resources->stocks[0][0]=11; resources->stocks[1][1]=23;
        resources->stockIndices[0]=1; resources->stockIndices[1]=2;
        const auto expected=referenceChecksum(world);
        std::swap(resources->stocks[0],resources->stocks[1]);
        resources->stocks.push_back(resources->stocks[1]);
        resources->stocks[1].fill(255); // Unreferenced, reusable storage is irrelevant.
        resources->stockIndices[0]=3; resources->stockIndices[1]=1;
        CHECK(referenceChecksum(world)==expected);
        ++resources->stocks[2][0];
        CHECK(referenceChecksum(world)!=expected);
    }
    TEST_CASE("local games omit map sweeps and remote maps retain the original fields") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto world=captured(fixture);
        auto resources=std::make_shared<SimulationSnapshot::Resources>(*world.resources);
        auto occupancy=std::make_shared<SimulationSnapshot::Occupancy>(*world.occupancy);
        auto areas=std::make_shared<SimulationSnapshot::Areas>(*world.areas);
        auto visibility=std::make_shared<SimulationSnapshot::Visibility>(*world.visibility);
        world.resources=resources; world.occupancy=occupancy; world.areas=areas; world.visibility=visibility;
        const auto local=SimulationChecksum::snapshotChecksum(world);
        const auto remote=referenceChecksum(world);
        CHECK_FALSE(world.checksumView().resources);
        CHECK_FALSE(world.checksumView(true).visibility);
        const auto checkField=[&](auto& field,auto changed,bool covered) {
            const auto original=field; field=changed;
            CHECK(SimulationChecksum::snapshotChecksum(world)==local);
            CHECK((referenceChecksum(world)!=remote)==covered);
            field=original;
        };
        for (std::size_t index:{std::size_t(0),resources->cells.size()-1}) {
            checkField(resources->cells[index].resource.amount,Uint32(0x80000000),true);
            checkField(resources->cells[index].mayGrow,Uint8(0x80),true);
            checkField(occupancy->cells[index].building,Uint16(0x8000),true);
            checkField(occupancy->cells[index].groundUnit,Uint16(0x8000),true);
            checkField(occupancy->cells[index].airUnit,Uint16(0x8000),true);
            checkField(areas->cells[index].forbidden,Uint32(0x80000000),true);
            checkField(areas->cells[index].farm,Uint32(0x80000000),true);
            checkField(resources->cells[index].fertility,Uint16(0x8000),false);
            checkField(occupancy->cells[index].immobileUnit,Uint8(0x80),false);
            checkField(areas->cells[index].guard,Uint32(0x80000000),false);
            checkField(areas->cells[index].clear,Uint32(0x80000000),false);
            checkField(visibility->discovered[index],Uint32(0x80000000),false);
            checkField(visibility->visible[index],Uint32(0x80000000),false);
        }
        ComputeExecutor executor; executor.configure(4);
        SimulationChecksum::Pipeline pipeline(executor);
        pipeline.submit(world,0); // Default policy must not sweep maps.
        const auto result=pipeline.collect(2); REQUIRE(result.size()==1);
        CHECK(result[0].sample.value==local);
    }
    TEST_CASE("large map arrays are covered only on absolute secondary ticks") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto world=captured(fixture);
        auto resources=std::make_shared<SimulationSnapshot::Resources>(*world.resources); world.resources=resources;
        for (Uint32 tick:{0u,1u,31u,32u,33u}) {
            world.tick=tick;
            const auto before=referenceChecksum(world);
            ++resources->cells.front().resource.amount;
            CHECK((before!=referenceChecksum(world))==(tick%32==0));
            CHECK(bool(world.checksumView(true).resources)==(tick%32==0));
        }
    }
    TEST_CASE("one tick jobs agree with reference and flush terminal samples") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto world=captured(fixture);
        for (unsigned participants:{1u,2u,4u}) {
            ComputeExecutor executor; executor.configure(participants);
            SimulationChecksum::Pipeline pipeline(executor);
            const auto expected=referenceChecksum(world);
            pipeline.submit(world,100,true); pipeline.submit(world,100,true);
            CHECK(pipeline.collect(100).empty());
            const auto results=pipeline.collect(101);
            REQUIRE(results.size()==1); CHECK(results[0].sample.sourceTick==100);
            CHECK(results[0].sample.value==expected); CHECK(results[0].worldTick==world.tick);
            pipeline.submit(world,101,true);
            CHECK(pipeline.collect(101).empty());
            pipeline.submit(world,102,true);
            CHECK(pipeline.collect(102,true).size()==2);
            pipeline.submit(world,103,true); pipeline.reset();
            CHECK(pipeline.collect(200,true).empty());
            auto temporary=captured(fixture);
            std::weak_ptr<const SimulationSnapshot::Entities> lease=temporary.entities;
            pipeline.submit(temporary,104,true); temporary={};
            if (participants==1) CHECK(lease.expired()); // No serial input lease waits for delivery.
            executor.joinAll({JoinReason::Verification,104});
            CHECK(lease.expired()); // Completed hashes retain results, not world buffers.
            auto completed=pipeline.collect(106); REQUIRE(completed.size()==1);
            CHECK(completed[0].sample.value==expected);
        }
    }
    TEST_CASE("recycled work slots preserve the reference hash without configuration walks") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addUnit(WORKER,12,12,0));
        auto world=captured(fixture);
        auto changedCatalog=std::make_shared<SimulationSnapshot::Catalogs>(*world.catalogs);
        auto kinds=std::make_shared<std::vector<SimulationSnapshot::BuildingKindView>>(*world.catalogs->buildings);
        ++kinds->front().hpMax;
        changedCatalog->buildings=kinds;
        auto changedRules=std::make_shared<SimulationSnapshot::Rules>(*world.rules);
        changedRules->named.emplace_back("checksum-test",17);
        const auto originalCatalog=world.catalogs;
        const auto originalRules=world.rules;
        for (unsigned participants:{1u,2u,4u}) {
            ComputeExecutor executor; executor.configure(participants);
            SimulationChecksum::Pipeline pipeline(executor);
            std::array<Uint32,96> expected{};
            unsigned received=0;
            const auto check=[&](const auto& results) {
                for (const auto& result:results) {
                    CHECK(result.sample.sourceTick==received);
                    CHECK(result.sample.value==expected[received]);
                    ++received;
                }
            };
            for (unsigned tick=0;tick<expected.size();++tick) {
                check(pipeline.collect(tick));
                world.tick=tick;
                world.catalogs=(tick/17)%2 ? changedCatalog : originalCatalog;
                world.rules=(tick/11)%2 ? changedRules : originalRules;
                auto entities=std::make_shared<SimulationSnapshot::Entities>(*world.entities);
                ++entities->units.front().hp;
                world.entities=entities;
                expected[tick]=referenceChecksum(world);
                pipeline.submit(world,tick,true);
            }
            check(pipeline.collect(96,true));
            CHECK(received==expected.size());
        }
    }
    TEST_CASE("world replacement discards stale samples and worker failures propagate") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addBuilding("inn",4,4,0));
        auto world=captured(fixture);
        ComputeExecutor executor; executor.configure(2);
        SimulationChecksum::Pipeline pipeline(executor);
        pipeline.submit(world,0,true); ++world.worldIdentity; pipeline.submit(world,1,true);
        auto results=pipeline.collect(3); REQUIRE(results.size()==1); CHECK(results[0].sample.sourceTick==1);
        auto entities=std::make_shared<SimulationSnapshot::Entities>(*world.entities);
        entities->buildings.front().fundingOffset=Uint32(entities->funding.size()+10);
        world.entities=entities;
        pipeline.submit(world,2,true);
        CHECK_THROWS_AS(pipeline.collect(4),std::out_of_range);
        CHECK_THROWS_AS(pipeline.collect(4,true),std::out_of_range);
        pipeline.reset();
        pipeline.submit(world,3,true);
        executor.configure(1); // Executor teardown drains slots and discards its own errors.
        CHECK_THROWS_AS(pipeline.collect(5),std::out_of_range);
        pipeline.reset();
    }
    TEST_CASE("worker checksums match direct recomputation across live map publications") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addUnit(WORKER,12,12,0));
        REQUIRE(fixture.addBuilding("inn",4,4,0));
        auto& executor=fixture.game.map.computeExecutor(); executor.configure(4);
        SimulationChecksum::Pipeline pipeline(executor);
        std::array<Uint32,129> expected{};
        unsigned received=0;
        const auto check=[&](const auto& results) {
            for (const auto& result:results) {
                CHECK(result.sample.sourceTick==received);
                CHECK(result.sample.value==expected[received]);
                ++received;
            }
        };
        for (unsigned tick=0;tick<expected.size();++tick) {
            check(pipeline.collect(tick));
            const auto& world=fixture.game.observeCompletedWorld();
            REQUIRE(world.tick==tick);
            expected[tick]=referenceChecksum(world);
            pipeline.submit(world,tick,true);
            if (tick+1<expected.size()) fixture.step(1);
        }
        check(pipeline.collect(129,true));
        CHECK(received==expected.size());
    }
    TEST_CASE("map checksum follows values independently of capture stamps and geometry") {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        const auto original=captured(fixture);
        for (unsigned participants:{1u,4u}) {
            ComputeExecutor executor; executor.configure(participants);
            SimulationChecksum::Pipeline pipeline(executor);
            auto world=original;
            std::array<Uint32,11> expected{};
            unsigned received=0;
            const auto check=[&](const auto& results) {
                for (const auto& result:results) {
                    CHECK(result.sample.sourceTick==received);
                    CHECK(result.sample.value==expected[received]);
                    ++received;
                }
            };
            for (unsigned phase=0;phase<expected.size();++phase) {
                check(pipeline.collect(phase));
                world.tick=phase*32;
                const auto index=std::size_t(8+phase*17)%world.resources->cells.size();
                MapState::ChunkGeometry geometry;
                geometry.reset(world.width,world.height,unsigned(std::countr_zero(unsigned(world.width))),Uint32(world.width-1));
                const auto touch=[&](auto& component,unsigned tracked) {
                    component.stamps.chunks.at(geometry.chunkOf(index))=++world.mapGenerations[tracked];
                };
                if (phase==1 || phase==8 || phase==9) {
                    auto component=std::make_shared<SimulationSnapshot::Resources>(*world.resources);
                    ++component->cells.at(index).resource.amount;
                    if (phase==9) component->stamps=original.resources->stamps;
                    touch(*component,1);
                    if (phase==8) component->stamps={};
                    if (phase==9) {
                        component->stamps=original.resources->stamps;
                        component->stamps.chunks.assign(geometry.count(),++world.mapGenerations[1]);
                    }
                    world.resources=component;
                } else if (phase==2) {
                    auto component=std::make_shared<SimulationSnapshot::Occupancy>(*world.occupancy);
                    component->cells.at(index).groundUnit^=1;
                    touch(*component,2); world.occupancy=component;
                } else if (phase==3) {
                    auto component=std::make_shared<SimulationSnapshot::Areas>(*world.areas);
                    component->cells.at(index).forbidden^=1;
                    touch(*component,3); world.areas=component;
                } else if (phase==4) {
                    auto component=std::make_shared<SimulationSnapshot::Areas>(*world.areas);
                    component->cells.at(index).farm^=1;
                    touch(*component,3); world.areas=component;
                } else if (phase==5) {
                    auto component=std::make_shared<SimulationSnapshot::Terrain>(*world.terrain);
                    auto vertices=std::make_shared<std::vector<TerrainType>>(*component->vertices);
                    vertices->at(index)=vertices->at(index)==SAND ? GRASS : SAND;
                    component->vertices=vertices;
                    touch(*component,0); world.terrain=component;
                } else if (phase==6) {
                    auto component=std::make_shared<SimulationSnapshot::Resources>(*world.resources);
                    ++component->materialSourceCounts[0]; // Outside the stamped cell arrays.
                    world.resources=component;
                } else if (phase==10) {
                    world.width*=2; world.height/=2;
                    const auto resize=[&](auto source) {
                        using T=std::remove_const_t<typename decltype(source)::element_type>;
                        auto component=std::make_shared<T>(*source);
                        component->stamps.width=world.width; component->stamps.height=world.height;
                        return component;
                    };
                    world.terrain=resize(world.terrain); world.resources=resize(world.resources);
                    world.occupancy=resize(world.occupancy); world.areas=resize(world.areas);
                    world.visibility=resize(world.visibility);
                }
                expected[phase]=referenceChecksum(world);
                pipeline.submit(world,phase,true);
            }
            check(pipeline.collect(11,true));
            CHECK(received==expected.size());
            std::weak_ptr<const SimulationSnapshot::Resources> resources=world.resources;
            world={};
            CHECK(resources.expired()); // The cache retains no map component leases.
        }
    }
}
