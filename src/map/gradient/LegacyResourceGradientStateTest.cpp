// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "LegacyResourceGradientState.h"
#include "FileFormatVersions.h"
#include "UnitConsts.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Legacy=LegacyResourceGradientState;
struct FieldFixture {
    unsigned key=0,requested=1,queryKnown=1,captureKnown=1;
    unsigned baselineMask=49,totalMask=49;
    Legacy::Sources captured{9,10,20};
    // The complete wire has six stable IDs, not just the selected three.
    std::array<std::uint64_t,6> baseline{1,100,200,300,2,10};
    std::array<std::uint64_t,6> total{2,101,201,301,3,14};
    std::array<std::uint32_t,128> queries{};
};
struct Fixture {
    unsigned version=176,initialized=1;
    Legacy::Sources current{12,12,23};
    std::vector<FieldFixture> fields{FieldFixture{}};
};
void put64(GAGCore::OutputStream& out,std::uint64_t value,const char* name)
{
    out.writeEnterSection(name);
    out.writeUint32(std::uint32_t(value>>32),"high");
    out.writeUint32(std::uint32_t(value),"low");
    out.writeLeaveSection();
}
void putSources(GAGCore::OutputStream& out,const Legacy::Sources& value)
{
    put64(out,value.count,"count");put64(out,value.added,"added");put64(out,value.removed,"removed");
}
void putMovement(GAGCore::OutputStream& out,unsigned mask,const std::array<std::uint64_t,6>& values)
{
    out.writeUint8(mask,"counterMask");
    for (unsigned id=0;id<6;++id) if (mask&(1u<<id)) {
        out.writeEnterSection(id);put64(out,values[id],"count");out.writeLeaveSection();
    }
}
void putCapture(GAGCore::OutputStream& out,const FieldFixture& field,unsigned version)
{
    out.writeEnterSection("capture");out.writeUint32(42,"tick");putSources(out,field.captured);
    if (version>=FILE_FORMAT_VERSION_GRADIENT_CONTRADICTIONS) {
        out.writeUint8(field.captureKnown,"contradictionKnown");
        putMovement(out,field.baselineMask,field.baseline);
    }
    out.writeLeaveSection();
}
// Historical layout generation belongs only in tests. Production intentionally
// has no writer, scheduler, live query counters or legacy model dependency.
std::vector<char> encode(const Fixture& fixture,bool text=false,bool captureOnly=false)
{
    auto* backend=new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> out(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
        : new GAGCore::BinaryOutputStream(backend));
    if (captureOnly) putCapture(*out,fixture.fields.front(),fixture.version);
    else {
        out->writeEnterSection("adaptiveResources");out->writeUint8(fixture.initialized,"initialized");
        out->writeEnterSection("sources");
        for (unsigned material=0;material<MaterialCount;++material) {
            out->writeEnterSection(material);putSources(*out,fixture.current);out->writeLeaveSection();
        }
        out->writeLeaveSection();out->writeUint16(fixture.fields.size(),"fieldCount");out->writeEnterSection("fields");
        for (unsigned i=0;i<fixture.fields.size();++i) {
            const auto& field=fixture.fields[i];out->writeEnterSection(i);out->writeUint16(field.key,"key");
            putCapture(*out,field,fixture.version);
            out->writeUint32(50,"submitted");out->writeUint32(UINT32_MAX-8,"queryTick");
            out->writeUint8(field.requested,"requested");out->writeUint8(field.queryKnown,"queryKnown");
            if (fixture.version>=FILE_FORMAT_VERSION_GRADIENT_CONTRADICTIONS) {
                out->writeEnterSection("movementTotals");putMovement(*out,field.totalMask,field.total);out->writeLeaveSection();
            }
            for (unsigned bucket=0;bucket<128;++bucket) {
                out->writeEnterSection(bucket);out->writeUint32(field.queries[bucket],"queries");out->writeLeaveSection();
            }
            out->writeLeaveSection();
        }
        out->writeLeaveSection();out->writeLeaveSection();
    }
    out->writeUint32(987654321,"sentinel");out->flush();
    return {backend->getBuffer(),backend->getBuffer()+backend->getPosition()};
}
template<class Read> auto decode(const std::vector<char>& bytes,bool text,Read read)
{
    auto memory=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());memory->seekFromStart(0);
    std::unique_ptr<GAGCore::InputStream> in(text
        ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(memory.get()))
        : new GAGCore::BinaryInputStream(memory.release()));
    auto value=read(in.get());
    CHECK(in->readUint32("sentinel")==987654321);
    return value;
}
Legacy load(const Fixture& fixture,bool text=false)
{
    return decode(encode(fixture,text),text,[&](auto* in) { Legacy state;state.load(in,1,fixture.version);return state; });
}
Legacy::Capture loadCapture(const Fixture& fixture)
{
    return decode(encode(fixture,false,true),false,[&](auto* in) { return Legacy::Capture::load(in,fixture.version); });
}
}

TEST_SUITE("LegacyResourceGradientState") {
TEST_CASE("formats174 through177 consume binary and text layouts without inventing history") {
    for (unsigned version:{174u,175u,176u,177u}) for (bool text:{false,true}) {
        Fixture fixture;fixture.version=version;fixture.fields[0].queries[127]=1000000;
        fixture.fields[0].baselineMask=fixture.fields[0].totalMask=63;
        const auto state=load(fixture,text);
        REQUIRE(state.fields.size()==1);CHECK(state.initialized);CHECK(state.sources[0].count==12);
        CHECK(state.sources[0].added==12);CHECK(state.sources[0].removed==23);
        const auto& field=state.fields.at(0);CHECK(field.serving.tick==42);
        CHECK(field.serving.sources.count==9);CHECK(field.serving.sources.added==10);CHECK(field.serving.sources.removed==20);
        CHECK(field.serving.contradictionKnown==(version>=176));
        CHECK(field.validMovementCapture(field.serving));
        if (version>=176) {
            CHECK((field.serving.movementBaseline==Legacy::MovementCounts{1,2,10}));
            CHECK((field.movementTotal==Legacy::MovementCounts{2,3,14}));
        } else {
            CHECK((field.serving.movementBaseline==Legacy::MovementCounts{}));
            CHECK((field.movementTotal==Legacy::MovementCounts{}));
        }
    }
}
TEST_CASE("pending captures consume obsolete IDs and mark incomplete masks unknown") {
    for (unsigned mask:{0u,1u,48u,49u,63u}) {
        Fixture fixture;fixture.fields[0].baselineMask=mask;
        const auto capture=loadCapture(fixture);
        CHECK(capture.contradictionKnown==((mask&49)==49));
        CHECK(capture.movementBaseline[0]==((mask&1) ? 1 : 0));
        CHECK(capture.movementBaseline[1]==((mask&16) ? 2 : 0));
        CHECK(capture.movementBaseline[2]==((mask&32) ? 10 : 0));
    }
    Fixture fixture;fixture.fields[0].baselineMask=64;CHECK_THROWS_AS(loadCapture(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields[0].captureKnown=2;CHECK_THROWS_AS(loadCapture(fixture),std::runtime_error);
}
TEST_CASE("legacy wire preserves both halves of64bit source and movement values") {
    Fixture fixture;fixture.current={std::uint64_t(1)<<40,std::uint64_t(1)<<50,std::uint64_t(1)<<52};
    auto& field=fixture.fields[0];field.captured=fixture.current;
    for (unsigned id:{0u,4u,5u}) {field.baseline[id]=UINT64_MAX-1;field.total[id]=UINT64_MAX;}
    const auto state=load(fixture);CHECK(state.sources[0].count==fixture.current.count);
    CHECK(state.sources[0].added==fixture.current.added);CHECK(state.sources[0].removed==fixture.current.removed);
    CHECK(state.fields.at(0).serving.movementBaseline[0]==UINT64_MAX-1);
    CHECK(state.fields.at(0).movementTotal[2]==UINT64_MAX);
    CHECK(state.fields.at(0).validMovementCapture(state.fields.at(0).serving));
}
TEST_CASE("incomplete saved total counters invalidate known capture evidence") {
    Fixture fixture;fixture.fields[0].totalMask=0;
    const auto state=load(fixture);CHECK_FALSE(state.fields.at(0).serving.contradictionKnown);
    fixture.fields[0].totalMask=64;CHECK_THROWS_AS(load(fixture),std::runtime_error);
}
TEST_CASE("movement baseline validation also covers separately restored pending captures") {
    auto state=load(Fixture{});const auto& field=state.fields.at(0);auto pending=field.serving;
    CHECK(field.validMovementCapture(pending));
    pending.movementBaseline[0]=3;CHECK_FALSE(field.validMovementCapture(pending));
    pending=field.serving;pending.movementBaseline[2]=14;CHECK_FALSE(field.validMovementCapture(pending));
    pending.contradictionKnown=false;CHECK(field.validMovementCapture(pending));
    Fixture fixture;fixture.fields[0].baseline[0]=3;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields[0].total[0]=20;CHECK_THROWS_AS(load(fixture),std::runtime_error);
}
TEST_CASE("invalid flags keys duplicate fields and source epochs remain rejected") {
    Fixture fixture;fixture.initialized=2;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.initialized=0;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields[0].key=MaterialCount*SWIM_CLASS_COUNT;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields.push_back(fixture.fields[0]);CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields[0].requested=2;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields[0].queryKnown=2;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields[0].captured.added=13;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    fixture=Fixture{};fixture.fields[0].captured.removed=24;CHECK_THROWS_AS(load(fixture),std::runtime_error);
}
TEST_CASE("query ring bounds and unknown-window zeroes are still validated") {
    for (unsigned bucket:{0u,31u,127u}) {
        Fixture fixture;fixture.fields[0].queries[bucket]=1000001;CHECK_THROWS_AS(load(fixture),std::runtime_error);
        fixture.fields[0].queries[bucket]=1;fixture.fields[0].queryKnown=0;CHECK_THROWS_AS(load(fixture),std::runtime_error);
    }
    Fixture fixture;fixture.fields[0].queryKnown=0;CHECK_NOTHROW(load(fixture));
    fixture.initialized=0;fixture.fields.clear();CHECK_NOTHROW(load(fixture));
}
}
