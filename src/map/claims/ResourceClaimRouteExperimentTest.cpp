// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceClaimRouteExperiment.h"
namespace {
using namespace ResourceClaims;
using namespace ResourceClaims::Oracle;
struct Fixture
{
    SimulationSnapshot::Handle snapshot;
    std::shared_ptr<SimulationSnapshot::Catalogs> catalogs = std::make_shared<SimulationSnapshot::Catalogs>();
    std::shared_ptr<SimulationSnapshot::Terrain> terrain = std::make_shared<SimulationSnapshot::Terrain>();
    std::shared_ptr<SimulationSnapshot::Resources> resources = std::make_shared<SimulationSnapshot::Resources>();
    std::shared_ptr<SimulationSnapshot::Occupancy> occupancy = std::make_shared<SimulationSnapshot::Occupancy>();
    std::shared_ptr<SimulationSnapshot::Areas> areas = std::make_shared<SimulationSnapshot::Areas>();
    std::shared_ptr<SimulationSnapshot::Visibility> visibility = std::make_shared<SimulationSnapshot::Visibility>();
    std::shared_ptr<SimulationSnapshot::Rules> rules = std::make_shared<SimulationSnapshot::Rules>();
    Query query{.owner=7, .origin=0, .material=WHEAT};
    Fixture(int width=16, int height=8)
    {
        snapshot.width=width; snapshot.height=height;
        snapshot.catalogs=catalogs; snapshot.terrain=terrain; snapshot.resources=resources;
        snapshot.occupancy=occupancy; snapshot.areas=areas; snapshot.visibility=visibility; snapshot.rules=rules;
        catalogs->resources=ResourceRegistry::legacy();
        terrain->registry=TerrainRegistry::builtins();
        terrain->rules=std::make_shared<CellRuleTable>(terrain->registry,catalogs->resources);
        const auto size=width*height;
        terrain->cellRules.assign(size,GRASS); resources->cells.resize(size);
        occupancy->cells.resize(size); areas->cells.resize(size);
        visibility->visible.assign(size,1);
    }
    Uint32 index(int x,int y) const { return Uint32(snapshot.view().index(x,y)); }
    void resource(int x,int y,unsigned type,unsigned amount)
    { resources->cells[index(x,y)].resource=Resource{Uint16(type),0,amount,0}; }
    Result run(std::vector<std::pair<Owner,Claim>> owners={}) const
    { return evaluate(snapshot,query,owners); }
};
 }
TEST_SUITE("ResourceClaimRouteExperiment")
{
    TEST_CASE("direct and via probe costs match the frozen exact access oracle")
    {
        Fixture f;
        f.query.origin=f.index(1,3);
        f.resource(5,3,WHEAT,4);
        const auto oracle=f.run();
        REQUIRE(oracle.best);
        const auto& candidate=oracle.candidates[*oracle.best];
        std::vector<Uint16> values(16*8,GRADIENT_UNREACHABLE);
        // Ascending row ending at a blocked resource; routing must stop adjacent.
        for(int x=1;x<=5;++x)values[f.index(x,3)]=Uint16(GRADIENT_AT_GOAL-(5-x)*10);
        f.occupancy->cells[f.index(4,3)].groundUnit=16;
        Experiment::ExposureProxy exposure(16,8,f.occupancy->cells,f.visibility->visible,2,1,16,8);
        Experiment::StaticRoutes routes(f.snapshot.view(),f.query,oracle,values,&exposure);
        const auto direct=routes.direct(candidate.source);
        REQUIRE(std::string_view(direct.status)=="reached");
        CHECK(direct.cost==candidate.distance);
        CHECK(direct.exposure.cost==direct.cost);CHECK(direct.exposure.steps>0);
        CHECK(direct.exposure.nearCost[0][2]==direct.cost);
        const auto via=routes.via(candidate.source,f.index(2,3));
        REQUIRE(std::string_view(via.status)=="reached");
        CHECK(via.cost==candidate.accessDistance);
        CHECK(via.continuationSteps==2);
        CHECK(via.exposure.steps==3);CHECK(via.exposure.cost==via.cost);
        CHECK(via.exposure.endpoint==f.index(4,3));CHECK(via.exposure.minimum[0]==0);
        CHECK(via.exposure.nearSteps[0][0]==2);
        CHECK(routes.via(candidate.source,f.index(2,3)).cost==via.cost);
        CHECK(routes.via(candidate.source,f.index(2,3)).exposure.steps==via.exposure.steps);
    }
    TEST_CASE("pooled source route stops at the closer connected farm access")
    {
        Fixture f; f.areas->farmEnabled=true;
        f.resource(3,0,WHEAT,1);f.resource(4,0,WHEAT,4);
        f.areas->cells[f.index(3,0)].farm=1;f.areas->cells[f.index(4,0)].farm=1;
        const auto oracle=f.run();REQUIRE(oracle.best);
        const auto& c=oracle.candidates[*oracle.best];REQUIRE(c.pooled);
        std::vector<Uint16> values(16*8,GRADIENT_UNREACHABLE);
        Experiment::StaticRoutes routes(f.snapshot.view(),f.query,oracle,values);
        const auto direct=routes.direct(c.source);
        REQUIRE(std::string_view(direct.status)=="reached");
        CHECK(direct.cost==c.distance);CHECK(direct.cost<c.accessDistance);
    }
    TEST_CASE("wrong goal and frozen blockers are contradictions rather than routes")
    {
        Fixture f;
        f.query.origin=f.index(1,3); f.resource(5,3,WHEAT,4);
        const auto oracle=f.run();
        std::vector<Uint16> values(16*8,GRADIENT_UNREACHABLE);
        values[f.index(2,3)]=GRADIENT_AT_GOAL;
        Experiment::StaticRoutes routes(f.snapshot.view(),f.query,oracle,values);
        const auto wrong=routes.via(f.index(5,3),f.index(2,3));
        CHECK(std::string_view(wrong.status)=="contradiction");
        CHECK(wrong.cost==Unreachable);
        values[f.index(2,3)]=GRADIENT_AT_GOAL-30;
        values[f.index(3,3)]=GRADIENT_AT_GOAL-20;
        values[f.index(4,3)]=GRADIENT_AT_GOAL-10;
        values[f.index(5,3)]=GRADIENT_AT_GOAL;
        f.occupancy->cells[f.index(3,3)].building=1;
        const auto blockedOracle=f.run();
        Experiment::StaticRoutes blocked(f.snapshot.view(),f.query,blockedOracle,values);
        const auto contradiction=blocked.via(f.index(5,3),f.index(2,3));
        CHECK(std::string_view(contradiction.status)=="contradiction");
        CHECK(contradiction.cost==Unreachable);
    }
}
