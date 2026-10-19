// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "AdaptiveGradientState.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include <vector>

TEST_SUITE("AdaptiveGradientState") {
TEST_CASE("logical keys distinguish all families teams classes and materials") {
    std::map<std::uint32_t,GradientFieldKey> keys;
    for(unsigned family=0;family<5;++family)for(unsigned team=0;team<8;++team)for(unsigned swim=0;swim<7;++swim)
        for(unsigned material=0;material<(family<2?4u:1u);++material) {
            GradientFieldKey key{GradientFieldFamily(family),std::uint8_t(team),std::uint8_t(swim),family<2?std::uint8_t(material):GradientFieldKey::NoMaterial};
            REQUIRE(key.valid(8,7,4));CHECK(GradientFieldKey::unpack(key.packed())==key);CHECK(keys.emplace(key.packed(),key).second);
        }
    CHECK_FALSE((GradientFieldKey{GradientFieldFamily::Guard,0,0,0}.valid(8,7,4)));
    CHECK(GradientTargetKey::of({GradientFieldFamily::Guard,0,0,255})!=GradientTargetKey::of({GradientFieldFamily::Guard,0,1,255}));
    CHECK(GradientTargetKey::of({GradientFieldFamily::Materials,0,0,0})==GradientTargetKey::of({GradientFieldFamily::Materials,0,1,0}));
    CHECK(GradientTargetKey::of({GradientFieldFamily::Materials,0,0,0})!=GradientTargetKey::of({GradientFieldFamily::Materials,0,AIR_MOVEMENT_CLASS,0}));
}
TEST_CASE("idle fields submit nothing and demanded fields below the safety cap require model benefit") {
    AdaptiveGradientState::Field field;field.serving.known=true;
    unsigned predictions=0;const auto low=[&](const auto&,const auto&){++predictions;return 0;};
    CHECK_FALSE(field.evaluate(1000,{},true,low).requested());CHECK(predictions==0);
    field.queried(10);CHECK_FALSE(field.evaluate(10,{},true,low).requested());CHECK(predictions==1);
    CHECK_FALSE(field.evaluate(1000,{},true,low).requested());CHECK(predictions==1);
    field.queried(1000);CHECK(field.evaluate(1000,{},true,low).requested()==false);
}
TEST_CASE("cold history reactive model and deferred admissions obey one association and spacing") {
    using Reason=AdaptiveGradientState::Reason;
    AdaptiveGradientState::Field f;const auto high=[](const auto&,const auto&){return AdaptiveGradientState::Threshold;};
    f.queried(0);CHECK(f.evaluate(0,{},false,high).reason==Reason::Cold);
    f.queued=true;CHECK_FALSE(f.evaluate(1,{},false,high).requested());f.queued=false;
    f.admitted(1);CHECK(f.inflight);f.queried(2);CHECK_FALSE(f.evaluate(2,{},true,high).requested());
    f.published({});f.queried(8);CHECK_FALSE(f.evaluate(8,{},true,high).requested());
    f.queried(9);CHECK(f.evaluate(9,{},true,high).reason==Reason::HistoryInitialization);
    f.admitted(9);f.published(f.capture(9,{},3));
    f.queried(17);CHECK(f.evaluate(17,{},true,high).reason==Reason::Model);f.deferred=true;
    CHECK(f.demand);const auto low=[](const auto&,const auto&){return 0;};CHECK(f.evaluate(18,{},true,low).reason==Reason::Model);f.admitted(18);CHECK_FALSE(f.deferred);
    f.published(f.capture(18,{},3));f.reactive=true;CHECK_FALSE(f.evaluate(25,{},true,high).requested());
    CHECK(f.evaluate(26,{},true,high).reason==Reason::Reactive);
}
TEST_CASE("demanded capture age is bounded at1024 without aging idle fields into work") {
    using State=AdaptiveGradientState;using Reason=State::Reason;
    for(unsigned captureTick:{100u,UINT32_MAX-500}) {
        State::Field f;f.serving=f.capture(captureTick,{},3);f.submitted=captureTick+4;
        unsigned predictions=0;const auto low=[&](const auto&,const auto&){++predictions;return 0;};
        CHECK_FALSE(f.evaluate(captureTick+State::MaximumAge+100,{},true,low).requested());
        CHECK(predictions==0); // Even a very old field still needs current demand.
        f.queried(captureTick+State::MaximumAge-1);
        CHECK_FALSE(f.evaluate(captureTick+State::MaximumAge-1,{},true,low).requested());
        CHECK(predictions==1);
        // Rejected ordinary demand expires; crossing the boundary alone does nothing.
        CHECK_FALSE(f.evaluate(captureTick+State::MaximumAge,{},true,low).requested());
        f.queried(captureTick+State::MaximumAge);
        CHECK(f.evaluate(captureTick+State::MaximumAge,{},true,low).reason==Reason::AgeCap);
        CHECK(predictions==1); // Neither model nor extra map work is needed at the cap.
    }
}
TEST_CASE("age cap uses the serving capture and retains association minimum and recovery precedence") {
    using State=AdaptiveGradientState;using Reason=State::Reason;
    const auto low=[](const auto&,const auto&){return 0;};
    State::Field f;const auto captured=f.capture(100,{},3);
    f.admitted(104);f.published(captured); // Publication must retain capture100.
    f.queried(1123);CHECK_FALSE(f.evaluate(1123,{},true,low).requested());
    f.queried(1124);CHECK(f.evaluate(1124,{},true,low).reason==Reason::AgeCap);
    CHECK(f.serving.tick==100);CHECK(f.submitted==104);
    f.queued=true;CHECK_FALSE(f.evaluate(1125,{},true,low).requested());f.queued=false;
    f.inflight=true;f.queried(1125);CHECK_FALSE(f.evaluate(1125,{},true,low).requested());f.inflight=false;
    f.submitted=1120;f.queried(1127);CHECK_FALSE(f.evaluate(1127,{},true,low).requested());
    f.queried(1128);CHECK(f.evaluate(1128,{},true,low).reason==Reason::AgeCap);
    f.reactive=true;CHECK(f.evaluate(1128,{},true,low).reason==Reason::Reactive);f.reactive=false;
    f.serving.known=false;CHECK(f.evaluate(1128,{},true,low).reason==Reason::HistoryInitialization);
    CHECK(f.evaluate(1128,{},false,low).reason==Reason::Cold);
}
TEST_CASE("research intervals remain explicit below the shared demanded age cap") {
    using State=AdaptiveGradientState;using Reason=State::Reason;
    State::Field f;f.serving.known=true;const auto low=[](const auto&,const auto&){return 0;};
    f.queried(31);CHECK_FALSE(f.evaluate(31,{},true,low,32).requested());
    f.queried(32);CHECK(f.evaluate(32,{},true,low,32).reason==Reason::Bootstrap);
    f.queried(1023);CHECK_FALSE(f.evaluate(1023,{},true,low,4096).requested());
    f.queried(1024);CHECK(f.evaluate(1024,{},true,low,4096).reason==Reason::AgeCap);
}
TEST_CASE("benefit threshold is inclusive at16 units and the eight tick minimum remains") {
    AdaptiveGradientState::Field f;f.serving.known=true;f.submitted=0;
    f.queried(7);CHECK_FALSE(f.evaluate(7,{},true,[](const auto&,const auto&){return AdaptiveGradientState::Threshold;}).requested());
    f.queried(8);CHECK_FALSE(f.evaluate(8,{},true,[](const auto&,const auto&){return AdaptiveGradientState::Threshold-1;}).requested());
    f.queried(8);CHECK(f.evaluate(8,{},true,[](const auto&,const auto&){return AdaptiveGradientState::Threshold;}).reason==AdaptiveGradientState::Reason::Model);
    CHECK(AdaptiveGradientState::Threshold==16000);
}
TEST_CASE("mutations never allocate unseen scopes and equivalent ground classes share turnover") {
    AdaptiveGradientState state;const GradientFieldKey first{GradientFieldFamily::Markets,0,0,0},second{GradientFieldFamily::Markets,0,1,0};
    state.targetChanged(GradientTargetKey::of(first),true);CHECK(state.targets.empty());
    state.enroll(first);state.targetChanged(GradientTargetKey::of(first),true);state.enroll(second);
    CHECK(state.targets.size()==1);CHECK(state.currentTargets(second).added==1);
}
TEST_CASE("capture baselines remain aligned while a delayed job is pending") {
    AdaptiveGradientState::Field field;GradientMovementEvidence invalid;invalid.emptyGoal=true;
    field.recordMovement(invalid);const auto pending=field.capture(10,{2,3,4,5},7);
    GradientMovementEvidence stalled;stalled.reachableNoMove=true;field.recordMovement(stalled);
    CHECK_FALSE(field.serving.known);field.published(pending);
    CHECK(field.serving.tick==10);CHECK(field.serving.goalCount==7);
    CHECK((field.contradictions()==AdaptiveGradientFeatures::Counts{0,1,1}));
    CHECK(field.validCapture(pending));auto corrupt=pending;corrupt.baseline[2]=3;CHECK_FALSE(field.validCapture(corrupt));
}
TEST_CASE("query windows are exact including unsigned tick wrap") {
    for(unsigned start:{0u,UINT32_MAX-64}) {
        AdaptiveGradientState::Field f;std::vector<unsigned> queries;
        for(unsigned elapsed=0;elapsed<300;++elapsed) {
            const auto tick=start+elapsed;if(elapsed%3==0){f.queried(tick);queries.push_back(tick);}const auto inputs=f.inputs(tick,{});
            unsigned n32=0,n128=0;for(auto query:queries){n32+=unsigned(tick-query)<32;n128+=unsigned(tick-query)<128;}
            CHECK(inputs[1]==n32);CHECK(inputs[2]==n128);
        }
    }
}
TEST_CASE("missing canonical target scope cannot silently disable turnover after loading") {
    AdaptiveGradientState state;state.fields[{GradientFieldFamily::Guard,0,0,255}].serving.known=true;
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(bytes);state.save(&out);out.flush();
    auto* copy=new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition());copy->seekFromStart(0);GAGCore::BinaryInputStream in(copy);
    AdaptiveGradientState restored;CHECK_THROWS_AS(restored.load(&in,1,MOVEMENT_CLASS_COUNT,12),std::runtime_error);
}
TEST_CASE("queued and deferred age-cap reasons retain binary and text admission continuity") {
    using State=AdaptiveGradientState;using Reason=State::Reason;
    const auto low=[](const auto&,const auto&){return 0;};
    for(bool text:{false,true})for(bool queued:{false,true}) {
        State state;const GradientFieldKey key{GradientFieldFamily::Clear,0,1,255};
        auto& f=state.enroll(key);f.serving=f.capture(100,{},3);f.submitted=104;
        f.queried(1124);REQUIRE(f.evaluate(1124,{},true,low).reason==Reason::AgeCap);
        if(queued)f.queued=true;else f.defer(Reason::AgeCap);
        auto* backend=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> out(text?static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend)):new GAGCore::BinaryOutputStream(backend));state.save(out.get());out->flush();
        auto memory=std::make_unique<GAGCore::MemoryStreamBackend>(backend->getBuffer(),backend->getPosition());memory->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> in(text?static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(memory.get())):new GAGCore::BinaryInputStream(memory.release()));
        State restored;restored.load(in.get(),1,MOVEMENT_CLASS_COUNT,4);CHECK(restored.checksum()==state.checksum());
        auto& loaded=restored.fields.at(key);CHECK(loaded.acceptedReason==Reason::AgeCap);
        CHECK(loaded.serving.tick==100);CHECK(loaded.submitted==104);
        if(queued)CHECK_FALSE(loaded.evaluate(1125,{},true,low).requested());
        else CHECK(loaded.evaluate(1125,{},true,low).reason==Reason::AgeCap);
        // Admission consumes this reason once; publication starts a new capture clock.
        for(auto* field:{&f,&loaded}) {field->queued=false;field->admitted(1125);CHECK(field->inflight);CHECK(field->acceptedReason==Reason::None);const auto next=field->capture(1125,{},3);field->published(next);field->queried(2148);CHECK_FALSE(field->evaluate(2148,{},true,low).requested());field->queried(2149);CHECK(field->evaluate(2149,{},true,low).reason==Reason::AgeCap);}
        CHECK(restored.checksum()==state.checksum());
    }
}
TEST_CASE("saved reason validation accepts age cap and still rejects undefined wire IDs") {
    AdaptiveGradientState state;const GradientFieldKey key{GradientFieldFamily::Guard,0,0,255};
    auto& f=state.enroll(key);f.serving.known=true;f.demand=true;f.acceptedReason=static_cast<AdaptiveGradientState::Reason>(7);
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(bytes);state.save(&out);out.flush();
    auto* copy=new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition());copy->seekFromStart(0);GAGCore::BinaryInputStream in(copy);
    AdaptiveGradientState restored;CHECK_THROWS_AS(restored.load(&in,1,MOVEMENT_CLASS_COUNT,4),std::runtime_error);
}
TEST_CASE("binary and text state continuation preserve common captures and demand") {
    for(bool text:{false,true}) {
        AdaptiveGradientState state;const GradientFieldKey key{GradientFieldFamily::Guard,0,2,255};
        state.enroll(key);state.targetChanged(GradientTargetKey::of(key),true);state.routesChanged();state.guardCostsChanged(0);state.seedCostsChanged(GradientTargetKey::of(key));
        auto& field=state.queried(key,10);field.recordMovement({});field.serving=field.capture(10,state.currentTargets(key),5);
        field.queried(12);GradientMovementEvidence event;event.reachableNoMove=true;field.recordMovement(event);field.reactive=true;field.deferred=true;field.acceptedReason=AdaptiveGradientState::Reason::Reactive;
        auto* backend=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> out(text?static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend)):new GAGCore::BinaryOutputStream(backend));state.save(out.get());out->flush();
        auto memory=std::make_unique<GAGCore::MemoryStreamBackend>(backend->getBuffer(),backend->getPosition());memory->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> in(text?static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(memory.get())):new GAGCore::BinaryInputStream(memory.release()));
        AdaptiveGradientState restored;restored.load(in.get(),1,7,4);CHECK(restored.checksum()==state.checksum());
        CHECK(restored.fields.at(key).contradictions()==field.contradictions());CHECK(restored.fields.at(key).reactive);CHECK(restored.fields.at(key).deferred);
        for(unsigned tick=12;tick<200;++tick){if(tick%3==0){state.queried(key,tick);restored.queried(key,tick);}CHECK(state.fields.at(key).inputs(tick,state.currentTargets(key))==restored.fields.at(key).inputs(tick,restored.currentTargets(key)));}
    }
}
}
