// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ThreadCpuEnvelope.h"
#include <future>
#include <chrono>
#include <vector>

namespace
{
struct EnvelopeClocks {
    std::uint64_t tid=1,handle=1,processCpu=100,selfCpu=0;
    std::array<glob2::CpuClockSample,300> cpu{};
    unsigned reads=0,processReads=0;
    bool processValid=true;
    std::promise<void>* entered=nullptr;
    std::shared_future<void> release;
    glob2::CpuClockReader reader() {
        return {this,[](void* p) noexcept {
            auto& f=*static_cast<EnvelopeClocks*>(p);++f.processReads;return glob2::CpuClockSample{f.processCpu,f.processValid};
        },[](void* p,std::int64_t h) noexcept {
            auto& f=*static_cast<EnvelopeClocks*>(p);++f.reads;
            if(f.entered){f.entered->set_value();f.release.wait();f.entered=nullptr;}
            return f.cpu[std::size_t(h)];
        },[](void* p) noexcept {
            auto& f=*static_cast<EnvelopeClocks*>(p);return glob2::CpuThreadClock{f.tid,1,std::int64_t(f.handle),true};
        },[](void* p) noexcept {
            auto& f=*static_cast<EnvelopeClocks*>(p);return glob2::CpuClockSample{f.selfCpu++,true};
        },1};
    }
};
void running(glob2::ProcessCpuEnvelope& window) {
    for(unsigned i=0;i<4;++i)window.advance();
    REQUIRE(window.progress()==glob2::ProcessCpuEnvelope::Progress::Running);
}
void complete(glob2::ProcessCpuEnvelope& window) {
    REQUIRE(window.finish());for(unsigned i=0;i<4;++i)window.advance();
}
}
TEST_SUITE("ThreadCpuEnvelope")
{
TEST_CASE("process envelope conserves unidentified CPU and deduplicates role aliases")
{
    using namespace glob2;EnvelopeClocks f;f.cpu[1]={50,true};CpuClockRegistry registry(f.reader());
    auto owner=registry.registerCurrent(CpuThreadRole::Owner);
    auto alias=registry.registerCurrent(CpuThreadRole::OtherOwned);REQUIRE(owner);REQUIRE(alias);
    ProcessCpuEnvelope window(registry);CHECK(f.processReads==1);CHECK_FALSE(window.finish());running(window);
    f.cpu[1]={450,true};f.processCpu=1100;complete(window);
    const auto& m=window.metrics();REQUIRE(m.valid);CHECK_FALSE(m.attributionComplete);
    CHECK(m.processCpuNs==1000);CHECK(m.knownInnerCpuNs==400);CHECK(m.unknownUpperCpuNs==600);
    REQUIRE(m.threadCount==1);CHECK(m.threads[0].tid==1);CHECK(m.threads[0].roles==9);
    CHECK(m.samplerCpuNs>0);CHECK(m.processCpuNs==m.knownInnerCpuNs+m.unknownUpperCpuNs);
    alias.reset();CHECK(owner);CHECK(window.advance()==ProcessCpuEnvelope::Progress::Complete);
}
TEST_CASE("retired reused and newly started thread CPU remains conservatively unidentified")
{
    using namespace glob2;EnvelopeClocks f;f.cpu[1]={10,true};CpuClockRegistry registry(f.reader());
    auto old=registry.registerCurrent(CpuThreadRole::Worker);ProcessCpuEnvelope window(registry);running(window);
    old.reset();f.handle=2;f.cpu[2]={5000,true};auto reused=registry.registerCurrent(CpuThreadRole::Worker);
    f.tid=2;f.handle=3;f.cpu[3]={900,true};auto added=registry.registerCurrent(CpuThreadRole::Coordinator);
    f.processCpu=1100;complete(window);const auto& m=window.metrics();
    REQUIRE(m.valid);CHECK(m.churn);CHECK(m.threadCount==0);CHECK(m.knownInnerCpuNs==0);
    CHECK(m.unknownUpperCpuNs==1000);CHECK(m.omittedThreads==2);CHECK_FALSE(m.attributionComplete);
}
TEST_CASE("unavailable thread clocks stay unidentified while failed process clocks invalidate")
{
    using namespace glob2;
    for(unsigned failure=0;failure<3;++failure) {
        EnvelopeClocks f;f.cpu[1]={50,true};CpuClockRegistry registry(f.reader());
        auto owner=registry.registerCurrent(CpuThreadRole::Owner);ProcessCpuEnvelope window(registry);running(window);
        f.processCpu=1100;f.cpu[1]=failure==0 ? CpuClockSample{} : CpuClockSample{40,true};
        if(failure==2)f.processValid=false;
        complete(window);const auto& m=window.metrics();CHECK_FALSE(m.attributionComplete);
        if(failure<2){CHECK(m.valid);CHECK(m.knownInnerCpuNs==0);CHECK(m.unknownUpperCpuNs==1000);CHECK(m.omittedThreads==1);}
        else {CHECK_FALSE(m.valid);CHECK(window.progress()==ProcessCpuEnvelope::Progress::Invalid);}
    }
    CpuClockRegistry unavailable(CpuClockReader{});CHECK_FALSE(unavailable.registerCurrent(CpuThreadRole::Owner));
    ProcessCpuEnvelope disabled(unavailable);CHECK(disabled.progress()==ProcessCpuEnvelope::Progress::Invalid);
}
TEST_CASE("inconsistent process and thread clocks never hide undercount by clamping")
{
    using namespace glob2;EnvelopeClocks f;f.cpu[1]={0,true};CpuClockRegistry registry(f.reader());
    auto owner=registry.registerCurrent(CpuThreadRole::Owner);ProcessCpuEnvelope window(registry);running(window);
    f.cpu[1]={1001,true};f.processCpu=1100;complete(window);
    CHECK_FALSE(window.metrics().valid);CHECK_FALSE(window.metrics().attributionComplete);
    CHECK(window.progress()==ProcessCpuEnvelope::Progress::Invalid);
}
TEST_CASE("process clock reversal and known thread sum overflow invalidate the window")
{
    using namespace glob2;
    for(unsigned overflow=0;overflow<2;++overflow) {
        EnvelopeClocks f;f.cpu[1]={0,true};CpuClockRegistry registry(f.reader());
        auto first=registry.registerCurrent(CpuThreadRole::Worker);f.tid=f.handle=2;f.cpu[2]={0,true};
        auto second=registry.registerCurrent(CpuThreadRole::Worker);ProcessCpuEnvelope window(registry);running(window);
        f.processCpu=overflow ? UINT64_MAX : 99;
        if(overflow){f.cpu[1]={UINT64_MAX,true};f.cpu[2]={1,true};}
        complete(window);CHECK_FALSE(window.metrics().valid);CHECK_FALSE(window.metrics().attributionComplete);
        CHECK(window.progress()==ProcessCpuEnvelope::Progress::Invalid);
    }
}
TEST_CASE("fixed registry overflow and sixty four slot advances cannot lose unknown CPU")
{
    using namespace glob2;EnvelopeClocks f;CpuClockRegistry registry(f.reader());
    std::vector<CpuClockRegistry::Lease> leases;
    for(unsigned i=1;i<=CpuClockRegistry::MaxThreads;++i) {
        f.tid=f.handle=i;f.cpu[i]={0,true};leases.push_back(registry.registerCurrent(CpuThreadRole::Worker));REQUIRE(leases.back());
    }
    f.tid=f.handle=257;CHECK_FALSE(registry.registerCurrent(CpuThreadRole::Worker));
    ProcessCpuEnvelope window(registry);window.advance();CHECK(f.reads==64);
    for(unsigned i=0;i<3;++i)window.advance();REQUIRE(window.progress()==ProcessCpuEnvelope::Progress::Running);
    for(unsigned i=1;i<=CpuClockRegistry::MaxThreads;++i)f.cpu[i]={1,true};
    f.processCpu=1100;complete(window);const auto& m=window.metrics();REQUIRE(m.valid);
    CHECK(m.threadCount==256);CHECK(m.knownInnerCpuNs==256);CHECK(m.unknownUpperCpuNs==744);
    CHECK(CpuClockRegistry::storageBytes()==sizeof registry);CHECK(ProcessCpuEnvelope::storageBytes()==sizeof window);
}
TEST_CASE("clock read and retirement synchronize before a native thread could exit")
{
    using namespace glob2;EnvelopeClocks f;f.cpu[1]={0,true};CpuClockRegistry registry(f.reader());
    auto owner=registry.registerCurrent(CpuThreadRole::Owner);ProcessCpuEnvelope window(registry);
    std::promise<void> entered,release;f.entered=&entered;f.release=release.get_future().share();
    auto read=std::async(std::launch::async,[&]{return window.advance();});
    const auto arrived=entered.get_future().wait_for(std::chrono::seconds(5));
    if(arrived!=std::future_status::ready)release.set_value();REQUIRE(arrived==std::future_status::ready);
    auto retire=std::async(std::launch::async,[&]{owner.reset();});
    CHECK(retire.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout);
    release.set_value();read.get();retire.get();
    for(unsigned i=0;i<3;++i)window.advance();f.processCpu=1100;complete(window);
    CHECK(window.metrics().valid);CHECK(window.metrics().knownInnerCpuNs==0);
    CHECK(window.metrics().unknownUpperCpuNs==1000);CHECK(window.metrics().churn);
}
}
