// Standalone lossless AIT1 storage mechanism model; no engine changes.
#include "ReplayTelemetry.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <sys/resource.h>
#include <time.h>
#include <cstdint>
using FullValue=AITelemetry::NamedValue;
static_assert(sizeof(FullValue)==136,"Expected frozen Linux GCC13 ABI");
struct Meta {std::string name,unit,meaning;};
struct CompactValue {std::string value;Uint32 schemaID=0,updated=0;};
struct CompactRow {int team=0,player=0;std::string name;bool available=false;std::vector<CompactValue> values;};
struct CompactFrame {Uint32 step=0;std::vector<CompactRow> changes;};
static_assert(sizeof(CompactValue)==40);
constexpr size_t Limit=16*1024*1024;
static void fail(const char*s){throw std::runtime_error(s);}
struct Input {const std::string &data;size_t at=0,budget=Limit;
 void spend(size_t n){if(n>budget)fail("budget");budget-=n;}
 void need(size_t n){if(n>data.size()-at)fail("truncated");}
 Uint32 u32(){need(4);Uint32 n=0;for(int i=0;i<4;i++)n=n*256+static_cast<unsigned char>(data[at++]);return n;}
 bool boolean(){need(1);unsigned c=static_cast<unsigned char>(data[at++]);if(c>1)fail("boolean");return c;}
 std::string text(){size_t n=u32();if(n>4096)fail("text bound");spend(n);need(n);std::string s(data,at,n);at+=n;return s;}
};
struct Output {std::string data;
 void u32(Uint32 v){for(int n=24;n>=0;n-=8)data.push_back(static_cast<char>((v>>n)&255));}
 void boolean(bool v){data.push_back(v?1:0);}
 void text(const std::string&v){u32(v.size());data.append(v);}
};
static std::string key(const Meta&m){Output o;o.text(m.name);o.text(m.unit);o.text(m.meaning);return std::move(o.data);}
static double cpu(){timespec t{};clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static double wall(){using namespace std::chrono;return duration<double>(steady_clock::now().time_since_epoch()).count();}
static long rss(){std::ifstream f("/proc/self/status");std::string line;while(std::getline(f,line))if(line.rfind("VmRSS:",0)==0)return std::stol(line.substr(6));return -1;}
struct Phase {std::string name;long rssKib;double wallSeconds,cpuSeconds;};
struct TextStats {size_t bytes=0,externalCapacity=0,strings=0;void add(const std::string&s){bytes+=s.size();strings++;auto p=reinterpret_cast<uintptr_t>(s.data()),a=reinterpret_cast<uintptr_t>(&s);if(p<a||p>=a+sizeof(s))externalCapacity+=s.capacity()+1;}};
int main(int argc,char**argv){try{
 if(argc!=5)fail("Usage: storage MODE REPLAY OFFSET OUTPUT_TAIL");
 std::string mode=argv[1];bool compact=mode=="intern-reserve",reserve=mode!="full-grow";
 if(mode!="full-grow"&&mode!="full-reserve"&&!compact)fail("mode");
 const double beginW=wall(),beginC=cpu();std::vector<Phase> phases;
 auto phase=[&](const std::string&n){phases.push_back({n,rss(),wall()-beginW,cpu()-beginC});};
 phase("before_input");
 std::ifstream file(argv[2],std::ios::binary);if(!file)fail("input");file.seekg(0,std::ios::end);auto size=file.tellg();size_t offset=std::stoull(argv[3]);
 if(size<0||size>256*1024*1024||offset>static_cast<size_t>(size))fail("file bound");
 std::string bytes(static_cast<size_t>(size)-offset,'\0');file.seekg(offset);file.read(bytes.data(),bytes.size());if(!file)fail("read");phase("after_input");
 Input in{bytes};if(in.u32()!=0x41495431||in.u32()!=1)fail("magic/version");Uint32 until=in.u32();bool truncated=in.boolean();Uint32 frames=in.u32();if(frames>65536)fail("frames");
 std::vector<ReplayTelemetry::Frame> full;std::vector<CompactFrame> small;std::vector<Meta> meta;std::unordered_map<std::string,Uint32> ids;
 size_t rowsTotal=0,valuesTotal=0,metadataRepeated=0,valueText=0;Uint32 last=0;
 for(Uint32 i=0;i<frames;i++){
  in.spend(8);Uint32 step=in.u32(),rows=in.u32();if(step>until||(i&&step<=last)||rows>32)fail("frame bound");last=step;
  ReplayTelemetry::Frame ff;CompactFrame cf;ff.step=cf.step=step;
  for(Uint32 j=0;j<rows;j++){
   in.spend(32);Uint32 team=in.u32(),player=in.u32();std::string name=in.text();if(team>=16||player>=16)fail("controller");bool available=in.boolean();Uint32 fields=in.u32();if(fields>4228)fail("fields");in.spend(size_t(fields)*24);
   ReplayTelemetry::Row fr;CompactRow cr;fr.team=cr.team=team;fr.player=cr.player=player;fr.available=cr.available=available;
   if(compact)cr.name=std::move(name);else fr.name=std::move(name);
   if(reserve){if(compact)cr.values.reserve(fields);else fr.values.reserve(fields);}
   for(Uint32 f=0;f<fields;f++){
    std::string n=in.text(),v=in.text(),u=in.text(),m=in.text();Uint32 updated=in.u32();metadataRepeated+=n.size()+u.size()+m.size();valueText+=v.size();valuesTotal++;
    if(compact){Meta triple{std::move(n),std::move(u),std::move(m)};std::string k=key(triple);auto it=ids.find(k);Uint32 id;
     if(it==ids.end()){id=meta.size();ids.emplace(std::move(k),id);meta.push_back(std::move(triple));}else id=it->second;
     cr.values.push_back({std::move(v),id,updated});
    }else fr.values.push_back({std::move(n),std::move(v),std::move(u),std::move(m),updated});
   }
   if(compact)cf.changes.push_back(std::move(cr));else ff.changes.push_back(std::move(fr));rowsTotal++;
  }
  if(compact)small.push_back(std::move(cf));else full.push_back(std::move(ff));
 }
 if(in.at!=bytes.size())fail("EOF");
 phase("after_storage");
 size_t frameSlots=compact?small.capacity():full.capacity(),rowSlots=0,valueSlots=0,uniqueBytes=0;TextStats texts,keys;
 if(compact){for(auto&f:small)for(auto&r:f.changes){texts.add(r.name);valueSlots+=r.values.capacity();for(auto&v:r.values)texts.add(v.value);}for(auto&f:small)rowSlots+=f.changes.capacity();for(auto&m:meta){texts.add(m.name);texts.add(m.unit);texts.add(m.meaning);uniqueBytes+=m.name.size()+m.unit.size()+m.meaning.size();}for(auto&[k,id]:ids)keys.add(k);
 }else{for(auto&f:full){rowSlots+=f.changes.capacity();for(auto&r:f.changes){texts.add(r.name);valueSlots+=r.values.capacity();for(auto&v:r.values){texts.add(v.name);texts.add(v.value);texts.add(v.unit);texts.add(v.meaning);}}}}
 const double storeW=wall()-beginW,storeC=cpu()-beginC;
 Output out;out.data.reserve(bytes.size());out.u32(0x41495431);out.u32(1);out.u32(until);out.boolean(truncated);out.u32(frames);
 if(compact){for(auto&f:small){out.u32(f.step);out.u32(f.changes.size());for(auto&r:f.changes){out.u32(r.team);out.u32(r.player);out.text(r.name);out.boolean(r.available);out.u32(r.values.size());for(auto&v:r.values){const auto&m=meta.at(v.schemaID);out.text(m.name);out.text(v.value);out.text(m.unit);out.text(m.meaning);out.u32(v.updated);}}}}
 else{for(auto&f:full){out.u32(f.step);out.u32(f.changes.size());for(auto&r:f.changes){out.u32(r.team);out.u32(r.player);out.text(r.name);out.boolean(r.available);out.u32(r.values.size());for(auto&v:r.values){out.text(v.name);out.text(v.value);out.text(v.unit);out.text(v.meaning);out.u32(v.updated);}}}}
 if(out.data!=bytes)fail("roundtrip bytes differ");
 phase("after_encode_exact_compare");
 std::ofstream output(argv[4],std::ios::binary);output.write(out.data.data(),out.data.size());output.close();if(!output)fail("output");phase("after_output");rusage ru{};getrusage(RUSAGE_SELF,&ru);
 std::cout<<"{\"mode\":\""<<mode<<"\",\"roundtrip_exact\":true,\"frames\":"<<frames<<",\"rows\":"<<rowsTotal<<",\"values\":"<<valuesTotal<<",\"until\":"<<until<<",\"truncated\":"<<(truncated?"true":"false")<<",\"wire_bytes\":"<<bytes.size()<<",\"logical_budget_used\":"<<Limit-in.budget<<",\"sizeof_named_value\":"<<sizeof(FullValue)<<",\"sizeof_compact_value\":"<<sizeof(CompactValue)<<",\"sizeof_row\":"<<sizeof(ReplayTelemetry::Row)<<",\"sizeof_compact_row\":"<<sizeof(CompactRow)<<",\"frame_slots\":"<<frameSlots<<",\"row_slots\":"<<rowSlots<<",\"value_slots\":"<<valueSlots<<",\"storage_vector_capacity_bytes\":"<<frameSlots*(compact?sizeof(CompactFrame):sizeof(ReplayTelemetry::Frame))+rowSlots*(compact?sizeof(CompactRow):sizeof(ReplayTelemetry::Row))+valueSlots*(compact?sizeof(CompactValue):sizeof(FullValue))<<",\"metadata_repeated_text_bytes\":"<<metadataRepeated<<",\"value_text_bytes\":"<<valueText<<",\"registry_unique_metadata_triples\":"<<meta.size()<<",\"registry_unique_metadata_text_bytes\":"<<uniqueBytes<<",\"metadata_registry_vector_capacity_bytes\":"<<meta.capacity()*sizeof(Meta)<<",\"registry_map_entries\":"<<ids.size()<<",\"registry_map_buckets\":"<<ids.bucket_count()<<",\"registry_key_text_bytes\":"<<keys.bytes<<",\"registry_key_external_capacity_bytes\":"<<keys.externalCapacity<<",\"stored_string_count\":"<<texts.strings<<",\"stored_string_text_bytes\":"<<texts.bytes<<",\"stored_string_external_capacity_bytes\":"<<texts.externalCapacity<<",\"input_capacity_bytes\":"<<bytes.capacity()+1<<",\"encode_capacity_bytes\":"<<out.data.capacity()+1<<",\"storage_build_cumulative_wall_seconds\":"<<storeW<<",\"storage_build_cumulative_process_cpu_seconds\":"<<storeC<<",\"total_wall_seconds\":"<<wall()-beginW<<",\"total_process_cpu_seconds\":"<<cpu()-beginC<<",\"self_peak_rss_kib\":"<<ru.ru_maxrss<<",\"phases\":[";
 for(size_t i=0;i<phases.size();i++){if(i)std::cout<<",";auto&p=phases[i];std::cout<<"{\"name\":\""<<p.name<<"\",\"rss_kib\":"<<p.rssKib<<",\"cumulative_wall_seconds\":"<<p.wallSeconds<<",\"cumulative_process_cpu_seconds\":"<<p.cpuSeconds<<"}";}std::cout<<"]}\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
