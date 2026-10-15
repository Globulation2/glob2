// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "AirGradient.h"
#include "AirPathfind.h"
#include "GradientPropagation.h"
#include <array>
#include <vector>

namespace {
struct AirFixture
{
    field::Grid grid;
    std::vector<std::uint8_t> passable;
    std::vector<gradient_kernel::EntrySteps> costs;
    std::vector<std::uint16_t> values;
    std::vector<std::uint32_t> labels;
    GradientWorkspace workspace;
    AirFixture(int w,int h):grid(w,h),passable(grid.cells(),1),
        costs(grid.cells(),gradient_kernel::LAND_STEPS),values(grid.cells()),labels(grid.cells()) {}
    field::air::Traversal traversal() const { return {grid,passable,costs}; }
    void build(std::span<const field::air::Goal> goals,
        field::air::GoalPolicy policy=field::air::GoalPolicy::Exact,
        field::air::Direction direction=field::air::Direction::ToDestination)
    { field::air::build(traversal(),goals,values,workspace,labels,policy,direction); }
    auto access() const { return [this](int x,int y) { return bool(passable[grid.index(x,y)]); }; }
    auto entry() const { return [this](int x,int y) { return costs[grid.index(x,y)].cardinal; }; }
};
std::optional<unsigned> forwardGoal(const AirFixture& fixture,std::size_t destination)
{
    auto best=field::air::decode(fixture.values[destination]);
    if (!fixture.passable[destination]) {
        best={};
        fixture.grid.neighborIndices(int(destination),field::Surrounding,[&](int cell) {
            if (!fixture.passable[cell]) return;
            if (const auto candidate=field::air::decode(fixture.values[cell]); candidate && (!best || *candidate<*best))
                best=candidate;
        });
    }
    return best;
}
}

TEST_SUITE("AirGradient")
{
TEST_CASE("reverse fields match wide air search on asymmetric rectangular thin and blocked maps")
{
    using namespace field::air;
    for (const auto [w,h]:{std::pair{1,1},{1,17},{17,1},{8,5},{16,8},{7,9}})
    {
        CAPTURE(w); CAPTURE(h);
        AirFixture fixture(w,h);
        for (std::size_t i=0;i<fixture.grid.cells();++i) {
            fixture.passable[i]=(i%11!=3);
            fixture.costs[i]=compileSteps(std::array{4u,10u,17u,31u,91u}[i%5]);
        }
        for (unsigned goal:{0u,unsigned(fixture.grid.cells()/2),unsigned(fixture.grid.cells()-1)})
        for (auto policy:{GoalPolicy::Exact,GoalPolicy::ApproachBlocked})
        {
            const std::array goals{Goal{goal,7}};
            fixture.build(goals,policy);
            const int gx=goal%w,gy=goal/w;
            field::AirDistanceField reference(w,h,gx,gy,fixture.access(),fixture.entry(),true,
                field::AirDistanceDirection::ToDestination);
            for (std::size_t source=0;source<fixture.grid.cells();++source) {
                CAPTURE(source); CAPTURE(goal); CAPTURE(int(policy));
                const auto actual=sample(fixture.traversal(),fixture.values,source,SourcePolicy::RequirePassable,fixture.labels);
                const unsigned expected=!fixture.passable[goal] && policy==GoalPolicy::Exact
                    ? decltype(reference)::unreachable : reference.costTo(source%w,source/w);
                CHECK(actual.reachable==(expected!=decltype(reference)::unreachable));
                if (actual.reachable) { CHECK(actual.cost==expected); CHECK(actual.rank==7); }
                if (!fixture.passable[source] && (fixture.passable[goal] || policy==GoalPolicy::ApproachBlocked)) {
                    field::AirDistanceField forward(w,h,source%w,source/w,fixture.access(),fixture.entry());
                    const auto leaving=sample(fixture.traversal(),fixture.values,source,SourcePolicy::LeaveBlocked,fixture.labels);
                    const auto cost=forward.costTo(gx,gy);
                    CHECK(leaving.reachable==(cost!=decltype(forward)::unreachable));
                    if (leaving.reachable) CHECK(leaving.cost==cost);
                }
            }
        }
    }
}

TEST_CASE("forward private-choice fields preserve blocked-source departure and blocked-goal approach")
{
    using namespace field::air;
    AirFixture fixture(16,7);
    for (std::size_t i=0;i<fixture.grid.cells();++i) {
        fixture.passable[i]=i%9!=3;
        fixture.costs[i]=compileSteps(i%3 ? 10 : 37);
    }
    for (unsigned source:{3u,17u,46u}) {
        CAPTURE(source);
        const std::array sources{Goal{source,0}};
        fixture.build(sources,GoalPolicy::Exact,Direction::FromSource);
        field::AirDistanceField reference(16,7,source%16,source/16,fixture.access(),fixture.entry());
        for (unsigned destination=0;destination<fixture.grid.cells();++destination) {
            const auto actual=forwardGoal(fixture,destination);
            const auto expected=reference.costTo(destination%16,destination/16);
            CHECK(bool(actual)==(expected!=decltype(reference)::unreachable));
            if (actual) CHECK(*actual==expected);
        }
    }
}

TEST_CASE("caller ranks break multi-source ties independent of enrollment order")
{
    using namespace field::air;
    AirFixture fixture(16,8);
    for (unsigned i=0;i<fixture.costs.size();++i) fixture.costs[i]=compileSteps(i%3 ? 10 : 13);
    std::array goals{Goal{unsigned(fixture.grid.index(3,4)),9},Goal{unsigned(fixture.grid.index(11,4)),2},Goal{unsigned(fixture.grid.index(11,4)),1}};
    fixture.build(goals);
    const auto expectedValues=fixture.values;
    const auto expectedLabels=fixture.labels;
    std::reverse(goals.begin(),goals.end());
    fixture.build(goals);
    CHECK(fixture.values==expectedValues); CHECK(fixture.labels==expectedLabels);
    for (unsigned source=0;source<fixture.grid.cells();++source) {
        unsigned bestCost=std::numeric_limits<unsigned>::max(),bestRank=NoGoal;
        field::AirDistanceField reference(16,8,source%16,source/16,fixture.access(),fixture.entry());
        for (const auto goal:goals) {
            const auto cost=reference.costTo(goal.cell%16,goal.cell/16);
            if (cost<bestCost || (cost==bestCost && goal.rank<bestRank)) { bestCost=cost; bestRank=goal.rank; }
        }
        const auto actual=sample(fixture.traversal(),fixture.values,source,SourcePolicy::RequirePassable,fixture.labels);
        REQUIRE(actual.reachable); CHECK(actual.cost==bestCost); CHECK(actual.rank==bestRank);
    }
    // Different labels can cross several layers before reaching this tie.
    AirFixture tie(32,1);
    const std::array tiedGoals{Goal{4,100},Goal{28,3}};
    tie.build(tiedGoals);
    CHECK(tie.labels[0]==3);
}

TEST_CASE("bounded encoding keeps the exact shared limit and rejects overrange arithmetic")
{
    using namespace field::air;
    constexpr unsigned limit=gradient_kernel::COST_LIMIT;
    AirFixture near(8,1);
    const std::array expensiveSeed{Goal{0,0,limit-10}};
    near.build(expensiveSeed);
    CHECK(decode(near.values[0])==limit-10);
    CHECK(decode(near.values[1])==limit);
    CHECK(near.values[2]==GRADIENT_UNREACHABLE);
    CHECK_FALSE(decode(GRADIENT_UNREACHABLE)); CHECK_FALSE(decode(GRADIENT_FORBIDDEN));
    CHECK_FALSE(decode(std::uint16_t(GRADIENT_AT_GOAL-limit-1)));
    AirFixture corridor(131072,1);
    for (unsigned i=0;i<corridor.grid.cells();++i) {
        corridor.passable[i]=i<=65535;
        corridor.costs[i]={1,1};
    }
    const std::array goal{Goal{0,1}};
    corridor.build(goal);
    CHECK(decode(corridor.values[limit])==limit);
    CHECK(corridor.values[limit+1]==GRADIENT_UNREACHABLE);
    field::AirDistanceField wide(131072,1,0,0,corridor.access(),corridor.entry());
    CHECK(wide.costTo(limit+1,0)==limit+1); // Deliberate new finite-range boundary.
    near.costs[0]={std::numeric_limits<unsigned>::max(),std::numeric_limits<unsigned>::max()};
    CHECK_THROWS_AS(near.build(goal),std::invalid_argument);
    CHECK_THROWS_AS(compileSteps(std::numeric_limits<unsigned>::max()),std::invalid_argument);
    CHECK_THROWS_AS(compileSteps(0),std::invalid_argument);
    CHECK(compileSteps(182).diagonal==254);
}

TEST_CASE("no goals metrics and ground specializations retain their contracts")
{
    using namespace field::air;
    AirFixture fixture(16,8);
    fixture.passable[9]=0;
    fixture.build({});
    CHECK(fixture.values[9]==GRADIENT_FORBIDDEN);
    CHECK(fixture.values[0]==GRADIENT_UNREACHABLE);
    CHECK(fixture.labels[0]==NoGoal);
    CHECK(ceilTiles(0)==0); CHECK(ceilTiles(1)==1); CHECK(ceilTiles(10)==1); CHECK(ceilTiles(11)==2);
    CHECK(ceilTiles(std::numeric_limits<unsigned>::max())==429496730u);
    CHECK(wrappedChebyshevTiles(fixture.grid,15,7,1,1)==2);
    CHECK(wrappedSquaredDistance(fixture.grid,15,7,1,1)==8);
    for (unsigned i=0;i<fixture.grid.cells();++i)
        fixture.costs[i]=i%3 ? gradient_kernel::LAND_STEPS : gradient_kernel::entrySteps(20);
    const std::array goals{Goal{24,0},Goal{91,1}};
    fixture.build(goals);
    std::vector<std::uint16_t> ground(fixture.grid.cells(),GRADIENT_UNREACHABLE);
    ground[9]=GRADIENT_FORBIDDEN;
    ground[24]=ground[91]=GRADIENT_AT_GOAL;
    GradientWorkspace workspace;
    gradient_kernel::propagateFieldCPUVariant<false>(ground.data(),5,gradient_kernel::COST_LIMIT,
        fixture.grid,workspace,[](std::size_t i) { return i%3==0; });
    CHECK(fixture.values==ground);
}
}
