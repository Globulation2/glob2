// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "GradientExecutionAudit.h"
#include "GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "DirectFlightDirection.h"
#include <array>
#include <vector>
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include <sstream>
#include <iomanip>

namespace {
// Advance isolated completed-world boundaries without unit decisions. Publication
// and capture see the same Game tick, so admission at c publishes at c+K.
void advanceFields(Game& game)
{
    ++game.stepCounter;
    game.map.advanceGradientPipeline();
    game.map.stagePeriodicGradientPreparation();
    glob2test::prepareGradientBoundary(game);
}

void fixedResearchIntervals(Map& map,unsigned interval=8)
{
    map.gradientRuntime->exploration=std::make_unique<ResourceGradientExploration>();
    map.gradientRuntime->exploration->intervals={interval};
}

std::vector<Uint32> normalizedChecksums(Game& game)
{
    std::vector<Uint32> state,buildings,units;
    game.checkSum(&state,&buildings,&units,true);
    state.erase(state.begin()); // The loader records the saved format.
    state.insert(state.end(),buildings.begin(),buildings.end());
    state.insert(state.end(),units.begin(),units.end());
    return state;
}

// Explicit byte order keeps these evidence hashes portable. Assertions below
// still compare the complete arrays/checksums rather than relying on a digest.
template<class Values> std::string evidenceHash(const Values& values)
{
    Uint64 hash=14695981039346656037ull;
    for (const auto value:values)
        for (unsigned shift=0;shift<32;shift+=8) {
            hash^=(Uint32(value)>>shift)&255;
            hash*=1099511628211ull;
        }
    std::ostringstream out;
    out<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    return out.str();
}
}

TEST_SUITE("DelayedGradientPublication")
{
TEST_CASE("private174 migration profile is explicit and does not replace published layouts")
{
    class HeaderVersionOutput final : public GAGCore::BinaryOutputStream {
    public:
        using BinaryOutputStream::BinaryOutputStream;
        void writeSint32(Sint32 value,std::string_view name) override {
            BinaryOutputStream::writeSint32(name=="versionMinor" ? 174 : value,name);
        }
    };
    MapHeader current;
    CHECK_THROWS_AS(current.applyLoadProfile(SaveLoadProfile::PrivateAdaptive174),std::runtime_error);
    current.applyLoadProfile(SaveLoadProfile::Published);
    auto* memory=new GAGCore::MemoryStreamBackend;HeaderVersionOutput out(memory);
    current.save(&out);out.flush();
    auto* copy=new GAGCore::MemoryStreamBackend(memory->getBuffer(),memory->getPosition());copy->seekFromStart(0);
    GAGCore::BinaryInputStream in(copy);MapHeader old;
    REQUIRE(old.load(&in));CHECK(old.loadingVersion()==174);CHECK_FALSE(old.privateAdaptive174);
    old.applyLoadProfile(SaveLoadProfile::PrivateAdaptive174);
    CHECK(old.getVersionMinor()==174);CHECK(old.loadingVersion()==173);CHECK(old.privateAdaptive174);
    old.applyLoadProfile(SaveLoadProfile::Published);
    CHECK(old.loadingVersion()==174);CHECK_FALSE(old.privateAdaptive174);
    for (unsigned version:{173u,174u,175u}) CHECK_FALSE(hasAdaptiveGradientState(version));
    CHECK(hasAdaptiveGradientState(FILE_FORMAT_VERSION_GRADIENT_DEMAND));
    CHECK(hasAdaptiveGradientState(173,true));
}
TEST_CASE("map conversion preserves encoded construction until ordinary gameplay initializes it")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame source({.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=source.game;
    const int siteType=game.buildingsTypes.getTypeNum("swarm",0,true);
    auto* site=game.addBuilding(20,20,siteType,0);REQUIRE(site);
    REQUIRE(site->type->isBuildingSite);
    for (int material=0;material<MaterialSlotCount;++material)
        site->setMaterial(material,site->constructionBudget[material]);
    site->fundConstructionFromInventory();
    REQUIRE(site->constructionReserved==site->constructionBudget);
    const auto gid=site->gid;
    auto* memory=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(memory);
    game.save(&out,true,"conversion-construction-map");out.flush();
    const auto bytes=memory->takeContents();
    GameGUI preserved(false),ordinary(false);
    const auto load=[&](Game& into,MapLoadInitialization initialization) {
        auto* data=new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size());data->seekFromStart(0);
        GAGCore::BinaryInputStream input(data);
        return into.load(&input,SaveLoadProfile::Published,initialization);
    };
    REQUIRE(load(preserved.game,MapLoadInitialization::PreserveEncoded));
    REQUIRE(load(ordinary.game,MapLoadInitialization::Normal));
    auto* unchanged=preserved.game.teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
    auto* initialized=ordinary.game.teams[Building::GIDtoTeam(gid)]->myBuildings[Building::GIDtoID(gid)];
    REQUIRE(unchanged);REQUIRE(initialized);
    CHECK(unchanged->typeNum==siteType);
    CHECK(unchanged->constructionReserved==site->constructionReserved);
    CHECK(initialized->typeNum!=siteType);
    CHECK(preserved.game.stepCounter==game.stepCounter);
}
TEST_CASE("all shared families initialize without stats and remain idle without replacement work")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;
    map.configureGradientPipeline(0,3);
    map.setResourceByIndex(20,20,WHEAT,1);
    std::vector<GradientFieldKey> keys;
    for(auto family:{GradientFieldFamily::Materials,GradientFieldFamily::Markets,GradientFieldFamily::Guard,GradientFieldFamily::Clear,GradientFieldFamily::Forbidden})
        for(int swim:{0,AIR_MOVEMENT_CLASS}) {
            const GradientFieldKey key{family,0,Uint8(swim),family==GradientFieldFamily::Materials||family==GradientFieldFamily::Markets?Uint8(WHEAT):GradientFieldKey::NoMaterial};
            keys.push_back(key);
            auto** slot=map.gradientFieldDestination(key);REQUIRE(slot);
            REQUIRE(map.requestGradientField(slot,int(family),0,WHEAT,swim));
            CHECK_FALSE(map.requestGradientField(slot,int(family),0,WHEAT,swim));
        }
    advanceFields(game);
    CHECK(map.gradientPipelineStatus().jobs==keys.size());
    CHECK(map.gradientPipelineStatus().pending==keys.size());
    // Requests repeated throughout the private-build delay retain one deadline.
    for(const auto key:keys)CHECK_FALSE(map.requestGradientField(map.gradientFieldDestination(key),int(key.family),key.team,WHEAT,key.swim));
    // Unchanged duplicate requests cannot manufacture follow-up maintenance.
    for(unsigned i=0;i<16;++i)advanceFields(game);
    for(const auto key:keys){REQUIRE(*map.gradientFieldDestination(key));CHECK_FALSE(map.gradientRuntime->adaptive.fields.at(key).reactive);}
    const auto jobs=map.gradientPipelineStatus().jobs;
    for(unsigned tick=0;tick<AdaptiveGradientState::QueryHistoryTicks+32;++tick) {
        advanceFields(game);map.syncStep(game.stepCounter);
        CHECK(map.gradientPipelineStatus().jobs==jobs);
    }
    CHECK(map.gradientPipelineStatus().pending==0);
}

TEST_CASE("simultaneous demanded families refresh together and skipped families stay idle with explicit fixed research intervals")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);fixedResearchIntervals(map);
    map.setResourceByIndex(20,20,WHEAT,1);
    std::vector<GradientFieldKey> keys;
    for(auto family:{GradientFieldFamily::Materials,GradientFieldFamily::Guard,GradientFieldFamily::Clear,GradientFieldFamily::Forbidden}) {
        GradientFieldKey key{family,0,0,family==GradientFieldFamily::Materials?Uint8(WHEAT):GradientFieldKey::NoMaterial};keys.push_back(key);
        REQUIRE(map.requestGradientField(map.gradientFieldDestination(key),int(family),0,WHEAT,0));
    }
    for(unsigned i=0;i<5;++i)advanceFields(game);
    game.stepCounter+=AdaptiveGradientState::QueryHistoryTicks;
    const auto jobs=map.gradientPipelineStatus().jobs;
    for(unsigned i=0;i<3;++i)map.gradientMovementDemand(keys[i]);
    advanceFields(game);
    CHECK(map.gradientPipelineStatus().jobs==jobs+3);
    CHECK_FALSE(map.gradientRuntime->adaptive.fields.at(keys.back()).inflight);
    for(unsigned i=0;i<3;++i)CHECK(map.gradientRuntime->adaptive.fields.at(keys[i]).inflight);
}

TEST_CASE("optional total-work observation preserves state and records actual family admissions [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame ordinary({.discovered=true,.clearImmobile=true,.header=true});
    glob2test::HeadlessGame observed({.discovered=true,.clearImmobile=true,.header=true});
    const auto path=(glob2test::artifactDir()/"all-family-work.csv").string();
    observed.game.map.configureGradientWorkStats(path);
    for(auto* world:{&ordinary,&observed}) {
        auto& map=world->game.map;map.configureGradientPipeline(0,3);
        map.setResourceByIndex(20,20,WHEAT,1);
        for(auto family:{GradientFieldFamily::Materials,GradientFieldFamily::Markets,GradientFieldFamily::Guard,GradientFieldFamily::Clear,GradientFieldFamily::Forbidden}) {
            const GradientFieldKey key{family,0,0,family==GradientFieldFamily::Materials||family==GradientFieldFamily::Markets?Uint8(WHEAT):GradientFieldKey::NoMaterial};
            REQUIRE(map.requestGradientField(map.gradientFieldDestination(key),int(family),0,WHEAT,0));
            CHECK_FALSE(map.requestGradientField(map.gradientFieldDestination(key),int(family),0,WHEAT,0));
        }
    }
    REQUIRE_FALSE(ordinary.game.map.gradientRuntime->workStats);
    for(unsigned tick=0;tick<16;++tick) {
        CHECK(normalizedChecksums(ordinary.game)==normalizedChecksums(observed.game));
        advanceFields(ordinary.game);advanceFields(observed.game);
    }
    const auto& stats=*observed.game.map.gradientRuntime->workStats;
    for(unsigned family=0;family<5;++family) {
        const auto& cold=stats.totals[family][unsigned(AdaptiveGradientState::Reason::Cold)];
        CHECK(cold.requests==2);CHECK(cold.duplicates==1);CHECK(cold.admissions==1);CHECK(cold.publications==1);
        CHECK(cold.fieldCells==observed.game.map.cellCount());
    }
    observed.game.map.gradientRuntime->workStats->flush();
}

TEST_CASE("successful planning reads leave warm natural fields idle beyond query history")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;
    map.configureGradientPipeline(0,3);
    auto* worker=world.addUnit(WORKER,8,8);REQUIRE(worker);
    auto* inn=world.addBuilding("inn",10,8);REQUIRE(inn);
    map.setResourceByIndex(20,20,WHEAT,1);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick) advanceFields(game);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Reachable);
    // Building routes now initialize lazily too. Warm that independent route
    // before asserting that repeated successful recruitment reads stay idle.
    int buildingDistance=0;
    auto buildingRoute=map.queryBuildingDistance(inn,worker->movementClass(),8,8,&buildingDistance,BuildingRoute::Footprint);
    for (unsigned wait=0;buildingRoute==RouteResult::Pending && wait<unsigned(game.gameHeader.getBuildingGradientDelay())+4;++wait) {
        advanceFields(game);
        buildingRoute=map.queryBuildingDistance(inn,worker->movementClass(),8,8,&buildingDistance,BuildingRoute::Footprint);
    }
    REQUIRE(buildingRoute==RouteResult::Reachable);
    const auto& field=map.gradientRuntime->adaptive.fields.at(GradientFieldKey{GradientFieldFamily::Materials,0,0,Uint8(WHEAT)});
    const auto submitted=field.submitted, serving=field.serving.tick;
    const auto jobs=map.gradientPipelineStatus().jobs;
    const auto* published=map.lookupMaterialGradientSlot(0,WHEAT,0);
    REQUIRE(published);
    // Recruiters repeatedly inspect this route, but no unit actually moves.
    // Successful planning past128 ticks remains observation rather than demand;
    // no field refreshes merely because its serving capture gets old.
    for(unsigned tick=0;tick<AdaptiveGradientState::QueryHistoryTicks+16;++tick) {
        int distance=-1;Sint32 targetX=-1,targetY=-1;
        CHECK(map.queryMaterialDistance(0,WHEAT,0,8,8,&distance)==RouteResult::Reachable);
        CHECK(map.materialAvailableSlot(0,WHEAT,0,8,8));
        CHECK(map.materialAvailableSlot(0,WHEAT,0,8,8,&distance));
        CHECK(map.materialAvailableUpdateSlot(0,WHEAT,0,8,8,&targetX,&targetY,&distance));
        CHECK(inn->considerUnitForMaterial(worker,WHEAT,&distance));
        CHECK_FALSE(field.demand);
        CHECK(field.count32==0);CHECK(field.count128==0);
        advanceFields(game);
        CHECK(map.gradientPipelineStatus().jobs==jobs);
    }
    CHECK(Uint32(game.stepCounter-field.submitted)>AdaptiveGradientState::QueryHistoryTicks);
    CHECK(field.submitted==submitted);CHECK(field.serving.tick==serving);
    CHECK(map.lookupMaterialGradientSlot(0,WHEAT,0)==published);
    CHECK(map.gradientRuntime->fieldRequests.empty());
    CHECK(map.gradientPipelineStatus().pending==0);
}

TEST_CASE("stale forbidden natural reads demand recovery without material escape evidence [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);fixedResearchIntervals(map,8);
    map.setResourceByIndex(20,20,WHEAT,1);map.markImmobileUnit(8,8,0);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    REQUIRE(map.materialGradients[0][WHEAT][0][map.coordToIndex(8,8)]==GRADIENT_FORBIDDEN);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};auto& field=map.gradientRuntime->adaptive.fields.at(key);
    map.clearImmobileUnit(8,8);
    const auto path=(glob2test::artifactDir()/"stale-zero-demand-probe.csv").string();
    map.gradientRuntime->qualityProbe=std::make_unique<ResourceGradientProbe>(path.c_str());auto& probe=*map.gradientRuntime->qualityProbe;
    EntityRandom random;random.seed(42,54);int dx=0,dy=0;bool stop=false;
    const auto before=field.total;
    map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false);
    CHECK(field.demand);CHECK(field.count128==1);CHECK(field.total==before);
    CHECK_FALSE(probe.movementHistory.contains(int(key.packed())));
    while(game.stepCounter<8) {advanceFields(game);CHECK(field.submitted==1);}
    // Repeated movement reads are demand, while escape remains its own evidence.
    map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false);
    advanceFields(game);CHECK(field.submitted==9);
    for(unsigned tick=0;tick<3;++tick)advanceFields(game);
    CHECK(map.materialGradients[0][WHEAT][0][map.coordToIndex(8,8)]>GRADIENT_UNREACHABLE);
}

TEST_CASE("episode movement reachability alternatives are separate from planning [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);map.setResourceByIndex(20,20,WHEAT,1);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};const int packed=int(key.packed());
    const auto* live=map.materialGradients[0][WHEAT][0];
    const auto path=(glob2test::artifactDir()/"movement-reachability-probe.csv").string();ResourceGradientProbe probe(path.c_str());probe.window=0;
    ResourceGradientProbe::Episode episode;episode.tick=16;episode.delay=8;
    episode.deferred.assign(map.size,GRADIENT_UNREACHABLE);episode.refreshed.assign(live,live+map.size);
    probe.fields[packed].episodes.push_back(std::move(episode));
    EntityRandom random;random.seed(42,54);const GradientQueryContext context{key,8,8,random};
    game.stepCounter=23;probe.observe(map,context,live);CHECK(probe.fields.at(packed).episodes.front().queries==0);
    game.stepCounter=24;probe.observe(map,context,live);
    map.replaceResource(20,20,Resource{});game.stepCounter=25;probe.observe(map,context,live);
    const auto& e=probe.fields.at(packed).episodes.front();CHECK(e.queries==2);CHECK(e.planningQueries==0);
    CHECK(e.deferFalseUnreachable==1);CHECK(e.refreshFalseUnreachable==0);CHECK(e.deferFalseReachable==0);CHECK(e.refreshFalseReachable==1);
    CHECK(e.deferFalseNegative==0);CHECK(e.refreshFalsePositive==0);
}

TEST_CASE("direct warm recovery retains demand but cannot bypass minimum8")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);
    map.setResourceByIndex(20,20,WHEAT,1);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};
    auto& field=map.gradientRuntime->adaptive.fields.at(key);REQUIRE(field.submitted==1);
    map.updateMaterialGradient(0,WHEAT,0,false);
    CHECK(field.reactive);CHECK(field.demand);CHECK_FALSE(field.queued);CHECK(map.gradientPipelineStatus().pending==0);
    while(game.stepCounter<8) {advanceFields(game);CHECK(map.gradientPipelineStatus().pending==0);}
    advanceFields(game);CHECK(game.stepCounter==9);CHECK(field.submitted==9);CHECK(map.gradientPipelineStatus().pending==1);
}

TEST_CASE("spatial overlap uses actual publication generations and bounded aligned queries [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);
    map.setResourceByIndex(20,20,WHEAT,1);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};const int packed=int(key.packed());
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    const auto path=(glob2test::artifactDir()/"spatial-generation-probe.csv").string();
    map.gradientRuntime->qualityProbe=std::make_unique<ResourceGradientProbe>(path.c_str());auto& probe=*map.gradientRuntime->qualityProbe;
    auto* live=map.materialGradients[0][WHEAT][0];REQUIRE(live);
    probe.observeOverlap(map,key,20,20,live);CHECK(probe.movementHistory.empty()); // late attach is unknown
    game.stepCounter=16;map.updateMaterialGradient(0,WHEAT,0,false);
    CHECK(probe.movementHistory.empty());advanceFields(game); // capture, not publication
    CHECK(probe.movementHistory.empty());
    for(unsigned tick=0;tick<3;++tick)advanceFields(game);
    auto& history=probe.movementHistory.at(packed).overlap;
    REQUIRE(history.known);CHECK(history.generation==1);CHECK(history.publicationTick==20);CHECK(history.queries==0);
    live=map.materialGradients[0][WHEAT][0];
    probe.observeOverlap(map,key,20,20,live);CHECK(history.queries==1);CHECK(history.goalQueries==0);
    map.replaceResource(20,20,Resource{});
    probe.observeOverlap(map,key,8,8,live);CHECK(history.goalQueries==0); // far edit is outside the patch
    const auto before=normalizedChecksums(game);
    probe.observeOverlap(map,key,23,23,live);CHECK(normalizedChecksums(game)==before);
    CHECK(history.goalQueries==1);CHECK(history.goalRemovedQueries==1);CHECK(history.passabilityQueries==0);
    map.setResourceByIndex(20,20,WHEAT,1);
    probe.observeOverlap(map,key,20,20,live);CHECK(history.goalQueries==1); // cancellation matches baseline
    map.addForbidden(21,21,0);probe.observeOverlap(map,key,20,20,live);
    CHECK(history.passabilityQueries==1);CHECK(history.blockedQueries==1);
    const auto queries=history.queries;map.updateMaterialGradient(0,WHEAT,0,false);
    CHECK(history.queries==queries); // a request must not reset the generation
    for(unsigned tick=0;tick<8 && history.generation==1;++tick)advanceFields(game);
    CHECK(history.generation==2);CHECK(history.queries==0);
    // Registry repair is not publication; it cannot make late/old history fresh.
    map.rebuildPlaneRegistry();CHECK(history.generation==2);
    map.setGradientSpatialOverlap(false);CHECK_FALSE(history.known);
    probe.observeOverlap(map,key,20,20,map.materialGradients[0][WHEAT][0]);CHECK(history.queries==0);
    map.setGradientSpatialOverlap(true);CHECK_FALSE(history.known);
    for(auto family:{GradientFieldFamily::Markets,GradientFieldFamily::Guard,GradientFieldFamily::Forbidden})
        CHECK_FALSE(gradient_seed_overlap::supported({family,0,0,WHEAT}));
    CHECK_FALSE(gradient_seed_overlap::supported({GradientFieldFamily::Materials,0,AIR_MOVEMENT_CLASS,WHEAT}));
}

TEST_CASE("spatial masks separate goal changes from passability and handle tiny patches")
{
    using namespace gradient_seed_overlap;
    const Patch old{1u,6u},now{2u,9u};const auto d=difference(old,now);
    CHECK(d.goals==3);CHECK(d.goalAdded==2);CHECK(d.goalRemoved==1);
    CHECK(d.passability==12);CHECK(d.blocked==4);CHECK(d.opened==8);
    unsigned reads=0;const auto patch=read(1,1,2,2,[&](size_t i){++reads;return Uint16(i==3?65535:1);});
    CHECK(reads==4);CHECK(patch.goals==(1u<<5));CHECK(patch.open==19);
}

TEST_CASE("capture anchored H128 outcome windows include offsets8 through127 only [artifacts]")
{
    CHECK(ResourceGradientProbe::HorizonTicks==128);
    for(unsigned age:{0u,7u,128u,129u})CHECK_FALSE(ResourceGradientProbe::labelEligible(age,8));
    for(unsigned age:{8u,31u,32u,126u,127u})CHECK(ResourceGradientProbe::labelEligible(age,8));
    CHECK_FALSE(ResourceGradientProbe::complete(127));CHECK(ResourceGradientProbe::complete(128));
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.setResourceByIndex(20,20,WHEAT,1);map.configureGradientPipeline(0,3);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};const int packed=int(key.packed());
    const auto path=(glob2test::artifactDir()/"h128-boundary-probe.csv").string();ResourceGradientProbe probe(path.c_str());
    auto* live=map.materialGradients[0][WHEAT][0];
    ResourceGradientProbe::Episode episode;episode.tick=272;episode.delay=8;
    episode.deferred.assign(live,live+map.size);episode.refreshed=episode.deferred;
    probe.fields[packed].episodes.push_back(episode);probe.fields[packed].firstTick=272;
    probe.window=0;
    for(unsigned age:{7u,8u,31u,32u,127u,128u}) {game.stepCounter=272+age;probe.observePlanning(map,key,8,8,live);}
    REQUIRE(probe.fields.at(packed).episodes.size()==1);
    CHECK(probe.fields.at(packed).episodes.front().planningQueries==4);
    game.stepCounter=399;probe.stageEpisodes(map);CHECK(probe.fields.at(packed).episodes.size()==1);
    game.stepCounter=400;probe.stageEpisodes(map);CHECK(probe.fields.at(packed).episodes.empty());
}

TEST_CASE("generic planning oracle preserves movement state and follows fresh route changes [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);
    REQUIRE(world.addUnit(WORKER,8,8));map.setResourceByIndex(20,20,WHEAT,1);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    const auto* live=map.lookupMaterialGradientSlot(0,WHEAT,0);REQUIRE(live);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};const int packed=int(key.packed());
    const auto path=(glob2test::artifactDir()/"generic-planning-probe.csv").string();
    map.gradientRuntime->qualityProbe=std::make_unique<ResourceGradientProbe>(path.c_str());auto& probe=*map.gradientRuntime->qualityProbe;
    game.stepCounter=16;probe.observePlanning(map,key,8,8,live);
    game.stepCounter=271;probe.observePlanning(map,key,8,8,live);
    for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)if(x!=20||y!=20)map.paintCell(x,y,WATER);
    game.stepCounter=272;const auto before=normalizedChecksums(game);probe.stageEpisodes(map);CHECK(normalizedChecksums(game)==before);
    REQUIRE(probe.fields.at(packed).episodes.size()==1);auto& e=probe.fields.at(packed).episodes.front();
    game.stepCounter+=3;const auto blocked=normalizedChecksums(game);probe.observePlanning(map,key,8,8,live);CHECK(normalizedChecksums(game)==blocked);
    CHECK(e.queries==0);CHECK_FALSE(e.movementObserved);CHECK(e.planningQueries==1);CHECK(e.deferFalsePositive==1);CHECK(e.refreshFalsePositive==0);
    for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)if(x!=20||y!=20)map.paintCell(x,y,GRASS);
    ++game.stepCounter;const auto opened=normalizedChecksums(game);probe.observePlanning(map,key,8,8,live);CHECK(normalizedChecksums(game)==opened);
    CHECK(e.planningQueries==2);CHECK(e.refreshFalseNegative==1);CHECK(map.gradientRuntime->adaptive.fields.at(key).count128==0);
    CHECK(probe.query==0);CHECK(probe.planningQuery==4);
}

TEST_CASE("generic capture metadata publishes its original movement baseline and freezes episode inputs [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);
    REQUIRE(world.addUnit(WORKER,8,8));map.setResourceByIndex(20,20,WHEAT,1);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};const int packed=int(key.packed());
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    auto& field=map.gradientRuntime->adaptive.enroll(key);GradientMovementEvidence invalid;invalid.emptyGoal=true;field.recordMovement(invalid);
    advanceFields(game);CHECK_FALSE(field.serving.known);
    CHECK_FALSE(map.requestGradientField(&map.materialGradients[0][WHEAT][0],int(gradient_preparation::Kind::Materials),0,WHEAT,0));
    CHECK(map.gradientPipelineStatus().pending==1);
    GradientMovementEvidence stalled;stalled.reachableNoMove=true;field.recordMovement(stalled);
    advanceFields(game);advanceFields(game);CHECK_FALSE(field.serving.known);advanceFields(game);
    REQUIRE(field.serving.known);CHECK(field.serving.tick==1);
    CHECK((field.serving.baseline==AdaptiveGradientFeatures::Counts{1,0,1}));
    CHECK((field.contradictions()==AdaptiveGradientFeatures::Counts{0,1,1}));
    const auto path=(glob2test::artifactDir()/"generic-capture-probe.csv").string();map.gradientRuntime->qualityProbe=std::make_unique<ResourceGradientProbe>(path.c_str());auto& probe=*map.gradientRuntime->qualityProbe;
    const auto* live=map.lookupMaterialGradientSlot(0,WHEAT,0);REQUIRE(live);EntityRandom random;random.seed(123,456);
    const GradientQueryContext context{key,8,8,random};game.stepCounter=16;probe.observe(map,context,live);game.stepCounter=271;probe.observe(map,context,live);game.stepCounter=272;probe.stageEpisodes(map);
    REQUIRE(probe.fields.at(packed).episodes.size()==1);const auto& episode=probe.fields.at(packed).episodes.front();
    CHECK(episode.contradictionCaptureKnown);CHECK(episode.contradictions.values[0]==0);CHECK(episode.contradictions.values[4]==1);CHECK(episode.contradictions.values[5]==1);
    const auto frozen=episode.contradictions.values;field.recordMovement(invalid);probe.recordMovement(key,invalid);CHECK(episode.contradictions.values==frozen);
}

TEST_CASE("generic movement evidence excludes delegated escape from material labels and leaves RNG unchanged [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);
    REQUIRE(world.addUnit(WORKER,8,8));map.setResourceByIndex(20,20,WHEAT,1);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    auto* cells=map.materialGradients[0][WHEAT][0];REQUIRE(cells);
    const auto path=(glob2test::artifactDir()/"generic-movement-probe.csv").string();map.gradientRuntime->qualityProbe=std::make_unique<ResourceGradientProbe>(path.c_str());auto& probe=*map.gradientRuntime->qualityProbe;
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,WHEAT};EntityRandom random,reference;random.seed(123,456);reference=random;int dx=0,dy=0;bool stop=false;
    cells[map.coordToIndex(8,8)]=GRADIENT_AT_GOAL;CHECK_FALSE(map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false));CHECK(random==reference);
    cells[map.coordToIndex(8,8)]=100;for(int y=7;y<=9;++y)for(int x=7;x<=9;++x)if(x!=8||y!=8)cells[map.coordToIndex(x,y)]=GRADIENT_UNREACHABLE;
    CHECK_FALSE(map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false));cells[map.coordToIndex(8,8)]=GRADIENT_UNREACHABLE;
    CHECK_FALSE(map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false));cells[map.coordToIndex(8,8)]=GRADIENT_FORBIDDEN;
    map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false);CHECK(random==reference);
    CHECK((probe.movementHistory.at(int(key.packed())).total.values==std::array<std::uint64_t,6>{1,0,0,0,1,3}));
    CHECK((map.gradientRuntime->adaptive.fields.at(key).total==AdaptiveGradientFeatures::Counts{1,1,3}));
}

TEST_CASE("all five generic family oracles are private and use stable identities [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.setResourceByIndex(20,20,WHEAT,1);map.addGuardArea(22,22,0);map.addClearArea(20,20,0);map.addForbidden(8,8,0);
    game.gameHeader.getExperiments().set(ExperimentId::GuardAreaBalancing,true);REQUIRE(world.addUnit(WARRIOR,22,22));
    const auto path=(glob2test::artifactDir()/"generic-families-probe.csv").string();ResourceGradientProbe probe(path.c_str());probe.window=game.stepCounter/4096; // Keep manually installed test episodes in the active window.
    for(unsigned family=0;family<5;++family)for(unsigned movement:{0u,unsigned(AIR_MOVEMENT_CLASS)}) {
        const GradientFieldKey key{GradientFieldFamily(family),0,Uint8(movement),family<2?Uint8(WHEAT):GradientFieldKey::NoMaterial};CAPTURE(family);CAPTURE(movement);
        std::vector<Uint16> fresh;const auto before=normalizedChecksums(game);probe.rebuildFresh(map,key,fresh);CHECK(normalizedChecksums(game)==before);REQUIRE(fresh.size()==map.size);
        EntityRandom random;random.seed(123,456);const GradientQueryContext context{key,family==4?8:10,family==4?8:10,random};const auto a=map.selectGradientQuery(context,fresh.data()),b=map.selectGradientQuery(context,fresh.data());CHECK(a.moved==b.moved);CHECK(a.dx==b.dx);CHECK(a.dy==b.dy);CHECK(context.random==random);CHECK(normalizedChecksums(game)==before);
        auto& sampled=probe.fields[int(key.packed())];ResourceGradientProbe::Episode episode;episode.tick=game.stepCounter;episode.delay=0;episode.deferred=fresh;episode.refreshed=fresh;sampled.episodes.push_back(std::move(episode));
        probe.observe(map,context,fresh.data());CHECK(sampled.episodes.back().deferExtra==0);CHECK(sampled.episodes.back().refreshExtra==0);CHECK(normalizedChecksums(game)==before);
    }
}

TEST_CASE("valid guard clearing and escaped forbidden arrivals never count as movement stalls")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);
    map.addGuardArea(10,10,0);
    map.setResourceByIndex(20,20,WOOD,1);map.addClearArea(20,20,0);
    map.getGuardAreasGradient(0,0);map.getClearAreasGradient(0,0);map.getForbiddenGradient(0,0);
    for(unsigned tick=0;tick<5;++tick)advanceFields(game);
    EntityRandom random;random.seed(123,456);const auto before=random;
    int dx=0,dy=0;
    REQUIRE(map.gradientGoalValid({GradientFieldFamily::Guard,0,0},10,10));
    REQUIRE(map.gradientGoalValid({GradientFieldFamily::Clear,0,0},20,20));
    CHECK(map.queryAreaStep(random,Map::AreaKind::Guard,0,0,10,10,&dx,&dy)==StepResult::AtGoal);
    CHECK(map.queryAreaStep(random,Map::AreaKind::Clear,0,0,20,20,&dx,&dy)==StepResult::AtGoal);
    map.queryForbiddenStep(nullptr,0,0,8,8,&dx,&dy);
    CHECK(random==before);
    for(auto family:{GradientFieldFamily::Guard,GradientFieldFamily::Clear,GradientFieldFamily::Forbidden}) {
        const auto& counts=map.gradientRuntime->adaptive.fields.at({family,0,0}).total;
        CHECK(counts[0]==0);CHECK(counts[1]==0);CHECK(counts[2]==1);
    }
}

TEST_CASE("direct flight alternatives use the live angular bearing wrap ties and traffic avoidance")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    auto* flyer=world.addUnit(EXPLORER,8,8);REQUIRE(flyer);REQUIRE(flyer->performance[FLY]>0);
    const GradientFieldKey key{GradientFieldFamily::Guard,0,AIR_MOVEMENT_CLASS};
    for(const auto target:std::array<std::pair<int,int>,3>{{{15,9},{8+map.getW()/2,8},{9,15}}}) {
        map.addGuardArea(target.first,target.second,0);
        std::vector<Uint16> cells(map.cellCount(),GRADIENT_UNREACHABLE);
        cells[map.coordToIndex(target.first,target.second)]=GRADIENT_AT_GOAL;
        map.propagateGradient(cells.data(),AIR_MOVEMENT_CLASS);
        flyer->targetX=target.first;flyer->targetY=target.second;
        const auto before=flyer->entityRandom;
        const GradientQueryContext context{key,8,8,before,nullptr,GradientQueryContext::Mode::DirectFlight};
        auto selected=map.selectGradientQuery(context,cells.data());
        flyer->flyToTarget();
        CHECK(selected.dx==flyer->dx);CHECK(selected.dy==flyer->dy);CHECK(flyer->entityRandom==before);
    }
    REQUIRE(world.addUnit(EXPLORER,9,8));
    const auto blocked=direct_flight::select(8,8,15,9,map.getW(),map.getH(),
        [&](int x,int y){return map.isFreeForMovement(x,y,AIR_MOVEMENT_CLASS,world.team->me);});
    flyer->targetX=15;flyer->targetY=9;flyer->flyToTarget();
    CHECK(blocked.dx==flyer->dx);CHECK(blocked.dy==flyer->dy);
    CHECK(blocked.dx==1);CHECK(blocked.dy==1);
}

TEST_CASE("unreachable shared areas recover from actual target edits without periodic maintenance with explicit fixed research intervals")
{
    glob2test::HeadlessGlobals globals;
    for(auto family:{GradientFieldFamily::Guard,GradientFieldFamily::Clear,GradientFieldFamily::Forbidden}) {
        CAPTURE(unsigned(family));
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
        auto& game=world.game;auto& map=game.map;map.configureGradientPipeline(0,3);fixedResearchIntervals(map);
        const GradientFieldKey key{family,0,0};
        if(family==GradientFieldFamily::Forbidden)
            for(int y=0;y<map.getH();++y)for(int x=0;x<map.getW();++x)map.addForbidden(x,y,0);
        REQUIRE(map.requestGradientField(map.gradientFieldDestination(key),int(family),0,0,0));
        for(unsigned tick=0;tick<5;++tick)advanceFields(game);
        const auto* serving=*map.gradientFieldDestination(key);REQUIRE(serving);
        REQUIRE(serving[map.coordToIndex(8,8)]<=GRADIENT_UNREACHABLE);
        const auto jobs=map.gradientPipelineStatus().jobs;
        if(family==GradientFieldFamily::Guard)map.addGuardArea(10,10,0);
        if(family==GradientFieldFamily::Clear){map.setResourceByIndex(10,10,WOOD,1);map.addClearArea(10,10,0);}
        if(family==GradientFieldFamily::Forbidden)map.removeForbidden(10,10,0);
        EntityRandom random;random.seed(123,456);bool recovered=false;
        for(unsigned tick=0;tick<AdaptiveGradientState::QueryHistoryTicks+8 && !recovered;++tick) {
            int dx=0,dy=0;
            recovered=family==GradientFieldFamily::Forbidden
                ?map.pathfindForbidden(nullptr,0,0,8,8,&dx,&dy)
                :map.pathfindArea(random,family==GradientFieldFamily::Guard?Map::AreaKind::Guard:Map::AreaKind::Clear,0,0,8,8,&dx,&dy);
            advanceFields(game);
        }
        REQUIRE(recovered);
        CHECK(map.gradientPipelineStatus().jobs==jobs+1);
        CHECK(map.gradientRuntime->adaptive.fields.at(key).total[2]>0);
    }
}

TEST_CASE("production movement evidence needs no probe and excludes cold and absent observations but enrolls air")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;
    map.configureGradientPipeline(0,3);
    map.setResourceByIndex(20,20,WHEAT,1);
    const GradientFieldKey key{GradientFieldFamily::Materials,0,0,Uint8(WHEAT)};
    EntityRandom random,reference;random.seed(123,456);reference=random;
    int dx=0,dy=0;bool stop=false;
    REQUIRE_FALSE(map.gradientRuntime->qualityProbe);
    // Cold enrollment creates scheduling state, but it is not a movement
    // observation until a published natural field can answer the query.
    CHECK_FALSE(map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false));
    for (const auto& [unused,field]:map.gradientRuntime->adaptive.fields)
        CHECK(field.total==AdaptiveGradientState::Counts{});
    for(unsigned tick=0;tick<4;++tick) advanceFields(game);
    auto* cells=map.materialGradients[0][WHEAT][0];REQUIRE(cells);
    cells[map.coordToIndex(8,8)]=GRADIENT_AT_GOAL; // Cached goal has no stock.
    CHECK_FALSE(map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false));
    cells[map.coordToIndex(8,8)]=GRADIENT_UNREACHABLE;
    CHECK_FALSE(map.pathfindMaterial(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr,false));
    CHECK(random==reference);
    const auto before=map.gradientRuntime->adaptive.fields.at(key).total;
    CHECK((before==AdaptiveGradientState::Counts{1,0,2}));
    const auto fields=map.gradientRuntime->adaptive.fields.size();
    REQUIRE_FALSE(map.hasMaterialSourceSlot(WOOD));
    CHECK_FALSE(map.pathfindMaterial(random,0,WOOD,0,8,8,&dx,&dy,&stop,nullptr,false));
    map.pathfindMaterial(random,0,WHEAT,AIR_MOVEMENT_CLASS,8,8,&dx,&dy,&stop,nullptr,false);
    CHECK(map.gradientRuntime->adaptive.fields.size()==fields+1);
    CHECK(map.gradientRuntime->adaptive.fields.at(key).total==before);
    for(unsigned tick=0;tick<4;++tick)advanceFields(game);
    map.pathfindMaterial(random,0,WHEAT,AIR_MOVEMENT_CLASS,8,8,&dx,&dy,&stop,nullptr,false);
    CHECK(map.gradientRuntime->adaptive.fields.at({GradientFieldFamily::Materials,0,AIR_MOVEMENT_CLASS,Uint8(WHEAT)}).total[2]==1);
}

TEST_CASE("recruitment-only demand recovers a published unreachable natural field after terrain opens with explicit fixed research intervals")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;
    map.configureGradientPipeline(0,3);fixedResearchIntervals(map);
    auto* worker=world.addUnit(WORKER,8,8);REQUIRE(worker);
    auto* inn=world.addBuilding("inn",10,8);REQUIRE(inn);
    map.setResourceByIndex(20,20,WHEAT,1);
    for(int y=19;y<=21;++y) for(int x=19;x<=21;++x)
        if(x!=20 || y!=20) map.paintCell(x,y,WATER);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick) advanceFields(game);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Unreachable);
    REQUIRE(map.hasMaterialSourceSlot(WHEAT));
    const auto jobs=map.gradientPipelineStatus().jobs;
    const auto delay=map.gradientPipelineStatus().delay;
    const auto& field=map.gradientRuntime->adaptive.fields.at(GradientFieldKey{GradientFieldFamily::Materials,0,0,Uint8(WHEAT)});
    CHECK(field.demand);
    CHECK(field.count32==0);CHECK(field.count128==0);
    int distance=0;
    CHECK_FALSE(inn->considerUnitForMaterial(worker,WHEAT,&distance));
    for(int y=19;y<=21;++y) for(int x=19;x<=21;++x) map.paintCell(x,y,GRASS);
    bool recruited=false;
    unsigned admittedAt=0;
    for(unsigned tick=0;tick<AdaptiveGradientState::QueryHistoryTicks+delay+4 && !recruited;++tick) {
        // No movement query is made: recruitment itself must request upkeep.
        recruited=inn->considerUnitForMaterial(worker,WHEAT,&distance);
        advanceFields(game);
        if(map.gradientPipelineStatus().jobs>jobs) {
            if(!admittedAt) admittedAt=game.stepCounter;
            CHECK(map.gradientPipelineStatus().jobs==jobs+1);
            // A failed planning read enrolls normal delayed maintenance: it
            // must not make freshly computed guidance visible before c+K.
            if(Uint32(game.stepCounter-admittedAt)<delay)
                CHECK(map.lookupMaterialGradientSlot(0,WHEAT,0)[map.coordToIndex(8,8)]<=GRADIENT_UNREACHABLE);
        }
    }
    REQUIRE(recruited);
    REQUIRE(admittedAt!=0);
    CHECK(map.gradientPipelineStatus().jobs==jobs+1);
    CHECK(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Reachable);
    CHECK(field.count32==0);CHECK(field.count128==0);
}

TEST_CASE("pending shared air fields continue through binary and text saves")
{
    glob2test::HeadlessGlobals globals;
    for(bool text:{false,true}) {
        CAPTURE(text);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto& game=world.game;auto& map=game.map;
        map.configureGradientPipeline(0,3);
        map.paintCell(30,30,VOID_HOLE);
        map.setResourceByIndex(20,20,WHEAT,1);map.setResourceByIndex(21,21,WOOD,1);
        REQUIRE_FALSE(map.getMaterialGradientSlot(0,WHEAT,AIR_MOVEMENT_CLASS));
        REQUIRE_FALSE(map.getMaterialGradientSlot(0,WOOD,AIR_MOVEMENT_CLASS));
        advanceFields(game);
        REQUIRE(map.gradientPipelineStatus().pending==2);
        auto* memory=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> out(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(memory))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(memory)));
        game.save(out.get(),false,"partially visited air cycle");out->flush();
        const auto bytes=memory->takeContents();
        auto backend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());
        backend->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> in(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend.get()))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend.release())));
        GameGUI resumed;REQUIRE(resumed.game.load(in.get()));
        for(unsigned tick=0;tick<16;++tick) {
            CHECK(normalizedChecksums(game)==normalizedChecksums(resumed.game));
            for(int material:{WHEAT,WOOD}) {
                CHECK(map.gradientRuntime->adaptive.checksum()==resumed.game.map.gradientRuntime->adaptive.checksum());
                const auto* a=map.lookupMaterialGradientSlot(0,material,AIR_MOVEMENT_CLASS);
                const auto* b=resumed.game.map.lookupMaterialGradientSlot(0,material,AIR_MOVEMENT_CLASS);
                REQUIRE(bool(a)==bool(b));if(a)CHECK(std::equal(a,a+map.cellCount(),b));
            }
            advanceFields(game);advanceFields(resumed.game);
        }
    }
}

TEST_CASE("known-zero movement statistics retain first-use demand including air")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    map.configureGradientPipeline(0,3);
    map.setResourceByIndex(20,20,WHEAT,1);
    auto& stats=world.team->stats.editMeasurements();
    stats.movementClassesKnown=true;
    stats.groundUnitsByMovementClass.fill(0);
    REQUIRE(map.getMaterialGradientSlot(0,WHEAT,0)==nullptr);
    REQUIRE(map.gradientRuntime->fieldRequests.size()==1);
    // The first unit may precede its delayed statistics sample. Its cold ticket
    // must still publish; known-zero counts only suppress periodic maintenance.
    for(unsigned tick=0;tick<4;++tick) advanceFields(game);
    REQUIRE(map.lookupMaterialGradientSlot(0,WHEAT,0)!=nullptr);
    CHECK(map.gradientPipelineStatus().published==1);
    const auto fields=map.gradientRuntime->adaptive.fields.size();
    EntityRandom random;int dx=0,dy=0;bool stop=false;
    REQUIRE(map.getMaterialGradientSlot(0,WHEAT,AIR_MOVEMENT_CLASS)==nullptr);
    map.queryMaterialStep(random,0,WHEAT,AIR_MOVEMENT_CLASS,8,8,&dx,&dy,&stop,nullptr);
    CHECK(map.gradientRuntime->adaptive.fields.size()==fields+1);
}

TEST_CASE("two demanded warm resources batch alongside one cold FIFO ticket at a shared deadline with explicit fixed research intervals")
{
    glob2test::HeadlessGlobals globals;
    for(unsigned participants:{1u,4u}) {
        CAPTURE(participants);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto& game=world.game;auto& map=game.map;
        map.configureCompute(participants);map.configureGradientPipeline(participants-1,3);fixedResearchIntervals(map);
        map.setResourceByIndex(20,20,WHEAT,1);map.setResourceByIndex(21,21,WOOD,1);
        REQUIRE(map.getMaterialGradientSlot(0,WHEAT,0)==nullptr);
        REQUIRE(map.getMaterialGradientSlot(0,WOOD,0)==nullptr);
        for(unsigned tick=0;tick<5;++tick) advanceFields(game);
        REQUIRE(map.lookupMaterialGradientSlot(0,WHEAT,0));
        REQUIRE(map.lookupMaterialGradientSlot(0,WOOD,0));
        const auto before=map.gradientPipelineStatus().jobs;
        // Explicit research behavior accepts demanded work; no production age cap exists.
        game.stepCounter+=AdaptiveGradientState::QueryHistoryTicks;
        EntityRandom random;int dx=0,dy=0;bool stop=false;
        map.queryMaterialStep(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr);
        map.queryMaterialStep(random,0,WOOD,0,8,8,&dx,&dy,&stop,nullptr);
        REQUIRE(map.getForbiddenGradient(0,0)==nullptr);
        advanceFields(game);
        CHECK(map.gradientPipelineStatus().jobs==before+3);
        CHECK(map.gradientPipelineStatus().pending==3);
        CHECK(map.gradientRuntime->fieldRequests.empty());
        for(unsigned tick=1;tick<3;++tick) {
            advanceFields(game);
            CHECK(map.lookupForbiddenGradient(0,0)==nullptr);
        }
        advanceFields(game);
        REQUIRE(map.lookupForbiddenGradient(0,0));
        CHECK(map.gradientPipelineStatus().pending==0);
        CHECK(map.gradientPipelineStatus().published==before+3);
    }
}

TEST_CASE("cold queued and privately completed fields continue through binary and text saves [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    for (bool text : {false,true}) for (unsigned checkpoint : {0u,2u,7u})
    {
        CAPTURE(text); CAPTURE(checkpoint);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=731});
        auto& game=world.game;
        auto& map=game.map;
        game.gameHeader.setBuildingGradientDelay(3);
        map.configureCompute(2);
        map.configureGradientPipeline(1,3);
        map.setResourceByIndex(20,20,WHEAT,1);
        map.addForbidden(24,24,0);
        map.addGuardArea(22,22,0);
        map.addClearArea(20,20,0);
        map.getMaterialGradientSlot(0,WHEAT,0);
        map.getForbiddenGradient(0,0);
        map.getGuardAreasGradient(0,0);
        map.getClearAreasGradient(0,0);
        const GradientFieldKey movementKey{GradientFieldFamily::Materials,0,0,Uint8(WHEAT)};
        GradientMovementEvidence movement;movement.emptyGoal=true;movement.reachableNoMove=true;
        for (unsigned i=0;i<checkpoint;++i) {
            map.gradientRuntime->adaptive.fields[movementKey].recordMovement(movement);
            advanceFields(game);
        }
        // Preserve both post-capture traffic and a potentially unknown serving
        // baseline across the checkpoint, including privately completed work.
        map.gradientRuntime->adaptive.fields[movementKey].recordMovement(movement);
        if (checkpoint==2) {
            // All four explicit destinations share the same capture boundary.
            // Their numeric save entries must not overwrite each other's
            // paths in text streams (binary streams are positional).
            REQUIRE(map.gradientPipelineStatus().jobs==4);
            REQUIRE(map.gradientPipelineStatus().pending==4);
            REQUIRE(map.gradientRuntime->fieldRequests.empty());
        }
        GradientExecutionAudit audit;
        auto* storage=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> output(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(storage))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(storage)));
        game.save(output.get(),false,"delayed gradient continuation");
        output->flush();
        const auto bytes=storage->takeContents();
        const auto evidenceName=std::string(text ? "text" : "binary")+"-checkpoint-"+std::to_string(checkpoint);
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+(text ? ".txt" : ".g2s")),bytes);
        GameGUI resumed;
        auto inputBackend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());
        inputBackend->seekFromStart(0); // TextInputStream parses in its constructor.
        std::unique_ptr<GAGCore::InputStream> input(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(inputBackend.get()))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(inputBackend.release())));
        REQUIRE(resumed.game.load(input.get()));
        // Saving can finish private work; loading restores it without kernels.
        if (GAGCore::ThreadSupport::available) {
            CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner==0);
            CHECK(audit.counts(GradientExecutionAudit::Kind::TerrainPlane).owner==0);
        }
        std::ostringstream trace;
        trace<<"boundary\toriginal_checksum_hash\tresumed_checksum_hash\n";
        const auto record=[&] {
            const auto original=normalizedChecksums(game), restored=normalizedChecksums(resumed.game);
            trace<<game.stepCounter<<'\t'<<evidenceHash(original)<<'\t'<<evidenceHash(restored)<<'\n';
            REQUIRE(original.size()==restored.size());
            for (size_t component=0;component<original.size();++component)
                CHECK_MESSAGE(original[component]==restored[component],
                    "simulation checksum component ",component," at boundary ",game.stepCounter);
        };
        record();
        for (int tick=0;tick<32;++tick) {
            CAPTURE(tick);
            map.gradientRuntime->adaptive.fields[movementKey].recordMovement(movement);
            resumed.game.map.gradientRuntime->adaptive.fields[movementKey].recordMovement(movement);
            advanceFields(game);
            advanceFields(resumed.game);
            record();
        }
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+"-continuation.tsv"),trace.str());
        map.finishGradientPipeline();
        resumed.game.map.finishGradientPipeline();
    }
}

TEST_CASE("cold material and area fields publish identical generations with one two four and eight participants [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    std::vector<std::vector<Uint32>> expected;
    std::vector<std::vector<Uint32>> expectedChecksums;
    for (unsigned participants : {1u, 2u, 4u, 8u})
    {
        CAPTURE(participants);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=731});
        auto& game = world.game;
        auto& map = game.map;
        game.gameHeader.setBuildingGradientDelay(3);
        map.configureCompute(participants);
        map.configureGradientPipeline(participants-1,3);
        map.setResourceByIndex(20,20,WHEAT,1);
        map.setResourceByIndex(21,21,WOOD,1);
        map.addGuardArea(22,22,0);
        map.addClearArea(20,20,0);
        map.addForbidden(24,24,0);
        const auto fields = [&] {
            return std::array<const Uint16*,5>{
                map.getMaterialGradientSlot(0,WHEAT,0),
                map.getMaterialGradientSlot(0,WOOD,0),
                map.getGuardAreasGradient(0,0),
                map.getClearAreasGradient(0,0),
                map.getForbiddenGradient(0,0)};
        };
        GradientExecutionAudit audit;
        for (const auto* field : fields()) CHECK(field==nullptr);
        const auto queued=map.gradientRuntime->fieldRequests.size();
        for (int i=0;i<20;++i)
            REQUIRE(map.getMaterialGradientSlot(0,WHEAT,0)==nullptr);
        CHECK(map.gradientRuntime->fieldRequests.size()==queued);

        std::vector<std::vector<Uint32>> trajectory;
        std::vector<std::vector<Uint32>> checksumTrajectory;
        std::ostringstream trace;
        trace<<"boundary\tchecksum_hash\tfield_hash\n";
        for (unsigned tick=0;tick<48;++tick)
        {
            advanceFields(game);
            auto& state=trajectory.emplace_back();
            for (const auto* field : fields())
            {
                state.push_back(field!=nullptr);
                if (field) state.insert(state.end(),field,field+map.getW()*map.getH());
            }
            const auto& checksums=checksumTrajectory.emplace_back(normalizedChecksums(game));
            trace<<game.stepCounter<<'\t'<<evidenceHash(checksums)<<'\t'<<evidenceHash(state)<<'\n';
        }
        const auto ready=fields();
        for (const auto* field : ready) REQUIRE(field!=nullptr);
        // Both source-bearing materials must publish complete guidance.
        CHECK(ready[1][map.coordToIndex(21,21)]==GRADIENT_AT_GOAL);
        // Drain outstanding private work before the audit's lifetime ends.
        map.finishGradientPipeline();
        const auto counts=audit.totals();
        const auto evidenceName="participants-"+std::to_string(participants);
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+".tsv"),trace.str());
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+"-kernel-entries.tsv"),
            "owner\tother\n"+std::to_string(counts.owner)+'\t'+std::to_string(counts.other)+'\n');
        if (participants>1 && GAGCore::ThreadSupport::available) {
            CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner==0);
            CHECK(audit.counts(GradientExecutionAudit::Kind::TerrainPlane).owner==0);
            CHECK(counts.other>0);
        } else CHECK(counts.owner>0);
        if (expected.empty()) {
            expected=std::move(trajectory);
            expectedChecksums=std::move(checksumTrajectory);
        } else {
            CHECK(trajectory==expected);
            CHECK(checksumTrajectory==expectedChecksums);
        }
    }
}
}

TEST_CASE("ordinary terrain edits with markets disabled round-trip binary and text saves" * doctest::test_suite("DelayedGradientPublication"))
{
    glob2test::HeadlessGlobals globals;
    for (bool text:{false,true}) {
        CAPTURE(text);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=947});
        auto& game=world.game; auto& map=game.map;
        game.gameHeader.getExperiments().set(ExperimentId::MarketsV2,false);
        game.gameHeader.setBuildingGradientDelay(3);
        map.configureCompute(2); map.configureGradientPipeline(1,3);
        REQUIRE_FALSE(map.marketsV2Enabled());
        map.setResourceByIndex(20,20,WHEAT,1);
        map.getMaterialGradientSlot(0,WHEAT,0);
        advanceFields(game); // Keep an ordinary material ticket during painting.
        map.paintCell(12,12,WATER);
        // Loading rejects market routing state when the feature is disabled,
        // so the round trip below also verifies the dirty-flag invariant.
        auto* storage=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> output(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(storage))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(storage)));
        game.save(output.get(),false,"terrain edit without markets"); output->flush();
        const auto bytes=storage->takeContents();
        GameGUI resumed;
        // TextInputStream borrows its backend; BinaryInputStream owns it.
        auto inputBackend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());
        inputBackend->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> input(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(inputBackend.get()))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(inputBackend.release())));
        REQUIRE(resumed.game.load(input.get()));
        REQUIRE_FALSE(resumed.game.map.marketsV2Enabled());
        CHECK(normalizedChecksums(game)==normalizedChecksums(resumed.game));
        for (unsigned i=0;i<12;++i) {
            advanceFields(game); advanceFields(resumed.game);
            CHECK(normalizedChecksums(game)==normalizedChecksums(resumed.game));
        }
    }
}
