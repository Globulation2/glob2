// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ClaimedGradientStep.h"
#include "field/GradientDestinations.h"
#include <vector>

namespace {
using Status = ResourceClaims::ClaimedGradientStep::Status;
struct Fixture {
    field::Grid grid{8,8};
    std::vector<std::uint16_t> values = std::vector<std::uint16_t>(grid.cells(),1);
    gradient_kernel::OwnedDestinations destinations{grid.cells()};
    std::vector<unsigned> costs = std::vector<unsigned>(grid.cells(),10);
    std::vector<bool> geometry = std::vector<bool>(grid.cells(),true), traffic = geometry;
    std::uint32_t current = grid.index(1,4), access = grid.index(5,4);
    Fixture() {
        std::fill_n(destinations.x.get(),grid.cells(),5);
        std::fill_n(destinations.y.get(),grid.cells(),4);
        values[current]=100;
    }
    auto step() const {
        return ResourceClaims::claimedGradientStep(grid,values,destinations,current,access,
            [&](auto i){return geometry[i];},[&](auto i){return traffic[i];},
            [&](auto i,bool diagonal){return costs[i]*(diagonal?14:10)/10;});
    }
};
}

TEST_SUITE("ClaimedGradientStep") {
TEST_CASE("weighted steps retain the exact claimed destination and stable ties") {
    Fixture f;
    const auto north=f.grid.index(1,3),east=f.grid.index(2,4),west=f.grid.index(0,4);
    f.values[north]=130;f.costs[north]=40; // Higher scalar, worse weighted step.
    f.values[east]=120;
    f.values[west]=60000;f.destinations.x[west]=0; // Wrong source must never win.
    auto result=f.step();REQUIRE(result.status==Status::Ready);CHECK(result.dx==1);CHECK(result.dy==0);
    f.costs[north]=20; // Both score110: north precedes east in tabClose.
    result=f.step();REQUIRE(result.status==Status::Ready);CHECK(result.dx==0);CHECK(result.dy==-1);
    f.costs[north]=0;result=f.step();CHECK(result.dx==1);CHECK(result.dy==0);
    f.costs[east]=std::numeric_limits<unsigned>::max();CHECK(f.step().status==Status::Unavailable);
}
TEST_CASE("queued traffic waits while broken geometry falls back to routing") {
    Fixture f;
    const auto east=f.grid.index(2,4),north=f.grid.index(1,3);
    f.values[east]=120;f.traffic[east]=false;
    CHECK(f.step().status==Status::Pending);
    f.geometry[east]=false;CHECK(f.step().status==Status::Unavailable);
    f.geometry[east]=true;f.values[north]=110;
    auto result=f.step();REQUIRE(result.status==Status::Ready);CHECK(result.dx==0);CHECK(result.dy==-1);
    f.values[north]=f.values[f.current]; // No random plateau sidesteps.
    CHECK(f.step().status==Status::Pending);
    f.destinations.x[east]=0;CHECK(f.step().status==Status::Unavailable);
}
TEST_CASE("arrival means the held access neighborhood rather than a scalar peak") {
    Fixture f;f.values[f.current]=65535;
    CHECK(f.step().status==Status::Unavailable);
    f.current=f.grid.index(4,3);CHECK(f.step().status==Status::Arrived);
    f.current=f.access;CHECK(f.step().status==Status::Arrived);
    f.access=f.grid.index(7,7);f.current=f.grid.index(0,0);
    CHECK(f.step().status==Status::Arrived);
    f.access=unsigned(f.grid.cells());CHECK(f.step().status==Status::Unavailable);
}
TEST_CASE("retained publication labels are read independently of replacement leases") {
    Fixture f;f.values[f.grid.index(2,4)]=120;
    auto old=std::make_shared<gradient_kernel::OwnedDestinations>(f.grid.cells());
    std::fill_n(old->x.get(),f.grid.cells(),5);std::fill_n(old->y.get(),f.grid.cells(),4);
    const std::shared_ptr<const gradient_kernel::OwnedDestinations> retained=old;
    auto replacement=std::make_shared<gradient_kernel::OwnedDestinations>(f.grid.cells());
    std::fill_n(replacement->x.get(),f.grid.cells(),0);std::fill_n(replacement->y.get(),f.grid.cells(),0);
    const auto run=[&](const auto& lease){return ResourceClaims::claimedGradientStep(f.grid,f.values,*lease,
        f.current,f.access,[](auto){return true;},[](auto){return true;},[](auto,bool){return 10u;});};
    CHECK(run(retained).status==Status::Ready);CHECK(run(replacement).status==Status::Unavailable);
    CHECK(run(retained).status==Status::Ready);
    f.values.clear();CHECK(run(retained).status==Status::Unavailable);
}
TEST_CASE("fresh weighted fields descend to their exact source on narrow and wrapped maps") {
    for(const auto [width,height]:{std::pair{1,17},std::pair{17,1},std::pair{7,9},std::pair{16,8}}) {
        CAPTURE(width);CAPTURE(height);
        const field::Grid grid{width,height};
        std::vector<std::uint16_t> values(grid.cells(),1);
        std::vector<gradient_kernel::EntrySteps> costs(grid.cells());
        for(unsigned i=0;i<grid.cells();++i) {
            costs[i]={10u+i%11,14u+i%13};
            if(i%17==7)values[i]=0;
        }
        values[0]=65535;values[grid.cells()/2]=65535;
        gradient_kernel::OwnedDestinations destinations(grid.cells());
        GradientWorkspace scratch;
        gradient_kernel::propagateDestinationFieldCPU(values.data(),destinations.planes(),65500,grid,scratch,
            [&](auto i){return costs[i];});
        for(unsigned start=0;start<grid.cells();++start) {
            if(values[start]<=1)continue;
            const auto access=unsigned(grid.index(destinations.x[start],destinations.y[start]));
            auto current=start;bool arrived=false;
            for(unsigned steps=0;steps<grid.cells();++steps) {
                const auto result=ResourceClaims::claimedGradientStep(grid,values,destinations,current,access,
                    [&](auto i){return values[i]>1 && values[i]!=65535;},[](auto){return true;},
                    [&](auto i,bool diagonal){return diagonal?costs[i].diagonal:costs[i].cardinal;});
                if(result.status==Status::Arrived){arrived=true;break;}
                REQUIRE(result.status==Status::Ready);
                const auto next=unsigned(grid.index(int(current%width)+result.dx,int(current/width)+result.dy));
                CHECK(values[next]>values[current]);
                CHECK(destinations.x[next]==destinations.x[start]);CHECK(destinations.y[next]==destinations.y[start]);
                const auto step=(result.dx&&result.dy)?costs[next].diagonal:costs[next].cardinal;
                CHECK(unsigned(values[next])-step==values[current]);
                current=next;
            }
            CHECK(arrived);
        }
    }
}
}
