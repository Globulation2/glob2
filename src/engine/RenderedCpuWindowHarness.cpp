// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "RenderedCpuWindow.h"
#include "RenderedWarmDurations.h"
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

TEST_SUITE("RenderedWarmDurationsHarness") {
using Capture=RenderedCpuDiagnostics::WarmDurations;
namespace {
std::uint64_t durationClockValue=100;
unsigned durationClockReads=0,actualTickReads=0;
std::uint64_t durationClock(){++durationClockReads;return durationClockValue;}
std::uint64_t actualTick(const void* tick)noexcept{++actualTickReads;return *static_cast<const std::uint64_t*>(tick);}
struct TelemetryBinding {
    PerformanceTelemetry::Collector* previous;
    explicit TelemetryBinding(PerformanceTelemetry::Collector& local):previous(&PerformanceTelemetry::collector())
        {PerformanceTelemetry::bindCollector(&local);}
    ~TelemetryBinding(){PerformanceTelemetry::bindCollector(previous);}
};
void completedTick(Capture& capture,std::uint64_t& tick,bool successful=true) {
    Capture::Advance advance(&capture,tick);
    PerformanceTelemetry::Scope loop(PerformanceTelemetry::Id::Loop);
    ++durationClockValue;
    PerformanceTelemetry::Scope work(PerformanceTelemetry::Id::Work);
    ++durationClockValue;
    {PerformanceTelemetry::Scope sync(PerformanceTelemetry::Id::Tick);durationClockValue+=5;++tick;}
    ++durationClockValue;work.stop();++durationClockValue;loop.stop();
    if(successful)advance.completed();
}
}
TEST_CASE("scope endpoints use existing clocks and actual successful ticks only") {
    std::uint64_t tick=40;actualTickReads=0;durationClockValue=100;durationClockReads=0;
    Capture capture({40,42,45},actualTick,&tick);
    PerformanceTelemetry::Collector owner;owner.clock=durationClock;owner.cpuClock=nullptr;
    owner.enableDurationDistributions(true);owner.distributions->ownerSink=&capture;
    TelemetryBinding binding(owner);
    for(unsigned i=0;i<6;++i)completedTick(capture,tick);
    CHECK(durationClockReads==36); // Three scopes, same original start/end reads.
    REQUIRE(capture.owner.count==3);
    CHECK(capture.owner.samples[0].tick==43);CHECK(capture.owner.samples[2].tick==45);
    CHECK(capture.owner.samples[0].scopes[0].duration==9);
    CHECK(capture.owner.samples[0].scopes[1].duration==7);
    CHECK(capture.owner.samples[0].scopes[2].duration==5);
    // Standalone Game::syncStep-like scopes receive no synthetic tick label.
    const auto beforeReads=actualTickReads;
    {PerformanceTelemetry::Scope unrelated(PerformanceTelemetry::Id::Tick);++durationClockValue;}
    CHECK(actualTickReads==beforeReads);CHECK(capture.owner.unassociated==1);
    const auto summary=capture.summarize(118,145,true);
    CHECK(summary->ownerValid);CHECK(summary->ownerRecords==3);CHECK(summary->missingTicks==0);
    CHECK_FALSE(summary->frameValid);CHECK_FALSE(summary->valid);
}
TEST_CASE("idle failed duplicate missing and mismatched scopes do not create accepted records") {
    for(unsigned fault=0;fault<5;++fault) {
        std::uint64_t tick=42;Capture capture({40,42,43},actualTick,&tick);
        {
            Capture::Advance advance(&capture,tick);
            if(fault!=0)++tick; // Idle advance never gains a tick tag.
            if(fault==4)++tick; // An unexpected two-tick call also gains none.
            capture.duration(PerformanceTelemetry::Id::Tick,100,105,5);
            if(fault==2)capture.duration(PerformanceTelemetry::Id::Tick,100,105,5);
            if(fault!=3)capture.duration(PerformanceTelemetry::Id::Work,99,106,7);
            capture.duration(PerformanceTelemetry::Id::Loop,98,107,9);
            if(fault!=1)advance.completed();
        }
        CHECK_FALSE(capture.owner.active);CHECK(capture.owner.expected==0);
        CHECK(capture.owner.count==0);CHECK_FALSE(capture.summarize(90,110,true)->ownerValid);
        // A later unrelated callback cannot inherit the aborted association.
        const auto beforeReads=actualTickReads;
        capture.duration(PerformanceTelemetry::Id::Tick,110,115,5);
        CHECK(actualTickReads==beforeReads);
    }
}
TEST_CASE("an exception after a completed syncStep clears association while scope destructors unwind") {
    std::uint64_t tick=42;durationClockValue=100;
    Capture capture({40,42,43},actualTick,&tick);
    PerformanceTelemetry::Collector local;local.clock=durationClock;local.cpuClock=nullptr;
    local.enableDurationDistributions(true);local.distributions->ownerSink=&capture;
    TelemetryBinding binding(local);
    try {
        Capture::Advance advance(&capture,tick);
        PerformanceTelemetry::Scope loop(PerformanceTelemetry::Id::Loop);
        ++durationClockValue;PerformanceTelemetry::Scope work(PerformanceTelemetry::Id::Work);
        ++durationClockValue;PerformanceTelemetry::Scope sync(PerformanceTelemetry::Id::Tick);
        durationClockValue+=5;++tick;
        throw std::runtime_error("failed required advance");
    }catch(const std::runtime_error&){}
    CHECK_FALSE(capture.owner.active);CHECK(capture.owner.expected==0);
    CHECK(capture.owner.abortedSelected==1);CHECK(capture.owner.count==0);
    const auto previousReads=actualTickReads;
    {PerformanceTelemetry::Scope unrelated(PerformanceTelemetry::Id::Tick);++durationClockValue;}
    CHECK(actualTickReads==previousReads);CHECK(capture.owner.unassociated==1);
    CHECK_FALSE(capture.summarize(90,120,true)->ownerValid);
}
TEST_CASE("presentation intervals survive telemetry mode reset and producer mailbox transfer") {
    std::uint64_t tick=40;Capture capture({40,42,43},actualTick,&tick,8);
    PerformanceTelemetry::Collector main;main.clock=durationClock;main.cpuClock=nullptr;
    main.enableDurationDistributions(true);main.distributions->presentationSink=&capture;
    actualTickReads=0;durationClockReads=0;
    durationClockValue=100;main.presented();
    main.configure(40,0,0,"mode-change"); // Resets legacy lastPresentation.
    durationClockValue=300;main.presented();
    durationClockValue=400;main.presented();
    durationClockValue=500;main.presented();
    durationClockValue=600;main.presented();
    CHECK(actualTickReads==0);CHECK(durationClockReads==5);
    REQUIRE(capture.presentation.count==4);
    CHECK(capture.presentation.samples[0].start==100);CHECK(capture.presentation.samples[0].end==300);
    auto summary=capture.summarize(250,550,true);
    CHECK(summary->frame.count==2);CHECK(summary->frameBoundary==2);CHECK(summary->frameOutside==0);
    summary=capture.summarize(300,500,true);
    CHECK(summary->frame.count==2);CHECK(summary->frameBoundary==0);CHECK(summary->frameOutside==2);
    PerformanceTelemetry::Collector mailbox;mailbox.enableDurationDistributions(true);
    mailbox.absorb(main);main.absorb(mailbox); // No event callbacks on merging.
    CHECK(capture.presentation.count==4);CHECK(actualTickReads==0);
    durationClockValue=0;main.presented();durationClockValue=650;main.presented();
    CHECK(capture.presentation.invalid==1);CHECK_FALSE(capture.summarize(300,500,true)->frameValid);
}
TEST_CASE("overflow and reversed clock invalidate instead of hiding frame tails") {
    std::uint64_t tick=40;Capture capture({40,42,43},actualTick,&tick,1);
    capture.presented(100);capture.presented(200);capture.presented(300);
    CHECK(capture.presentation.overflow==1);CHECK_FALSE(capture.summarize(100,300,true)->frameValid);
    capture.presented(250);CHECK(capture.presentation.invalid==1);
    Capture reversed({40,42,43},actualTick,&tick);
    tick=42;{Capture::Advance advance(&reversed,tick);++tick;
        reversed.duration(PerformanceTelemetry::Id::Tick,100,99,0);
        reversed.duration(PerformanceTelemetry::Id::Work,99,110,11);
        reversed.duration(PerformanceTelemetry::Id::Loop,98,111,13);advance.completed();}
    CHECK(reversed.owner.invalid==1);CHECK(reversed.owner.count==0);
    CHECK_FALSE(reversed.summarize(90,120,true)->ownerValid);
    Capture intermediate({40,42,44},actualTick,&tick);
    tick=42;{Capture::Advance advance(&intermediate,tick);++tick;
        intermediate.duration(PerformanceTelemetry::Id::Tick,100,120,20);
        // Each interval alone is nonnegative, but the next clock went backward.
        intermediate.duration(PerformanceTelemetry::Id::Work,99,119,20);
        intermediate.duration(PerformanceTelemetry::Id::Loop,98,121,23);advance.completed();}
    CHECK(intermediate.owner.invalid==1);CHECK(intermediate.owner.count==0);
    CHECK_FALSE(intermediate.summarize(90,130,true)->ownerValid);
}
TEST_CASE("separate owner and presentation producers can run concurrently then join") {
    std::uint64_t tick=42;Capture capture({40,42,43},actualTick,&tick);
    std::thread simulation([&]{Capture::Advance advance(&capture,tick);++tick;
        capture.duration(PerformanceTelemetry::Id::Tick,120,150,30);
        capture.duration(PerformanceTelemetry::Id::Work,110,160,50);
        capture.duration(PerformanceTelemetry::Id::Loop,100,170,70);advance.completed();});
    capture.presented(90);capture.presented(180);capture.presented(200);
    simulation.join();
    const auto summary=capture.summarize(95,190,true);
    CHECK(summary->ownerValid);CHECK(summary->frameBoundary==2);CHECK_FALSE(summary->frameValid);
}
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
