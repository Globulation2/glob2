// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceGradientPolicy.h"
#include "UnitConsts.h"
#include <Stream.h>
#include <stdexcept>

namespace {
void write64(GAGCore::OutputStream* s,std::uint64_t v,const char* name) {
    s->writeEnterSection(name);s->writeUint32(v>>32,"high");s->writeUint32(v,"low");s->writeLeaveSection();
}
std::uint64_t read64(GAGCore::InputStream* s,const char* name) {
    s->readEnterSection(name);const std::uint64_t hi=s->readUint32("high"),lo=s->readUint32("low");s->readLeaveSection();return (hi<<32)|lo;
}
void saveSources(GAGCore::OutputStream* s,const ResourceGradientPolicy::Sources& c) {
    write64(s,c.count,"count");write64(s,c.added,"added");write64(s,c.removed,"removed");
}
ResourceGradientPolicy::Sources loadSources(GAGCore::InputStream* s) {
    return {read64(s,"count"),read64(s,"added"),read64(s,"removed")};
}
}
void ResourceGradientPolicy::Capture::save(GAGCore::OutputStream* s) const
{
    s->writeEnterSection("capture");s->writeUint32(tick,"tick");saveSources(s,sources);s->writeLeaveSection();
}
ResourceGradientPolicy::Capture ResourceGradientPolicy::Capture::load(GAGCore::InputStream* s)
{
    s->readEnterSection("capture");Capture c;c.tick=s->readUint32("tick");c.sources=loadSources(s);s->readLeaveSection();return c;
}
void ResourceGradientPolicy::save(GAGCore::OutputStream* s) const
{
    // Demand, capture ages and source epochs affect the next decision, so they
    // must survive a save even though the fields themselves are derived data.
    s->writeEnterSection("adaptiveResources");s->writeUint8(initialized,"initialized");
    s->writeEnterSection("sources");
    for (unsigned m=0;m<MaterialCount;++m) { s->writeEnterSection(m);saveSources(s,sources[m]);s->writeLeaveSection(); }
    s->writeLeaveSection();
    s->writeUint16(fields.size(),"fieldCount");s->writeEnterSection("fields");unsigned i=0;
    for (const auto& [key,f]:fields) {
        s->writeEnterSection(i++);s->writeUint16(key,"key");f.serving.save(s);
        s->writeUint32(f.submitted,"submitted");s->writeUint32(f.queryTick,"queryTick");
        s->writeUint8(f.requested,"requested");s->writeUint8(f.queryKnown,"queryKnown");
        for (unsigned j=0;j<128;++j) { s->writeEnterSection(j);s->writeUint32(f.queries[j],"queries");s->writeLeaveSection(); }
        s->writeLeaveSection();
    }
    s->writeLeaveSection();s->writeLeaveSection();
}
void ResourceGradientPolicy::load(GAGCore::InputStream* s,unsigned teams)
{
    *this={};s->readEnterSection("adaptiveResources");const auto flag=s->readUint8("initialized");
    if (flag>1) throw std::runtime_error("Invalid resource policy initialization");initialized=flag;
    s->readEnterSection("sources");
    for (unsigned m=0;m<MaterialCount;++m) { s->readEnterSection(m);sources[m]=loadSources(s);s->readLeaveSection(); }
    s->readLeaveSection();
    const unsigned n=s->readUint16("fieldCount"),limit=teams*MaterialCount*SWIM_CLASS_COUNT;
    s->readEnterSection("fields");
    if (n>limit || (!initialized && n)) throw std::runtime_error("Invalid resource policy field count");
    for (unsigned i=0;i<n;++i) {
        s->readEnterSection(i);const int key=s->readUint16("key");
        if (key<0 || unsigned(key)>=limit || fields.contains(key)) throw std::runtime_error("Invalid resource policy key");
        auto& f=fields[key];f.serving=Capture::load(s);
        f.submitted=s->readUint32("submitted");f.queryTick=s->readUint32("queryTick");
        const auto requested=s->readUint8("requested"),known=s->readUint8("queryKnown");
        if (requested>1 || known>1) throw std::runtime_error("Invalid resource policy flags");f.requested=requested;f.queryKnown=known;
        const auto& now=sources[(key/SWIM_CLASS_COUNT)%MaterialCount];
        if (f.serving.sources.added>now.added || f.serving.sources.removed>now.removed) throw std::runtime_error("Invalid resource policy source epoch");
        for (unsigned j=0;j<128;++j) {
            s->readEnterSection(j);f.queries[j]=s->readUint32("queries");s->readLeaveSection();
            if (f.queries[j]>1000000 || (!known && f.queries[j])) throw std::runtime_error("Invalid resource query count");
            f.historyHash^=Field::bucketHash(j,f.queries[j]);
            f.count128+=f.queries[j];if (((f.queryTick-j)&127)<32) f.count32+=f.queries[j];
        }
        s->readLeaveSection();
    }
    s->readLeaveSection();s->readLeaveSection();
}
std::uint32_t ResourceGradientPolicy::checksum() const
{
    std::uint32_t h=initialized;
    const auto add=[&](std::uint64_t v){h=std::rotl(h,1)^std::uint32_t(v)^std::uint32_t(v>>32);};
    for (const auto& c:sources) { add(c.count);add(c.added);add(c.removed); }
    for (const auto& [key,f]:fields) {
        add(key);add(f.serving.tick);add(f.serving.sources.count);add(f.serving.sources.added);add(f.serving.sources.removed);
        add(f.submitted);add(f.queryTick);add(f.requested);add(f.queryKnown);
        add(f.historyHash);add(f.count32);add(f.count128);
    }
    return h;
}
