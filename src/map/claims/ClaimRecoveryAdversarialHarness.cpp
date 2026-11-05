// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary adversarial live-recovery experiments; no synthetic route failures.
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "ActiveRouteRuntime.h"
#include "ClaimPolicyConfig.h"
#include "gradient/GradientRuntime.h"
#include "engine/sim/snapshot/SnapshotChecksum.h"
#include "online/Sha256.h"
#include "GlobalContainer.h"
#include <FileManager.h>
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace {
constexpr std::size_t MaximumForkBytes=64u*1024u*1024u;
// Diagnostic intervention only. Ordinary loads and production policy setters
// retain their existing contracts. A claims-off checkpoint supplies identical
// warm gradients, AI/hiring work, stocks, clocks and animation state to every
// policy branch; only claim ownership/configuration is introduced here.
Uint32 nonClaimWorldChecksum(Game& game)
{
    auto world=SimulationSnapshot::capture(game,SimulationSnapshot::captureCatalog(game));
    auto entities=std::make_shared<SimulationSnapshot::Entities>(*world.entities);
    for(auto& unit:entities->units) {unit.activeClaimsEnabled=false;unit.experimentalMaterialClaims=false;}
    world.entities=std::move(entities);world.activeClaimRuntimeChecksum=0;
    return SimulationChecksum::snapshotChecksum(world,true);
}
nlohmann::json forkClassicCheckpoint(Game& game,ResourceClaims::ClaimPolicyConfig config)
{
    auto& map=game.map;config.validate();
    REQUIRE(game.stepCounter>0);REQUIRE(map.materialDestinationsEnabled());
    REQUIRE(map.materialClaimPolicy()==ResourceClaims::ActiveClaimPolicy::Disabled);
    REQUIRE_FALSE(map.materialClaimTracking);REQUIRE_FALSE(map.materialClaimRoutes);
    REQUIRE(map.materialClaimLedger().size()==0);
    std::vector<Unit*> units;
    for(int team=0;team<game.teamsCount();++team)for(auto* unit:game.teams[team]->liveUnits) {
        REQUIRE_FALSE(unit->activeMaterialClaim);REQUIRE(unit->materialClaimTask==0);
        REQUIRE(unit->materialClaimPolicyState==ResourceClaims::MaterialClaimPolicyState{});
        units.push_back(unit);
    }
    std::sort(units.begin(),units.end(),[](const Unit* a,const Unit* b) {
        // Preserve work already inside a harvest animation before reserving
        // stock for travelers. Both groups use stable generation/GID ownership.
        const bool ah=a->displacement==Unit::DIS_HARVESTING,bh=b->displacement==Unit::DIS_HARVESTING;
        return std::tuple{!ah,ResourceClaims::unitOwner(a->gid,a->scriptIdentity)}<
               std::tuple{!bh,ResourceClaims::unitOwner(b->gid,b->scriptIdentity)};
    });
    const auto before=nonClaimWorldChecksum(game),hiring=game.hiringChecksum();
    const auto adaptive=map.gradientRuntime->adaptive.checksum();
    const auto pending=[&] {
        std::vector<std::tuple<Uint16**,unsigned,bool>> jobs;
        map.gradientRuntime->pipeline.visitLogical([&](auto slot,unsigned due,bool superseded){jobs.emplace_back(slot,due,superseded);});
        return jobs;
    };
    const auto pendingBefore=pending();const auto queued=map.gradientRuntime->fieldRequests.size();
    std::ostringstream rngBefore;rngBefore<<game.syncRandom;
    // Allocate/validate before changing owner state. No pipeline is drained,
    // canceled, rescheduled or forced to publish by this intervention.
    const auto digest=Online::Sha256::digest(config.canonicalJson());
    auto frozen=std::make_unique<const ResourceClaims::ClaimPolicyConfig>(std::move(config));
    map.materialClaimConfiguration=std::move(frozen);map.materialClaimConfigurationDigest=digest;
    map.activeClaimPolicy=ResourceClaims::ActiveClaimPolicy::Experimental;
    map.checkActiveClaimRules();map.claimChanges.markAll();
    unsigned harvesting=0,traveling=0,rejected=0;
    for(auto* unit:units) {
        if(unit->activity!=Unit::ACT_FILLING || !unit->attachedBuilding || unit->medical!=Unit::MED_FREE || unit->isDead)continue;
        std::optional<Uint32> access;
        if(unit->displacement==Unit::DIS_HARVESTING)
            access=Uint32(map.coordToIndex(unit->posX+unit->dx,unit->posY+unit->dy));
        else if(unit->displacement==Unit::DIS_GOING_TO_RESOURCE && unit->validTarget() &&
                unit->targetX>=0 && unit->targetX<map.getW() && unit->targetY>=0 && unit->targetY<map.getH())
            access=Uint32(map.coordToIndex(unit->targetX,unit->targetY));
        if(!access)continue;
        const UnitState state=*unit;const auto random=unit->entityRandom;
        const bool acquired=unit->acquireMaterialClaimAt(*access);
        // Harvest animations may no longer have a live pursuit target. Their
        // reserved access is separate; retain all preexisting animation fields.
        unit->targetX=state.targetX;unit->targetY=state.targetY;unit->setValidTarget(state.validTarget());
        REQUIRE(static_cast<const UnitState&>(*unit)==state);REQUIRE(unit->entityRandom==random);
        if(acquired) {
            REQUIRE(unit->activeMaterialClaimValid());
            if(unit->displacement==Unit::DIS_HARVESTING)++harvesting;else ++traveling;
        } else ++rejected;
    }
    game.invalidateObservation();
    REQUIRE(game.hiringChecksum()==hiring);REQUIRE(map.gradientRuntime->adaptive.checksum()==adaptive);
    REQUIRE(pending()==pendingBefore);REQUIRE(map.gradientRuntime->fieldRequests.size()==queued);
    std::ostringstream rngAfter;rngAfter<<game.syncRandom;REQUIRE(rngAfter.str()==rngBefore.str());
    REQUIRE(nonClaimWorldChecksum(game)==before);
    return {{"version",1},{"intervention","claims-off-checkpoint-bootstrap-v1"},
        {"tick",game.stepCounter},{"policy_digest",map.materialClaimPolicyConfig().digest()},
        {"policy",nlohmann::json::parse(map.materialClaimPolicyConfig().canonicalJson())},
        {"bootstrap_order","harvesting first, then stable generation/GID; travelers second"},
        {"harvesting_claims",harvesting},{"traveling_claims",traveling},{"rejected_existing_accesses",rejected},
        {"unclaimed_behavior","existing access only; retry through normal movement/pickup; no pattern search during bootstrap"},
        {"nonclaim_world_checksum",before},{"hiring_checksum",hiring},{"pending_gradient_jobs",pendingBefore.size()},
        {"queued_gradient_jobs",queued},{"pending_work_reset",false},{"ordinary_continuation",false}};
}

std::unique_ptr<glob2test::HeadlessGame> copyRecoveryGame(glob2test::HeadlessGame& source,unsigned participants,const std::string& exportName={})
{
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(bytes);
    const auto before=source.game.checkSum();source.gui.save(&output,"claim recovery adversarial continuation");output.flush();
    if(!exportName.empty()) {
        std::ofstream file(glob2test::artifactDir()/(exportName+".game"),std::ios::binary);
        file.write(bytes->getBuffer(),bytes->getPosition());REQUIRE(file.good());
    }
    auto* copied=new GAGCore::MemoryStreamBackend(*bytes);copied->seekFromStart(0);
    auto result=std::make_unique<glob2test::HeadlessGame>();
    result->game.map.clear();result->game.map.resourceCells.clear();result->game.map.configureCompute(participants);
    GAGCore::BinaryInputStream input(copied);REQUIRE(result->gui.load(&input,true));
    REQUIRE(result->game.checkSum()==before);REQUIRE(source.game.checkSum()==before);
    return result;
}

struct Islands {
    glob2test::HeadlessGame world;
    Unit* worker=nullptr;Unit* holder=nullptr;Building* consumer=nullptr;
    Uint32 nearest=0,reachable=0;
    Islands(unsigned participants,bool closest,ResourceClaims::ClaimPolicyConfig::Fallback fallback=ResourceClaims::ClaimPolicyConfig::Fallback::Wait,bool adjacentPickup=false)
        :world({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9341}) {
        auto& game=world.game;auto& map=game.map;
        map.configureCompute(participants);game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
        {Map::TerrainEditBatch edit(map);for(int y=0;y<32;++y)for(int x:{7,8,16,17})map.setVertexTerrain(x,y,WATER);}
        map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;
        REQUIRE(nlohmann::json::parse(config.canonicalJson())["source_version"]==2);
        if(adjacentPickup) {config.sourceVersion=3;config.opportunisticAdjacentPickup=true;}
        config.fallback=fallback;config.fallbackAfterDecisions=1;
        if(closest)config.selection=ResourceClaims::ClaimPolicyConfig::Selection::ClosestGeometric;
        map.setMaterialClaimPolicyConfig(config);
        consumer=world.addBuilding("inn",9,22);REQUIRE(consumer);
        worker=world.addUnit(WORKER,8,8);holder=world.addUnit(WORKER,11,10);REQUIRE(worker);REQUIRE(holder);
        worker->setPerformance(SWIM,0);holder->setPerformance(SWIM,0);
        consumer->maxUnitWorking=2;consumer->setMaterial(WHEAT,0);consumer->updateCallLists();
        for(auto* unit:{worker,holder}) {unit->hungriness=0;unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;}
        nearest=Uint32(map.coordToIndex(12,8));reachable=Uint32(map.coordToIndex(14,18));
        // Six independent unavailable-island accesses exceed the four-entry cache.
        for(auto [x,y]:{std::pair{12,8},{14,18},{5,8},{4,10},{3,12},{2,14},{1,16},{0,18}}) {
            REQUIRE(map.incResourceByIndex(x,y,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(x,y),WHEAT,1);
        }
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(consumer->subscribeToBringMaterialsStep());REQUIRE(consumer->subscribeToBringMaterialsStep());
        glob2test::discardPrematureHiringSuggestions(game);
        REQUIRE(worker->attachedBuilding==consumer);REQUIRE(holder->attachedBuilding==consumer);
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.invalidateObservation();game.observeCompletedWorld();
    }
};
}

TEST_CASE("adjacent pickup exchange survives full-save continuation with an outstanding old route" * doctest::test_suite("ActiveClaimRoutes"))
{
    glob2test::HeadlessGlobals globals;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);
        Islands fixture(participants,false,ResourceClaims::ClaimPolicyConfig::Fallback::Wait,true);
        auto& game=fixture.world.game;auto& map=game.map;
        REQUIRE(fixture.worker->acquireMaterialClaimAt(fixture.reachable));
        REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(9,8),WHEAT,1);
        game.invalidateObservation();game.observeCompletedWorld();int dx=0,dy=0;
        REQUIRE(map.nextClaimRouteStep(*fixture.worker,&dx,&dy)==ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes(); // A checkpoint boundary has sealed this tick's submissions.
        const auto gid=fixture.worker->gid;
        auto resumed=copyRecoveryGame(fixture.world,participants,participants==1?"adjacent-pickup-v3-pending":"");
        if(participants==1) {
            std::ofstream metadata(glob2test::artifactDir()/"adjacent-pickup-v3-pending.json");
            metadata<<nlohmann::json{{"start_tick",game.stepCounter},{"gid",gid},{"old_source",fixture.reachable},
                {"adjacent_access",map.coordToIndex(9,8)},{"simulation_checksum",game.checkSum()}}.dump();REQUIRE(metadata.good());
        }
        REQUIRE(resumed->game.map.materialClaimPolicyConfig().opportunisticAdjacentPickup);
        const auto complete=[&](Game& branch) {
            auto* unit=branch.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(unit);
            auto& world=branch.map;const auto random=unit->entityRandom;
            unit->handleDisplacement();REQUIRE(unit->displacement==Unit::DIS_HARVESTING);
            REQUIRE(unit->activeMaterialClaim);CHECK(unit->activeMaterialClaim->access==world.coordToIndex(9,8));
            CHECK(world.materialClaimState().at(fixture.reachable)==0);
            CHECK(world.materialClaimState().at(world.coordToIndex(9,8))==1);
            world.flushClaimRoutes();++branch.stepCounter;world.publishClaimRoutes();
            CHECK(world.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Arrived);
            REQUIRE(unit->takeActiveMaterialClaim());
            CHECK(world.materialAmountAtSlot(world.coordToIndex(9,8),WHEAT)==0);
            CHECK(world.materialAmountAtSlot(fixture.reachable,WHEAT)==1);
            CHECK(world.materialClaimState().at(world.coordToIndex(9,8))==0);
            CHECK(unit->entityRandom==random);return branch.checkSum();
        };
        CHECK(complete(game)==complete(resumed->game));
    }
}

TEST_CASE("diagnostic checkpoint bootstrap preserves the warm nonclaim world" * doctest::test_suite("ClaimLateFork"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame source({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=3197});
    auto& game=source.game;auto& map=game.map;map.setMaterialDestinationsEnabled(true);
    game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
    auto* consumer=source.addBuilding("inn",20,20);REQUIRE(consumer);
    std::vector<Unit*> workers{source.addUnit(WORKER,13,8),source.addUnit(WORKER,8,8),source.addUnit(WORKER,9,8)};
    REQUIRE(map.incResourceByIndex(12,8,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(12,8),WHEAT,1);
    consumer->maxUnitWorking=3;consumer->setMaterial(WHEAT,0);consumer->updateCallLists();
    for(auto* unit:workers) {REQUIRE(unit);unit->hungriness=0;unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;}
    glob2test::publishCurrentHiringSuggestions(game);
    for(unsigned i=0;i<workers.size();++i)REQUIRE(consumer->subscribeToBringMaterialsStep());
    glob2test::discardPrematureHiringSuggestions(game);
    REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,workers[0]->movementAccess());}));
    for(auto* unit:workers) {
        REQUIRE(unit->attachedBuilding==consumer);REQUIRE(unit->activity==Unit::ACT_FILLING);
        unit->destinationPurpose=WHEAT;unit->targetX=12;unit->targetY=8;unit->setValidTarget(true);
        unit->displacement=Unit::DIS_GOING_TO_RESOURCE;
    }
    workers[0]->displacement=Unit::DIS_HARVESTING;workers[0]->movement=Unit::MOV_HARVESTING;
    workers[0]->dx=-1;workers[0]->dy=0;
    game.invalidateObservation();game.observeCompletedWorld();
    const auto sourceChecksum=game.checkSum();
    auto serial=copyRecoveryGame(source,1,"classic-checkpoint"),parallel=copyRecoveryGame(source,3);
    const auto first=forkClassicCheckpoint(serial->game,ResourceClaims::ClaimPolicyConfig{});
    const auto second=forkClassicCheckpoint(parallel->game,ResourceClaims::ClaimPolicyConfig{});
    CHECK(first==second);CHECK(first["harvesting_claims"]==1);CHECK(first["traveling_claims"]==0);
    CHECK(first["rejected_existing_accesses"]==2);CHECK(game.checkSum()==sourceChecksum);
    REQUIRE(serial->game.checkSum()==parallel->game.checkSum());
    auto resumed=copyRecoveryGame(*serial,3);
    for(unsigned tick=0;tick<128;++tick) {
        serial->step();parallel->step();resumed->step();
        REQUIRE(serial->game.checkSum()==parallel->game.checkSum());
        REQUIRE(serial->game.checkSum()==resumed->game.checkSum());
    }
    std::ofstream evidence(glob2test::artifactDir()/"bootstrap.json");evidence<<first.dump(2)<<'\n';REQUIRE(evidence.good());
}

TEST_CASE("external classic checkpoint creates an explicitly attributed claim experiment" *
    doctest::test_suite("ClaimLateFork") * doctest::skip(std::getenv("GLOB2_CLAIM_FORK_INPUT")==nullptr))
{
    const char* inputPath=std::getenv("GLOB2_CLAIM_FORK_INPUT");
    const char* configPath=std::getenv("GLOB2_CLAIM_FORK_CONFIG");
    REQUIRE(inputPath);REQUIRE(configPath);
    REQUIRE(std::filesystem::file_size(inputPath)<=MaximumForkBytes);
    REQUIRE(std::filesystem::file_size(configPath)<=4096);
    std::ifstream configuration(configPath);std::ostringstream text;text<<configuration.rdbuf();REQUIRE(configuration.good());
    const auto policy=ResourceClaims::ClaimPolicyConfig::parse(text.str());
    glob2test::HeadlessGlobals globals;glob2test::HeadlessGame fixture;
    unsigned participants=1;
    if(const char* value=std::getenv("GLOB2_CLAIM_FORK_PARTICIPANTS")) {
        REQUIRE((std::string(value)=="1" || std::string(value)=="2" || std::string(value)=="3"));
        participants=unsigned(value[0]-'0');
    }
    fixture.game.map.clear();fixture.game.map.resourceCells.clear();fixture.game.map.configureCompute(participants);
    GAGCore::BinaryInputStream input(globalContainer->fileManager->openInflatingInputStreamBackend(inputPath));
    REQUIRE(fixture.gui.load(&input,true));
    const auto hashFile=[](const char* path) {
        std::ifstream file(path,std::ios::binary);Online::Sha256 hash;std::array<char,65536> buffer;
        while(file) {file.read(buffer.data(),buffer.size());hash.update(buffer.data(),std::size_t(file.gcount()));}
        REQUIRE(file.eof());return Online::Sha256::toHex(hash.finish());
    };
    auto report=forkClassicCheckpoint(fixture.game,policy);
    report["source_sha256"]=hashFile(inputPath);report["source_configuration_sha256"]=hashFile(configPath);
    const auto forkChecksum=fixture.game.checkSum();
    const auto output=glob2test::artifactDir()/"fork.game.gz";
    REQUIRE_FALSE(std::filesystem::exists(output));
    REQUIRE(globalContainer->fileManager->writeGzipAtomically(output.string(),[&](GAGCore::OutputStream& stream){
        fixture.gui.save(&stream,"diagnostic claim checkpoint fork; see fork.json");}));
    REQUIRE(fixture.game.checkSum()==forkChecksum);report["fork_sim_checksum"]=forkChecksum;
    REQUIRE(std::filesystem::file_size(output)<=MaximumForkBytes);
    report["storage_contract_version"]=2;report["maximum_file_bytes"]=MaximumForkBytes;
    report["fork_sha256"]=hashFile(output.string().c_str());report["fork_bytes"]=std::filesystem::file_size(output);
    std::ofstream metadata(glob2test::artifactDir()/"fork.json");metadata<<report.dump(2)<<'\n';REQUIRE(metadata.good());
}

TEST_CASE("a terminal island path failure reaches the configured bounded release fallback" * doctest::test_suite("ClaimRecoveryAdversarial"))
{
    glob2test::HeadlessGlobals globals;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);
        Islands fixture(participants,false,ResourceClaims::ClaimPolicyConfig::Fallback::BoundedWaitRelease);
        auto& game=fixture.world.game;auto& map=game.map;
        bool released=false;
        for(unsigned decision=0;decision<32;++decision) {
            fixture.worker->handleActiveMaterialMovement();
            if(!fixture.worker->attachedBuilding) {released=true;break;}
            map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        }
        REQUIRE(released);
        CHECK_FALSE(fixture.worker->activeMaterialClaim);
        CHECK(map.materialClaimLedger().size()==1); // The independent holder remains reserved.
    }
}

TEST_CASE("recovery bounds island attempts and preserves pending GUI continuation" * doctest::test_suite("ClaimRecoveryAdversarial"))
{
    glob2test::HeadlessGlobals globals;
    for(bool closest:{false,true}) {
    CAPTURE(closest);std::vector<Uint32> reference;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);Islands fixture(participants,closest);
        auto& game=fixture.world.game;auto& map=game.map;const auto gid=fixture.worker->gid;
        std::unique_ptr<glob2test::HeadlessGame> resumed;
        bool reachedLater=false,savedPending=false;
        std::vector<Uint32> trace;
        const auto advance=[&](Game& current) {
            auto* unit=current.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(unit);
            unit->handleActiveMaterialMovement();current.map.flushClaimRoutes();
            ++current.stepCounter;current.observeCompletedWorld();
        };
        for(unsigned tick=0;tick<256;++tick) {
            CAPTURE(tick);
            if(!savedPending) {
                fixture.worker->handleActiveMaterialMovement();
                REQUIRE(fixture.worker->activeMaterialClaim);
                REQUIRE(map.materialClaimRoutes);
                REQUIRE(map.materialClaimRoutes->states.begin()->second.pending);
                map.flushClaimRoutes();
                resumed=copyRecoveryGame(fixture.world,participants,participants==1?
                    std::string("recovery-")+(closest?"closest":"first")+"-pending":"");savedPending=true;
                // This request publishes exactly one tick after its original admission.
                ++game.stepCounter;game.observeCompletedWorld();
                ++resumed->game.stepCounter;resumed->game.observeCompletedWorld();
            } else {
                advance(game);advance(resumed->game);
            }
            REQUIRE(game.checkSum()==resumed->game.checkSum());
            if(tick==10 || tick==80) {
                // Continue from actual failed-target history and from the later
                // recovery window too, not just an empty initial retry record.
                resumed=copyRecoveryGame(fixture.world,participants,participants==1?
                    std::string("recovery-")+(closest?"closest":"first")+(tick==10?"-cooldown":"-later"):"");
                REQUIRE(game.checkSum()==resumed->game.checkSum());
            }
            trace.push_back(game.checkSum());
            REQUIRE(map.materialClaimRoutes);
            // First fixed64tick admission window cannot be escaped by evicting
            // one of the six bad accesses from a four-entry rejection cache.
            if(tick<64)CHECK(map.materialClaimRoutes->pipeline.requestSequence()<=4);
            if(fixture.worker->activeMaterialClaim && fixture.worker->activeMaterialClaim->access==fixture.reachable) {
                int dx=0,dy=0;const auto step=map.nextClaimRouteStep(*fixture.worker,&dx,&dy);
                auto* peer=resumed->game.teams[0]->myUnits[Unit::GIDtoID(gid)];int peerX=0,peerY=0;
                CHECK(resumed->game.map.nextClaimRouteStep(*peer,&peerX,&peerY)==step);
                CHECK(peerX==dx);CHECK(peerY==dy);
                if(step==ResourceClaims::StepResult::Ready)reachedLater=true;
            }
        }
        CHECK(savedPending);CHECK(reachedLater);
        if(reference.empty())reference=trace;else CHECK(reference==trace);
    }
    }
}

TEST_CASE("source four resumes raw candidate after failed XY at same coordinate across saves" * doctest::test_suite("ActiveClaimRoutes"))
{
    glob2test::HeadlessGlobals globals;
    std::vector<Uint32> reference;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9377});
        auto& game=fixture.game;auto& map=game.map;map.configureCompute(participants);
        game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
        map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;policy.sourceVersion=4;policy.directResourceProbes=true;
        policy.maximumExpansions=1;map.setMaterialClaimPolicyConfig(policy);
        auto* consumer=fixture.addBuilding("inn",20,20);auto* unit=fixture.addUnit(WORKER,8,8);REQUIRE(consumer);REQUIRE(unit);
        consumer->maxUnitWorking=1;consumer->setMaterial(WHEAT,0);consumer->updateCallLists();
        unit->hungriness=0;unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;
        const auto old=Uint32(map.coordToIndex(12,8)),first=Uint32(map.coordToIndex(4,8)),raw=Uint32(map.coordToIndex(9,8));
        REQUIRE(map.incResourceByIndex(4,8,WHEAT,0));map.setMaterialAmountSlot(first,WHEAT,1);
        REQUIRE(map.incResourceByIndex(12,8,WHEAT,0));map.setMaterialAmountSlot(old,WHEAT,1);
        glob2test::publishCurrentHiringSuggestions(game);REQUIRE(consumer->subscribeToBringMaterialsStep());
        glob2test::discardPrematureHiringSuggestions(game);REQUIRE(unit->attachedBuilding==consumer);
        glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,unit->movementAccess());});
        game.invalidateObservation();game.observeCompletedWorld();unit->handleActiveMaterialMovement();
        REQUIRE(unit->activeMaterialClaim);CHECK(unit->activeMaterialClaim->access==first);
        const auto owner=ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity);
        REQUIRE(map.materialClaimRoutes);CHECK(map.materialClaimRoutes->recovery.at(owner).nextProbe==1);
        map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        unit->handleActiveMaterialMovement();REQUIRE_FALSE(unit->activeMaterialClaim);
        REQUIRE(map.materialClaimRoutes->recovery.at(owner).recovering);
        CHECK(map.materialClaimRoutes->recovery.at(owner).nextProbe==1);
        // Coordinate0 raw is empty; coordinate1 XY leads to the other goal.
        // Its real bounded route also fails while the worker remains stationary.
        unit->handleActiveMaterialMovement();REQUIRE(unit->activeMaterialClaim);
        CHECK(unit->activeMaterialClaim->access==old);
        CHECK(map.materialClaimRoutes->recovery.at(owner).nextProbe==3);
        map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        unit->handleActiveMaterialMovement();REQUIRE_FALSE(unit->activeMaterialClaim);
        CHECK(map.materialClaimRoutes->recovery.at(owner).nextProbe==3);
        // New adjacent stock appears after failure; XY still names the old goal.
        // Only raw phase1 of coordinate1 finds this legal, unoccupied tile.
        REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));map.setMaterialAmountSlot(raw,WHEAT,1);
        REQUIRE(map.materialDestinationValidSlot(0,WHEAT,unit->movementAccess(),9,8,false,nullptr));
        game.invalidateObservation();
        const auto gid=unit->gid;auto restored=copyRecoveryGame(fixture,participants);
        std::vector<Uint32> trace{game.checkSum()};
        for(auto* current:{&game,&restored->game}) {
            auto* worker=current->teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(worker);
            const auto* field=current->map.lookupMaterialGradientSlot(0,WHEAT,worker->movementAccess(),false,nullptr);REQUIRE(field);
            const auto labels=current->map.materialGradientDestinations(field);REQUIRE(labels);CHECK(labels->x[raw]==12);
            REQUIRE(worker->selectActiveMaterialClaim(field));REQUIRE(worker->activeMaterialClaim);
            CHECK(worker->activeMaterialClaim->source==raw);CHECK(worker->activeMaterialClaim->access==raw);
            CHECK(current->map.materialClaimRoutes->recovery.at(owner).nextProbe==4);
        }
        CHECK(game.checkSum()==restored->game.checkSum());trace.push_back(game.checkSum());
        if(participants==1)reference=trace;else CHECK(trace==reference);
    }
}
