// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceClaimOracle.h"
#include "ResourceClaimPatternEvaluation.h"
#include <nlohmann/json.hpp>

namespace
{
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
const Candidate& candidate(const Result& result,Uint32 source)
{
    const auto it=std::find_if(result.candidates.begin(),result.candidates.end(),[&](const auto& c){return c.source==source;});
    REQUIRE(it!=result.candidates.end()); return *it;
}
}

TEST_SUITE("ResourceClaimOracle")
{
    TEST_CASE("weighted forward distance stops at harvest stance and wraps the torus")
    {
        Fixture fixture;
        fixture.resource(4,0,WHEAT,1);
        auto result=fixture.run();
        REQUIRE(result.best);
        const auto& best=result.candidates[*result.best];
        CHECK(best.source==fixture.index(4,0)); CHECK(best.access==best.source);
        CHECK(best.stance==fixture.index(3,0)); CHECK(best.distance==30);
        CHECK(result.distances[fixture.index(1,1)]==14);
        fixture.resource(15,0,WHEAT,1);
        result=fixture.run(); REQUIRE(result.best);
        CHECK(result.candidates[*result.best].source==fixture.index(15,0));
        CHECK(result.candidates[*result.best].distance==0);
        CHECK(result.candidates[*result.best].stance==fixture.query.origin);
    }
    TEST_CASE("static routes retain blockers but frozen-unit routes include moving traffic")
    {
        Fixture fixture;
        fixture.resource(4,0,WHEAT,1);
        for (int y=0;y<8;++y) {
            fixture.occupancy->cells[fixture.index(2,y)].groundUnit=99;
            fixture.occupancy->cells[fixture.index(14,y)].groundUnit=99;
        }
        CHECK(fixture.run().best.has_value());
        fixture.query.route.occupancy=OccupancyPolicy::FrozenUnits;
        CHECK_FALSE(fixture.run().best.has_value());
        fixture.query.route.occupancy=OccupancyPolicy::Static;
        for (int y=0;y<8;++y) {
            fixture.occupancy->cells[fixture.index(2,y)].immobileUnit=0;
            fixture.occupancy->cells[fixture.index(14,y)].immobileUnit=0;
        }
        CHECK_FALSE(fixture.run().best.has_value());
        fixture.query.route.movement=AIR_MOVEMENT_CLASS;
        CHECK(fixture.run().best.has_value());
        fixture.areas->cells[fixture.index(4,0)].forbidden=1;
        CHECK_FALSE(fixture.run().best.has_value()); // Air movement ignores paint, pickup does not.
    }
    TEST_CASE("water-only and amphibious entry costs use the frozen movement profile")
    {
        Fixture fixture;
        std::fill(fixture.terrain->cellRules.begin(),fixture.terrain->cellRules.end(),WATER);
        fixture.resource(4,0,ALGA,1); fixture.query.material=ALGA;
        CHECK_FALSE(fixture.run().best.has_value());
        fixture.query.route.movement=1;
        auto result=fixture.run(); REQUIRE(result.best);
        CHECK(result.candidates[*result.best].distance==15);
        CHECK(result.distances[fixture.index(1,1)]==7);
        fixture.query.route.movement=WATER_ONLY_CLASS;
        result=fixture.run(); REQUIRE(result.best);
        CHECK(result.candidates[*result.best].distance==30);
    }
    TEST_CASE("farm reserve tiles connect pooled sources and an empty tile splits them")
    {
        Fixture fixture;
        fixture.areas->farmEnabled=true;
        fixture.resource(3,0,WHEAT,1); fixture.resource(4,0,WHEAT,4);
        fixture.areas->cells[fixture.index(3,0)].farm=1;
        fixture.areas->cells[fixture.index(4,0)].farm=1;
        auto result=fixture.run(); REQUIRE(result.best);
        const auto& ripe=candidate(result,fixture.index(4,0));
        CHECK(ripe.pooled); CHECK(ripe.capacity==3); CHECK(ripe.available);
        CHECK(ripe.distance==20); CHECK(ripe.access==fixture.index(3,0));
        CHECK(ripe.stance==fixture.index(2,0));
        CHECK(ripe.accessDistance==34); // Touching this exact source costs more than entering its farm.
        CHECK_FALSE(candidate(result,fixture.index(3,0)).available);
        fixture.resources->cells[fixture.index(3,0)].resource.clear();
        result=fixture.run(); REQUIRE(result.best);
        CHECK(result.candidates[*result.best].distance==30);
        fixture.resource(4,0,WHEAT,1);
        CHECK_FALSE(fixture.run().best.has_value());
        fixture.rules->values.resourceGrowthDisabled=true;
        result=fixture.run(); REQUIRE(result.best);
        CHECK(result.candidates[*result.best].capacity==1);
    }
    TEST_CASE("claims exclude self and infinite supply while destructive sources stay exclusive")
    {
        Fixture fixture;
        fixture.resource(4,0,WHEAT,1);
        Claim claim{fixture.index(4,0),fixture.index(4,0),WHEAT,WHEAT};
        CHECK(fixture.run({{7,claim}}).best.has_value());
        auto result=fixture.run({{8,claim}});
        CHECK_FALSE(result.best.has_value()); CHECK(result.candidates[0].claimed==1);
        fixture.resource(4,0,STONE,1); fixture.query.material=STONE;
        claim.material=STONE; claim.resource=STONE;
        result=fixture.run({{8,claim},{9,claim}}); REQUIRE(result.best);
        CHECK(result.candidates[*result.best].infinite);
        fixture.resource(4,0,WOOD,1); fixture.query.material=WOOD;
        claim.material=WOOD; claim.resource=WOOD; claim.exclusive=true;
        result=fixture.run({{8,claim}});
        CHECK_FALSE(result.best); CHECK(result.candidates[0].exclusive);
    }
    TEST_CASE("oracle respects another owner's reserve for direct and pooled candidates")
    {
        Fixture fixture;fixture.resource(4,0,WHEAT,2);
        const Claim pooled{fixture.index(4,0),fixture.index(4,0),WHEAT,WHEAT,false,1};
        auto result=fixture.run({{8,pooled}});
        REQUIRE(result.candidates.size()==1);
        CHECK_FALSE(result.candidates[0].available); // Direct access cannot spend the other owner's seed.
        result=fixture.run({{7,pooled}});
        CHECK(result.best.has_value()); // This owner may lower its own reserve by retargeting.
        fixture.areas->farmEnabled=true;
        fixture.areas->cells[fixture.index(4,0)].farm=1;
        fixture.resource(4,0,WHEAT,3);
        result=fixture.run({{8,pooled}});
        REQUIRE(result.best);
        CHECK(result.candidates[*result.best].capacity==2);
        CHECK(result.candidates[*result.best].available); // Both reservations fit above the shared seed.
    }
    TEST_CASE("multi-yield material claims never substitute aggregate tile counts")
    {
        Fixture fixture;
        using Json=nlohmann::json;
        auto definition=Json::parse(fixture.catalogs->resources->serialize())["resources"][1];
        definition["key"]="mixed";
        definition["yields"]["wood"]={{"capacity",4},{"initial",1},{"seedReserve",0},{"consumption","one"}};
        fixture.catalogs->resources=ResourceRegistry::fromJson(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
        fixture.terrain->rules=std::make_shared<CellRuleTable>(fixture.terrain->registry,fixture.catalogs->resources);
        fixture.resource(4,0,0,2);
        fixture.resources->stockIndices.resize(128);
        fixture.resources->stockIndices[fixture.index(4,0)]=1;
        fixture.resources->stocks.resize(1);
        fixture.resources->stocks[0][WOOD]=1; fixture.resources->stocks[0][WHEAT]=1;
        fixture.query.material=WOOD;
        Claim food{fixture.index(4,0),fixture.index(4,0),WHEAT,0};
        auto result=fixture.run({{8,food}}); REQUIRE(result.best);
        CHECK(result.candidates[*result.best].claimed==0);
        food.exclusive=true;
        CHECK_FALSE(fixture.run({{8,food}}).best.has_value());
    }
    TEST_CASE("missing frozen state and ambiguous ownership input fail explicitly")
    {
        Fixture fixture;
        const Claim claim{3,3,WHEAT,WHEAT};
        CHECK_THROWS_AS(fixture.run({{8,claim},{7,claim}}),std::invalid_argument);
        CHECK_THROWS_AS(fixture.run({{7,claim},{7,claim}}),std::invalid_argument);
        fixture.query.origin=128;
        CHECK_THROWS_AS(fixture.run(),std::invalid_argument);
        fixture.query.origin=0; fixture.snapshot.visibility.reset();
        CHECK_THROWS_AS(fixture.run(),std::invalid_argument);
    }
    TEST_CASE("pattern policies share probes without using oracle costs to choose")
    {
        using namespace ResourceClaims::Experiment;
        Result oracle;
        for (const auto& [source, distance] : {std::pair{3u,Uint64(1)},std::pair{4u,Uint64(2)},std::pair{7u,Uint64(100)}}) {
            Candidate c; c.source=source; c.distance=distance; c.available=source!=3; c.accessEligible=true;
            oracle.candidates.push_back(c);
        }
        PatternEvaluator evaluator(oracle,8,8,0,3);
        const Sint16 x[]={0,1,2,1}, y[]={0,0,0,0}; const Uint8 origins[]={0,0,0,0};
        unsigned reads=0;
        const auto destination=[&](Uint32 probe){ ++reads; return probe==0?3u:probe==1?4u:7u; };
        const auto first=evaluator.evaluate({x,y,origins},PatternSelection::FirstAvailable,destination);
        CHECK_FALSE(first.fallback); CHECK(first.source==4); CHECK(first.probes==2); CHECK(first.destinationReads==2);
        const auto geometric=evaluator.evaluate({x,y,origins},PatternSelection::ClosestGeometric,destination);
        CHECK(geometric.source==7); CHECK(geometric.distance==100); // Geometric choice is not the oracle-cost minimum.
        CHECK(geometric.probes==4); CHECK(geometric.destinationReads==1); CHECK(reads==3);
        oracle.candidates[1].distance=Unreachable;
        PatternEvaluator fresh(oracle,8,8,0,3);
        const auto unreachable=fresh.evaluate({x,y,origins},PatternSelection::FirstAvailable,destination);
        CHECK_FALSE(unreachable.fallback); CHECK(unreachable.distance==Unreachable);
    }
    TEST_CASE("pattern farm probes resolve spare stock and midpoint wraps consistently")
    {
        using namespace ResourceClaims::Experiment;
        Result oracle;
        Candidate seed; seed.source=3; seed.component=0; seed.accessEligible=true; seed.accessDistance=30;
        Candidate ripe; ripe.source=4; ripe.component=0; ripe.available=true; ripe.distance=20;
        oracle.candidates={seed,ripe};
        PatternEvaluator evaluator(oracle,8,8,1,7);
        CHECK(evaluator.closestOctant()==4);
        const Sint16 x[]={0}, y[]={0}; const Uint8 origins[]={2};
        const auto outcome=evaluator.evaluate({x,y,origins},PatternSelection::FirstAvailable,
            [&](Uint32 probe){ CHECK(probe==0); return 3u; });
        CHECK_FALSE(outcome.fallback); CHECK(outcome.source==4); CHECK(outcome.access==3);
        CHECK(outcome.distance==20);
        CHECK(outcome.accessDistance==30);
    }
}
