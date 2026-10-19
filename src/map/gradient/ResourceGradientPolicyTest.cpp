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
TEST_CASE("movement baselines follow capture rather than delayed publication") {
    ResourceGradientPolicy::Field field;
    GradientMovementEvidence first;first.emptyGoal=true;
    field.recordMovement(first);
    const auto captured=field.capture(20,{});
    GradientMovementEvidence later;later.reachableNoMove=true;
    field.recordMovement(later);
    // The old serving field remains unknown while work is pending.
    CHECK_FALSE(field.serving.contradictionKnown);
    field.serving=captured;
    REQUIRE(field.serving.contradictionKnown);
    const auto delta=field.contradictions();
    for (unsigned i=0;i<delta.size();++i)
        CHECK(delta[i]==(ResourceGradientModel::RequiredMovementCounters[i]==4 || ResourceGradientModel::RequiredMovementCounters[i]==5 ? 1 : 0));
}
TEST_CASE("known capture baselines reject reversed epochs and impossible event deltas") {
    ResourceGradientPolicy::Field field;
    GradientMovementEvidence event;event.emptyGoal=true;field.recordMovement(event);
    auto capture=field.capture(5,{});CHECK(field.validMovementCapture(capture));
    ++capture.movementBaseline[0];CHECK_FALSE(field.validMovementCapture(capture));
    capture.contradictionKnown=false;CHECK(field.validMovementCapture(capture));
    capture=field.capture(5,{});
    field.movementTotal[0]+=2;CHECK_FALSE(field.validMovementCapture(capture));
}
TEST_CASE("legacy capture lacks evidence until a real new capture is published") {
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream out(backend);
    out.writeEnterSection("capture");out.writeUint32(42,"tick");
    for (const auto* name:{"count","added","removed"}) {
        out.writeEnterSection(name);out.writeUint32(0,"high");out.writeUint32(0,"low");out.writeLeaveSection();
    }
    out.writeLeaveSection();out.flush();
    auto* memory=new GAGCore::MemoryStreamBackend(backend->getBuffer(),backend->getPosition());memory->seekFromStart(0);
    GAGCore::BinaryInputStream in(memory);
    ResourceGradientPolicy::Field field;field.serving=ResourceGradientPolicy::Capture::load(&in,174);
    CHECK(field.serving.tick==42);CHECK_FALSE(field.serving.contradictionKnown);
    GradientMovementEvidence evidence;evidence.emptyGoal=true;field.recordMovement(evidence);
    const auto pending=field.capture(50,{});
    CHECK_FALSE(field.serving.contradictionKnown); // Admission alone cannot cure old history.
    field.serving=pending;CHECK(field.serving.contradictionKnown);
    for (auto value:field.contradictions()) CHECK(value==0);
}
TEST_CASE("format174 policy continuation consumes legacy layout without invented history") {
    auto* backend=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(backend);
    const auto sources=[&] {
        for (const auto* name:{"count","added","removed"}) {
            out.writeEnterSection(name);out.writeUint32(0,"high");out.writeUint32(0,"low");out.writeLeaveSection();
        }
    };
    out.writeEnterSection("adaptiveResources");out.writeUint8(1,"initialized");
    out.writeEnterSection("sources");
    for (unsigned material=0;material<MaterialCount;++material) { out.writeEnterSection(material);sources();out.writeLeaveSection(); }
    out.writeLeaveSection();out.writeUint16(1,"fieldCount");out.writeEnterSection("fields");out.writeEnterSection(0u);
    out.writeUint16(0,"key");out.writeEnterSection("capture");out.writeUint32(42,"tick");sources();out.writeLeaveSection();
    out.writeUint32(50,"submitted");out.writeUint32(55,"queryTick");out.writeUint8(1,"requested");out.writeUint8(1,"queryKnown");
    for (unsigned i=0;i<128;++i) { out.writeEnterSection(i);out.writeUint32(i==55 ? 3 : 0,"queries");out.writeLeaveSection(); }
    out.writeLeaveSection();out.writeLeaveSection();out.writeLeaveSection();out.writeUint32(987654,"sentinel");out.flush();
    auto* memory=new GAGCore::MemoryStreamBackend(backend->getBuffer(),backend->getPosition());memory->seekFromStart(0);
    GAGCore::BinaryInputStream in(memory);ResourceGradientPolicy restored;restored.load(&in,1,174);
    CHECK(in.readUint32("sentinel")==987654);
    const auto& field=restored.fields.at(0);
    CHECK(field.serving.tick==42);CHECK(field.submitted==50);CHECK(field.count32==3);CHECK(field.count128==3);
    CHECK(field.requested);CHECK_FALSE(field.serving.contradictionKnown);
    for (auto value:field.movementTotal) CHECK(value==0); // Storage is empty, not claimed measured history.
    auto decision=field;decision.requested=true;
    const auto base=decision.inputs(60,{});
    CHECK(decision.refresh(60,{})==(ResourceGradientModel::predictLegacy(base)>=ResourceGradientPolicy::Threshold));
}
TEST_CASE("capture save wire consumes obsolete IDs and invalidates unsupported baselines") {
    for (unsigned mask:{63u,0u,64u}) {
        auto* backend=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(backend);
        const auto value=[&](std::uint64_t v) {
            out.writeEnterSection("count");out.writeUint32(v>>32,"high");out.writeUint32(v,"low");out.writeLeaveSection();
        };
        out.writeEnterSection("capture");out.writeUint32(42,"tick");
        for (const auto* name:{"count","added","removed"}) {
            out.writeEnterSection(name);out.writeUint32(0,"high");out.writeUint32(0,"low");out.writeLeaveSection();
        }
        out.writeUint8(1,"contradictionKnown");out.writeUint8(mask,"counterMask");
        for (unsigned id=0;id<6;++id) if (mask&(1u<<id)) { out.writeEnterSection(id);value(id+100);out.writeLeaveSection(); }
        out.writeLeaveSection();out.writeUint32(123456,"sentinel");out.flush();
        auto* memory=new GAGCore::MemoryStreamBackend(backend->getBuffer(),backend->getPosition());memory->seekFromStart(0);
        GAGCore::BinaryInputStream in(memory);
        if (mask==64) {
            CHECK_THROWS_AS(ResourceGradientPolicy::Capture::load(&in),std::runtime_error);
            continue;
        }
        const auto capture=ResourceGradientPolicy::Capture::load(&in);
        CHECK(capture.contradictionKnown==(mask==63));
        for (unsigned i=0;i<capture.movementBaseline.size();++i)
            CHECK(capture.movementBaseline[i]==(mask ? ResourceGradientModel::RequiredMovementCounters[i]+100 : 0));
        CHECK(in.readUint32("sentinel")==123456);
    }
}
TEST_CASE("binary and text continuation retain query windows capture epochs and pending demand") {
    for (bool text:{false,true}) {
        ResourceGradientPolicy p;p.initialized=true;p.sources[0]={100,20,30};
        auto& f=p.fields[0];f.serving={10,{90,10,30}};f.submitted=16;
        f.queried(20,true);f.queried(40,true);f.queried(40,true);
        GradientMovementEvidence event;event.emptyGoal=true;f.recordMovement(event);
        f.serving=f.capture(10,{90,10,30});
        event.emptyGoal=false;event.reachableNoMove=true;f.recordMovement(event);
        const auto before=p.checksum();
        auto* backend=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> out(text ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend)) : new GAGCore::BinaryOutputStream(backend));
        p.save(out.get());out->flush();
        auto memory=std::make_unique<GAGCore::MemoryStreamBackend>(backend->getBuffer(),backend->getPosition());memory->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> in(text ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(memory.get())) : new GAGCore::BinaryInputStream(memory.release()));
        ResourceGradientPolicy restored;restored.load(in.get(),1);
        CHECK(restored.checksum()==before);
        CHECK(restored.fields[0].movementTotal==f.movementTotal);
        CHECK(restored.fields[0].serving.movementBaseline==f.serving.movementBaseline);
        CHECK(restored.fields[0].serving.contradictionKnown);
        CHECK(restored.fields[0].contradictions()==f.contradictions());
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
TEST_CASE("checkpoint completion is recorded only for the selected admitted branch") {
    ResourceGradientExploration config;config.forkField=7;
    config.record(356,7,"checkpoint_fork",false);CHECK_FALSE(config.forkComplete);
    config.record(356,8,"checkpoint_fork",true);CHECK_FALSE(config.forkComplete);
    config.record(356,7,"model",true);CHECK_FALSE(config.forkComplete);
    config.record(356,7,"checkpoint_fork",true);CHECK(config.forkComplete);
}
}
