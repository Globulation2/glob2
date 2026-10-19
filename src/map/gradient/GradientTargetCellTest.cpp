// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "GradientTargetCell.h"

namespace {
GradientFieldKey key(GradientFieldFamily family,unsigned movement=0) {
    return {family,0,std::uint8_t(movement),family==GradientFieldFamily::Materials
        || family==GradientFieldFamily::Markets ? std::uint8_t(0) : GradientFieldKey::NoMaterial};
}
}
TEST_SUITE("GradientTargetCell") {
TEST_CASE("natural and clearing goals precede route obstacles but respect forbidden paint") {
    GradientTargetCell cell;cell.materials=1;cell.visibleTeams=1;
    cell.groundBlocked=true;cell.buildingTeam=0;cell.clearable=true;cell.clear=1;
    CHECK(cell.goal(key(GradientFieldFamily::Materials)));
    CHECK(cell.goal(key(GradientFieldFamily::Clear)));
    cell.immobile=true;
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Materials)));
    CHECK(cell.goal(key(GradientFieldFamily::Clear)));
    cell.forbidden=1;
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Clear)));
    CHECK(cell.goal(key(GradientFieldFamily::Materials,AIR_MOVEMENT_CLASS)));
    CHECK(cell.goal(key(GradientFieldFamily::Clear,AIR_MOVEMENT_CLASS)));
}
TEST_CASE("market destinations are the natural and supplier tile union") {
    GradientTargetCell cell;cell.visibleTeams=1;cell.buildingTeam=0;
    cell.occupyingSupplies[0]=1;
    CHECK(cell.goal(key(GradientFieldFamily::Markets)));
    cell.groundBlocked=true;
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Markets)));
    cell.overlaySupplies[0]=1;
    CHECK(cell.goal(key(GradientFieldFamily::Markets)));
    cell.overlaySupplies[0]=0;cell.occupyingSupplies[0]=0;cell.materials=1;
    CHECK(cell.goal(key(GradientFieldFamily::Markets)));
    cell.visibleTeams=0;
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Markets)));
}
TEST_CASE("guard membership uses real movement eligibility and ally building restrictions") {
    GradientTargetCell cell;cell.guard=1;cell.openClasses=1;
    CHECK(cell.goal(key(GradientFieldFamily::Guard)));
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Guard,1)));
    cell.buildingTeam=1; // Enemy structures are permitted by the guard seeder.
    CHECK(cell.goal(key(GradientFieldFamily::Guard)));
    cell.guardBuildingBlockedTeams=1;
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Guard)));
}
TEST_CASE("escape targets are eligible outside tiles rather than painted or neighbor counts") {
    GradientTargetCell cell;cell.openClasses=(1u<<0)|(1u<<AIR_MOVEMENT_CLASS);
    CHECK(cell.goal(key(GradientFieldFamily::Forbidden)));
    cell.forbidden=1;
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Forbidden)));
    CHECK(cell.goal(key(GradientFieldFamily::Forbidden,AIR_MOVEMENT_CLASS)));
    cell.airBlocked=true;
    CHECK_FALSE(cell.goal(key(GradientFieldFamily::Forbidden,AIR_MOVEMENT_CLASS)));
}
}
