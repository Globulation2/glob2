// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <vector>
#include <string>
#include <map>
#include <set>
#include <utility>
#include <memory>
#include "AIMaximaContinuation.h"
#include <TextStream.h>
#include <array>
// Hide the binary dynamic type to exercise the unchanged scalar archive path.
class ScalarStream : public GAGCore::OutputStream
{
 GAGCore::OutputStream& target;
public:
 explicit ScalarStream(GAGCore::OutputStream& target):target(target) {}
 void write(const void* data,size_t size,const std::string name) override {target.write(data,size,name);}
 void writeSint8(const Sint8 value,const std::string name) override {target.writeSint8(value,name);}
 void writeUint8(const Uint8 value,const std::string name) override {target.writeUint8(value,name);}
 void writeSint16(const Sint16 value,const std::string name) override {target.writeSint16(value,name);}
 void writeUint16(const Uint16 value,const std::string name) override {target.writeUint16(value,name);}
 void writeSint32(const Sint32 value,const std::string name) override {target.writeSint32(value,name);}
 void writeUint32(const Uint32 value,const std::string name) override {target.writeUint32(value,name);}
 void writeFloat(const float value,const std::string name) override {target.writeFloat(value,name);}
 void writeDouble(const double value,const std::string name) override {target.writeDouble(value,name);}
 void writeText(const std::string& value,const std::string name) override {target.writeText(value,name);}
 void flush() override {target.flush();}
 void writeEnterSection(const std::string name) override {target.writeEnterSection(name);}
 void writeEnterSection(unsigned id) override {target.writeEnterSection(id);}
 void writeLeaveSection(size_t count=1) override {target.writeLeaveSection(count);}
 bool canSeek() override {return target.canSeek();}
 void seekFromStart(int n) override {target.seekFromStart(n);}
 void seekFromEnd(int n) override {target.seekFromEnd(n);}
 void seekRelative(int n) override {target.seekRelative(n);}
 size_t getPosition() override {return target.getPosition();}
 bool isEndOfStream() override {return target.isEndOfStream();}
 bool isValid() override {return target.isValid();}
};
namespace Probe {
struct Record {
 std::vector<int8_t> small;
 std::vector<uint64_t> wide;
 std::string text;
 std::map<std::pair<int,uint32_t>,std::vector<int>> entries;
 std::set<int> keys;
 int64_t edge[3]={INT64_MIN,-1,INT64_MAX};
};
template<class A> void fields(A& a,Record& r) {
 a("small",r.small);a("wide",r.wide);a("text",r.text);a("entries",r.entries);a("keys",r.keys);a("edge",r.edge);
}
}
struct Saved {std::string bytes;std::array<Uint8,20> hash{};bool operator==(const Saved&)const=default;};
Saved save(const Probe::Record& r,bool binary,bool scalarPath,bool compact=false) {
 auto m=new GAGCore::MemoryStreamBackend();
 std::unique_ptr<GAGCore::OutputStream> s(binary?static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(m)):static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(m)));
 Saved result;
 if(binary)dynamic_cast<GAGCore::BinaryOutputStream*>(s.get())->enableSHA1();
 ScalarStream scalar(*s);
 AIMaximaContinuation::Writer a(scalarPath?&scalar:s.get(),compact);
 s->writeUint8(31,"firstMarker");a("record",r);
 s->writeUint16(0xAA55,"middleMarker");a("wide",r.wide);
 s->writeUint32(0x99887766,"lastMarker");a("edge",r.edge);
 if(binary)dynamic_cast<GAGCore::BinaryOutputStream*>(s.get())->finishSHA1(result.hash.data());
 result.bytes=m->takeContents();return result;
}
TEST_SUITE("Maxima.Continuation")
{
TEST_CASE("compact arrays preserve nested state and signed limits [save-format]")
{
 Probe::Record r;
 r.small={INT8_MIN,-1,0,1,INT8_MAX};
 r.wide={0,UINT64_MAX,UINT64_MAX-1,0,1};
 r.entries[{-1,UINT32_MAX}]={INT_MIN,-1,0,INT_MAX};r.keys={INT_MIN,-1,0,INT_MAX};r.text="contents";
 for(bool binary:{false,true}) {
  const auto saved=save(r,binary,false,true);
  auto* m=new GAGCore::MemoryStreamBackend(saved.bytes.data(),saved.bytes.size());m->seekFromStart(0);
  std::unique_ptr<GAGCore::InputStream> in(binary?static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(m)):static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(m)));
  GAGCore::BinaryInputStream::CheckedReads checked(in.get());
  AIMaximaContinuation::Reader reader(in.get(),true);
  REQUIRE(in->readUint8("firstMarker")==31);
  Probe::Record restored;reader("record",restored);
  CHECK(restored.small==r.small);CHECK(restored.wide==r.wide);CHECK(restored.text==r.text);
  CHECK(restored.entries==r.entries);CHECK(restored.keys==r.keys);
  for(int i=0;i<3;++i)CHECK(restored.edge[i]==r.edge[i]);
  CHECK(in->readUint16("middleMarker")==0xAA55);
  reader("wide",restored.wide);CHECK(restored.wide==r.wide);
  CHECK(in->readUint32("lastMarker")==0x99887766);
  reader("edge",restored.edge);
  CHECK(save(restored,binary,false,true)==saved);
 }
}
TEST_CASE("binary bytes; SHA1; text fallback; nested records; signed limits; 64-bit order; strings; container boundaries and interleaved writes are identical [save-format]")
{
 for(size_t n: {0u,1u,4095u,4096u,4097u,65536u}) {
  Probe::Record r;r.small.resize(n);r.wide.resize(n);
  for(size_t i=0;i<n;++i){r.small[i]=static_cast<int8_t>(i);r.wide[i]=uint64_t(i)*0x123456789ABCDEFu;}
  r.text=std::string(n+7,'x');r.text[0]='\0';r.text[1]='\n';r.text[2]='"';r.text[3]='\\';
  r.entries[{-3,UINT32_MAX}]={INT32_MIN,-1,0,INT32_MAX};r.keys={-2,0,65535};
  for(bool binary:{true,false}) {
   auto old=save(r,binary,true);auto now=save(r,binary,false);
   REQUIRE_MESSAGE(old==now, "mismatch " << n << " " << binary);
  }
 }
}
}

TEST_SUITE("Maxima.Continuation")
{
TEST_CASE("compact fields retain legacy binary and text encodings [save-format]")
{
 using AIMaximaPlacement::DistanceField;
 const std::vector<int32_t> oldDistances={0,1,512,65534,INT_MAX};
 const std::vector<uint64_t> oldFood={0,1,UINT32_MAX};
 DistanceField distances; distances.assign(oldDistances.size(),0);
 for(size_t i=0;i<oldDistances.size();++i) distances[i]=oldDistances[i];
 std::vector<uint32_t> food(oldFood.begin(),oldFood.end());
 for(bool binary:{true,false}) for(bool scalar:{true,false}) {
  auto emit=[&](bool compact){
   auto* backend=new GAGCore::MemoryStreamBackend();
   std::unique_ptr<GAGCore::OutputStream> out(binary?static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)):static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend)));
   ScalarStream wrapped(*out); AIMaximaContinuation::Writer a(scalar?&wrapped:out.get());
   if(compact) {
    a("distances",distances);
    a.legacyVector<uint64_t>("food",food,[](uint32_t x){return uint64_t(x);},[](uint64_t){return uint32_t{};});
   } else { a("distances",oldDistances); a("food",oldFood); }
   return backend->takeContents();
  };
  const auto bytes=emit(false); REQUIRE(bytes==emit(true));
  auto* backend=new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size());backend->seekFromStart(0);
  std::unique_ptr<GAGCore::InputStream> in(binary?static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend)):static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend)));
  AIMaximaContinuation::Reader reader(in.get()); DistanceField restored; std::vector<uint32_t> restoredFood;
  reader("distances",restored);
  reader.legacyVector<uint64_t>("food",restoredFood,[](uint32_t){return uint64_t{};},[](uint64_t x){return uint32_t(x);});
  REQUIRE(restored.size()==oldDistances.size());
  for(size_t i=0;i<oldDistances.size();++i) REQUIRE(int(restored[i])==oldDistances[i]);
  REQUIRE(restoredFood==food);
 }
}
}
