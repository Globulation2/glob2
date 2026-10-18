// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourceGradientPolicy.h"
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
