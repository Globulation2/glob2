// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptValue.h"
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
namespace Script
{
namespace
{
void word(std::string& out, std::uint64_t v, unsigned count)
{
 for (unsigned i=count; i; --i) out.push_back(char(v >> ((i-1)*8)));
}
void write(std::string& out, const Value& v, unsigned depth)
{
 if (depth>DepthLimit) throw std::runtime_error("Script data is too deeply nested");
 out.push_back(char(v.kind));
 switch(v.kind)
 {
 case Value::Null: break;
 case Value::Boolean: out.push_back(v.number != 0); break;
 case Value::Number:
  if (!std::isfinite(v.number)) throw std::runtime_error("Script state requires finite numbers");
  word(out,std::bit_cast<std::uint64_t>(v.number),8); break;
 case Value::String: word(out,v.text.size(),4); out+=v.text; break;
 case Value::Array:
  word(out,v.items.size(),4); for(const auto& item:v.items) write(out,item,depth+1); break;
 case Value::Object:
  word(out,v.fields.size(),4);
  for(const auto& [key,value]:v.fields) { word(out,key.size(),4); out+=key; write(out,value,depth+1); }
  break;
 }
 if(out.size()>StateLimit) throw std::runtime_error("Script data exceeds limit");
}
struct Reader
{
 const std::string& bytes; std::size_t position=0,nativeBytes=0;
 void charge(std::size_t n){if(n>NativeDataLimit-nativeBytes)throw std::runtime_error("Native script data exceeds limit");nativeBytes+=n;}
 std::uint64_t word(unsigned n)
 {
  if(n>bytes.size()-position) throw std::runtime_error("Truncated script data");
  std::uint64_t v=0; while(n--) v=(v<<8)|static_cast<unsigned char>(bytes[position++]); return v;
 }
 std::string text()
 {
  auto n=word(4); if(n>bytes.size()-position) throw std::runtime_error("Truncated script text");
  charge(n);auto s=bytes.substr(position,n); position+=n; return s;
 }
 Value read(unsigned depth)
 {
  if(depth>DepthLimit) throw std::runtime_error("Script data is too deeply nested");
  charge(NativeValueCost);Value v; auto tag=word(1); if(tag>Value::Object) throw std::runtime_error("Invalid script data tag");
  v.kind=Value::Kind(tag);
  switch(v.kind)
  {
  case Value::Null:break;
  case Value::Boolean: { auto b=word(1); if(b>1) throw std::runtime_error("Invalid boolean"); v.number=b; break; }
  case Value::Number:v.number=std::bit_cast<double>(word(8)); if(!std::isfinite(v.number)) throw std::runtime_error("Invalid state number"); break;
  case Value::String:v.text=text();break;
  case Value::Array: {auto n=word(4); if(n>bytes.size()-position) throw std::runtime_error("Invalid array size");charge(n*NativeValueCost);v.items.reserve(n); while(n--) v.items.push_back(read(depth+1));break;}
  case Value::Object: {auto n=word(4); if(n>(bytes.size()-position)/5) throw std::runtime_error("Invalid record size");charge(n*NativeFieldCost);v.fields.reserve(n); std::set<std::string> keys; while(n--) {auto key=text();charge(2*key.size()); if(!keys.insert(key).second) throw std::runtime_error("Duplicate state key"); v.fields.emplace_back(key,read(depth+1));}break;}
  }
  return v;
 }
};
}
Value& Value::set(const std::string& key,Value v)
{
 if(kind!=Object) throw std::runtime_error("Expected a record");
 for(auto& f:fields) if(f.first==key) {f.second=std::move(v);return *this;}
 fields.emplace_back(key,std::move(v));return *this;
}
const Value& Value::get(const std::string& key) const
{
 static const Value absent;
 for(const auto& f:fields) if(f.first==key) return f.second;
 return absent;
}
int Value::integer(const std::string& key,int minimum,int maximum) const
{
 const auto& v=get(key);
 if(v.kind!=Number || !std::isfinite(v.number) || std::trunc(v.number)!=v.number || v.number<minimum || v.number>maximum)
  throw std::runtime_error("Invalid integer argument: "+key);
 return int(v.number);
}
std::string Value::string(const std::string& key) const
{
 const auto& v=get(key); if(v.kind!=String) throw std::runtime_error("Expected string: "+key); return v.text;
}
std::string Value::encode() const { std::string out; write(out,*this,0);return out; }
Value Value::decode(const std::string& bytes)
{
 if(bytes.size()>StateLimit) throw std::runtime_error("Script state exceeds limit");
 Reader r{bytes};auto v=r.read(0);if(r.position!=bytes.size()) throw std::runtime_error("Trailing script data");return v;
}
std::string config(const std::string& source)
{
 if(source.size()>SourceLimit) throw std::runtime_error("Script source exceeds limit");
 return "glob2-js/1\n"+source;
}
std::string sourceFromConfig(const std::string& text)
{
 const std::string prefix="glob2-js/1\n";
 if(text.compare(0,prefix.size(),prefix)!=0 || text.size()-prefix.size()>SourceLimit) throw std::runtime_error("Unsupported JavaScript profile or oversized source");
 return text.substr(prefix.size());
}
}
