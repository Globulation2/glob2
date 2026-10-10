// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "RenderedCpuWindow.h"
#include <string>
#include <thread>
#include <vector>

namespace {
std::vector<char> readings;
std::uint64_t processReading=100,ownerReading=30,wallReading=1000,tidReading=77;
std::uint64_t processClock(){readings.push_back('p');return processReading;}
std::uint64_t ownerClock(){readings.push_back('o');return ownerReading;}
std::uint64_t wallClock(){readings.push_back('w');return wallReading;}
std::uint64_t tidClock(){readings.push_back('t');return tidReading;}
using Window=RenderedCpuDiagnostics::Window<unsigned>;
Window window(){readings.clear();processReading=100;ownerReading=30;wallReading=1000;tidReading=77;
    return Window(RenderedCpuDiagnostics::range(40,2,3,50),processClock,ownerClock,wallClock,tidClock);}
}
TEST_SUITE("RenderedCpuWindowHarness") {
TEST_CASE("fixed loaded-tick boundaries enclose metadata without repeated clocks") {
    auto w=window();unsigned metadataCalls=0;
    const auto metadata=[&]{readings.push_back('m');return ++metadataCalls;};
    w.onTick(41,metadata);CHECK(readings.empty());
    readings.clear();w.onTick(42,metadata);
    CHECK((readings==std::vector<char>{'m','t','o','w','p'}));
    CHECK(w.endpoints[0].tick==42);CHECK(w.endpoints[0].counters==1);
    readings.clear();w.onTick(42,metadata);w.onTick(43,metadata);w.onTick(44,metadata);
    CHECK(readings.empty());processReading=300;ownerReading=90;wallReading=6000;
    w.onTick(45,metadata);CHECK((readings==std::vector<char>{'p','w','o','t','m'}));
    readings.clear();w.onTick(46,metadata);CHECK(readings.empty());CHECK(metadataCalls==2);
    CHECK(w.processValid());CHECK(w.ownerValid());CHECK(w.wallValid());
    CHECK(w.endpoints[1].processCpuNs-w.endpoints[0].processCpuNs==200);
    CHECK(w.endpoints[1].ownerTid==77);CHECK(w.ticks.initial==40);
}
TEST_CASE("invalid clocks and skipped boundaries never produce valid CPU evidence") {
    for(unsigned fault=0;fault<4;++fault){auto w=window();
        if(fault==0)processReading=0;
        w.onTick(42,[]{return 1u;});
        processReading=fault==1?99:200;ownerReading=fault==2?0:50;wallReading=fault==3?999:2000;
        w.onTick(45,[]{return 2u;});
        CHECK(w.processValid()==(fault>=2));CHECK(w.ownerValid()==(fault!=2));CHECK(w.wallValid()==(fault!=3));
    }
    auto w=window();w.onTick(43,[]{return 1u;});w.onTick(45,[]{return 2u;});
    CHECK(w.missedBoundary);CHECK_FALSE(w.processValid());CHECK_FALSE(w.endpoints[0].captured);
    auto early=window();early.onTick(42,[]{return 1u;});CHECK_FALSE(early.processValid());
    auto missedEnd=window();missedEnd.onTick(42,[]{return 1u;});missedEnd.onTick(46,[]{return 2u;});
    CHECK(missedEnd.missedBoundary);CHECK_FALSE(missedEnd.processValid());CHECK_FALSE(missedEnd.endpoints[1].captured);
}
TEST_CASE("changing simulation owner rejects thread CPU while retaining process evidence") {
    auto w=window();w.onTick(42,[]{return 1u;});
    processReading=200;ownerReading=50;wallReading=2000;
    std::thread other([&]{w.onTick(45,[]{return 2u;});});other.join();
    CHECK(w.processValid());CHECK_FALSE(w.ownerValid());CHECK(w.ownerChanged);CHECK(w.endpoints[1].ownerTid==77);
}
TEST_CASE("changed native owner identity invalidates owner CPU despite reused TLS identity") {
    auto w=window();w.onTick(42,[]{return 1u;});
    processReading=200;ownerReading=50;wallReading=2000;tidReading=88;
    w.onTick(45,[]{return 2u;});
    CHECK_FALSE(w.ownerChanged);CHECK(w.processValid());CHECK_FALSE(w.ownerValid());
    CHECK(w.endpoints[0].ownerTid==77);CHECK(w.endpoints[1].ownerTid==88);
}
TEST_CASE("ranges reject wrapping negative zero or beyond-ending input") {
    CHECK(RenderedCpuDiagnostics::parseTicks("8192")==8192);
    for(const auto value:{"","0","-1","+1"," 1","1x","4294967296"})
        CHECK_THROWS_AS(RenderedCpuDiagnostics::parseTicks(value),std::invalid_argument);
    CHECK_THROWS_AS(RenderedCpuDiagnostics::range(40,2,9,50),std::invalid_argument);
    CHECK_THROWS_AS(RenderedCpuDiagnostics::range(51,1,1,50),std::invalid_argument);
    CHECK_THROWS_AS(RenderedCpuDiagnostics::range(0,0,1,50),std::invalid_argument);
    const auto r=RenderedCpuDiagnostics::range(40,2,8,50);CHECK(r.end==50);
}
}
