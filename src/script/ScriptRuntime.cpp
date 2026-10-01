// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScriptRuntime.h"
#include "quickjs.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <set>
#include <string_view>
#include <stdexcept>
namespace Script
{
namespace
{
struct Environment
{
 JSRuntime* runtime=nullptr;
 JSContext* ctx=nullptr;
 JSValue recordPrototype=JS_NULL;
 Host* host=nullptr;
 std::uint64_t fuel=FuelLimit;
 std::size_t dataBytes=0,nativeBytes=0;
 bool hostFailed=false;
 std::set<void*> ancestors;
 Environment(Host* h):host(h)
 {
  if(std::fegetround()!=FE_TONEAREST) throw HostFailure("Unsupported floating point rounding mode");
  runtime=JS_NewRuntime();if(!runtime) throw HostFailure("Cannot create JavaScript runtime");
  JS_SetMemoryLimit(runtime,32*1024*1024);
  JS_SetMaxStackSize(runtime,256*1024);
  JS_SetInterruptHandler(runtime,[](JSRuntime*,void* opaque)->int {auto& e=*static_cast<Environment*>(opaque);if(!e.fuel)return 1;--e.fuel;return 0;},this);
  ctx=JS_NewContextRaw(runtime);
  if(!ctx) {JS_FreeRuntime(runtime);runtime=nullptr;throw HostFailure("Cannot create JavaScript context");}
  JS_SetContextOpaque(ctx,this);
  if(JS_AddIntrinsicBaseObjects(ctx)<0 || JS_AddIntrinsicEval(ctx)<0 || JS_AddIntrinsicPromise(ctx)<0 || JS_AddIntrinsicJSON(ctx)<0 || JS_AddIntrinsicMapSet(ctx)<0)
  {try{fail();}catch(...){JS_FreeContext(ctx);JS_FreeRuntime(runtime);ctx=nullptr;runtime=nullptr;throw;}}
  JSValue global=JS_GetGlobalObject(ctx);
  for(const char* name:{"BigInt","eval","Function","Promise"}) {JSAtom atom=JS_NewAtom(ctx,name);JS_DeleteProperty(ctx,global,atom,0);JS_FreeAtom(ctx,atom);}
  JSValue array=JS_GetPropertyStr(ctx,global,"Array");JSAtom fromAsync=JS_NewAtom(ctx,"fromAsync");JS_DeleteProperty(ctx,array,fromAsync,0);JS_FreeAtom(ctx,fromAsync);JS_FreeValue(ctx,array);
  JSValue object=JS_GetPropertyStr(ctx,global,"Object");recordPrototype=JS_GetPropertyStr(ctx,object,"prototype");JS_FreeValue(ctx,object);
  JSValue math=JS_GetPropertyStr(ctx,global,"Math");
  JS_SetPropertyStr(ctx,math,"random",JS_NewCFunction(ctx,random,"random",0));
  JS_FreeValue(ctx,math);JS_FreeValue(ctx,global);
 }
 ~Environment(){if(ctx){JS_FreeValue(ctx,recordPrototype);JS_FreeContext(ctx);}if(runtime)JS_FreeRuntime(runtime);}
 void charge(std::size_t n)
 {
  if(n>fuel){fuel=0;throw std::runtime_error("JavaScript work budget exhausted");}fuel-=n;
 }
 void chargeNative(std::size_t n)
 {
  if(n>NativeDataLimit-nativeBytes)throw std::runtime_error("Native script data exceeds limit");nativeBytes+=n;
 }
 [[noreturn]] void fail()
 {
  JSValue ex=JS_GetException(ctx);
  const char* text=JS_ToCString(ctx,ex);
  std::string message=text?std::string(text).substr(0,16384):"JavaScript execution failed";
  if(text)JS_FreeCString(ctx,text);JS_FreeValue(ctx,ex);
  if(hostFailed || JS_Glob2HostFailure(runtime))throw HostFailure("JavaScript native resource limit exhausted");
  if(!fuel)message="JavaScript work budget exhausted";
  throw std::runtime_error(message);
 }
 static JSValue random(JSContext* ctx,JSValueConst,int,JSValueConst*)
 {
  auto& e=*static_cast<Environment*>(JS_GetContextOpaque(ctx));
  if(!e.host || !e.host->random) return JS_ThrowTypeError(ctx,"Randomness unavailable during source validation");
  try {e.charge(1);return JS_NewFloat64(ctx,e.host->random()/4294967296.0);}catch(const std::exception& ex){return JS_ThrowInternalError(ctx,"%s",ex.what());}
 }
 JSValue toJS(const Value& v,unsigned depth=0)
 {
  charge(1);if(depth>DepthLimit)throw std::runtime_error("Observation nesting limit exceeded");
  switch(v.kind)
  {
  case Value::Null:return JS_NULL;
  case Value::Boolean:return JS_NewBool(ctx,v.number!=0);
  case Value::Number:return JS_NewFloat64(ctx,v.number);
  case Value::String:charge(v.text.size());return JS_NewStringLen(ctx,v.text.data(),v.text.size());
  case Value::Array: {
   JSValue a=JS_NewArray(ctx);if(JS_IsException(a))fail();
   try{for(unsigned i=0;i<v.items.size();++i)if(JS_SetPropertyUint32(ctx,a,i,toJS(v.items[i],depth+1))<0)fail();}
   catch(...){JS_FreeValue(ctx,a);throw;}return a;
  }
  case Value::Object: {
   JSValue o=JS_NewObjectProto(ctx,JS_NULL);if(JS_IsException(o))fail();
   try{for(const auto& f:v.fields){JSValue value=toJS(f.second,depth+1);JSAtom key=JS_NewAtomLen(ctx,f.first.data(),f.first.size());
    if(key==JS_ATOM_NULL){JS_FreeValue(ctx,value);fail();}int r=JS_DefinePropertyValue(ctx,o,key,value,JS_PROP_C_W_E);JS_FreeAtom(ctx,key);if(r<0)fail();}}
   catch(...){JS_FreeValue(ctx,o);throw;}return o;
  }
  }
  return JS_NULL;
 }
 void validateArrayKeys(JSValueConst array,JSPropertyEnum* keys,uint32_t count,uint32_t length)
 {
  if(count!=length+1)throw std::runtime_error("Sparse or named arrays are not script data");
  charge(count);
  for(uint32_t i=0;i<count;++i)
  {
   JSValue key=JS_AtomToValue(ctx,keys[i].atom);
   if(JS_IsSymbol(key)){JS_FreeValue(ctx,key);throw std::runtime_error("Symbol keys are not script data");}
   size_t len;const char* text=JS_ToCStringLen(ctx,&len,key);JS_FreeValue(ctx,key);if(!text)fail();
   bool valid=len==6 && std::string_view(text,len)=="length";
   if(!valid && len && len<=10 && (len==1 || text[0]!='0'))
   {
    std::uint64_t index=0;valid=true;for(size_t j=0;j<len;++j){if(text[j]<'0' || text[j]>'9'){valid=false;break;}index=index*10+unsigned(text[j]-'0');}valid=valid && index<length;
   }
   JS_FreeCString(ctx,text);if(!valid)throw std::runtime_error("Sparse or named arrays are not script data");
   JSPropertyDescriptor d{};if(JS_GetOwnProperty(ctx,&d,array,keys[i].atom)<0)fail();bool accessor=d.flags&JS_PROP_GETSET;JS_FreeValue(ctx,d.value);JS_FreeValue(ctx,d.getter);JS_FreeValue(ctx,d.setter);if(accessor)throw std::runtime_error("Accessors are not script data");
  }
 }
 Value fromJS(JSValueConst v,unsigned depth=0)
 {
  charge(1);chargeNative(NativeValueCost);if(depth>DepthLimit)throw std::runtime_error("Script data nesting limit exceeded");
  if(JS_IsNull(v))return {};
  if(JS_IsBool(v))return Value(bool(JS_ToBool(ctx,v)));
  if(JS_IsNumber(v)){double n;JS_ToFloat64(ctx,&n,v);if(!std::isfinite(n))throw std::runtime_error("Script data requires finite numbers");return Value(n);}
  if(JS_IsString(v)) {size_t len;const char* p=JS_ToCStringLen(ctx,&len,v);if(!p)fail();if(2*len>NativeDataLimit-nativeBytes){JS_FreeCString(ctx,p);throw std::runtime_error("Native script data exceeds limit");}std::string s(p,len);JS_FreeCString(ctx,p);chargeNative(2*len);charge(len);dataBytes+=len;if(dataBytes>StateLimit)throw std::runtime_error("Script data exceeds limit");return Value(s);}
  if(!JS_IsObject(v) || JS_IsProxy(v) || (JS_GetClassID(v)!=1 && !JS_IsArray(v)))throw std::runtime_error("Script data requires plain records and arrays");
  if(!JS_IsArray(v)){JSValue prototype=JS_GetPrototype(ctx,v);bool plain=JS_IsNull(prototype) || (JS_IsObject(prototype) && JS_VALUE_GET_PTR(prototype)==JS_VALUE_GET_PTR(recordPrototype));JS_FreeValue(ctx,prototype);if(!plain)throw std::runtime_error("Class instances are not script data");}
  void* identity=JS_VALUE_GET_PTR(v);
  if(!ancestors.insert(identity).second)throw std::runtime_error("Cyclic script data");
  const bool array=JS_IsArray(v);Value out=array?Value::array():Value::object();
  JSPropertyEnum* keys=nullptr;uint32_t count=0;
  if(JS_GetOwnPropertyNames(ctx,&keys,&count,v,JS_GPN_STRING_MASK|JS_GPN_SYMBOL_MASK)<0)fail();
  try
  {
   uint32_t length=0;
   if(array){JSValue l=JS_GetPropertyStr(ctx,v,"length");JS_ToUint32(ctx,&length,l);JS_FreeValue(ctx,l);if(length>StateLimit)throw std::runtime_error("Array length exceeds limit");validateArrayKeys(v,keys,count,length);charge(length);chargeNative(std::size_t(length)*NativeValueCost);out.items.resize(length);}
   uint32_t arrayEntries=0;
   for(uint32_t i=0;i<count;++i)
   {
    JSPropertyDescriptor d{};
    if(JS_GetOwnProperty(ctx,&d,v,keys[i].atom)<0)fail();
    JSValue keyValue=JS_AtomToValue(ctx,keys[i].atom);
    if(JS_IsSymbol(keyValue)){JS_FreeValue(ctx,keyValue);JS_FreeValue(ctx,d.value);JS_FreeValue(ctx,d.getter);JS_FreeValue(ctx,d.setter);throw std::runtime_error("Symbol keys are not script data");}
    size_t keyLength=0;const char* p=JS_ToCStringLen(ctx,&keyLength,keyValue);JS_FreeValue(ctx,keyValue);if(!p){JS_FreeValue(ctx,d.value);JS_FreeValue(ctx,d.getter);JS_FreeValue(ctx,d.setter);fail();}if(2*keyLength+NativeFieldCost>NativeDataLimit-nativeBytes){JS_FreeCString(ctx,p);JS_FreeValue(ctx,d.value);JS_FreeValue(ctx,d.getter);JS_FreeValue(ctx,d.setter);throw std::runtime_error("Native script data exceeds limit");}std::string key(p,keyLength);JS_FreeCString(ctx,p);chargeNative(2*keyLength+NativeFieldCost);
    if(d.flags&JS_PROP_GETSET){JS_FreeValue(ctx,d.value);JS_FreeValue(ctx,d.getter);JS_FreeValue(ctx,d.setter);throw std::runtime_error("Accessors are not script data");}
    JSValue atom=JS_AtomToValue(ctx,keys[i].atom);
    bool symbol=JS_IsSymbol(atom);JS_FreeValue(ctx,atom);
    if(symbol){JS_FreeValue(ctx,d.value);throw std::runtime_error("Symbol keys are not script data");}
    if(array && key=="length"){JS_FreeValue(ctx,d.value);continue;}
    Value item;
    try{item=fromJS(d.value,depth+1);}catch(...){JS_FreeValue(ctx,d.value);throw;}
    JS_FreeValue(ctx,d.value);dataBytes+=key.size()+16;if(dataBytes>StateLimit)throw std::runtime_error("Script data exceeds limit");
    if(array){size_t index;try{index=std::stoull(key);}catch(...){throw std::runtime_error("Named array properties are not script data");}if(std::to_string(index)!=key || index>=length)throw std::runtime_error("Invalid array key");out.items[index]=std::move(item);++arrayEntries;}
    else out.fields.emplace_back(key,std::move(item));
   }
   if(array && arrayEntries!=length)throw std::runtime_error("Sparse arrays are not script data");
  }
  catch(...){JS_FreePropertyEnum(ctx,keys,count);ancestors.erase(identity);throw;}
  JS_FreePropertyEnum(ctx,keys,count);ancestors.erase(identity);return out;
 }
 static JSValue query(JSContext* ctx,JSValueConst,int argc,JSValueConst* argv,int magic)
 {
  static const char* names[]={"teams","units","buildings","unit","building","tile","region","objectives","hints","interface","buildingTypes"};
  auto& e=*static_cast<Environment*>(JS_GetContextOpaque(ctx));
  try {e.charge(1);std::vector<Value> args;for(int i=0;i<argc;++i)args.push_back(e.fromJS(argv[i]));
   if(magic==6){if(args.size()!=4)throw std::runtime_error("region requires x, y, width, height");
    auto dimensions=Value::object().set("w",args[2]).set("h",args[3]);auto w=dimensions.integer("w",0,256),h=dimensions.integer("h",0,256);e.charge(std::size_t(w)*h*128);}
   if(magic==1 || magic==2)e.charge(32*1024);
   return e.toJS(e.host->query(names[magic],args,[&e](std::size_t work,std::size_t bytes){e.charge(work);e.chargeNative(bytes);}));}
  catch(const std::bad_alloc&){e.hostFailed=true;return JS_ThrowOutOfMemory(ctx);}
  catch(const HostFailure& ex){e.hostFailed=true;return JS_ThrowInternalError(ctx,"%s",ex.what());}
  catch(const std::exception& ex){return JS_ThrowTypeError(ctx,"%s",ex.what());}
 }
 JSValue context()
 {
  JSValue c=JS_NewObjectProto(ctx,JS_NULL),g=JS_NewObjectProto(ctx,JS_NULL),m=JS_NewObjectProto(ctx,JS_NULL);
  JS_SetPropertyStr(ctx,c,"tick",JS_NewUint32(ctx,host->tick));JS_SetPropertyStr(ctx,c,"myTeam",JS_NewInt32(ctx,host->team));
  JS_SetPropertyStr(ctx,c,"random",JS_NewCFunction(ctx,random,"random",0));
  static const char* names[]={"teams","units","buildings","unit","building","tile","region","objectives","hints","interface","buildingTypes"};
  for(int i=0;i<11;++i)JS_SetPropertyStr(ctx,i==5||i==6?m:g,names[i],JS_NewCFunctionMagic(ctx,query,names[i],0,JS_CFUNC_generic_magic,i));
  JS_SetPropertyStr(ctx,m,"width",JS_NewUint32(ctx,host->width));JS_SetPropertyStr(ctx,m,"height",JS_NewUint32(ctx,host->height));
  JS_SetPropertyStr(ctx,g,"map",m);JS_SetPropertyStr(ctx,c,"game",g);return c;
 }
 JSValue compile(const std::string& source)
 {
  if(source.size()>SourceLimit || source.find('\0')!=std::string::npos)throw std::runtime_error("Invalid or oversized JavaScript source");
  charge(source.size());
  JSValue v=JS_Eval(ctx,source.data(),source.size(),"<glob2-script>",JS_EVAL_TYPE_MODULE|JS_EVAL_FLAG_COMPILE_ONLY);
  if(JS_IsException(v))fail();
  if(JS_ResolveModule(ctx,v)<0){JS_FreeValue(ctx,v);fail();}
  return v;
 }
};
class QuickRuntime:public Runtime
{
public:
 void validate(const std::string& source) override {Environment e(nullptr);JSValue code=e.compile(source);JS_FreeValue(e.ctx,code);}
 Result invoke(const std::string& source,const Value& state,bool initialize,Host& host) override
 {
  Environment e(&host);JSValue code=e.compile(source);
  auto* module=static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(code));
  JSValue evaluation=JS_EvalFunction(e.ctx,code);
  if(JS_IsException(evaluation))e.fail();
  if(JS_IsPromise(evaluation) && JS_PromiseState(e.ctx,evaluation)!=JS_PROMISE_FULFILLED){JSValue reason=JS_PromiseResult(e.ctx,evaluation);JS_FreeValue(e.ctx,evaluation);JS_Throw(e.ctx,reason);e.fail();}
  JS_FreeValue(e.ctx,evaluation);
  JSValue ns=JS_NULL,c=JS_NULL,s=JS_NULL,effects=JS_NULL;
  try
  {
   ns=JS_GetModuleNamespace(e.ctx,module);c=e.context();s=e.toJS(state);
   if(initialize)
   {
    JSValue init=JS_GetPropertyStr(e.ctx,ns,"init");
    if(!JS_IsUndefined(init)){JSValue args[]={c,s};JSValue r=JS_Call(e.ctx,init,JS_UNDEFINED,2,args);JS_FreeValue(e.ctx,init);if(JS_IsException(r))e.fail();if(!JS_IsUndefined(r) && !JS_IsNull(r)){JS_FreeValue(e.ctx,r);throw std::runtime_error("init must not return effects");}JS_FreeValue(e.ctx,r);}else JS_FreeValue(e.ctx,init);
   }
   JSValue step=JS_GetPropertyStr(e.ctx,ns,"step");if(!JS_IsFunction(e.ctx,step)){JS_FreeValue(e.ctx,step);throw std::runtime_error("Script must export step(ctx, state)");}
   JSValue args[]={c,s};effects=JS_Call(e.ctx,step,JS_UNDEFINED,2,args);JS_FreeValue(e.ctx,step);if(JS_IsException(effects))e.fail();
   if(e.hostFailed || JS_Glob2HostFailure(e.runtime))throw HostFailure("JavaScript native resource limit exhausted");
   if(!e.fuel)throw std::runtime_error("JavaScript work budget exhausted");
   Result result{e.fromJS(s),JS_IsUndefined(effects)?Value():e.fromJS(effects)};
   result.state.encode();result.effects.encode();
   JS_FreeValue(e.ctx,effects);JS_FreeValue(e.ctx,s);JS_FreeValue(e.ctx,c);JS_FreeValue(e.ctx,ns);return result;
  }
  catch(...){JS_FreeValue(e.ctx,effects);JS_FreeValue(e.ctx,s);JS_FreeValue(e.ctx,c);JS_FreeValue(e.ctx,ns);throw;}
 }
};
}
std::unique_ptr<Runtime> makeRuntime(){return std::make_unique<QuickRuntime>();}
}
