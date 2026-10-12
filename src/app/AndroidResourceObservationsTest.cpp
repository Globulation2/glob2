// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "AndroidResourceObservations.h"
using namespace resource_policy;
TEST_SUITE("AndroidResourceObservations") {
TEST_CASE("packet bounds and future thermal status never imply known healthy") {
    CHECK_FALSE(decodeAndroidResourceObservations(nullptr,3).batterySaver);
    const int good[]={1,0,0}, wrong[]={2,1,6}, future[]={1,2,7}, absent[]={1,-1,-2};
    auto v=decodeAndroidResourceObservations(good,3);
    REQUIRE(v.batterySaver.has_value()); CHECK_FALSE(*v.batterySaver);
    CHECK(v.recognizedThermalStatus==0);
    CHECK_FALSE(decodeAndroidResourceObservations(good,2).rawThermalStatus);
    CHECK_FALSE(decodeAndroidResourceObservations(good,4).rawThermalStatus);
    CHECK_FALSE(decodeAndroidResourceObservations(wrong,3).batterySaver);
    v=decodeAndroidResourceObservations(future,3);
    CHECK_FALSE(v.batterySaver); CHECK(v.rawThermalStatus==7); CHECK_FALSE(v.recognizedThermalStatus);
    v=decodeAndroidResourceObservations(absent,3); CHECK_FALSE(v.batterySaver); CHECK_FALSE(v.rawThermalStatus);
    const int hot[]={1,1,6}; v=decodeAndroidResourceObservations(hot,3);
    CHECK(v.batterySaver==true); CHECK(v.recognizedThermalStatus==6);
}
TEST_CASE("caller saver precedence and battery use remain separate") {
    RuntimeConstraints input; input.onBattery=true;
    AndroidResourceObservations observed; observed.batterySaver=false;
    auto v=mergeAndroidResourceObservations(input,observed);
    CHECK(v.onBattery==true); CHECK(v.batterySaver==false); CHECK_FALSE(input.batterySaver);
    for(bool caller : {false,true}) {
        input.batterySaver=caller; observed.batterySaver=!caller;
        v=mergeAndroidResourceObservations(input,observed); CHECK(v.batterySaver==caller);
    }
    input.onBattery=false; input.batterySaver.reset(); observed.batterySaver=true;
    v=mergeAndroidResourceObservations(input,observed); CHECK(v.onBattery==false); CHECK(v.batterySaver==true);
}
}
