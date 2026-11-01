// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "AIMaximaPrestige.h"
#include <climits>
using namespace AIMaxima::Prestige;
TEST_SUITE("Maxima.Prestige")
{
TEST_CASE("race decisions respect activation readiness and emergency")
{
    const std::vector<Standing> scores{{0,true,false},{0,true,false}};
    const auto run=[&](bool enabled,bool emergency,bool available,bool ready) {
        return assess(scores,0,0,500,enabled,emergency,50,available,ready,0,30,45,35,100);
    };
    CHECK(run(false,false,true,true).reason==Disabled);
    CHECK(run(true,true,true,true).reason==Emergency);
    CHECK(run(true,false,false,true).reason==Unavailable);
    CHECK(run(true,false,true,false).state==Prepare);
    CHECK(run(true,false,true,false).priority==30);
    CHECK(run(true,false,true,true).state==Invest);
    CHECK(run(true,false,true,true).priority==45);
}
TEST_CASE("catch up uses the shared threshold and accepts tied wins")
{
    const std::vector<Standing> behind{{100,true,false},{200,true,false}};
    auto a=assess(behind,0,300,500,true,false,50,true,true,50,30,45,35,100);
    CHECK(a.catchUpSteps==2);CHECK(a.finishSteps==4);CHECK(a.state==Invest);CHECK(a.pending==50);
    CHECK_FALSE(safe(behind,0,450,500,50));
    const std::vector<Standing> tied{{150,true,false},{200,true,false}};
    CHECK(safe(tied,0,450,500,50));
    a=assess(tied,0,450,500,true,false,50,true,true,0,30,45,35,100);
    CHECK(a.state==Finish);CHECK(a.priority==100);
    a=assess(behind,0,450,500,true,false,50,true,true,0,30,45,35,100);
    CHECK(a.reason==CannotCatchUp);CHECK(a.priority==0);
}
TEST_CASE("only surviving individual leaders win and alliance sharing is explicit")
{
    std::vector<Standing> scores{{0,true,false},{200,true,false},{250,true,false},{900,false,false}};
    CHECK_FALSE(wins(scores,0,50));
    scores[2].sharesVictory=true;
    CHECK(wins(scores,0,50));
    // Two allied scores are never summed against another team's score.
    scores={{100,true,false},{250,true,false},{100,true,true}};
    CHECK_FALSE(wins(scores,0,50));
    CHECK(wins(scores,0,150));
}
TEST_CASE("catalog increments and wide arithmetic determine economic feasibility")
{
    std::vector<Standing> scores{{60,true,false},{150,true,false}};
    auto a=assess(scores,0,210,300,true,false,30,true,true,0,30,45,35,100);
    CHECK(a.catchUpSteps==3);CHECK(a.finishSteps==3);CHECK(a.state==Invest);
    CHECK(safe(scores,0,270,300,90));
    scores={{INT_MAX-1,true,false},{INT_MAX,true,false}};
    CHECK(wins(scores,0,100));
    CHECK(safe(scores,0,INT_MAX-1,INT_MAX,100));
}
}
