// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceClaimExposure.h"
using ResourceClaims::Experiment::ExposureProxy;
TEST_SUITE("ResourceClaimExposure")
{
TEST_CASE("hostile proximity respects incoming mask visibility and torus without assuming weapon capability")
{
    std::vector<MapState::OccupancyCell> cells(64);
    std::vector<std::uint32_t> visible(64,0);
    // Team1 ground unit; Team2 air unit; Team1 building footprint.
    cells[0].groundUnit=16;cells[7].airUnit=32;cells[18].building=8;cells[19].building=8;
    visible[0]=1;visible[18]=2; // Visibility to another team is not ours.
    ExposureProxy proxy(8,8,cells,visible,2,1,16,8);
    CHECK(proxy.seedCells()==std::array<std::uint32_t,4>{1,2,1,0});
    CHECK(proxy.at(7)[0]==1);CHECK(proxy.at(63)[0]==1); // Wrap in one or both axes.
    CHECK(proxy.at(18)[1]==0);CHECK(proxy.at(18)[3]==17);
    CHECK(proxy.at(0)[0]==0);CHECK(proxy.at(0)[2]==0);
    // Incoming attack mask, not an outgoing enemy declaration, determines seeds.
    ExposureProxy other(8,8,cells,visible,4,1,16,8);
    CHECK(other.at(7)[0]==0);CHECK(other.at(7)[2]==17);
    ExposureProxy peaceful(8,8,cells,visible,0,1,16,8);
    CHECK(peaceful.at(0)==ExposureProxy::Point{17,17,17,17});
}
TEST_CASE("distance cap and actual entered path counters compose without counting prefix twice")
{
    std::vector<MapState::OccupancyCell> cells(64*64);
    std::vector<std::uint32_t> visible(cells.size(),1);cells[0].groundUnit=16;
    ExposureProxy proxy(64,64,cells,visible,2,1,16,8);
    CHECK(proxy.at(16)[0]==16);CHECK(proxy.at(17)[0]==17);CHECK(proxy.at(63)[0]==1);
    ExposureProxy::Path prefix;prefix.endpoint=0;
    proxy.enter(prefix,1,10);proxy.enter(prefix,2,14);
    ExposureProxy::Path tail;tail.endpoint=2;proxy.enter(tail,8,30);
    prefix.append(tail);
    CHECK(prefix.steps==3);CHECK(prefix.cost==54);CHECK(prefix.endpoint==8);
    CHECK(prefix.minimum[0]==1);
    CHECK(prefix.nearSteps[0]==std::array<std::uint32_t,3>{1,2,3});
    CHECK(prefix.nearCost[0]==std::array<std::uint64_t,3>{10,24,54});
    CHECK(prefix.nearSteps[1]==std::array<std::uint32_t,3>{0,0,0});
    CHECK_THROWS(ExposureProxy(8,8,cells,visible,2,1,16,8));
}
}
