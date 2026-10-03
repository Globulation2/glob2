// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <PackedArray.h>
#include <random>

template<class U> static std::string pack(const std::vector<U>& values)
{
    auto* m=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream s(m);
    GAGCore::PackedArray::write<U>(&s,values.size(),[&](size_t i){return values[i];});
    return m->takeContents();
}
template<class U> static std::vector<U> unpack(const std::string& bytes,size_t count)
{
    GAGCore::BinaryInputStream s(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    s.seekFromStart(0);
    std::vector<U> result(count);
    GAGCore::PackedArray::read<U>(&s,count,[&](size_t i,U v){result[i]=v;});
    REQUIRE(s.getPosition()==bytes.size());
    return result;
}
TEST_SUITE("PackedArray")
{
TEST_CASE_TEMPLATE("all widths, wraparound, random values and block boundaries [save-format]", U, Uint8, Uint16, Uint32, Uint64)
{
    std::mt19937_64 random(713);
    for(size_t n:{0u,1u,2u,255u,4095u,4096u,4097u,8193u})
        for(int pattern=0;pattern<6;++pattern)
        {
            std::vector<U> a(n);
            for(size_t i=0;i<n;++i)
                a[i]=pattern==0?U(0):pattern==1?U(-1):pattern==2?U(i):pattern==3?U(0-i):pattern==4?U(random()):U(i/93);
            const auto bytes=pack(a);
            CHECK(unpack<U>(bytes,n)==a);
            CHECK(bytes==pack(a));
            CHECK(bytes.size()<=n*sizeof(U)+5*((n+4095)/4096));
        }
}
TEST_CASE("reject truncated, overflowing and invalid blocks before out of bounds writes [save-format]")
{
    const auto bytes=pack(std::vector<Uint64>{0,1,1,1,UINT64_MAX,0,0,0,0,0,0,0});
    for(size_t n=0;n<bytes.size();++n) CHECK_THROWS(unpack<Uint64>(bytes.substr(0,n),12));
    auto corrupt=bytes; corrupt[0]=3; CHECK_THROWS(unpack<Uint64>(corrupt,12));
    corrupt=bytes; corrupt[1]=127; CHECK_THROWS(unpack<Uint64>(corrupt,12));
    corrupt=bytes; corrupt[4]=0; CHECK_THROWS(unpack<Uint64>(corrupt,12));
    for(const std::vector<Uint8>& b:{std::vector<Uint8>{128,128,128},std::vector<Uint8>{255,2},std::vector<Uint8>{128,0}})
    { size_t p=0; CHECK_THROWS(GAGCore::PackedArray::readVarint<Uint8>(b,p)); }
    // Delta zero-run exceeding the expected count; trailing payload; short raw.
    CHECK_THROWS(unpack<Uint8>(std::string("\2\0\0\0\3\0\0\177",8),8));
    CHECK_THROWS(unpack<Uint8>(std::string("\1\0\0\0\2\0\0",7),8));
    CHECK_THROWS(unpack<Uint8>(std::string("\0\0\0\0\1\0",6),8));
}
}

TEST_SUITE("DeferredSnapshot") {
TEST_CASE("deferred arrays own contents and relocate offsets [save-format]") {
    GAGCore::DeferredStream capture;
    std::vector<Uint16> v(8192,42);
    capture.writeUint32(0,"mapOffset");
    GAGCore::PackedArray::write<Uint16>(&capture,v.size(),[&](size_t i){return v[i];});
    auto end=capture.getPosition();
    capture.seekFromStart(0);capture.writeUint32(end,"mapOffset");
    v.assign(v.size(),123);
    auto snapshot=capture.takeSnapshot();auto result=snapshot.finish();
    GAGCore::BinaryInputStream input(new GAGCore::ChunkedStreamBackend(std::move(result)));
    auto offset=input.readUint32("mapOffset");
    std::vector<Uint16> decoded(v.size());
    GAGCore::PackedArray::read<Uint16>(&input,decoded.size(),[&](size_t i,Uint16 value){decoded[i]=value;});
    CHECK(decoded==std::vector<Uint16>(v.size(),42));CHECK(input.getPosition()==offset);
}
}
