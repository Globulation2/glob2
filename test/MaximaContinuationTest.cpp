// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#ifdef main
#undef main
#endif
#include <memory>
#include "AIMaximaContinuation.h"
#include <TextStream.h>
#include <iostream>
#include <array>
#include "../gnupg/sha1.c"
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
Saved save(const Probe::Record& r,bool binary,bool scalarPath) {
 auto m=new GAGCore::MemoryStreamBackend();
 std::unique_ptr<GAGCore::OutputStream> s(binary?static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(m)):static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(m)));
 Saved result;
 if(binary)dynamic_cast<GAGCore::BinaryOutputStream*>(s.get())->enableSHA1();
 ScalarStream scalar(*s);
 AIMaximaContinuation::Writer a(scalarPath?&scalar:s.get());
 s->writeUint8(31,"first_marker");a("record",r);
 s->writeUint16(0xAA55,"middle_marker");a("wide",r.wide);
 s->writeUint32(0x99887766,"last_marker");a("edge",r.edge);
 if(binary)dynamic_cast<GAGCore::BinaryOutputStream*>(s.get())->finishSHA1(result.hash.data());
 result.bytes=m->takeContents();return result;
}
int main(int, char**) {
 for(size_t n: {0u,1u,4095u,4096u,4097u,65536u}) {
  Probe::Record r;r.small.resize(n);r.wide.resize(n);
  for(size_t i=0;i<n;++i){r.small[i]=static_cast<int8_t>(i);r.wide[i]=uint64_t(i)*0x123456789ABCDEFu;}
  r.text=std::string(n+7,'x');r.text[0]='\0';r.text[1]='\n';r.text[2]='"';r.text[3]='\\';
  r.entries[{-3,UINT32_MAX}]={INT32_MIN,-1,0,INT32_MAX};r.keys={-2,0,65535};
  for(bool binary:{true,false}) {
   auto old=save(r,binary,true);auto now=save(r,binary,false);
   if(!(old==now)){std::cerr<<"mismatch "<<n<<" "<<binary<<"\n";return 1;}
  }
 }
 std::cout<<"Binary bytes, SHA1, text fallback, nested records, signed limits, 64-bit low/high order, strings, container boundaries and interleaved direct writes: identical\n";
 return 0;
}
