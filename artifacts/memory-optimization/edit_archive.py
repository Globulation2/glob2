from pathlib import Path
p=Path('src/ai/maxima/AIMaximaContinuation.h');s=p.read_text().replace('#include <Stream.h>', '#include "AIMaximaDistanceField.h"\n#include <Stream.h>');start=s.index('class BufferedBinaryWriter');end=s.index('// Record overloads',start)
helper='''    template<class Wire, class T, class Encode, class Decode>
    void legacyVector(const char*, const std::vector<T>& value, Encode encode, Decode)
    {
        word(uint32_t(value.size()));
        for(const auto& item:value) { const Wire wire=encode(item); (*this)("value",wire); }
    }
    void operator()(const char* name,const AIMaximaPlacement::DistanceField& value)
    {
        legacyVector<int32_t>(name,value.storage(),
            [](uint16_t x){return x==UINT16_MAX?INT_MAX:int(x);},[](int32_t){return uint16_t{};});
    }
'''
s=s[:end].replace('    template<class T> void operator()(const char*,const std::set<T>& value)',helper+'    template<class T> void operator()(const char*,const std::set<T>& value)',1)+s[end:]
start=s.index('class Writer\n{');pos=s.index('    template<class T> void operator()(const char* name,const std::vector<T>& value)',start)
helper='''    template<class Wire, class T, class Encode, class Decode>
    void legacyVector(const char* name,const std::vector<T>& value,Encode encode,Decode decode)
    {
        if(auto* binary=dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
        { BufferedBinaryWriter packed(binary); packed.legacyVector<Wire>(name,value,encode,decode); packed.flush(); return; }
        stream->writeEnterSection(name); stream->writeUint32(value.size(),"size");
        for(size_t i=0;i<value.size();++i)
        { stream->writeEnterSection(i); const Wire wire=encode(value[i]); (*this)("value",wire); stream->writeLeaveSection(); }
        stream->writeLeaveSection();
    }
    void operator()(const char* name,const AIMaximaPlacement::DistanceField& value)
    {
        legacyVector<int32_t>(name,value.storage(),
            [](uint16_t x){return x==UINT16_MAX?INT_MAX:int(x);},[](int32_t){return uint16_t{};});
    }
''';s=s[:pos]+helper+s[pos:]
start=s.index('class Reader\n{');pos=s.index('    template<class T> void operator()(const char* name,std::vector<T>& value)',start)
helper='''    template<class Wire, class T, class Encode, class Decode>
    void legacyVector(const char* name,std::vector<T>& value,Encode,Decode decode)
    {
        stream->readEnterSection(name); value.clear(); value.resize(count());
        for(size_t i=0;i<value.size();++i)
        { stream->readEnterSection(i); Wire wire{}; (*this)("value",wire); value[i]=decode(wire); stream->readLeaveSection(); }
        stream->readLeaveSection();
    }
    void operator()(const char* name,AIMaximaPlacement::DistanceField& value)
    {
        legacyVector<int32_t>(name,value.storage(),[](uint16_t){return int32_t{};},
            [](int32_t x){
                if(x==INT_MAX) return uint16_t(UINT16_MAX);
                if(x<0 || x>=UINT16_MAX) throw std::runtime_error("Invalid compact distance");
                return uint16_t(x);
            });
    }
''';s=s[:pos]+helper+s[pos:];p.write_text(s)
