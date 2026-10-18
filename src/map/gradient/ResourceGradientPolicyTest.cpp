// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceGradientPolicy.h"
#include "ResourceGradientExploration.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include <vector>

TEST_SUITE("ResourceGradientPolicy") {
TEST_CASE("rolling demand matches exact query windows including tick wrap") {
    for (unsigned start:{0u,UINT32_MAX-64}) {
        ResourceGradientPolicy::Field f;std::vector<unsigned> queries;
        for (unsigned elapsed=0;elapsed<500;++elapsed) {
            const unsigned tick=start+elapsed;
            const int count=elapsed%13<7 ? elapsed%9 : 0;
            for (int i=0;i<count;++i) {f.queried(tick,true);queries.push_back(tick);}
            auto copy=f;const auto x=copy.inputs(tick,{});
            unsigned n32=0,n128=0;
            for (auto q:queries) {n32+=unsigned(tick-q)<32;n128+=unsigned(tick-q)<128;}
            CHECK(x[1]==n32);CHECK(x[2]==n128);
        }
        f.advance(start+1000);CHECK(f.count32==0);CHECK(f.count128==0);CHECK(f.historyHash==0);
    }
}
TEST_CASE("minimum spacing maximum age and source turnover are deterministic") {
    ResourceGradientPolicy p;p.initialized=true;p.sources[0].count=10;
    p.stockChanged(0,1);p.stockChanged(1,0);
    CHECK(p.sources[0].count==10);CHECK(p.sources[0].added==1);CHECK(p.sources[0].removed==1);
    auto& f=p.fields[0];f.serving={0,{10,0,0}};f.submitted=0;
    auto x=f.inputs(7,p.sources[0]);CHECK(x[3]==1);CHECK(x[4]==1);CHECK(x[5]==409);CHECK(x[6]==409);
    f.requested=true;CHECK_FALSE(f.refresh(7,p.sources[0]));
    f.requested=true;CHECK(f.refresh(128,p.sources[0]));
    CHECK_FALSE(f.refresh(129,p.sources[0])); // no new movement demand
}
TEST_CASE("binary and text continuation retain query windows capture epochs and pending demand") {
    for (bool text:{false,true}) {
        ResourceGradientPolicy p;p.initialized=true;p.sources[0]={100,20,30};
        auto& f=p.fields[0];f.serving={10,{90,10,30}};f.submitted=16;
        f.queried(20,true);f.queried(40,true);f.queried(40,true);
        const auto before=p.checksum();
        auto* backend=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> out(text ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend)) : new GAGCore::BinaryOutputStream(backend));
        p.save(out.get());out->flush();
        auto memory=std::make_unique<GAGCore::MemoryStreamBackend>(backend->getBuffer(),backend->getPosition());memory->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> in(text ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(memory.get())) : new GAGCore::BinaryInputStream(memory.release()));
        ResourceGradientPolicy restored;restored.load(in.get(),1);
        CHECK(restored.checksum()==before);
        for (unsigned tick=40;tick<200;++tick) {
            if (tick%3==0) {p.fields[0].queried(tick,true);restored.fields[0].queried(tick,true);}
            CHECK(p.fields[0].inputs(tick,p.sources[0])==restored.fields[0].inputs(tick,restored.sources[0]));
            CHECK(p.fields[0].refresh(tick,p.sources[0])==restored.fields[0].refresh(tick,restored.sources[0]));
        }
    }
}
}

TEST_SUITE("ResourceGradientExploration") {
TEST_CASE("interval strata are validated and assigned without consuming game randomness") {
    CHECK((ResourceGradientExploration::parseIntervals("0,32,128,512")==std::vector<unsigned>{0,32,128,512}));
    for (const auto* invalid:{"","8,","-1","1","7","65537","32,32","32, 64","32x"})
        CHECK_THROWS_AS(ResourceGradientExploration::parseIntervals(invalid),std::invalid_argument);
    ResourceGradientExploration first,second;
    first.intervals=second.intervals={0,32,128,512};first.seed=second.seed=123456;
    unsigned changed=0;std::array<unsigned,4> counts{};
    for (int key=0;key<1024;++key) {
        const auto interval=first.intervalFor(key);
        CHECK(interval==second.intervalFor(key));
        ++counts[std::find(first.intervals.begin(),first.intervals.end(),interval)-first.intervals.begin()];
        second.seed=654321;changed+=interval!=second.intervalFor(key);second.seed=first.seed;
    }
    for (auto count:counts) CHECK(count>150);
    CHECK(changed>500);
}
TEST_CASE("fixed interval exploration bypasses model and production cap but retains demand") {
    ResourceGradientExploration config;config.intervals={512};
    ResourceGradientPolicy::Field field;field.submitted=0;
    field.requested=true;CHECK(config.refresh(0,field,128,{})==nullptr);
    field.requested=true;CHECK(config.refresh(0,field,511,{})==nullptr);
    CHECK(config.refresh(0,field,512,{})==nullptr); // idle fields do no work
    field.requested=true;CHECK(std::string_view(config.refresh(0,field,512,{}))=="fixed_interval");
    CHECK_FALSE(field.requested);
}
TEST_CASE("adaptive stratum is exactly the official refresh decision including state") {
    ResourceGradientExploration config;config.intervals={0};
    ResourceGradientPolicy::Field explored,official;
    for (unsigned tick=0;tick<600;++tick) {
        if (tick%3==0) {explored.queried(tick,true);official.queried(tick,true);}
        const bool accepted=config.refresh(19,explored,tick,{})!=nullptr;
        CHECK(accepted==official.refresh(tick,{}));
        CHECK(explored.requested==official.requested);
        CHECK(explored.count32==official.count32);
        CHECK(explored.count128==official.count128);
        CHECK(explored.queryTick==official.queryTick);
        if (accepted) explored.submitted=official.submitted=tick;
    }
}
TEST_CASE("checkpoint branch waits for one demanded admission and then restores baseline") {
    ResourceGradientExploration config;config.forkField=7;config.forkStart=100;config.forkWait=256;
    ResourceGradientPolicy::Field field;field.submitted=99;
    for (unsigned tick=100;tick<356;++tick) {
        field.requested=true;CHECK(config.refresh(7,field,tick,{})==nullptr);
    }
    CHECK(config.refresh(7,field,356,{})==nullptr); // not a synthetic movement query
    field.requested=true;CHECK(std::string_view(config.refresh(7,field,356,{}))=="checkpoint_fork");
    config.record(356,7,"checkpoint_fork",false);CHECK_FALSE(config.forkComplete);
    field.requested=true;CHECK(config.refresh(7,field,357,{})!=nullptr);
    config.record(357,7,"checkpoint_fork",true);CHECK(config.forkComplete);
    field.submitted=357;field.requested=true;CHECK(config.refresh(7,field,358,{})==nullptr);
    field.requested=true;CHECK(std::string_view(config.refresh(7,field,485,{}))=="age_cap");
}
}
