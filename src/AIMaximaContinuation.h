#ifndef AI_MAXIMA_CONTINUATION_H
#define AI_MAXIMA_CONTINUATION_H

// Portable field-wise serialization for execution state added in save version 95.
// Never serialize object layouts, pointers, padding, or host-sized containers.
#include "Version.h"
#include <Stream.h>
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
        stream->writeEnterSection(name);
        for(size_t i=0;i<N;++i) { stream->writeEnterSection(i); (*this)("value",value[i]); stream->writeLeaveSection(); }
        stream->writeLeaveSection();
    }
    template<class T> void operator()(const char* name,const std::vector<T>& value)
    {
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
