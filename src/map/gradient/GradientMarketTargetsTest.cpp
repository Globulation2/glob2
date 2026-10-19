// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "GradientMarketTargets.h"
#include "GradientTargetCell.h"

TEST_SUITE("GradientMarketTargets") {
TEST_CASE("overlapping providers change membership only when last source disappears") {
    GradientMarketTargets::Footprints before{{3,{2,0}},{7,{0,1}}},after{{3,{1,0}},{9,{0,1}}};
    unsigned changes=0,added=0,removed=0;
    GradientMarketTargets::diff(before,after,[&](auto index,auto oldRefs,auto newRefs) {
        ++changes;const bool was=oldRefs.occupying||oldRefs.overlay,now=newRefs.occupying||newRefs.overlay;
        added+=!was&&now;removed+=was&&!now;
        if(index==3)CHECK(was==now);
    });
    CHECK(changes==3);CHECK(added==1);CHECK(removed==1);
}
TEST_CASE("inverse supplier masks preserve independent materials teams and overlap kinds") {
    GradientMarketTargets cache;cache.update(4,0,0,{2,0});cache.update(4,0,1,{0,1});cache.update(4,1,0,{0,1});
    CHECK(cache.inverse.at(4).at(0).occupying==1);CHECK(cache.inverse.at(4).at(0).overlay==2);
    cache.update(4,0,0,{1,0});CHECK(cache.inverse.at(4).at(0).occupying==1);
    cache.update(4,0,0,{});CHECK(cache.inverse.at(4).at(0).occupying==0);CHECK(cache.inverse.at(4).at(0).overlay==2);
    cache.eraseTeam(0);CHECK_FALSE(cache.inverse.at(4).contains(0));CHECK(cache.inverse.at(4).at(1).overlay==1);
    cache.update(4,1,0,{});CHECK(cache.inverse.empty());
}
TEST_CASE("supplier changes count destination union rather than provider count or stock amount") {
    const GradientFieldKey key{GradientFieldFamily::Markets,0,0,0};GradientTargetCell cell;cell.buildingTeam=0;
    cell.occupyingSupplies[0]=1;CHECK(cell.goal(key));
    auto before=cell;cell.materials=1;cell.visibleTeams=1;CHECK(cell.goal(key));
    cell.occupyingSupplies[0]=0;CHECK(cell.goal(key)); // Natural goal retains membership.
    cell.materials=0;CHECK_FALSE(cell.goal(key));
    cell=before;cell.groundBlocked=true;CHECK_FALSE(cell.goal(key));
    cell.overlaySupplies[0]=1;CHECK(cell.goal(key)); // Overlay uses max after resource obstruction.
    cell.forbidden=1;CHECK_FALSE(cell.goal(key));
    auto air=key;air.swim=AIR_MOVEMENT_CLASS;CHECK(cell.goal(air));
    cell.overlaySupplies[0]=0;cell.airBlocked=true;CHECK_FALSE(cell.goal(air));
}
TEST_CASE("no-op derived cache rebuild retains union and reports no changes") {
    GradientMarketTargets::Footprints current{{2,{1,1}},{9,{0,3}}};unsigned changed=0;
    GradientMarketTargets::diff(current,current,[&](auto,auto,auto){++changed;});CHECK(changed==0);
    GradientMarketTargets cache;CHECK_FALSE(cache.initialized(0,0));
    auto& ledger=cache.ledgers[GradientMarketTargets::key(0,0)];ledger.cells=current;ledger.initialized=true;
    CHECK(cache.initialized(0,0));CHECK(cache.at(0,0,2).occupying==1);CHECK(cache.at(0,0,9).overlay==3);
}
}
