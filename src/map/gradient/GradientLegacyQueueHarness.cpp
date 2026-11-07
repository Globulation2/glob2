// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"
#include "gradient/MapRouting.h"
#include "HeadlessGame.h"
#include "GameFixtures.h"
#include "GradientRuntime.h"
#include "MapInternal.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <algorithm>
#include <memory>

namespace {
std::string checkpoint(Game& game) {
    auto* storage=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(storage);
    glob2test::discardPrematureHiringSuggestions(game);game.save(&out,false,"legacy overlapping map jobs");out.flush();return storage->takeContents();
}
bool restore(Game& game,const std::string& bytes) {
    auto* storage=new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size());storage->seekFromStart(0);GAGCore::BinaryInputStream in(storage);return game.load(&in);
}
}
TEST_CASE("grandfathered repeated legacy field survives first publication and a second checkpoint" * doctest::test_suite("GradientLegacyQueue")) {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame original({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=original.game.map;const GradientFieldKey key{GradientFieldFamily::Guard,0,0};
    map.routing().configureGradientPipeline(0,8);auto** slot=map.routing().gradientFieldDestination(key);
    *slot=new Uint16[map.size];std::fill_n(*slot,map.size,GRADIENT_UNREACHABLE);
    auto first=std::make_unique<Uint16[]>(map.size),last=std::make_unique<Uint16[]>(map.size);
    std::fill_n(first.get(),map.size,GRADIENT_UNREACHABLE);std::fill_n(last.get(),map.size,GRADIENT_UNREACHABLE);
    const auto a=map.coordToIndex(8,8),b=map.coordToIndex(12,12);first[a]=GRADIENT_AT_GOAL;last[b]=GRADIENT_AT_GOAL;
    // This is exactly the completed-output handoff used by the <=177 reader.
    // Short old visitation rounds could submit one slot again before its first
    // result was due. New scheduling never creates such associations.
    map.routing().gradientRuntime->pipeline.restoreCompleted({slot,0,2,false,std::move(first)});
    map.routing().gradientRuntime->pipeline.restoreCompleted({slot,0,4,false,std::move(last)});
    map.routing().gradientRuntime->pipeline.associateLegacy([&](auto* destination){return map.routing().gradientFieldKey(destination);});
    map.routing().gradientRuntime->adaptive.enroll(key).inflight=true;
    const auto bytes=checkpoint(original.game);
    glob2test::HeadlessGame loaded({.loadDefaultRace=true,.header=true});REQUIRE(restore(loaded.game,bytes));
    auto& resumed=loaded.game.map;CHECK(resumed.routing().gradientRuntime->pipeline.pendingCount()==2);
    resumed.routing().advanceGradientPipeline();resumed.routing().advanceGradientPipeline();
    CHECK((*resumed.routing().gradientFieldDestination(key))[a]==GRADIENT_AT_GOAL);
    CHECK(resumed.routing().gradientRuntime->pipeline.pendingCount()==1);
    CHECK(resumed.routing().gradientRuntime->adaptive.fields.at(key).inflight);
    CHECK_FALSE(resumed.routing().gradientRuntime->adaptive.fields.at(key).serving.known);
    const auto between=checkpoint(loaded.game);
    glob2test::HeadlessGame final({.loadDefaultRace=true,.header=true});REQUIRE(restore(final.game,between));
    final.game.map.routing().advanceGradientPipeline();final.game.map.routing().advanceGradientPipeline();
    CHECK((*final.game.map.routing().gradientFieldDestination(key))[b]==GRADIENT_AT_GOAL);
    CHECK(final.game.map.routing().gradientRuntime->pipeline.pendingCount()==0);
    CHECK_FALSE(final.game.map.routing().gradientRuntime->adaptive.fields.at(key).inflight);
    CHECK_FALSE(final.game.map.routing().gradientRuntime->adaptive.fields.at(key).serving.known);
}
