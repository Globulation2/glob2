#ifndef AI_MAXIMA_CONTINUATION_H
#define AI_MAXIMA_CONTINUATION_H

// Portable field-wise serialization for execution state added in save version 95.
// Never serialize object layouts, pointers, padding, or host-sized containers.
#include "Version.h"
#include <Stream.h>
#include <BinaryStream.h>
#include <cstring>
#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace AIMaximaContinuation
{
class Writer;
class Reader;
template<class A,class X,class Y> void fields(A& a,std::pair<X,Y>& v)
{ a("first",v.first); a("second",v.second); }

// Run the existing field descriptions into a bounded
// binary byte buffer. A root Writer call flushes before returning, preserving
// interleaved writes made directly to the original stream by its callers.
class BufferedBinaryWriter
{
    GAGCore::BinaryOutputStream* stream;
    int formatVersion;
    unsigned char bytes[16384];
    size_t used=0;
    void word(uint32_t x)
    {
        if(used+4>sizeof(bytes)) flush();
        bytes[used++]=x>>24;bytes[used++]=x>>16;
        bytes[used++]=x>>8;bytes[used++]=x;
    }
    void raw(const char* data,size_t length)
    {
        while(length)
        {
            const size_t n=std::min(length,sizeof(bytes)-used);
            std::memcpy(bytes+used,data,n);used+=n;data+=n;length-=n;
            if(used==sizeof(bytes))flush();
        }
    }
public:
    BufferedBinaryWriter(GAGCore::BinaryOutputStream* s,int v):stream(s),formatVersion(v) {}
    int version() const {return formatVersion;}
    void flush() {if(used){stream->write(bytes,used,"");used=0;}}
    template<class T> typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value>::type
    operator()(const char*,const T& value)
    {
        const uint64_t bits=static_cast<uint64_t>(value);
        word(uint32_t(bits));
        if constexpr(sizeof(T)>4)word(uint32_t(bits>>32));
    }
    void index(const char* name,const size_t& value)
    {const uint64_t portable=value;(*this)(name,portable);}
    void operator()(const char*,const std::string& value)
    {word(uint32_t(value.size()));raw(value.data(),value.size());}
    template<class T,size_t N> void operator()(const char*,const T (&value)[N])
    {for(size_t i=0;i<N;++i)(*this)("value",value[i]);}
    template<class T> void operator()(const char*,const std::vector<T>& value)
    {
        word(uint32_t(value.size()));
        if constexpr ((std::is_integral<T>::value || std::is_enum<T>::value) && sizeof(T)<=8)
        {
            constexpr size_t stride=sizeof(T)>4?8:4;
            for(size_t off=0;off<value.size();)
            {
                if(sizeof(bytes)-used<stride)flush();
                const size_t n=std::min(value.size()-off,(sizeof(bytes)-used)/stride);
                for(size_t i=0;i<n;++i)
                {
                    const uint64_t x=static_cast<uint64_t>(value[off+i]);
                    unsigned char* out=bytes+used+i*stride;
                    out[0]=x>>24;out[1]=x>>16;out[2]=x>>8;out[3]=x;
                    if constexpr(sizeof(T)>4)
                    {out[4]=x>>56;out[5]=x>>48;out[6]=x>>40;out[7]=x>>32;}
                }
                used+=n*stride;off+=n;
            }
        }
        else for(size_t i=0;i<value.size();++i)(*this)("value",value[i]);
    }
    template<class T> void operator()(const char*,const std::set<T>& value)
    {word(uint32_t(value.size()));for(const auto& item:value)(*this)("value",item);}
    template<class K,class V> void operator()(const char*,const std::map<K,V>& value)
    {word(uint32_t(value.size()));for(const auto& item:value){(*this)("key",item.first);(*this)("value",item.second);}}
    template<class T> typename std::enable_if<!std::is_integral<T>::value && !std::is_enum<T>::value>::type
    operator()(const char*,const T& value)
    {fields(*this,const_cast<T&>(value));}
};

// Record overloads live in their type's namespace and are found through ADL.
class Writer
{
    GAGCore::OutputStream* stream;
    int formatVersion;
public:
    explicit Writer(GAGCore::OutputStream* stream,int formatVersion=VERSION_MINOR)
        :stream(stream),formatVersion(formatVersion) {}
    /// Save format of the state being written, so a record added in a later
    /// version can be written unconditionally and read only where it exists.
    int version() const { return formatVersion; }
    template<class T> typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value>::type
    operator()(const char* name,const T& value)
    {
        if(sizeof(T)>4)
        {
            stream->writeEnterSection(name);
            const uint64_t bits=static_cast<uint64_t>(value);
            stream->writeUint32(uint32_t(bits),"low");
            stream->writeUint32(uint32_t(bits>>32),"high");
            stream->writeLeaveSection();
        }
        else if(std::is_signed<T>::value || std::is_enum<T>::value)
            stream->writeSint32(static_cast<int32_t>(value),name);
        else stream->writeUint32(static_cast<uint32_t>(value),name);
    }
    void index(const char* name,const size_t& value)
    { const uint64_t portable=value; (*this)(name,portable); }
    void operator()(const char* name,const std::string& value) { stream->writeText(value,name); }
    template<class T,size_t N> void operator()(const char* name,const T (&value)[N])
    {
        if(auto* binary=dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
        {BufferedBinaryWriter packed(binary,formatVersion);packed(name,value);packed.flush();return;}
        stream->writeEnterSection(name);
        for(size_t i=0;i<N;++i) { stream->writeEnterSection(i); (*this)("value",value[i]); stream->writeLeaveSection(); }
        stream->writeLeaveSection();
    }
    template<class T> void operator()(const char* name,const std::vector<T>& value)
    {
        if(auto* binary=dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
        {BufferedBinaryWriter packed(binary,formatVersion);packed(name,value);packed.flush();return;}
        stream->writeEnterSection(name); stream->writeUint32(value.size(),"size");
        for(size_t i=0;i<value.size();++i) { stream->writeEnterSection(i); (*this)("value",value[i]); stream->writeLeaveSection(); }
        stream->writeLeaveSection();
    }
    template<class T> void operator()(const char* name,const std::set<T>& value)
    { (*this)(name,std::vector<T>(value.begin(),value.end())); }
    template<class K,class V> void operator()(const char* name,const std::map<K,V>& value)
    {
        stream->writeEnterSection(name); stream->writeUint32(value.size(),"size");
        size_t i=0;
        for(const auto& item:value)
        { stream->writeEnterSection(i++); (*this)("key",item.first); (*this)("value",item.second); stream->writeLeaveSection(); }
        stream->writeLeaveSection();
    }
    template<class T> typename std::enable_if<!std::is_integral<T>::value && !std::is_enum<T>::value>::type
    operator()(const char* name,const T& value)
    {
        if(auto* binary=dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
        {BufferedBinaryWriter packed(binary,formatVersion);packed(name,value);packed.flush();return;}
        stream->writeEnterSection(name);
        // fields() is shared with Reader; Writer's operators never modify data.
        fields(*this,const_cast<T&>(value));
        stream->writeLeaveSection();
    }
};
class Reader
{
    GAGCore::InputStream* stream;
    int formatVersion;
    uint32_t count()
    {
        const uint32_t size=stream->readUint32("size");
        if(size>16777216u) throw std::runtime_error("Invalid Maxima continuation container size");
        return size;
    }
public:
    explicit Reader(GAGCore::InputStream* stream,int formatVersion=VERSION_MINOR)
        :stream(stream),formatVersion(formatVersion) {}
    /// Save format of the state being read. Records added after that version
    /// are absent from the stream and keep their constructed defaults.
    int version() const { return formatVersion; }
    template<class T> typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value>::type
    operator()(const char* name,T& value)
    {
        if(sizeof(T)>4)
        {
            stream->readEnterSection(name);
            const uint64_t low=stream->readUint32("low");
            const uint64_t high=stream->readUint32("high");
            value=static_cast<T>(low|(high<<32));
            stream->readLeaveSection();
        }
        else if(std::is_signed<T>::value || std::is_enum<T>::value)
            value=static_cast<T>(stream->readSint32(name));
        else value=static_cast<T>(stream->readUint32(name));
    }
    void index(const char* name,size_t& value)
    {
        uint64_t portable=0; (*this)(name,portable);
        if(portable>std::numeric_limits<size_t>::max()) throw std::runtime_error("Continuation index overflow");
        value=static_cast<size_t>(portable);
    }
    void operator()(const char* name,std::string& value) { value=stream->readText(name); }
    template<class T,size_t N> void operator()(const char* name,T (&value)[N])
    {
        stream->readEnterSection(name);
        for(size_t i=0;i<N;++i) { stream->readEnterSection(i); (*this)("value",value[i]); stream->readLeaveSection(); }
        stream->readLeaveSection();
    }
    template<class T> void operator()(const char* name,std::vector<T>& value)
    {
        stream->readEnterSection(name); value.clear(); value.resize(count());
        for(size_t i=0;i<value.size();++i) { stream->readEnterSection(i); (*this)("value",value[i]); stream->readLeaveSection(); }
        stream->readLeaveSection();
    }
    template<class T> void operator()(const char* name,std::set<T>& value)
    {
        std::vector<T> entries; (*this)(name,entries); value.clear();
        for(const auto& entry:entries) if(!value.insert(entry).second) throw std::runtime_error("Duplicate continuation set entry");
    }
    template<class K,class V> void operator()(const char* name,std::map<K,V>& value)
    {
        stream->readEnterSection(name); const uint32_t size=count(); value.clear();
        for(uint32_t i=0;i<size;++i)
        {
            stream->readEnterSection(i); K key{}; V item{};
            (*this)("key",key); (*this)("value",item);
            if(!value.emplace(key,item).second) throw std::runtime_error("Duplicate continuation map entry");
            stream->readLeaveSection();
        }
        stream->readLeaveSection();
    }
    template<class T> typename std::enable_if<!std::is_integral<T>::value && !std::is_enum<T>::value>::type
    operator()(const char* name,T& value)
    { stream->readEnterSection(name); fields(*this,value); stream->readLeaveSection(); }
};
}
#endif
